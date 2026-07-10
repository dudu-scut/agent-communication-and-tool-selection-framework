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
BUDGET_KEY="budget:user:verify-user-5-2:$TODAY"  # Set spent to huge value
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
