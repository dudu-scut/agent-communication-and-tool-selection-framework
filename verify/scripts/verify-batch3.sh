#!/bin/bash
# ============================================================================
# Batch 3 Verification: Cache + Control
#  3.1 — Semantic cache hit
#  3.2 — Context compression trigger
#  3.3 — L1 autonomy (read-only suggestion)
#  3.4 — L2 autonomy (intervention required)
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

# ----- 3.1: Semantic cache hit ----------------------------------------------
scenario "3.1 — Semantic Cache Hit"

step "First query (cache miss)"
START1=$(date +%s%N)
RESP1=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"What is the capital of France?","user_id":"verify-user-3-1","context_id":"verify-ctx-3-1"}' 2>&1)
END1=$(date +%s%N)
ELAPSED1=$(( (END1 - START1) / 1000000 ))

verify "First response has content" \
    assert_contains "$RESP1" "content"

step "Second query — semantically similar (different wording)"
START2=$(date +%s%N)
RESP2=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"Tell me the capital city of France","user_id":"verify-user-3-1","context_id":"verify-ctx-3-1"}' 2>&1)
END2=$(date +%s%N)
ELAPSED2=$(( (END2 - START2) / 1000000 ))

# If cache works, second should be much faster
if [ "$ELAPSED2" -lt "$ELAPSED1" ]; then
    verify "Second query faster than first (cache hit)" true
else
    verify_warn "Cache speedup not observed ($ELAPSED1 ms vs $ELAPSED2 ms)" false
fi

verify "Second response indicates cache hit" \
    assert_contains "$RESP2" "cache_hit"

# ----- 3.2: Context compression trigger -------------------------------------
scenario "3.2 — Context Compression Trigger"

step "Send 15 long messages to trigger compression"
CTX="verify-ctx-3-2"
for i in $(seq 1 15); do
    send_grpc \
        "agent_communication.AIQueryService/Query" \
        "{\"query_text\":\"This is a long conversation message number $i with substantial content to fill up the context window and eventually trigger the compression mechanism that should activate after about ten rounds of dialogue history.\",\"user_id\":\"verify-user-3-2\",\"context_id\":\"$CTX\"}" \
        > /dev/null 2>&1 || true
done

step "Check trace_spans for compression marker"
verify_warn "Trace spans contain context_compressed marker" \
    assert_pg_row_exists "trace_spans" "metadata::text LIKE '%context_compressed%'"

# ----- 3.3: L1 Autonomy — read-only suggestion ------------------------------
scenario "3.3 — L1 Autonomy — Read-Only Suggestion"

step "Set agent autonomy to L1 and send 'modify file' query"
RESP_L1=$(send_grpc \
    "agent_communication.AIQueryService/Query" \
    '{"query_text":"modify the config file to change the port","user_id":"verify-user-3-3","context_id":"verify-ctx-3-3","autonomy_level":1}' 2>&1 || echo "")

verify "L1 response is suggestion mode (not executing tool call)" \
    assert_not_contains "$RESP_L1" "tool_call_executed"

# ----- 3.4: L2 Autonomy — intervention required -----------------------------
scenario "3.4 — L2 Autonomy — Intervention Required"

step "Send query at L2 autonomy"
STREAM_OUT=$(send_grpc_stream \
    "agent_communication.AIQueryService/QueryStream" \
    '{"query_text":"send an email to the team","user_id":"verify-user-3-4","context_id":"verify-ctx-3-4","autonomy_level":2}' 2>&1 || echo "")

verify "SSE stream contains intervention_required event" \
    assert_contains "$STREAM_OUT" "intervention_required"

step "Send PROCEED intervention response"
INT_RESP=$(send_grpc \
    "agent_communication.AIQueryService/InterventionResponse" \
    '{"trace_id":"verify-ctx-3-4","decision":"PROCEED"}' 2>&1 || echo "")

verify "Intervention response accepted" \
    assert_not_contains "$INT_RESP" "error"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 3 — Cache + Control"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
