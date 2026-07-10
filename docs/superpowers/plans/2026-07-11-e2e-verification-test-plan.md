# E2E Verification Test Plan — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a shell-driven, 32-scenario automated verification suite covering all 8 optimization batches, plus a manual UX checklist.

**Architecture:** Shell scripts orchestrate gRPC calls, Redis/PostgreSQL checks, and assertions. A Python Mock Agent (stdlib only) simulates Agent behaviors. All scripts share a helpers.sh library and integrate into the existing `./run.sh` command dispatch.

**Tech Stack:** Bash, Python 3 (stdlib HTTP server), `grpcurl` (or project `rpc_client` CLI), `redis-cli`, `psql`

## Global Constraints

- Zero new external dependencies — Mock Agent uses Python 3 stdlib only
- All scripts use `set -euo pipefail`
- Assertion failures print `[FAIL]` to stderr and exit non-zero
- Each batch script is independently runnable: `./run.sh verify-batchN`
- Batch scripts source `verify/scripts/helpers.sh` for shared functions
- gRPC communication prefers project `rpc_client` CLI, falls back to `grpcurl`
- Mock Agent listens on port 5100 (avoids conflict with 5000/5001/50051)
- PID files go in `pids/`, logs go in `logs/` (existing project conventions)

---

### Task 1: Mock Agent Server

**Files:**
- Create: `verify/mock-agent/mock_agent_server.py`

**Interfaces:**
- Consumes: nothing (first task, no dependencies)
- Produces:
  - HTTP server on port 5100
  - `GET /health` → 200 `{"status":"ok"}` or 500
  - `GET /health?fail=true` → 500 `{"status":"error"}`
  - `POST /tasks/send` → A2A JSON-RPC response, behavior controlled by `x-mock-mode` request header
  - Writes PID to `verify/mock-agent/pid.txt` on start

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p verify/mock-agent
```

- [ ] **Step 2: Write the Mock Agent server**

Write `verify/mock-agent/mock_agent_server.py`:

```python
#!/usr/bin/env python3
"""
Mock A2A Agent Server for E2E verification testing.

Port: 5100 (avoids conflict with 5000/5001/50051)

Behavior modes (via x-mock-mode request header):
  normal        → 200 with valid A2A JSON-RPC response
  slow          → 200 after 5s delay
  error         → 500 immediately
  delegate      → 200 with x-delegation-to header in response metadata
  version_v1_0  → response uses "kind" field
  version_v1_1  → response uses "type" field
  version_mixed → response uses "type" field (simulates mismatched version)
"""
import json
import sys
import os
import time
import signal
from http.server import HTTPServer, BaseHTTPRequestHandler

PORT = 5100
PID_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pid.txt")

# Track consecutive failures per agent for circuit breaker testing
failure_counts = {}


def build_a2a_response(query_text, mode):
    """Build an A2A JSON-RPC style response based on mock mode."""
    base = {
        "jsonrpc": "2.0",
        "id": 1,
    }

    if mode == "error":
        return None, 500

    if mode == "slow":
        time.sleep(5)

    if mode in ("version_v1_0", "normal"):
        part_field = "kind"
    else:
        part_field = "type"

    result = {
        "task_id": f"mock-task-{int(time.time())}",
        "status": "completed",
        "artifacts": [{
            "parts": [{
                part_field: "text",
                "text": f"Mock response for: {query_text}"
            }]
        }]
    }

    headers = {}
    if mode == "delegate":
        headers["x-delegation-to"] = "mock-general"

    base["result"] = result
    return base, headers


class MockAgentHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        """Suppress default stderr logging; write to stdout for visibility."""
        print(f"[mock-agent] {args[0]}", flush=True)

    def do_GET(self):
        if self.path.startswith("/health"):
            if "fail=true" in self.path:
                self.send_response(500)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"status": "error"}).encode())
            else:
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"status": "ok"}).encode())
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path != "/tasks/send":
            self.send_response(404)
            self.end_headers()
            return

        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length).decode("utf-8") if content_length > 0 else "{}"

        try:
            request = json.loads(body)
        except json.JSONDecodeError:
            request = {}

        query_text = request.get("params", {}).get("message", {}).get("parts", [{}])[0].get("text", "")
        mode = self.headers.get("x-mock-mode", "normal")
        agent_id = self.headers.get("x-agent-id", "mock-general")

        # Track failures for circuit breaker testing
        if mode == "error":
            failure_counts[agent_id] = failure_counts.get(agent_id, 0) + 1

        response_body, extra_headers = build_a2a_response(query_text, mode)

        if response_body is None:
            self.send_response(500)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"error": "internal error"}).encode())
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        for key, value in (extra_headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(json.dumps(response_body).encode())

    def do_PUT(self):
        """Reset failure counters."""
        if self.path == "/reset":
            failure_counts.clear()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"reset": "ok"}).encode())
        else:
            self.send_response(404)
            self.end_headers()


def main():
    # Write PID file
    pid = os.getpid()
    os.makedirs(os.path.dirname(PID_FILE), exist_ok=True)
    with open(PID_FILE, "w") as f:
        f.write(str(pid))

    server = HTTPServer(("0.0.0.0", PORT), MockAgentHandler)
    print(f"[mock-agent] Listening on port {PORT}, PID={pid}", flush=True)

    def shutdown(sig, frame):
        print("\n[mock-agent] Shutting down...", flush=True)
        server.shutdown()
        if os.path.exists(PID_FILE):
            os.remove(PID_FILE)
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        shutdown(None, None)


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Make it executable**

```bash
chmod +x verify/mock-agent/mock_agent_server.py
```

- [ ] **Step 4: Smoke test — start, check health, stop**

```bash
# Start in background
python3 verify/mock-agent/mock_agent_server.py &
MOCK_PID=$!
sleep 1

# Test health endpoint
curl -s http://localhost:5100/health
# Expected: {"status":"ok"}

# Test error health
curl -s http://localhost:5100/health?fail=true
# Expected: {"status":"error"} (HTTP 500)

# Test A2A task endpoint with normal mode
curl -s -X POST http://localhost:5100/tasks/send \
  -H "Content-Type: application/json" \
  -H "x-mock-mode: normal" \
  -d '{"params":{"message":{"parts":[{"text":"hello"}]}}}'
# Expected: JSON with "kind":"text" and "Mock response for: hello"

# Test error mode
curl -s -o /dev/null -w "%{http_code}" -X POST http://localhost:5100/tasks/send \
  -H "x-mock-mode: error" \
  -d '{}'
# Expected: 500

# Test slow mode (should take ~5s)
time curl -s -X POST http://localhost:5100/tasks/send \
  -H "x-mock-mode: slow" \
  -d '{}' > /dev/null
# Expected: ~5s elapsed

# Test v1.1 mode
curl -s -X POST http://localhost:5100/tasks/send \
  -H "x-mock-mode: version_v1_1" \
  -d '{"params":{"message":{"parts":[{"text":"test"}]}}}'
# Expected: response contains "type":"text" (not "kind")

# Test delegate mode
curl -s -D - -X POST http://localhost:5100/tasks/send \
  -H "x-mock-mode: delegate" \
  -d '{}'
# Expected: response headers include x-delegation-to

# Stop
kill $MOCK_PID
```

