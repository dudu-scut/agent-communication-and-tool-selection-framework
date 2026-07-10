#pragma once

#include <string>
#include <memory>

namespace agent_rpc {
namespace orchestrator {

/**
 * @brief Query Replay Service (Batch 5)
 *
 * Reads from the query_log table and re-executes queries through the
 * existing pipeline for regression testing, debugging, or comparison.
 */
class ReplayService {
public:
    struct ReplayResult {
        std::string trace_id;
        std::string original_response;
        std::string replayed_response;
        bool success;      // Whether replay succeeded (not comparison)
    };

    /**
     * @brief Replay query using the exact same route (agent selection).
     *
     * Reads query_log, reconstructs AIQueryRequest, and re-executes
     * through the full pipeline.
     *
     * @param trace_id The trace_id from query_log to replay.
     * @return ReplayResult with original and replayed responses.
     */
    static ReplayResult replayExact(const std::string& trace_id);

    /**
     * @brief Replay only the routing layer (re-run intent classification).
     *
     * Does NOT re-execute the full query — only re-runs the routing
     * decision to see if the route changes.
     *
     * @param trace_id The trace_id from query_log to replay.
     * @return ReplayResult (replayed_response contains the new route decision).
     */
    static ReplayResult replayRoute(const std::string& trace_id);
};

} // namespace orchestrator
} // namespace agent_rpc
