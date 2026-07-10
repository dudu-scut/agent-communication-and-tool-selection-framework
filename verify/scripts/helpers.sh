#!/bin/bash
# ============================================================================
# NexusAI E2E Verification — Shared Helpers
#
# Source this file in all verify-batch*.sh scripts:
#   source "$(dirname "$0")/helpers.sh"
# ============================================================================

set -euo pipefail

# ============================================================================
# Paths
# ============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERIFY_DIR="$(dirname "$SCRIPT_DIR")"
PROJECT_ROOT="$(dirname "$VERIFY_DIR")"

REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"
PG_HOST="${PG_HOST:-127.0.0.1}"
PG_PORT="${PG_PORT:-5432}"
PG_DB="${PG_DB:-nexusai}"
PG_USER="${PG_USER:-nexusai}"
GRPC_SERVER="${GRPC_SERVER:-localhost:50051}"
MOCK_AGENT_URL="${MOCK_AGENT_URL:-http://localhost:5100}"

# ============================================================================
# Color output
# ============================================================================
PASS='\033[0;32m[PASS]\033[0m'
FAIL='\033[0;31m[FAIL]\033[0m'
WARN='\033[0;33m[WARN]\033[0m'
SKIP='\033[0;34m[SKIP]\033[0m'

# ============================================================================
# Counters
# ============================================================================
PASS_COUNT=0
FAIL_COUNT=0
WARN_COUNT=0
FAILED_SCENARIOS=()

# ============================================================================
# Scenario scaffolding
# ============================================================================
scenario() {
    echo ""
    echo "=========================================="
    echo "  $1"
    echo "=========================================="
}

step() {
    echo "  → $1"
}

verify() {
    local desc="$1"
    shift
    if "$@"; then
        echo -e "    $PASS $desc"
        PASS_COUNT=$((PASS_COUNT+1))
        return 0
    else
        echo -e "    $FAIL $desc"
        FAIL_COUNT=$((FAIL_COUNT+1))
        FAILED_SCENARIOS+=("$desc")
        return 0
    fi
}

verify_warn() {
    # Like verify() but failure is downgraded to WARN (non-blocking)
    local desc="$1"
    shift
    if "$@"; then
        echo -e "    $PASS $desc"
        PASS_COUNT=$((PASS_COUNT+1))
        return 0
    else
        echo -e "    $WARN $desc (non-blocking)"
        WARN_COUNT=$((WARN_COUNT+1))
        return 0
    fi
}

# ============================================================================
# Pre-check: ensure required services are running
# ============================================================================
PIDS_DIR="$PROJECT_ROOT/pids"
precheck_services() {
    echo "--- Pre-check: Services ---"

    # gRPC server
    if echo > /dev/tcp/127.0.0.1/50051 2>/dev/null; then
        echo "  gRPC Server: ✅ (localhost:50051)"
    else
        echo "  gRPC Server: ❌ not running — attempting auto-start..."
        cd "$PROJECT_ROOT" && ./run.sh start
        sleep 2
    fi

    # Redis
    if (echo > /dev/tcp/"$REDIS_HOST"/"$REDIS_PORT") 2>/dev/null; then
        echo "  Redis: ✅ ($REDIS_HOST:$REDIS_PORT)"
    else
        echo "  Redis: ❌ not running — attempting auto-start..."
        redis-server --port "$REDIS_PORT" --daemonize yes --loglevel notice
        sleep 1
    fi

    # Mock Agent
    if curl -s -o /dev/null -w "%{http_code}" "$MOCK_AGENT_URL/health" 2>/dev/null | grep -q "200"; then
        echo "  Mock Agent: ✅ ($MOCK_AGENT_URL)"
    else
        echo "  Mock Agent: ❌ not running — attempting auto-start..."
        python3 "$VERIFY_DIR/mock-agent/mock_agent_server.py" &
        local mock_pid=$!
        mkdir -p "$PIDS_DIR"
        echo "$mock_pid" > "$PIDS_DIR/mock_agent.pid"
        sleep 1
    fi

    echo ""
}