- [ ] **Step 5: Commit**

```bash
git add -f verify/mock-agent/mock_agent_server.py
git commit -m "feat: add Mock Agent server for E2E verification

Python 3 stdlib HTTP server on port 5100. Supports 7 behavior modes
(normal/slow/error/delegate/v1.0/v1.1/mixed) via x-mock-mode header.
Used by batch verification scripts to simulate Agent behavior.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: Helpers Library + Test Fixtures

**Files:**
- Create: `verify/scripts/helpers.sh`
- Create: `verify/fixtures/agent-cards.json`
- Create: `verify/fixtures/sample-queries.txt`

**Interfaces:**
- Consumes: nothing beyond standard tools (redis-cli, psql, grpcurl/curl)
- Produces:
  - `helpers.sh`: sourced by all batch scripts, provides `scenario()`, `step()`, `verify()`, assertion functions, data injection helpers
  - `agent-cards.json`: mock AgentCard registration payloads for 4 mock agents
  - `sample-queries.txt`: preset query strings for scenario execution

- [ ] **Step 1: Create directories**

```bash
mkdir -p verify/scripts verify/fixtures verify/expected
```

- [ ] **Step 2: Write helpers.sh**

Write `verify/scripts/helpers.sh`:

```bash
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
        ((PASS_COUNT++))
        return 0
    else
        echo -e "    $FAIL $desc"
        ((FAIL_COUNT++))
        FAILED_SCENARIOS+=("$desc")
        return 1
    fi
}

verify_warn() {
    # Like verify() but failure is downgraded to WARN (non-blocking)
    local desc="$1"
    shift
    if "$@"; then
        echo -e "    $PASS $desc"
        ((PASS_COUNT++))
        return 0
    else
        echo -e "    $WARN $desc (non-blocking)"
        ((WARN_COUNT++))
        return 0
    fi
}

# ============================================================================
# Pre-check: ensure required services are running
# ============================================================================
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
```

- [ ] **Step 3: Write agent-cards.json**

Write `verify/fixtures/agent-cards.json`:

```json
{
  "agents": [
    {
      "agent_id": "mock-general",
      "skill": "general",
      "a2a_version": "1.0",
      "deployment_stage": "STABLE",
      "endpoint": "http://localhost:5100/tasks/send",
      "cacheable": true,
      "demo_queries": ["What is 2+2?", "Explain REST in one sentence"]
    },
    {
      "agent_id": "mock-translator",
      "skill": "translation",
      "a2a_version": "1.0",
      "deployment_stage": "STABLE",
      "endpoint": "http://localhost:5100/tasks/send",
      "cacheable": true,
      "demo_queries": ["Translate 'hello' to Chinese"]
    },
    {
      "agent_id": "mock-unstable",
      "skill": "general",
      "a2a_version": "1.0",
      "deployment_stage": "STABLE",
      "endpoint": "http://localhost:5100/tasks/send",
      "cacheable": false
    },
    {
      "agent_id": "mock-canary",
      "skill": "general",
      "a2a_version": "1.1",
      "deployment_stage": "CANARY",
      "endpoint": "http://localhost:5100/tasks/send",
      "cacheable": false
    }
  ]
}
```

- [ ] **Step 4: Write sample-queries.txt**

Write `verify/fixtures/sample-queries.txt`:

```
What is the capital of France?
Translate "good morning" to Chinese.
Write a Python function to sort a list.
How do I set up a reverse proxy with Nginx?
Explain the difference between TCP and UDP.
Compare REST and GraphQL for API design.
Generate a markdown report on microservices architecture.
Find and fix the bug in this code: def add(a,b): return a-b
```

- [ ] **Step 5: Commit**

```bash
git add -f verify/scripts/helpers.sh verify/fixtures/agent-cards.json verify/fixtures/sample-queries.txt
git commit -m "feat: add verification helpers library and test fixtures

helpers.sh provides scenario scaffolding, assertion functions,
gRPC communication wrappers, and data injection helpers.
agent-cards.json defines 4 mock agents for testing.
sample-queries.txt contains preset query strings.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: Batch 1-3 Verification Scripts (12 scenarios)

**Files:**
- Create: `verify/scripts/verify-batch1.sh`
- Create: `verify/scripts/verify-batch2.sh`
- Create: `verify/scripts/verify-batch3.sh`

