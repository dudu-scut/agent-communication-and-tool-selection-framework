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

register_mock_agent "mock-general" "general" "1.0" "STABLE"
sleep 1

# ----- 4.1: Unified memory injection ----------------------------------------
scenario "4.1 — Unified Memory Injection"

step "Send query — Agent should receive profile summary"
query_grpc "help me write an API" "verify-user-4-1" "verify-ctx-4-1" > /dev/null 2>&1 || true

verify "Server log contains agent communication activity" \
    assert_contains "$(tail -50 "$PROJECT_ROOT/logs/rpc_server.log" 2>/dev/null || echo '')" "agent"

# ----- 4.2: Activity feed recording -----------------------------------------
scenario "4.2 — Activity Feed Recording"

step "Send multi-step query via stream"
ACT_RESPONSE=$(query_stream_grpc "research and summarize microservices patterns" "verify-user-4-2" "verify-ctx-4-2")

verify "SSE stream contains event_type (status/partial events)" \
    assert_contains "$ACT_RESPONSE" "event_type"

# ----- 4.3: DAG plan preview ------------------------------------------------
scenario "4.3 — DAG Plan Preview"

step "Send complex query triggering multi-agent planning"
DAG_RESPONSE=$(query_stream_grpc "Compare microservices and monolith, then write a Python example for each, and create a summary report" "verify-user-4-3" "verify-ctx-4-3")

verify "Response contains plan-related event_type or nodes" \
    assert_contains "$DAG_RESPONSE" "plan"

# ----- 4.4: User-adjusted plan execution ------------------------------------
scenario "4.4 — User-Adjusted Plan Execution"

step "Send ExecutePlan with user-specified DAG"
EXEC_RESPONSE=$(execute_plan_grpc "verify-ctx-4-4" "verify-user-4-4" '{"nodes":[{"id":"n1","description":"test task","agent_id":"mock-general"}]}')

verify "ExecutePlan returns without error" \
    assert_not_contains "$EXEC_RESPONSE" "error"

verify_warn "agent_calls table has record with correct agent_id (requires psql)" \
    assert_pg_row_exists "agent_calls" "agent_id = 'mock-general'"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 4 — UX Core"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
