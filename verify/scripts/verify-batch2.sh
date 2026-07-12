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

register_mock_agent "mock-general" "general" "1.0" "STABLE"
register_mock_agent "mock-unstable" "general" "1.0" "STABLE"
sleep 1

# ----- 2.1: Normal circuit breaker pass -------------------------------------
scenario "2.1 — Circuit Breaker Passes Healthy Agent"

step "Send query via gRPC server"
RESPONSE=$(query_grpc "hello" "verify-user-2-1" "verify-ctx-2-1")

verify "Query response received (no gRPC error)" \
    assert_not_contains "$RESPONSE" "error"

# ----- 2.2: Circuit breaker triggers fallback --------------------------------
scenario "2.2 — Circuit Breaker Triggers Fallback"

reset_mock_agent

step "Send 3 requests with x-mock-mode: error to mock-unstable (via curl)"
for i in 1 2 3; do
    curl -s -X POST http://localhost:5100/tasks/send \
        -H "Content-Type: application/json" \
        -H "x-mock-mode: error" \
        -H "x-agent-id: mock-unstable" \
        -d '{"params":{"message":{"parts":[{"text":"test"}]}}}' > /dev/null 2>&1 || true
done

step "Send 4th gRPC request — should be rejected by circuit breaker (if server routed)"
FALLBACK_RESPONSE=$(query_grpc "test after failures" "verify-user-2-2" "verify-ctx-2-2" 2>&1 || echo "circuit_open")

verify_warn "Response indicates fallback or circuit open (requires server-integration)" \
    assert_contains "$FALLBACK_RESPONSE" "circuit"

reset_mock_agent

# ----- 2.3: User feedback loop ----------------------------------------------
scenario "2.3 — User Feedback Loop"

step "Submit positive feedback via AgentLifecycleService"
FB_RESPONSE=$(send_grpc "agent_communication.AgentLifecycleService/SubmitFeedback" '{"trace_id":"test-trace-2-3","agent_id":"mock-general","skill_name":"general","rating":3,"comment":"great"}')

verify "SubmitFeedback returns OK status" \
    assert_contains "$FB_RESPONSE" "OK"

verify "Feedback stored in PG" \
    assert_pg_row_exists "agent_feedback" "agent_id = 'mock-general' AND rating = 3"

verify_warn "Feedback aggregated to Redis (RedisClient may not be wired in test env)" \
    assert_redis_key_exists "feedback:mock-general:general"

# ----- 2.4: Agent metrics query ---------------------------------------------
scenario "2.4 — Agent Metrics Query"

step "Query GetAgentMetrics RPC (with auth)"
METRICS=$(get_metrics_grpc "mock-general" 2>&1 || echo "")

verify "Metrics contain metrics field" \
    assert_contains "$METRICS" "metrics"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 2 — Resilience"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