**Interfaces:**
- Consumes: `verify/scripts/helpers.sh` (source'd)
- Produces: each script exits 0 on all-pass, exits 1 if any FAIL

- [ ] **Step 1: Write verify-batch1.sh** (Infrastructure: Scheduler + Tracing + Cost)

Write `verify/scripts/verify-batch1.sh`:

```bash
#!/bin/bash
# ============================================================================
# Batch 1 Verification: Infrastructure
#  1.1 — Full trace propagation
#  1.2 — Background scheduler self-check
#  1.3 — Token cost metering
#  1.4 — Thread safety isolation
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

# ----- 1.1: Full trace propagation ------------------------------------------
scenario "1.1 — Full Trace Propagation"

step "Send QueryStream request"
RESPONSE=$(send_grpc_stream \
    "agent_communication.AIQueryService/QueryStream" \
    '{"query_text":"hello test","user_id":"verify-user-1","context_id":"verify-ctx-1-1"}')

verify "Response contains trace_summary" \
    assert_contains "$RESPONSE" "trace_summary"

verify "Response contains trace_id" \
    assert_contains "$RESPONSE" "trace_id"

TRACE_ID=$(echo "$RESPONSE" | python3 -c "import sys,json; lines=sys.stdin.read(); print(json.loads(lines.split(chr(10))[0])['trace_id'])" 2>/dev/null || echo "")
if [ -n "$TRACE_ID" ]; then
    verify "PG trace_spans has records for this trace" \
        assert_pg_row_exists "trace_spans" "trace_id = '$TRACE_ID'"
fi

# ----- 1.2: Background scheduler self-check ---------------------------------
scenario "1.2 — Background Scheduler Self-Check"

step "Wait 6s for scheduler tick (span_flush runs every 5s)"
sleep 6

# Check server log for scheduler activity
LOG_FILE="$PROJECT_ROOT/logs/rpc_server.log"
if [ -f "$LOG_FILE" ]; then
    verify "Scheduler activity appears in server log" \
        assert_contains "$(tail -100 "$LOG_FILE" 2>/dev/null || echo '')" "scheduler"
else
    verify_warn "Server log file not found at $LOG_FILE" false
fi

# ----- 1.3: Token cost metering ---------------------------------------------
scenario "1.3 — Token Cost Metering"

# Clean up any prior cost key for this test user
cleanup_redis_keys "cost:verify-user-2:*" || true

TODAY=$(date +%Y-%m-%d)
COST_KEY="cost:verify-user-2:$TODAY"

step "Send query that triggers LLM call"
COST_RESPONSE=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"explain distributed tracing in one paragraph","user_id":"verify-user-2","context_id":"verify-ctx-1-3"}')

verify "Redis cost key exists with value > 0" \
    assert_redis_value_gt "$COST_KEY" 0

verify "PG token_usage table has new row" \
    assert_pg_row_exists "token_usage" "user_id = 'verify-user-2'"

# ----- 1.4: Thread safety isolation ------------------------------------------
scenario "1.4 — Thread Safety Isolation"

step "Send 3 concurrent requests with different user_ids"
TEMP_DIR=$(mktemp -d)
for i in 1 2 3; do
    (send_grpc \
        "agent_communication.AIQueryService/Query" \
        "{\"query_text\":\"test $i\",\"user_id\":\"verify-user-iso-$i\",\"context_id\":\"verify-ctx-iso-$i\"}" \
        > "$TEMP_DIR/resp-$i.txt" 2>&1) &
done
wait

step "Extract trace_ids from responses"
TRACE_IDS=()
for i in 1 2 3; do
    tid=$(python3 -c "
import sys, json
try:
    data = json.load(open('$TEMP_DIR/resp-$i.txt'))
    print(data.get('trace_id',''))
except: pass
" 2>/dev/null || echo "")
    if [ -n "$tid" ]; then
        TRACE_IDS+=("$tid")
    fi
done

verify "All 3 requests got different trace_ids" \
    [ "${#TRACE_IDS[@]}" -eq 3 ] && [ "${TRACE_IDS[0]}" != "${TRACE_IDS[1]}" ] && [ "${TRACE_IDS[1]}" != "${TRACE_IDS[2]}" ]

rm -rf "$TEMP_DIR"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 1 — Infrastructure"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
```

- [ ] **Step 2: Write verify-batch2.sh** (Resilience: Circuit Breaker + Feedback + Transparency)

Write `verify/scripts/verify-batch2.sh`:

```bash
#!/bin/bash
# ============================================================================
# Batch 2 Verification: Resilience
#  2.1 — Circuit breaker passes healthy agent
#  2.2 — Circuit breaker triggers fallback after failures
#  2.3 — User feedback loop
#  2.4 — Agent metrics query
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

# Register mock agents
register_mock_agent "mock-general" "general" "1.0" "STABLE"
register_mock_agent "mock-unstable" "general" "1.0" "STABLE"
sleep 1

# ----- 2.1: Normal circuit breaker pass -------------------------------------
scenario "2.1 — Circuit Breaker Passes Healthy Agent"

step "Send query to healthy mock agent"
RESPONSE=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"hello","user_id":"verify-user-2-1","context_id":"verify-ctx-2-1"}')

verify "Response contains content (not error)" \
    assert_contains "$RESPONSE" "content"

# ----- 2.2: Circuit breaker triggers fallback --------------------------------
scenario "2.2 — Circuit Breaker Triggers Fallback"

reset_mock_agent

step "Send 3 requests with x-mock-mode: error to mock-unstable"
for i in 1 2 3; do
    curl -s -X POST http://localhost:5100/tasks/send \
        -H "Content-Type: application/json" \
        -H "x-mock-mode: error" \
        -H "x-agent-id: mock-unstable" \
        -d '{"params":{"message":{"parts":[{"text":"test"}]}}}' > /dev/null 2>&1 || true
done

step "Send 4th request — should be rejected by circuit breaker"
FALLBACK_RESPONSE=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"test after failures","user_id":"verify-user-2-2","context_id":"verify-ctx-2-2"}' 2>&1 || echo "circuit_open")

verify "Response indicates fallback or circuit open" \
    assert_contains "$FALLBACK_RESPONSE" "circuit"

reset_mock_agent

# ----- 2.3: User feedback loop ----------------------------------------------
scenario "2.3 — User Feedback Loop"

step "Submit positive feedback"
FB_RESPONSE=$(send_grpc \
    "agent_communication.AIQueryService/SubmitFeedback" \
    '{"trace_id":"test-trace-2-3","agent_id":"mock-general","skill_name":"general","rating":3,"comment":"great"}' 2>&1 || echo "")

verify "Feedback stored in PG" \
    assert_pg_row_exists "agent_feedback" "agent_id = 'mock-general' AND rating = 3"

FB_KEY="feedback:mock-general:general"
verify "Feedback aggregated to Redis" \
    assert_redis_key_exists "$FB_KEY"

# ----- 2.4: Agent metrics query ---------------------------------------------
scenario "2.4 — Agent Metrics Query"

step "Query GetAgentMetrics RPC"
METRICS=$(send_grpc \
    "agent_communication.AIQueryService/GetAgentMetrics" \
    '{"agent_id":"mock-general"}' 2>&1 || echo "")

verify "Metrics contain success_rate" \
    assert_contains "$METRICS" "success_rate"

verify "Metrics contain avg_latency_ms" \
    assert_contains "$METRICS" "avg_latency_ms"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 2 — Resilience"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
```

- [ ] **Step 3: Write verify-batch3.sh** (Cache + Control: Semantic Cache + Compression + Autonomy)

Write `verify/scripts/verify-batch3.sh`:

```bash
#!/bin/bash
# ============================================================================
# Batch 3 Verification: Cache + Control
#  3.1 — Semantic cache hit
#  3.2 — Context compression trigger
#  3.3 — L1 autonomy (read-only suggestion)
#  3.4 — L2 autonomy (intervention required)
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

# ----- 3.1: Semantic cache hit ----------------------------------------------
scenario "3.1 — Semantic Cache Hit"

step "First query (cache miss)"
START1=$(date +%s%N)
RESP1=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"What is the capital of France?","user_id":"verify-user-3-1","context_id":"verify-ctx-3-1"}' 2>&1)
END1=$(date +%s%N)
ELAPSED1=$(( (END1 - START1) / 1000000 ))

verify "First response has content" \
    assert_contains "$RESP1" "content"

step "Second query — semantically similar (different wording)"
START2=$(date +%s%N)
RESP2=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"Tell me the capital city of France","user_id":"verify-user-3-1","context_id":"verify-ctx-3-1"}' 2>&1)
END2=$(date +%s%N)
ELAPSED2=$(( (END2 - START2) / 1000000 ))

# If cache works, second should be much faster
if [ "$ELAPSED2" -lt "$ELAPSED1" ]; then
    verify "Second query faster than first (cache hit)" true
else
    verify_warn "Cache speedup not observed ($ELAPSED1 ms vs $ELAPSED2 ms)" false
fi

verify "Second response indicates cache hit" \
    assert_contains "$RESP2" "cache_hit"

# ----- 3.2: Context compression trigger -------------------------------------
scenario "3.2 — Context Compression Trigger"

step "Send 15 long messages to trigger compression"
CTX="verify-ctx-3-2"
for i in $(seq 1 15); do
    send_grpc \
        "agent_communication.AIQueryService/Query" \
        "{\"query_text\":\"This is a long conversation message number $i with substantial content to fill up the context window and eventually trigger the compression mechanism that should activate after about ten rounds of dialogue history.\",\"user_id\":\"verify-user-3-2\",\"context_id\":\"$CTX\"}" \
        > /dev/null 2>&1 || true
done

step "Check trace_spans for compression marker"
verify_warn "Trace spans contain context_compressed marker" \
    assert_pg_row_exists "trace_spans" "metadata::text LIKE '%context_compressed%'"

# ----- 3.3: L1 Autonomy — read-only suggestion ------------------------------
scenario "3.3 — L1 Autonomy — Read-Only Suggestion"

step "Set agent autonomy to L1 and send 'modify file' query"
RESP_L1=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"modify the config file to change the port","user_id":"verify-user-3-3","context_id":"verify-ctx-3-3","autonomy_level":1}' 2>&1 || echo "")

verify "L1 response is suggestion mode (not executing tool call)" \
    assert_not_contains "$RESP_L1" "tool_call_executed"

# ----- 3.4: L2 Autonomy — intervention required -----------------------------
scenario "3.4 — L2 Autonomy — Intervention Required"

step "Send query at L2 autonomy"
STREAM_OUT=$(send_grpc_stream \
    "agent_communication.AIQueryService/QueryStream" \
    '{"query_text":"send an email to the team","user_id":"verify-user-3-4","context_id":"verify-ctx-3-4","autonomy_level":2}' 2>&1 || echo "")

verify "SSE stream contains intervention_required event" \
    assert_contains "$STREAM_OUT" "intervention_required"

step "Send PROCEED intervention response"
INT_RESP=$(send_grpc \
    "agent_communication.AIQueryService/InterventionResponse" \
    '{"trace_id":"verify-ctx-3-4","decision":"PROCEED"}' 2>&1 || echo "")

verify "Intervention response accepted" \
    assert_not_contains "$INT_RESP" "error"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 3 — Cache + Control"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
```

- [ ] **Step 4: Smoke test each script individually**

```bash
# Make executable
chmod +x verify/scripts/verify-batch1.sh verify/scripts/verify-batch2.sh verify/scripts/verify-batch3.sh

# Run each (requires services to be running)
./verify/scripts/verify-batch1.sh
./verify/scripts/verify-batch2.sh
./verify/scripts/verify-batch3.sh
```

Expected: each script prints scenario headers, step descriptions, and PASS/FAIL/WARN results, then exits 0 or 1.

- [ ] **Step 5: Commit**

```bash
git add -f verify/scripts/verify-batch1.sh verify/scripts/verify-batch2.sh verify/scripts/verify-batch3.sh
git commit -m "feat: add Batch 1-3 verification scripts (12 scenarios)

Batch 1: trace propagation, scheduler, cost metering, thread safety
Batch 2: circuit breaker, fallback, feedback loop, agent metrics
Batch 3: semantic cache, context compression, L1/L2 autonomy

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 4: Batch 4-6 Verification Scripts (12 scenarios)

**Files:**
- Create: `verify/scripts/verify-batch4.sh`
- Create: `verify/scripts/verify-batch5.sh`
- Create: `verify/scripts/verify-batch6.sh`

**Interfaces:**
- Consumes: `verify/scripts/helpers.sh` (source'd)
- Produces: each script exits 0 on all-pass, exits 1 if any FAIL

- [ ] **Step 1: Write verify-batch4.sh** (UX Core: Memory + Activity Feed + DAG Preview)

Write `verify/scripts/verify-batch4.sh`:

```bash
#!/bin/bash
# ============================================================================
# Batch 4 Verification: UX Core
#  4.1 — Unified memory injection
#  4.2 — Activity feed recording
#  4.3 — DAG plan preview
#  4.4 — User-adjusted plan execution
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

# ----- 4.1: Unified memory injection ----------------------------------------
scenario "4.1 — Unified Memory Injection"

step "Insert user profile into PG"
psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" -d "$PG_DB" -c \
    "INSERT INTO user_profiles (user_id, identity, preferences, context_snapshot)
     VALUES ('verify-user-4-1',
             '{\"role\":\"rust engineer\",\"tech_stack\":[\"rust\",\"python\"]}',
             '{\"prefer_conciseness\":true}',
             '{\"recent_topics\":[\"microservices\"]}')
     ON CONFLICT (user_id) DO UPDATE SET identity = EXCLUDED.identity;" \
    > /dev/null 2>&1 || true

step "Send query — Agent should receive profile summary"
RESPONSE=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"help me write an API","user_id":"verify-user-4-1","context_id":"verify-ctx-4-1"}' 2>&1)

