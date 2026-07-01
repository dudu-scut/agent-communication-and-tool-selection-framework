/**
 * @file task_executor.cpp
 * @brief TaskExecutor — DAG execution engine implementation (P4-2)
 */

#include "agent_rpc/orchestrator/task_executor.h"
#include <algorithm>
#include <chrono>
#include <future>
#include <queue>
#include <sstream>

namespace agent_rpc {
namespace orchestrator {

TaskExecutor::TaskExecutor(AgentRouter& router, const ExecutorConfig& config)
    : router_(router)
    , config_(config)
{}

std::unordered_map<std::string, SubTaskResult> TaskExecutor::execute(
    const ExecutionPlan& plan,
    const AgentCallFn& call_agent,
    const ProgressCallback& on_progress) {

    std::unordered_map<std::string, SubTaskResult> results;

    auto global_start = std::chrono::steady_clock::now();
    auto global_deadline = global_start + std::chrono::seconds(config_.global_timeout_seconds);

    // Topological sort into layers
    auto layers = topologicalLayers(plan.tasks);

    // Build a lookup map: id → SubTask
    std::unordered_map<std::string, const SubTask*> task_map;
    for (const auto& t : plan.tasks) {
        task_map[t.id] = &t;
    }

    // Execute layer by layer
    for (const auto& layer : layers) {
        // Check global timeout
        if (std::chrono::steady_clock::now() >= global_deadline) {
            // Mark ALL remaining subtasks (current + future layers) as timed out
            for (size_t li = &layer - &layers[0]; li < layers.size(); ++li) {
                for (const auto& tid : layers[li]) {
                    if (results.find(tid) == results.end()) {
                        SubTaskResult r;
                        r.subtask_id = tid;
                        r.success = false;
                        r.error_message = "Global timeout exceeded";
                        results[tid] = std::move(r);
                    }
                }
            }
            break;
        }

        if (layer.size() == 1) {
            // Single subtask — execute directly, no async overhead
            const auto& tid = layer[0];
            auto it = task_map.find(tid);
            if (it == task_map.end()) continue;

            const SubTask& st = *it->second;
            std::string prompt = buildSubtaskPrompt(st, results);

            if (on_progress) {
                on_progress({SubTaskEventType::START, tid, ""});
            }

            SubTaskResult result = executeSubtask(st, prompt, call_agent);

            if (on_progress) {
                SubTaskEventType evt_type = result.success
                    ? SubTaskEventType::COMPLETE : SubTaskEventType::FAILED;
                on_progress({evt_type, tid,
                    result.success ? result.result : result.error_message});
            }

            results[tid] = std::move(result);
        } else {
            // Multiple subtasks — execute in parallel via std::async
            std::vector<std::pair<std::string, std::future<SubTaskResult>>> futures;

            for (const auto& tid : layer) {
                auto it = task_map.find(tid);
                if (it == task_map.end()) continue;

                const SubTask& st = *it->second;
                std::string prompt = buildSubtaskPrompt(st, results);

                if (on_progress) {
                    on_progress({SubTaskEventType::START, tid, ""});
                }

                // Capture st by reference (valid throughout layer execution)
                // and prompt by value (moved into lambda)
                futures.emplace_back(tid,
                    std::async(std::launch::async,
                        [this, &st, p = std::move(prompt), &call_agent]() {
                            return executeSubtask(st, p, call_agent);
                        }));
            }

            // Collect results
            for (auto& [tid, fut] : futures) {
                try {
                    SubTaskResult result = fut.get();

                    if (on_progress) {
                        SubTaskEventType evt_type = result.success
                            ? SubTaskEventType::COMPLETE : SubTaskEventType::FAILED;
                        on_progress({evt_type, tid,
                            result.success ? result.result : result.error_message});
                    }

                    results[tid] = std::move(result);
                } catch (const std::exception& e) {
                    SubTaskResult r;
                    r.subtask_id = tid;
                    r.success = false;
                    r.error_message = std::string("Future exception: ") + e.what();

                    if (on_progress) {
                        on_progress({SubTaskEventType::FAILED, tid, r.error_message});
                    }

                    results[tid] = std::move(r);
                }
            }
        }
    }

    return results;
}

std::vector<std::vector<std::string>> TaskExecutor::topologicalLayers(
    const std::vector<SubTask>& tasks) const {

    // Kahn's algorithm with layer tracking
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> dependents;

    for (const auto& t : tasks) {
        if (in_degree.find(t.id) == in_degree.end()) {
            in_degree[t.id] = 0;
        }
        for (const auto& dep : t.depends_on) {
            dependents[dep].push_back(t.id);
            in_degree[t.id]++;
        }
    }

    std::vector<std::vector<std::string>> layers;
    std::queue<std::string> ready;

    // Layer 0: all subtasks with no dependencies
    for (const auto& [id, deg] : in_degree) {
        if (deg == 0) {
            ready.push(id);
        }
    }

    while (!ready.empty()) {
        std::vector<std::string> current_layer;
        size_t layer_size = ready.size();

        for (size_t i = 0; i < layer_size; ++i) {
            std::string id = ready.front();
            ready.pop();
            current_layer.push_back(id);

            // Decrement in-degree for dependents
            auto it = dependents.find(id);
            if (it != dependents.end()) {
                for (const auto& dep_id : it->second) {
                    in_degree[dep_id]--;
                    if (in_degree[dep_id] == 0) {
                        ready.push(dep_id);
                    }
                }
            }
        }

        if (!current_layer.empty()) {
            // Sort within layer for deterministic order
            std::sort(current_layer.begin(), current_layer.end());
            layers.push_back(std::move(current_layer));
        }
    }

    // Cycle detection: if not all tasks were emitted, there's a cycle
    size_t total_emitted = 0;
    for (const auto& layer : layers) {
        total_emitted += layer.size();
    }
    if (total_emitted < tasks.size()) {
        throw std::runtime_error(
            "Circular dependency detected in execution plan: " +
            std::to_string(tasks.size() - total_emitted) + " task(s) in cycle");
    }

    return layers;
}

std::string TaskExecutor::buildSubtaskPrompt(
    const SubTask& subtask,
    const std::unordered_map<std::string, SubTaskResult>& results) const {

    std::string prompt = subtask.description;

    // Inject predecessor results as context
    if (!subtask.depends_on.empty()) {
        std::string context = "\n\n--- 前置任务结果 ---\n";
        bool has_context = false;

        for (const auto& dep_id : subtask.depends_on) {
            auto it = results.find(dep_id);
            if (it != results.end() && it->second.success) {
                context += "\n[" + dep_id + "] " + it->second.description + ":\n";
                context += it->second.result + "\n";
                has_context = true;
            } else if (it != results.end()) {
                context += "\n[" + dep_id + "] 执行失败: " + it->second.error_message + "\n";
                has_context = true;
            }
        }

        if (has_context) {
            prompt += context;
            prompt += "\n请基于以上前置任务的结果完成你的任务。";
        }
    }

    return prompt;
}

SubTaskResult TaskExecutor::executeSubtask(
    const SubTask& subtask,
    const std::string& enriched_prompt,
    const AgentCallFn& call_agent) {

    SubTaskResult result;
    result.subtask_id = subtask.id;
    result.description = subtask.description;

    auto start = std::chrono::steady_clock::now();

    try {
        std::string response = call_agent(subtask.required_skill, enriched_prompt);

        auto end = std::chrono::steady_clock::now();
        result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        result.result = response;
        result.success = true;
    } catch (const std::exception& e) {
        auto end = std::chrono::steady_clock::now();
        result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        result.success = false;
        result.error_message = e.what();
    }

    return result;
}

} // namespace orchestrator
} // namespace agent_rpc
