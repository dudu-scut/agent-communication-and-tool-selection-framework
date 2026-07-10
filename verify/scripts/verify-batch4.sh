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