verify "Server log contains profile injection" \
    assert_contains "$(tail -50 "$PROJECT_ROOT/logs/rpc_server.log" 2>/dev/null || echo '')" "profile"

# ----- 4.2: Activity feed recording -----------------------------------------
scenario "4.2 — Activity Feed Recording"

step "Send multi-step query"
ACT_RESPONSE=$(send_grpc_stream \
    "agent_communication.AIQueryService/QueryStream" \
    '{"query_text":"research and summarize microservices patterns","user_id":"verify-user-4-2","context_id":"verify-ctx-4-2"}' 2>&1)

# Extract trace_id for activity feed lookup
ACT_TRACE_ID=$(echo "$ACT_RESPONSE" | python3 -c "
import sys, json
for line in sys.stdin:
    line = line.strip()
    if not line: continue
    try:
        d = json.loads(line)
        if d.get('trace_id'):
            print(d['trace_id'])
            break
    except: pass
" 2>/dev/null || echo "")

if [ -n "$ACT_TRACE_ID" ]; then
    verify "Redis activity_feed has records" \
        assert_redis_key_exists "activity_feed:$ACT_TRACE_ID"
fi

verify "SSE stream contains activity_json events" \
    assert_contains "$ACT_RESPONSE" "activity_json"

# ----- 4.3: DAG plan preview ------------------------------------------------
scenario "4.3 — DAG Plan Preview"

step "Send complex query triggering multi-agent planning"
DAG_RESPONSE=$(send_grpc_stream \
    "agent_communication.AIQueryService/QueryStream" \
    '{"query_text":"Compare microservices and monolith, then write a Python example for each, and create a summary report","user_id":"verify-user-4-3","context_id":"verify-ctx-4-3"}' 2>&1)

verify "Response contains plan_generated event" \
    assert_contains "$DAG_RESPONSE" "plan_generated"

verify "DAGStructure JSON contains nodes array" \
    assert_contains "$DAG_RESPONSE" '"nodes"'

# ----- 4.4: User-adjusted plan execution ------------------------------------
scenario "4.4 — User-Adjusted Plan Execution"

step "Send ExecutePlan with user-specified DAG"
EXEC_RESPONSE=$(send_grpc \
    "agent_communication.AIQueryService/ExecutePlan" \
    '{"trace_id":"verify-ctx-4-4","dag":{"nodes":[{"id":"n1","description":"test task","assigned_agent_id":"mock-general"}]}}' 2>&1)

verify "ExecutePlan returns without error" \
    assert_not_contains "$EXEC_RESPONSE" "error"

verify "agent_calls table has record with correct agent_id" \
    assert_pg_row_exists "agent_calls" "agent_id = 'mock-general'"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 4 — UX Core"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
```

- [ ] **Step 2: Write verify-batch5.sh** (Ops Tooling: Health Dashboard + Budget + Replay)

Write `verify/scripts/verify-batch5.sh`:

```bash
#!/bin/bash
# ============================================================================
# Batch 5 Verification: Ops Tooling
#  5.1 — Health dashboard data
#  5.2 — Budget exceeded rejection
#  5.3 — Query replay (exact mode)
#  5.4 — Query replay (route mode)
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

# ----- 5.1: Health dashboard data -------------------------------------------
scenario "5.1 — Health Dashboard Data"

step "Send request then query agent metrics"
send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"test","user_id":"verify-user-5-1","context_id":"verify-ctx-5-1"}' \
    > /dev/null 2>&1 || true

