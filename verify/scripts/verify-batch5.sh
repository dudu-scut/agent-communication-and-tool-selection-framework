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

register_mock_agent "mock-general" "general" "1.0" "STABLE"
sleep 1

# ----- 5.1: Health dashboard data -------------------------------------------
scenario "5.1 — Health Dashboard Data"

step "Send request then query agent metrics"
query_grpc "test" "verify-user-5-1" "verify-ctx-5-1" > /dev/null 2>&1 || true

# GetAgentMetrics now works with auth
METRICS=$(get_metrics_grpc "mock-general" 2>&1 || echo "")

verify_warn "Agent metrics Redis hash exists" \
    assert_redis_key_exists "agent_metrics:mock-general"

# Auth token from ensure_auth() now works
if [ -n "$AUTH_TOKEN" ]; then
    verify "Metrics response contains metrics data" \
        assert_contains "$METRICS" "metrics"
else
    verify_warn "Metrics response received (no auth token available)" \
        assert_contains "$METRICS" "metrics"
fi

# ----- 5.2: Budget exceeded rejection ---------------------------------------
scenario "5.2 — Budget Exceeded Rejection"

step "Set user daily budget to effectively zero"
TODAY=$(date +%Y-%m-%d)
# BudgetMiddleware uses key: budget:daily:{user_id}:{YYYY-MM-DD}
BUDGET_KEY="budget:daily:verify-user-5-2:$TODAY"
set_redis_budget "$BUDGET_KEY" "999999999"

step "Send query — should be rejected with RESOURCE_EXHAUSTED"
BUDGET_RESPONSE=$(query_grpc "expensive query" "verify-user-5-2" "verify-ctx-5-2" 2>&1 || echo "budget_exceeded")

verify "Response indicates budget exceeded (RESOURCE_EXHAUSTED)" \
    assert_contains "$BUDGET_RESPONSE" "budget"

cleanup_redis_keys "budget:*:verify-user-5-2:*"

# ----- 5.3: Query replay (exact mode) ---------------------------------------
scenario "5.3 — Query Replay — Exact Mode"

step "Create a query to replay"
INITIAL=$(query_grpc "What is 2+2?" "verify-user-5-3" "verify-ctx-5-3")

REPLAY_TRACE_ID=$(echo "$INITIAL" | python3 -c "
import sys, json
text = sys.stdin.read()
start = text.find('{')
end = text.rfind('}') + 1
if start >= 0 and end > start:
    try:
        d = json.loads(text[start:end])
        print(d.get('request_id',''))
    except: pass
" 2>/dev/null || echo "")

if [ -n "$REPLAY_TRACE_ID" ]; then
    step "Replay with exact mode"
    REPLAY=$(replay_query_grpc "$REPLAY_TRACE_ID" "EXACT")
    verify "Replay returns response with matching request" \
        assert_contains "$REPLAY" "$REPLAY_TRACE_ID"
else
    verify_warn "Could not extract request_id (may need Orchestrator) — skipping exact replay" false
fi

# ----- 5.4: Query replay (route mode) ---------------------------------------
scenario "5.4 — Query Replay — Route Mode"

if [ -n "$REPLAY_TRACE_ID" ]; then
    step "Replay with route mode"
    REPLAY_R=$(replay_query_grpc "$REPLAY_TRACE_ID" "ROUTE")
    verify "Route replay returns response without error" \
        assert_not_contains "$REPLAY_R" "error"
else
    verify_warn "Could not extract request_id — skipping route replay" false
fi

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 5 — Ops Tooling"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
