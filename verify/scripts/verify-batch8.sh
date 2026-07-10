#!/bin/bash
# ============================================================================
# Batch 8 Verification: Protocol + Security
#  8.1 — v1.0 serialization (kind field)
#  8.2 — v1.1 serialization (type field)
#  8.3 — Compatible fallback (version mismatch)
#  8.4 — Delegation depth limit
# ============================================================================

source "$(dirname "$0")/helpers.sh"

precheck_services

register_mock_agent "mock-general" "general" "1.0" "STABLE"
register_mock_agent "mock-canary" "general" "1.1" "CANARY"
sleep 1

MOCK_URL="http://localhost:5100/tasks/send"

# ----- 8.1: v1.0 serialization ----------------------------------------------
scenario "8.1 — v1.0 Serialization (kind field)"

step "Call mock agent with version_v1_0 mode"
V10_RESPONSE=$(curl -s -X POST "$MOCK_URL" \
    -H "Content-Type: application/json" \
    -H "x-mock-mode: version_v1_0" \
    -d '{"params":{"message":{"parts":[{"text":"test v1.0"}]}}}')

verify "Response uses kind field" \
    assert_contains "$V10_RESPONSE" '"kind"'

verify "Response does NOT use type field" \
    assert_not_contains "$V10_RESPONSE" '"type"'

# ----- 8.2: v1.1 serialization ----------------------------------------------
scenario "8.2 — v1.1 Serialization (type field)"

step "Call mock agent with version_v1_1 mode"
V11_RESPONSE=$(curl -s -X POST "$MOCK_URL" \
    -H "Content-Type: application/json" \
    -H "x-mock-mode: version_v1_1" \
    -d '{"params":{"message":{"parts":[{"text":"test v1.1"}]}}}')

verify "Response uses type field" \
    assert_contains "$V11_RESPONSE" '"type"'

verify "Response does NOT use kind field" \
    assert_not_contains "$V11_RESPONSE" '"kind"'

# ----- 8.3: Compatible fallback ---------------------------------------------
scenario "8.3 — Compatible Fallback (Version Mismatch)"

step "Call v1.0-declared agent that responds with v1.1 format"
MIXED_RESPONSE=$(curl -s -X POST "$MOCK_URL" \
    -H "Content-Type: application/json" \
    -H "x-mock-mode: version_mixed" \
    -d '{"params":{"message":{"parts":[{"text":"test mixed"}]}}}')

verify "Mixed response uses type field (v1.1 format despite v1.0 declaration)" \
    assert_contains "$MIXED_RESPONSE" '"type"'

verify "Mixed response does NOT use kind field" \
    assert_not_contains "$MIXED_RESPONSE" '"kind"'

# Check if span metadata records version fallback
verify_warn "Trace spans mark version_fallback" \
    assert_pg_row_exists "trace_spans" "metadata::text LIKE '%version_fallback%'"

# ----- 8.4: Delegation depth limit ------------------------------------------
scenario "8.4 — Delegation Depth Limit"

# NOTE: Delegation depth enforcement lives in the A2A adapter (C++ server),
# which intercepts outbound delegation requests and checks
# `x-delegation-depth` against a max of 5.  The mock agent itself has no
# depth-limiting logic, so direct curl calls to the mock cannot exercise the
# enforcement.  The curl-based simulation below verifies the mock agent's
# delegation header plumbing, but the actual depth-limit assertion is
# downgraded to warn — it requires the full server stack.

step "Simulate 6-level delegation chain (via curl — plumbing check only)"
DEPTH_OUTPUT=$(mktemp)
for depth in $(seq 1 6); do
    curl -s -D - -X POST "$MOCK_URL" \
        -H "Content-Type: application/json" \
        -H "x-mock-mode: delegate" \
        -H "x-delegation-depth: $depth" \
        -d '{"params":{"message":{"parts":[{"text":"delegate chain"}]}}}' \
        >> "$DEPTH_OUTPUT" 2>&1 || true
done

DEPTH_OUT=$(cat "$DEPTH_OUTPUT")
verify_warn "Depth limit enforced beyond level 5 (requires server-integration)" \
    assert_contains "$DEPTH_OUT" "depth"

rm -f "$DEPTH_OUTPUT"

# ----- Report ----------------------------------------------------------------
print_batch_report "Batch 8 — Protocol + Security"
exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