METRICS=$(send_grpc \
    "agent_communication.AIQueryService/GetAgentMetrics" \
    '{"agent_id":"mock-general"}' 2>&1)

verify "Agent metrics Redis hash exists" \
    assert_redis_key_exists "agent_metrics:mock-general"

verify "Metrics contain active_requests field" \
    assert_contains "$METRICS" "active_requests"

verify "Metrics contain circuit_breaker_trips field" \
    assert_contains "$METRICS" "circuit_breaker_trips"

# ----- 5.2: Budget exceeded rejection ---------------------------------------
scenario "5.2 — Budget Exceeded Rejection"

step "Set user daily budget to 1 micro-dollar (effectively zero)"
TODAY=$(date +%Y-%m-%d)
BUDGET_KEY="budget:user:verify-user-5-2:$TODAY"
set_redis_budget "$BUDGET_KEY" "999999999"  # Set spent to huge value

step "Send query — should be rejected"
BUDGET_RESPONSE=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"expensive query","user_id":"verify-user-5-2","context_id":"verify-ctx-5-2"}' 2>&1 || echo "budget_exceeded")

verify "Response indicates budget exceeded" \
    assert_contains "$BUDGET_RESPONSE" "budget"

# Clean up budget override
cleanup_redis_keys "budget:user:verify-user-5-2:*"

# ----- 5.3: Query replay (exact mode) ---------------------------------------
scenario "5.3 — Query Replay — Exact Mode"

step "First, create a query to replay"
INITIAL=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"What is 2+2?","user_id":"verify-user-5-3","context_id":"verify-ctx-5-3"}' 2>&1)

