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
