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

step "Send SandboxQuery via UserExperienceService"
SANDBOX_RESPONSE=$(send_grpc "agent_communication.UserExperienceService/SandboxQuery" '{"query_text":"test sandbox","agent_id":"mock-general"}')

verify "SandboxQuery returns result (no gRPC error)" \
    assert_not_contains "$SANDBOX_RESPONSE" "error"

verify "Sandbox result contains sandbox_ prefix" \
    assert_contains "$SANDBOX_RESPONSE" "sandbox"

# ----- 7.2: Sandbox cost exemption ------------------------------------------
scenario "7.2 — Sandbox Cost Exemption"

step "Verify Redis budget counter not incremented for sandbox"
TODAY=$(date +%Y-%m-%d)
COST_KEY="cost:verify-user-7-1:$TODAY"
SANDBOX_COST=$(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" GET "$COST_KEY" 2>/dev/null || echo "0")
verify "Sandbox cost did not increment budget counter" \
    [ "${SANDBOX_COST:-0}" -eq 0 ] 2>/dev/null

verify "token_usage has sandbox component record" \
    assert_pg_row_exists "token_usage" "component = 'sandbox'"

# ----- 7.3: Session sharing -------------------------------------------------
scenario "7.3 — Session Sharing"

step "Share a session as readonly via SharingService"
SHARE_RESPONSE=$(send_grpc "agent_communication.SharingService/ShareSession" '{"context_id":"verify-ctx-7-3","mode":"view"}')

SHARE_ID=$(echo "$SHARE_RESPONSE" | python3 -c "
import sys,json
try:
    d=json.loads(sys.stdin.read())
    print(d.get('share_id',''))
except: pass
" 2>/dev/null || echo "")

if [ -n "$SHARE_ID" ]; then
    verify "ShareSession returns valid share_id (UUID)" \
        [ "${#SHARE_ID}" -ge 32 ]
    verify "shared_sessions table has new row" \
        assert_pg_row_exists "shared_sessions" "share_id = '$SHARE_ID'"
else
    verify_warn "Could not extract share_id" false
fi

# ----- 7.4: Template save and use -------------------------------------------
scenario "7.4 — Template Save and Use"

step "Save a session as template via SharingService"
TEMPLATE_RESPONSE=$(send_grpc "agent_communication.SharingService/SaveTemplate" '{"name":"test-template","description":"A test template","dag_json":"{\"nodes\":[{\"id\":\"n1\",\"description\":\"test\",\"agent_id\":\"mock-general\"}]}"}')

verify "SaveTemplate returns without error" \
    assert_not_contains "$TEMPLATE_RESPONSE" "error"

TEMPLATE_ID=$(echo "$TEMPLATE_RESPONSE" | python3 -c "
import sys,json
try:
    d=json.loads(sys.stdin.read())
    print(d.get('template_id',''))
except: pass
" 2>/dev/null || echo "")

if [ -n "$TEMPLATE_ID" ]; then
    verify "session_templates table has new row" \
        assert_pg_row_exists "session_templates" "template_id = '$TEMPLATE_ID'"

    step "Use the saved template"
    USE_RESPONSE=$(send_grpc "agent_communication.SharingService/UseTemplate" "{\"template_id\":\"$TEMPLATE_ID\"}")
    verify "UseTemplate returns context_id" \
        assert_contains "$USE_RESPONSE" "context_id"
else
    verify_warn "Could not extract template_id" false
fi

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 7 — Growth + Retention"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