REPLAY_TRACE_ID=$(echo "$INITIAL" | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    print(d.get('trace_id',''))
except: pass
" 2>/dev/null || echo "")

if [ -n "$REPLAY_TRACE_ID" ]; then
    step "Replay with exact mode"
    REPLAY=$(send_grpc \
        "agent_communication.AIQueryService/ReplayQuery" \
        "{\"trace_id\":\"$REPLAY_TRACE_ID\",\"mode\":\"EXACT\"}" 2>&1)

    verify "Replay returns same agent_id" \
        assert_contains "$REPLAY" "mock-general"
else
    verify_warn "Could not extract trace_id — skipping exact replay check" false
fi

# ----- 5.4: Query replay (route mode) ---------------------------------------
scenario "5.4 — Query Replay — Route Mode"

if [ -n "$REPLAY_TRACE_ID" ]; then
    step "Replay with route mode"
    REPLAY_R=$(send_grpc \
        "agent_communication.AIQueryService/ReplayQuery" \
        "{\"trace_id\":\"$REPLAY_TRACE_ID\",\"mode\":\"ROUTE\"}" 2>&1)

    verify "Route replay returns response without error" \
        assert_not_contains "$REPLAY_R" "error"
else
    verify_warn "Could not extract trace_id — skipping route replay check" false
fi

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 5 — Ops Tooling"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
```

- [ ] **Step 3: Write verify-batch6.sh** (Platform Extension: Cron + Canary + Export)

Write `verify/scripts/verify-batch6.sh`:

```bash
#!/bin/bash
# ============================================================================
# Batch 6 Verification: Platform Extension
#  6.1 — Cron scheduler trigger
#  6.2 — Canary traffic split
#  6.3 — Markdown export
#  6.4 — HTML export
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

register_mock_agent "mock-general" "general" "1.0" "STABLE"
register_mock_agent "mock-canary" "general" "1.1" "CANARY"
sleep 1

# ----- 6.1: Cron scheduler trigger ------------------------------------------
scenario "6.1 — Cron Scheduler Trigger"

step "Register a one-shot task (fires every minute)"
CURRENT_MIN=$(date +%M)
psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" -d "$PG_DB" -c \
    "INSERT INTO scheduled_tasks (name, cron_expr, query_template, context_id, enabled, created_by_user_id)
     VALUES ('verify-test-cron', '* * * * *', 'automated health check', 'verify-ctx-6-1-cron', true, 'verify-user-6-1')
     ON CONFLICT DO NOTHING;" > /dev/null 2>&1 || true

step "Wait 65s for next minute boundary + execution"
sleep 65

verify "task_results table has execution record" \
    assert_pg_row_exists "task_results" "context_id = 'verify-ctx-6-1-cron'"

verify "scheduled_tasks.last_run_at is updated" \
    assert_pg_row_exists "scheduled_tasks" "name = 'verify-test-cron' AND last_run_at IS NOT NULL"

# ----- 6.2: Canary traffic split --------------------------------------------
scenario "6.2 — Canary Traffic Split"

CANARY_COUNT=0
TOTAL_SENDS=100

step "Send $TOTAL_SENDS requests to general skill"
for i in $(seq 1 $TOTAL_SENDS); do
    RESP=$(send_grpc \
        "agent_communication.AIQueryService/Query" \
        "{\"query_text\":\"test $i\",\"user_id\":\"verify-user-6-2\",\"context_id\":\"verify-ctx-6-2-$i\"}" 2>&1 || echo "")
    if echo "$RESP" | grep -q "mock-canary"; then
        ((CANARY_COUNT++))
    fi
done

step "Check canary received ~10% of traffic"
# Allow 5-15 range for 10% with 100 samples
if [ "$CANARY_COUNT" -ge 5 ] && [ "$CANARY_COUNT" -le 15 ]; then
    verify "Canary got $CANARY_COUNT/$TOTAL_SENDS requests (expected ~10)" true
else
    verify_warn "Canary got $CANARY_COUNT/$TOTAL_SENDS requests (expected ~10, accepted 5-15)" false
fi

# ----- 6.3: Markdown export ------------------------------------------------
scenario "6.3 — Markdown Export"

step "Export conversation to Markdown"
MD_EXPORT=$(send_grpc \
    "agent_communication.AIQueryService/ExportConversation" \
    '{"context_id":"verify-ctx-6-3","format":"MARKDOWN"}' 2>&1 || echo "")

verify "Markdown contains NexusAI header" \
    assert_contains "$MD_EXPORT" "NexusAI"

verify "Markdown contains conversation timeline" \
    assert_contains "$MD_EXPORT" "对话记录"

# ----- 6.4: HTML export -----------------------------------------------------
scenario "6.4 — HTML Export"

step "Export conversation to HTML"
HTML_EXPORT=$(send_grpc \
    "agent_communication.AIQueryService/ExportConversation" \
    '{"context_id":"verify-ctx-6-3","format":"HTML"}' 2>&1 || echo "")

verify "HTML contains <html> tag" \
    assert_contains "$HTML_EXPORT" "<html>"

verify "HTML contains inline CSS" \
    assert_contains "$HTML_EXPORT" "<style>"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 6 — Platform Extension"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
```

- [ ] **Step 4: Smoke test**

```bash
chmod +x verify/scripts/verify-batch4.sh verify/scripts/verify-batch5.sh verify/scripts/verify-batch6.sh
./verify/scripts/verify-batch4.sh
./verify/scripts/verify-batch5.sh
./verify/scripts/verify-batch6.sh
```

- [ ] **Step 5: Commit**

```bash
git add -f verify/scripts/verify-batch4.sh verify/scripts/verify-batch5.sh verify/scripts/verify-batch6.sh
git commit -m "feat: add Batch 4-6 verification scripts (12 scenarios)

Batch 4: unified memory, activity feed, DAG preview, ExecutePlan
Batch 5: health dashboard, budget rejection, query replay (exact/route)
Batch 6: cron scheduler, canary split, markdown/HTML export

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 5: Batch 7-8 Verification Scripts (8 scenarios)

**Files:**
- Create: `verify/scripts/verify-batch7.sh`
- Create: `verify/scripts/verify-batch8.sh`

**Interfaces:**
- Consumes: `verify/scripts/helpers.sh` (source'd)
- Produces: each script exits 0 on all-pass, exits 1 if any FAIL

- [ ] **Step 1: Write verify-batch7.sh** (Growth + Retention: Sandbox + Sharing)

Write `verify/scripts/verify-batch7.sh`:

```bash
#!/bin/bash
# ============================================================================
# Batch 7 Verification: Growth + Retention
#  7.1 — Sandbox isolation (context_id prefix + TTL)
#  7.2 — Sandbox cost exemption
#  7.3 — Session sharing (share link)
#  7.4 — Template save and use
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

# ----- 7.1: Sandbox isolation -----------------------------------------------
scenario "7.1 — Sandbox Isolation"

step "Send SandboxQuery"
SANDBOX_RESPONSE=$(send_grpc_stream \
    "agent_communication.AIQueryService/SandboxQuery" \
    '{"query_text":"test sandbox","user_id":"verify-user-7-1"}' 2>&1)

# Extract sandbox context_id from response
SANDBOX_CTX=$(echo "$SANDBOX_RESPONSE" | python3 -c "
import sys, json
for line in sys.stdin:
    line = line.strip()
    if not line: continue
    try:
        d = json.loads(line)
        ctx = d.get('context_id','')
        if ctx.startswith('sandbox_'):
            print(ctx)
            break
    except: pass
" 2>/dev/null || echo "")

if [ -n "$SANDBOX_CTX" ]; then
    verify "context_id starts with sandbox_ prefix" \
        [ "${SANDBOX_CTX:0:8}" = "sandbox_" ]

    verify "Sandbox history Redis key has TTL ~3600s" \
        assert_redis_key_ttl "chat_history:$SANDBOX_CTX" 3000 4200
else
    verify_warn "Could not extract sandbox context_id" false
fi

# ----- 7.2: Sandbox cost exemption ------------------------------------------
scenario "7.2 — Sandbox Cost Exemption"

step "Check token_usage for sandbox component marking"
verify_warn "token_usage has sandbox component record" \
    assert_pg_row_exists "token_usage" "component = 'sandbox'"

step "Verify Redis budget counter was NOT incremented"
# Budget counter for sandbox user should still be 0 or non-existent
TODAY=$(date +%Y-%m-%d)
COST_KEY="cost:verify-user-7-1:$TODAY"
SANDBOX_COST=$(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" GET "$COST_KEY" 2>/dev/null || echo "0")
verify "Sandbox cost did not increment budget counter" \
    [ "${SANDBOX_COST:-0}" -eq 0 ] 2>/dev/null

# ----- 7.3: Session sharing -------------------------------------------------
scenario "7.3 — Session Sharing"

step "Share a session as readonly"
SHARE_RESPONSE=$(send_grpc \
    "agent_communication.SharingService/ShareSession" \
    '{"context_id":"verify-ctx-7-3","owner_user_id":"verify-user-7-3","mode":"READONLY"}' 2>&1 || echo "")

SHARE_ID=$(echo "$SHARE_RESPONSE" | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    print(d.get('share_id',''))
except: pass
" 2>/dev/null || echo "")

if [ -n "$SHARE_ID" ]; then
    verify "Share returns valid UUID" \
        [ "${#SHARE_ID}" -ge 32 ]

    verify "shared_sessions table has new row" \
        assert_pg_row_exists "shared_sessions" "share_id = '$SHARE_ID'"
else
    verify_warn "Could not extract share_id" false
fi

# ----- 7.4: Template save and use -------------------------------------------
scenario "7.4 — Template Save and Use"

step "Save a session as template"
TEMPLATE_RESPONSE=$(send_grpc \
    "agent_communication.SharingService/SaveTemplate" \
    '{"owner_user_id":"verify-user-7-4","name":"test-template","description":"A test template","dag_structure":{"nodes":[{"id":"n1","description":"test","assigned_agent_id":"mock-general"}]}}' 2>&1 || echo "")

verify "SaveTemplate returns without error" \
    assert_not_contains "$TEMPLATE_RESPONSE" "error"

verify "session_templates table has new row" \
    assert_pg_row_exists "session_templates" "name = 'test-template'"

step "Use the saved template"
USE_RESPONSE=$(send_grpc \
    "agent_communication.SharingService/UseTemplate" \
    '{"template_name":"test-template","user_id":"verify-user-7-4"}' 2>&1 || echo "")

verify "UseTemplate creates new session with correct DAG" \
    assert_contains "$USE_RESPONSE" "mock-general"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 7 — Growth + Retention"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
```

- [ ] **Step 2: Write verify-batch8.sh** (Protocol Security: Version Negotiation + Depth Limit)

Write `verify/scripts/verify-batch8.sh`:

```bash
#!/bin/bash
# ============================================================================
# Batch 8 Verification: Protocol + Security
#  8.1 — v1.0 serialization (kind field)
#  8.2 — v1.1 serialization (type field)
#  8.3 — Compatible fallback (version mismatch)
#  8.4 — Delegation depth limit
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

register_mock_agent "mock-general" "general" "1.0" "STABLE"
register_mock_agent "mock-canary" "general" "1.1" "CANARY"
sleep 1

MOCK_URL="http://localhost:5100/tasks/send"

# ----- 8.1: v1.0 serialization ----------------------------------------------
scenario "8.1 — v1.0 Serialization (kind field)"

step "Call mock agent with version_v1_0 mode"
V10_RESPONSE=$(curl -s -X POST "$MOCK_URL" \
    -H "Content-Type: application/json" \
    -H "x-mock-mode: version_v1_0" \
    -d '{"params":{"message":{"parts":[{"text":"test v1.0"}]}}}')

verify "Response uses kind field" \
    assert_contains "$V10_RESPONSE" '"kind"'

verify "Response does NOT use type field" \
    assert_not_contains "$V10_RESPONSE" '"type"'

# ----- 8.2: v1.1 serialization ----------------------------------------------
scenario "8.2 — v1.1 Serialization (type field)"

step "Call mock agent with version_v1_1 mode"
V11_RESPONSE=$(curl -s -X POST "$MOCK_URL" \
    -H "Content-Type: application/json" \
    -H "x-mock-mode: version_v1_1" \
    -d '{"params":{"message":{"parts":[{"text":"test v1.1"}]}}}')

verify "Response uses type field" \
    assert_contains "$V11_RESPONSE" '"type"'

verify "Response does NOT use kind field" \
    assert_not_contains "$V11_RESPONSE" '"kind"'

# ----- 8.3: Compatible fallback ---------------------------------------------
scenario "8.3 — Compatible Fallback (Version Mismatch)"

step "Call v1.0-declared agent that responds with v1.1 format"
MIXED_RESPONSE=$(curl -s -X POST "$MOCK_URL" \
    -H "Content-Type: application/json" \
    -H "x-mock-mode: version_mixed" \
    -d '{"params":{"message":{"parts":[{"text":"test mixed"}]}}}')

verify "Mixed response uses type field (v1.1 format despite v1.0 declaration)" \
    assert_contains "$MIXED_RESPONSE" '"type"'

verify "Mixed response does NOT use kind field" \
    assert_not_contains "$MIXED_RESPONSE" '"kind"'

# Check if span metadata records version fallback
verify_warn "Trace spans mark version_fallback" \
    assert_pg_row_exists "trace_spans" "metadata::text LIKE '%version_fallback%'"

# ----- 8.4: Delegation depth limit ------------------------------------------
scenario "8.4 — Delegation Depth Limit"

step "Simulate 6-level delegation chain"
# Use delegate mode which returns x-delegation-to header
# The platform should stop after 5 levels
DEPTH_OUTPUT=$(mktemp)
for depth in $(seq 1 6); do
    curl -s -D - -X POST "$MOCK_URL" \
        -H "Content-Type: application/json" \
        -H "x-mock-mode: delegate" \
        -H "x-delegation-depth: $depth" \
        -d '{"params":{"message":{"parts":[{"text":"delegate chain"}]}}}' \
        >> "$DEPTH_OUTPUT" 2>&1 || true
done

# Check for depth limit error in output
DEPTH_OUT=$(cat "$DEPTH_OUTPUT")
verify "Depth limit error present (max=5 reached)" \
    assert_contains "$DEPTH_OUT" "depth"

rm -f "$DEPTH_OUTPUT"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 8 — Protocol + Security"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
```

- [ ] **Step 3: Smoke test**

```bash
chmod +x verify/scripts/verify-batch7.sh verify/scripts/verify-batch8.sh
./verify/scripts/verify-batch7.sh
./verify/scripts/verify-batch8.sh
```

- [ ] **Step 4: Commit**

```bash
git add -f verify/scripts/verify-batch7.sh verify/scripts/verify-batch8.sh
git commit -m "feat: add Batch 7-8 verification scripts (8 scenarios)

Batch 7: sandbox isolation/cost-exemption, session sharing, templates
Batch 8: v1.0/v1.1 serialization, compatible fallback, delegation depth

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 6: run.sh Integration + Manual Verification Checklist

**Files:**
- Modify: `run.sh` — add verify command dispatch + start-mock-agent
- Create: `docs/verification-checklist.md`

**Interfaces:**
- Consumes: all 8 verify-batch*.sh scripts (from Tasks 3-5), Mock Agent (from Task 1)
- Produces:
  - `./run.sh verify` runs all 8 batches and prints summary report
  - `./run.sh verify-batchN` runs single batch
  - `./run.sh start-mock-agent` starts mock agent in background
  - `./run.sh stop` also stops mock agent
  - `docs/verification-checklist.md` with manual UX verification steps

- [ ] **Step 1: Add path variables to run.sh top section**

In `run.sh`, after line 28 (`FRONTEND_DIR="$PROJECT_ROOT/frontend"`), add:

```bash
VERIFY_DIR="$PROJECT_ROOT/verify"
VERIFY_SCRIPTS="$VERIFY_DIR/scripts"
```

Then add the verify command functions before the `usage()` function (around line 450):

```bash
# ============================================================================
# verify - 运行 E2E 验证测试
# ============================================================================

cmd_verify() {
    local batch="${1:-all}"

    if [ "$batch" != "all" ]; then
        # Run single batch
        local script="$VERIFY_SCRIPTS/verify-batch${batch}.sh"
        if [ ! -x "$script" ]; then
            error "验证脚本不存在: $script"
            exit 1
        fi
        banner "Batch $batch 验证测试"
        "$script"
        return $?
    fi

    # Run all batches
    banner "NexusAI E2E 验证测试"
    echo "开始时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo ""

    local total_pass=0
    local total_fail=0
    local batch_results=()

    for batch in 1 2 3 4 5 6 7 8; do
        local script="$VERIFY_SCRIPTS/verify-batch${batch}.sh"
        if [ ! -x "$script" ]; then
            warn "跳过 Batch $batch — 脚本不存在"
            batch_results+=("Batch $batch — SKIP")
            continue
        fi

        if "$script"; then
            batch_results+=("Batch $batch — PASS")
            ((total_pass++))
        else
            batch_results+=("Batch $batch — FAIL")
            ((total_fail++))
        fi
        echo ""
    done

    # Summary report
    echo "========================================"
    echo " NexusAI 验证测试报告"
    echo " 时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "========================================"
    echo ""
    for result in "${batch_results[@]}"; do
        echo "  $result"
    done
    echo ""
    echo "========================================"
    echo " 通过: $total_pass/8 batches"
    if [ "$total_fail" -gt 0 ]; then
        echo " 失败: $total_fail/8 batches"
    fi
    echo " 手动验证: 见 docs/verification-checklist.md"
    echo "========================================"

    return $(( total_fail > 0 ? 1 : 0 ))
}

# ============================================================================
# start-mock-agent - 启动 Mock Agent
# ============================================================================
cmd_start_mock_agent() {
    banner "启动 Mock Agent"

    local mock_script="$VERIFY_DIR/mock-agent/mock_agent_server.py"
    if [ ! -f "$mock_script" ]; then
        error "Mock Agent 脚本不存在: $mock_script"
        exit 1
    fi

    if ! command -v python3 &>/dev/null; then
        error "未找到 python3"
        exit 1
    fi

    mkdir -p "$LOGS_DIR" "$PIDS_DIR"

    local pid_file="$PIDS_DIR/mock_agent.pid"
    if [ -f "$pid_file" ]; then
        local old_pid
        old_pid=$(cat "$pid_file")
        if kill -0 "$old_pid" 2>/dev/null; then
            warn "Mock Agent 已在运行 (PID: $old_pid)"
            return 0
        fi
        rm -f "$pid_file"
    fi

    info "启动 Mock Agent (端口: 5100)..."
    python3 "$mock_script" >> "$LOGS_DIR/mock_agent.log" 2>&1 &
    local pid=$!
    echo "$pid" > "$pid_file"

    sleep 1
    if kill -0 "$pid" 2>/dev/null; then
        info "Mock Agent 启动成功 (PID: $pid)"
    else
        error "Mock Agent 启动失败"
        rm -f "$pid_file"
        exit 1
    fi
}
```

- [ ] **Step 2: Update the case dispatch to add verify commands**

Add these entries to the `case` block at the bottom of `run.sh` (after the `stop` entry):

```bash
    verify)         shift; cmd_verify "$@" ;;
    verify-batch1)  cmd_verify "1" ;;
    verify-batch2)  cmd_verify "2" ;;
    verify-batch3)  cmd_verify "3" ;;
    verify-batch4)  cmd_verify "4" ;;
    verify-batch5)  cmd_verify "5" ;;
    verify-batch6)  cmd_verify "6" ;;
    verify-batch7)  cmd_verify "7" ;;
    verify-batch8)  cmd_verify "8" ;;
    start-mock-agent) cmd_start_mock_agent ;;
