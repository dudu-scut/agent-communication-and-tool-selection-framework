/**
 * @file result_aggregator.h
 * @brief ResultAggregator — merges multiple subtask results into a final answer (P4-3)
 *
 * Two strategies:
 *   - concat:        Topological-order concatenation (fast, zero-cost)
 *   - llm_synthesize: LLM-powered synthesis (slower, higher quality)
 */

#pragma once

#include "agent_rpc/orchestrator/task_planner.h"
#include "agent_rpc/orchestrator/task_executor.h"
#include <a2a/examples/llm_client.hpp>
#include <memory>
#include <string>
#include <vector>

namespace agent_rpc {
namespace orchestrator {

// ── Aggregated result ──────────────────────────────────────────────────────

struct AggregatedResult {
    std::string final_answer;
    std::vector<SubTaskResult> sub_results;   // Ordered by topology
    std::string strategy;                      // "concat" | "llm_synthesize"
    int64_t total_time_ms = 0;
};

// ── Configuration ──────────────────────────────────────────────────────────

struct AggregatorConfig {
    std::string default_strategy = "concat";   // "concat" | "llm_synthesize"
    std::string api_key;
    std::string model    = "deepseek-v4-pro";
    std::string api_url  = "https://api.deepseek.com/v1/chat/completions";
};

// ── ResultAggregator class ─────────────────────────────────────────────────

class ResultAggregator {
public:
    explicit ResultAggregator(const AggregatorConfig& config);

    /**
     * Aggregate subtask results into a final answer.
     * @param plan     The execution plan (for ordering and context)
     * @param results  Map of subtask_id → SubTaskResult
     * @return AggregatedResult with final_answer and per-subtask details
     */
    AggregatedResult aggregate(
        const ExecutionPlan& plan,
        const std::unordered_map<std::string, SubTaskResult>& results);

private:
    std::string aggregateConcat(
        const ExecutionPlan& plan,
        const std::unordered_map<std::string, SubTaskResult>& results) const;

    std::string aggregateLLMSynthesize(
        const ExecutionPlan& plan,
        const std::unordered_map<std::string, SubTaskResult>& results);

    AggregatorConfig config_;
    std::unique_ptr<LLMClient> llm_client_;
};

} // namespace orchestrator
} // namespace agent_rpc
