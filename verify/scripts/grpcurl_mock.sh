#!/bin/bash
# Mock grpcurl for smoke testing the verification scripts
# Usage: grpcurl -plaintext -d '<body>' <server> <method>
# Returns mock JSON responses matching the gRPC methods used in verify-batch*.sh

METHOD="${5}"

case "${METHOD}" in
    *QueryStream*)
        echo '{"trace_id":"mock-trace-12345","trace_summary":{"spans":[{"span_id":"span-1"}]},"content":"mock response"}'
        ;;
    *Query*)
        echo '{"trace_id":"mock-trace-67890","content":"mock response","cache_hit":true,"success_rate":0.95,"avg_latency_ms":42}'
        ;;
    *SubmitFeedback*)
        echo '{"status":"ok"}'
        ;;
    *GetAgentMetrics*)
        echo '{"success_rate":0.95,"avg_latency_ms":42,"total_requests":100}'
        ;;
    *RegisterAgent*)
        echo '{"status":"registered"}'
        ;;
    *InterventionResponse*)
        echo '{"status":"accepted","decision":"PROCEED"}'
        ;;
    *)
        echo '{}'
        ;;
esac
