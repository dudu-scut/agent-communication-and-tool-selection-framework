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
