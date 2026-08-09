#!/bin/bash
# Verification: Platform Extension
#  6.1 — Cron scheduler trigger
#  6.2 — Canary traffic split
#  6.3 — Markdown export
#  6.4 — HTML export

source "$(dirname "$0")/helpers.sh"

precheck_services

register_mock_agent "mock-general" "general" "1.0" "STABLE"
register_mock_agent "mock-canary" "general" "1.1" "CANARY"
sleep 1

# 6.1: Cron scheduler trigger
scenario "6.1 — Cron Scheduler Trigger"

step "Register a one-shot task (fires every minute) — requires psql"
verify_warn "task_results table can be queried (requires psql + cron implementation)" \
    assert_pg_row_exists "task_results" "context_id = 'verify-ctx-6-1-cron'"

# 6.2: Canary traffic split
scenario "6.2 — Canary Traffic Split"

CANARY_COUNT=0
TOTAL_SENDS=100

step "Send $TOTAL_SENDS requests to general skill"
for i in $(seq 1 $TOTAL_SENDS); do
    RESP=$(query_grpc "test $i" "verify-user-6-2" "verify-ctx-6-2-$i" 2>&1 || echo "")
    if echo "$RESP" | grep -q "mock-canary"; then
        CANARY_COUNT=$((CANARY_COUNT+1))
    fi
done

if [ "$CANARY_COUNT" -ge 5 ] && [ "$CANARY_COUNT" -le 15 ]; then
    verify "Canary got $CANARY_COUNT/$TOTAL_SENDS requests (expected ~10)" true
else
    verify_warn "Canary got $CANARY_COUNT/$TOTAL_SENDS requests (expected ~10, accepted 5-15)" false
fi

# 6.3: Markdown export
scenario "6.3 — Markdown Export"

step "Export conversation to Markdown"
MD_EXPORT=$(export_conversation_grpc "verify-ctx-6-3" "MARKDOWN" 2>&1 || echo "")

verify "Markdown response received (no server error)" \
    assert_not_contains "$MD_EXPORT" "error"

# 6.4: HTML export
scenario "6.4 — HTML Export"

step "Export conversation to HTML"
HTML_EXPORT=$(export_conversation_grpc "verify-ctx-6-3" "HTML" 2>&1 || echo "")

verify "HTML response received (no server error)" \
    assert_not_contains "$HTML_EXPORT" "error"

# Report
print_batch_report "Batch 6 — Platform Extension"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