# ============================================================================
# Assertion functions
# ============================================================================
assert_http_ok() {
    local url="$1"
    local code
    code=$(curl -s -o /dev/null -w "%{http_code}" "$url" 2>/dev/null)
    [ "$code" = "200" ]
}

assert_redis_key_exists() {
    local key="$1"
    local result
    result=$(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" EXISTS "$key" 2>/dev/null)
    [ "$result" = "1" ]
}

assert_redis_key_ttl() {
    local key="$1"
    local min_ttl="$2"
    local max_ttl="${3:-999999}"
    local ttl
    ttl=$(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" TTL "$key" 2>/dev/null)
    [ "$ttl" -ge "$min_ttl" ] && [ "$ttl" -le "$max_ttl" ]
}

assert_redis_value_gt() {
    local key="$1"
    local threshold="$2"
    local val
    val=$(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" GET "$key" 2>/dev/null)
    [ -n "$val" ] && [ "$val" -gt "$threshold" ] 2>/dev/null
}

assert_pg_row_exists() {
    local table="$1"
    local where_clause="$2"
    local count
    count=$(psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" -d "$PG_DB" -t -c \
        "SELECT COUNT(*) FROM $table WHERE $where_clause;" 2>/dev/null)
    [ "${count// /}" -gt 0 ] 2>/dev/null
}

assert_contains() {
    local haystack="$1"
    local needle="$2"
    echo "$haystack" | grep -q "$needle"
}

assert_not_contains() {
    local haystack="$1"
    local needle="$2"
    ! echo "$haystack" | grep -q "$needle"
}

assert_json_field_eq() {
    local json="$1"
    local field="$2"
    local expected="$3"
    local actual
    actual=$(echo "$json" | python3 -c "import sys,json; print(json.load(sys.stdin).get('$field',''))" 2>/dev/null)
    [ "$actual" = "$expected" ]
}

# ============================================================================
# gRPC communication
# ============================================================================
send_grpc() {
    local method="$1"
    local body="$2"
    grpcurl -plaintext -d "$body" "$GRPC_SERVER" "$method" 2>&1
}

send_grpc_stream() {
    local method="$1"
    local body="$2"
    grpcurl -plaintext -d "$body" "$GRPC_SERVER" "$method" 2>&1
}

# ============================================================================
# Data injection helpers
# ============================================================================
register_mock_agent() {
    local agent_id="$1"
    local skill="$2"
    local version="${3:-1.0}"
    local stage="${4:-STABLE}"
    # Use grpcurl to call RegisterAgent RPC
    local payload
    payload=$(cat <<EOF
{
    "agent_id": "$agent_id",
    "skill": "$skill",
    "a2a_version": "$version",
    "deployment_stage": "$stage",
    "endpoint": "http://localhost:5100/tasks/send"
}
EOF
)
    send_grpc "agent_communication.AgentService/RegisterAgent" "$payload" || true
    # Registration may fail if already registered — non-fatal for verify scripts
}

set_redis_budget() {
    local key="$1"
    local value="$2"
    redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" SET "$key" "$value" > /dev/null
}

reset_mock_agent() {
    # Reset failure counters and restore normal mode
    curl -s -X PUT "$MOCK_AGENT_URL/reset" > /dev/null 2>&1 || true
}

cleanup_redis_keys() {
    local pattern="$1"
    redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" KEYS "$pattern" 2>/dev/null | \
        while read -r key; do
            [ -n "$key" ] && redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" DEL "$key" > /dev/null
        done
}

# ============================================================================
# Report generation
# ============================================================================
print_batch_report() {
    local batch_name="$1"
    local total=$((PASS_COUNT + FAIL_COUNT + WARN_COUNT))
    echo ""
    echo "----------------------------------------"
    echo -e " $batch_name: $PASS_COUNT/$total PASS"
    if [ ${#FAILED_SCENARIOS[@]} -gt 0 ]; then
        echo " Failed scenarios:"
        for s in "${FAILED_SCENARIOS[@]}"; do
            echo "   - $s"
        done
    fi
    echo "----------------------------------------"
}