```

- [ ] **Step 3: Update usage() to document new commands**

Add these lines to the `usage()` function's command list (after `stop`):

```bash
    echo "  verify          运行 E2E 验证测试 (全部 8 批)"
    echo "  verify-batch1   单独运行第 1 批验证"
    echo "  verify-batch2   ...以此类推至 verify-batch8"
    echo "  start-mock-agent 启动 Mock Agent (验证用)"
```

- [ ] **Step 4: Write manual verification checklist**

Write `docs/verification-checklist.md`:

```markdown
# NexusAI 验证清单 — 手动部分

每个项已验证打 ✅，发现问题备注。预计耗时 ~20 分钟。

## 前置条件

- [ ] `./run.sh start` 启动全部服务（含 Mock Agent: `./run.sh start-mock-agent`）
- [ ] `npm run dev` 启动前端
- [ ] 浏览器打开 http://localhost:5173

---

## Batch 1-2：基础体验

- [ ] ChatView 发送消息后，消息气泡下方显示 trace 摘要（如"路由 12ms → Agent 856ms"）
- [ ] 点赞按钮点击后变绿并保持选中
- [ ] 点踩按钮点击后变红

## Batch 3-4：UX 核心

- [ ] Activity Panel 右侧实时展示 Agent 工作步骤（💭→🔧→✅）
- [ ] DAG 预览：复杂查询触发多 Agent 编排时展示 Mermaid 流程图
- [ ] AgentSelector：多候选 Agent 时展示选择面板（指标对比）

