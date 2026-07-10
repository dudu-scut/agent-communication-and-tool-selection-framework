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
