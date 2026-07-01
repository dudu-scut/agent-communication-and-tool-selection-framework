/**
 * @file result_aggregator.cpp
 * @brief ResultAggregator implementation (P4-3)
 */

#include "agent_rpc/orchestrator/result_aggregator.h"
#include <algorithm>
#include <chrono>
#include <sstream>

namespace agent_rpc {
namespace orchestrator {

ResultAggregator::ResultAggregator(const AggregatorConfig& config)
    : config_(config) {
    if (config.default_strategy == "llm_synthesize" && !config.api_key.empty()) {
        llm_client_ = std::make_unique<LLMClient>(config.api_key, config.model, config.api_url);
    }
}

AggregatedResult ResultAggregator::aggregate(
    const ExecutionPlan& plan,
    const std::unordered_map<std::string, SubTaskResult>& results) {

    auto start = std::chrono::steady_clock::now();

    AggregatedResult agg;
    agg.strategy = config_.default_strategy;

    // Collect sub_results in task order
    for (const auto& task : plan.tasks) {
        auto it = results.find(task.id);
        if (it != results.end()) {
            agg.sub_results.push_back(it->second);
        }
    }

    // Route to strategy
    if (config_.default_strategy == "llm_synthesize" && llm_client_) {
        agg.final_answer = aggregateLLMSynthesize(plan, results);
    } else {
        agg.final_answer = aggregateConcat(plan, results);
        agg.strategy = "concat";
    }

    auto end = std::chrono::steady_clock::now();
    agg.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    return agg;
}

std::string ResultAggregator::aggregateConcat(
    const ExecutionPlan& plan,
    const std::unordered_map<std::string, SubTaskResult>& results) const {

    // Concatenate results in topological order (plan.tasks order)
    std::ostringstream out;
    bool first = true;

    for (const auto& task : plan.tasks) {
        auto it = results.find(task.id);
        if (it == results.end()) continue;

        const auto& r = it->second;
        if (!r.success) continue;  // Skip failed subtasks

        if (!first) {
            out << "\n\n";
        }
        first = false;

        out << "## " << task.id << ": " << task.description << "\n\n";
        out << r.result;
    }

    if (first) {
        // All subtasks failed
        out << "所有子任务均未成功完成。";
        for (const auto& task : plan.tasks) {
            auto it = results.find(task.id);
            if (it != results.end() && !it->second.success) {
                out << "\n- " << task.id << ": " << it->second.error_message;
            }
        }
    }

    return out.str();
}

std::string ResultAggregator::aggregateLLMSynthesize(
    const ExecutionPlan& plan,
    const std::unordered_map<std::string, SubTaskResult>& results) {

    // Build context from all successful subtask results
    std::string context;
    for (const auto& task : plan.tasks) {
        auto it = results.find(task.id);
        if (it == results.end()) continue;

        const auto& r = it->second;
        if (!r.success) continue;

        context += "[" + task.id + " - " + task.description + "]\n";
        context += r.result + "\n\n";
    }

    if (context.empty()) {
        // Fall back to concat if no successful results
        return aggregateConcat(plan, results);
    }

    std::string system_prompt =
        "你是一个智能助手。以下是多个专业 Agent 协作完成用户请求后的各自结果。\n"
        "请将这些结果综合整理，给出一个完整、连贯、有条理的最终回答。\n"
        "不要提及'子任务'、'Agent'等内部概念，直接回答用户的问题。";

    std::string user_message =
        "用户的原始请求：" + plan.original_query + "\n\n"
        "各子任务结果：\n" + context +
        "\n请综合以上内容，给出最终回答。";

    try {
        return llm_client_->chat(system_prompt, user_message);
    } catch (const std::exception&) {
        // LLM synthesis failed — fall back to concat
        return aggregateConcat(plan, results);
    }
}

} // namespace orchestrator
} // namespace agent_rpc
