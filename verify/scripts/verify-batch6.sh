#!/bin/bash
# ============================================================================
# Batch 6 Verification: Platform Extension
#  6.1 — Cron scheduler trigger
#  6.2 — Canary traffic split
#  6.3 — Markdown export
#  6.4 — HTML export
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

register_mock_agent "mock-general" "general" "1.0" "STABLE"
register_mock_agent "mock-canary" "general" "1.1" "CANARY"
sleep 1

# ----- 6.1: Cron scheduler trigger ------------------------------------------
scenario "6.1 — Cron Scheduler Trigger"

step "Register a one-shot task (fires every minute)"
CURRENT_MIN=$(date +%M)
psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" -d "$PG_DB" -c \
    "INSERT INTO scheduled_tasks (name, cron_expr, query_template, context_id, enabled, created_by_user_id)
     VALUES ('verify-test-cron', '* * * * *', 'automated health check', 'verify-ctx-6-1-cron', true, 'verify-user-6-1')
     ON CONFLICT DO NOTHING;" > /dev/null 2>&1 || true

step "Wait 65s for next minute boundary + execution"
sleep 65

verify "task_results table has execution record" \
    assert_pg_row_exists "task_results" "context_id = 'verify-ctx-6-1-cron'"

verify "scheduled_tasks.last_run_at is updated" \
    assert_pg_row_exists "scheduled_tasks" "name = 'verify-test-cron' AND last_run_at IS NOT NULL"

# ----- 6.2: Canary traffic split --------------------------------------------
scenario "6.2 — Canary Traffic Split"

CANARY_COUNT=0
TOTAL_SENDS=100

step "Send $TOTAL_SENDS requests to general skill"
for i in $(seq 1 $TOTAL_SENDS); do
    RESP=$(send_grpc \
        "agent_communication.AIQueryService/Query" \
        "{\"query_text\":\"test $i\",\"user_id\":\"verify-user-6-2\",\"context_id\":\"verify-ctx-6-2-$i\"}" 2>&1 || echo "")
    if echo "$RESP" | grep -q "mock-canary"; then
        CANARY_COUNT=$((CANARY_COUNT+1))
    fi
done

step "Check canary received ~10% of traffic"
# Allow 5-15 range for 10% with 100 samples
if [ "$CANARY_COUNT" -ge 5 ] && [ "$CANARY_COUNT" -le 15 ]; then
    verify "Canary got $CANARY_COUNT/$TOTAL_SENDS requests (expected ~10)" true
else
    verify_warn "Canary got $CANARY_COUNT/$TOTAL_SENDS requests (expected ~10, accepted 5-15)" false
fi

# ----- 6.3: Markdown export ------------------------------------------------
scenario "6.3 — Markdown Export"

step "Export conversation to Markdown"
MD_EXPORT=$(send_grpc \
    "agent_communication.AIQueryService/ExportConversation" \
    '{"context_id":"verify-ctx-6-3","format":"MARKDOWN"}' 2>&1 || echo "")

verify "Markdown contains NexusAI header" \
    assert_contains "$MD_EXPORT" "NexusAI"

verify "Markdown contains conversation timeline" \
    assert_contains "$MD_EXPORT" "对话记录"

# ----- 6.4: HTML export -----------------------------------------------------
scenario "6.4 — HTML Export"

step "Export conversation to HTML"
HTML_EXPORT=$(send_grpc \
    "agent_communication.AIQueryService/ExportConversation" \
    '{"context_id":"verify-ctx-6-3","format":"HTML"}' 2>&1 || echo "")

verify "HTML contains <html> tag" \
    assert_contains "$HTML_EXPORT" "<html>"

verify "HTML contains inline CSS" \
    assert_contains "$HTML_EXPORT" "<style>"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 6 — Platform Extension"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