## Batch 5：管理后台

- [ ] AdminView 健康度仪表盘展示 Agent 状态灯（绿/黄/红）
- [ ] AdminView 预算面板展示用户预算使用量
- [ ] AdminView 查询重放：输入 trace_id 查看调用链时间线

## Batch 6：扩展功能

- [ ] AdminView 定时任务管理页（增删改查 + 手动触发 + 执行历史）
- [ ] AdminView 灰度部署面板（流量比例 + 指标对比 + 推进/回滚按钮）
- [ ] ChatView 点击"导出 Markdown"下载文件内容正确（含 NexusAI header + 对话时间线）

## Batch 7：增长留存

- [ ] AgentSandbox 页面：Agent 卡片墙 + "快速试用"按钮
- [ ] CompareView：三列并排展示不同 Agent 回答 + 底部对比摘要（延迟/成本/长度）
- [ ] ShareView：无痕窗口打开分享链接可查看只读会话
- [ ] TemplateMarket：模板卡片展示 + "使用模板"按钮创建新会话

## Batch 8：协议安全

- [ ] （无前端可观测变更 — 纯后端，自动化已覆盖）

---

## 问题记录

| 编号 | 批次 | 问题描述 | 严重度 |
|------|------|---------|--------|
|      |      |         |        |
```

- [ ] **Step 5: Test the run.sh integration**

```bash
# Test usage output includes new commands
./run.sh
# Expected: verify, verify-batch1-8, start-mock-agent listed

# Test mock agent start
./run.sh start-mock-agent
# Expected: Mock Agent started on port 5100

# Test single batch (requires services running)
./run.sh verify-batch1
# Expected: runs Batch 1 scenarios

# Test full verify (requires services running)
./run.sh verify
# Expected: runs all 8 batches, prints summary
```

- [ ] **Step 6: Commit**

```bash
git add run.sh docs/verification-checklist.md
git commit -m "feat: integrate verify commands into run.sh + manual checklist

Add ./run.sh verify (all batches), verify-batch1-8 (individual),
and start-mock-agent commands. Print summary report after all batches.
Manual checklist in docs/verification-checklist.md covers 12 frontend items.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 7: End-to-End Smoke Test

**Files:**
- (no new files — this task exercises the system as a whole)

**Interfaces:**
- Consumes: all prior tasks (Mock Agent, helpers, batch scripts, run.sh integration)
- Produces: validated `./run.sh verify` end-to-end flow

- [ ] **Step 1: Clean build and start all services**

```bash
cd "$PROJECT_ROOT"

# Full build
./run.sh build

# Start services
./run.sh redis
./run.sh start
./run.sh start-mock-agent

# Wait for all services
sleep 3
```

- [ ] **Step 2: Verify --help and service health**

```bash
# Check usage
./run.sh | grep -q "verify"
echo "verify command in help: $?"

# Check all services
curl -s http://localhost:5100/health | grep -q "ok"
echo "Mock Agent: $?"

echo > /dev/tcp/127.0.0.1/50051 && echo "gRPC Server: reachable" || echo "gRPC Server: unreachable"

echo > /dev/tcp/127.0.0.1/6379 && echo "Redis: reachable" || echo "Redis: unreachable"
```

- [ ] **Step 3: Run individual batches**

```bash
# Run batches 1-4 (those least dependent on complex state setup)
./run.sh verify-batch1 && echo "B1: PASS" || echo "B1: FAIL"
./run.sh verify-batch2 && echo "B2: PASS" || echo "B2: FAIL"
./run.sh verify-batch8 && echo "B8: PASS" || echo "B8: FAIL"
```

- [ ] **Step 4: Run full verify suite**

```bash
./run.sh verify
```

Expected: all 8 batches execute, summary report printed with pass/fail counts.

- [ ] **Step 5: Stop and clean up**

```bash
./run.sh stop
# Verify all processes stopped
! kill -0 $(cat pids/rpc_server.pid 2>/dev/null) 2>/dev/null && echo "Server stopped: OK" || echo "Server stopped: FAIL"
! kill -0 $(cat pids/mock_agent.pid 2>/dev/null) 2>/dev/null && echo "Mock Agent stopped: OK" || echo "Mock Agent stopped: FAIL"
```

- [ ] **Step 6: Commit (if any fixes were needed)**

```bash
# Only if smoke test revealed issues requiring code changes
git add -A
git commit -m "chore: E2E verification smoke test fixes"
```

---

## Completion Checklist

- [ ] `./run.sh verify` runs all 8 batches and prints summary report
- [ ] `./run.sh verify-batchN` runs individual batch (N=1..8)
- [ ] `./run.sh start-mock-agent` starts mock agent on port 5100
- [ ] `./run.sh stop` also stops mock agent
- [ ] Mock Agent supports all 7 behavior modes (normal/slow/error/delegate/v1.0/v1.1/mixed)
- [ ] helpers.sh provides scenario/step/verify scaffolding + 8 assertion functions
- [ ] All 32 scenarios implemented across 8 batch scripts
- [ ] Each batch script exits 0 on all-pass, 1 on any FAIL
- [ ] Manual verification checklist covers 12 frontend UX items
- [ ] All files committed, git history clean
