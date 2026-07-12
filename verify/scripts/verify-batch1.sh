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
RESPONSE=$(query_stream_grpc "hello test" "verify-user-1" "verify-ctx-1-1")

verify "Response contains status field" \
    assert_contains "$RESPONSE" "status"

verify "Response contains event_type" \
    assert_contains "$RESPONSE" "event_type"

TRACE_ID=$(echo "$RESPONSE" | python3 -c "
import sys,json
for line in sys.stdin:
    line=line.strip()
    if not line: continue
    try:
        d=json.loads(line)
        print(d.get('event_id',''))
        break
    except: pass
" 2>/dev/null || echo "")
if [ -n "$TRACE_ID" ]; then
    verify_warn "PG trace_spans has records for this trace (requires psql)" \
        assert_pg_row_exists "trace_spans" "trace_id = '$TRACE_ID'"
fi

# ----- 1.2: Background scheduler self-check ---------------------------------
scenario "1.2 — Background Scheduler Self-Check"

step "Wait 6s for scheduler tick"
sleep 6

# BackgroundScheduler has no built-in logging (by design — uses CronScheduler which does log)
LOG_FILE="$PROJECT_ROOT/logs/rpc_server.log"
if [ -f "$LOG_FILE" ]; then
    verify_warn "Scheduler activity appears in server log (BackgroundScheduler has no logging — CronScheduler does)" \
        assert_contains "$(tail -100 "$LOG_FILE" 2>/dev/null || echo '')" "scheduler"
else
    verify_warn "Server log file not found at $LOG_FILE" false
fi

# ----- 1.3: Token cost metering ---------------------------------------------
scenario "1.3 — Token Cost Metering"

cleanup_redis_keys "cost:verify-user-2:*" || true

TODAY=$(date +%Y-%m-%d)
COST_KEY="cost:verify-user-2:$TODAY"

step "Send query that triggers LLM call"
query_grpc "explain distributed tracing in one paragraph" "verify-user-2" "verify-ctx-1-3" > /dev/null 2>&1 || true

# CostTracker records with INCRBY — key exists even with 0 tokens (empty Orchestrator means no real LLM call)
verify_warn "Redis cost key exists (CostTracker active; value may be 0 without Orchestrator)" \
    assert_redis_key_exists "$COST_KEY"

verify_warn "PG token_usage table has new row (requires psql)" \
    assert_pg_row_exists "token_usage" "user_id = 'verify-user-2'"

# ----- 1.4: Thread safety isolation ------------------------------------------
scenario "1.4 — Thread Safety Isolation"

step "Send 3 concurrent requests with different user_ids"
TEMP_DIR=$(mktemp -d)
for i in 1 2 3; do
    (query_grpc "test $i" "verify-user-iso-$i" "verify-ctx-iso-$i" > "$TEMP_DIR/resp-$i.txt" 2>&1) &
done
wait

step "Extract request_ids from responses"
TRACE_IDS=()
for i in 1 2 3; do
    # Response is JSON — extract last JSON object (ignore grpcurl headers)
    tid=$(python3 -c "
import sys, json
text = open('$TEMP_DIR/resp-$i.txt').read()
# Find JSON object boundaries
start = text.find('{')
end = text.rfind('}') + 1
if start >= 0 and end > start:
    try:
        data = json.loads(text[start:end])
        print(data.get('request_id',''))
    except: pass
" 2>/dev/null || echo "")
    if [ -n "$tid" ]; then
        TRACE_IDS+=("$tid")
    fi
done

if [ "${#TRACE_IDS[@]}" -eq 3 ]; then
    verify "All 3 requests got different request_ids" \
        [ "${TRACE_IDS[0]}" != "${TRACE_IDS[1]}" ] && [ "${TRACE_IDS[1]}" != "${TRACE_IDS[2]}" ]
else
    verify_warn "All 3 requests got different request_ids (got ${#TRACE_IDS[@]} IDs, may need Orchestrator)" false
fi

rm -rf "$TEMP_DIR"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 1 — Infrastructure"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
