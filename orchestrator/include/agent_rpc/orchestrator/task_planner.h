/**
 * @file task_planner.h
 * @brief TaskPlanner — decomposes user queries into multi-agent execution plans (P4-1)
 *
 * Uses LLM to analyze whether a query requires single-agent or multi-agent
 * execution.  For multi-agent queries, produces an ExecutionPlan with
 * dependency-aware SubTasks forming a DAG.
 */

#pragma once

#include <a2a/llm_client.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent_rpc {
namespace orchestrator {

class AgentRouter;  // forward declaration for resolveAgents()

// ── Data structures ────────────────────────────────────────────────────────

struct SubTask {
    std::string id;                        // "t1", "t2", ...
    std::string description;               // Prompt sent to the Agent
    std::string required_skill;            // Skill needed
    std::vector<std::string> depends_on;   // IDs of prerequisite subtasks
    std::string preferred_agent_id;        // Pre-resolved agent (set by resolveAgents)
    std::string preferred_agent_name;      // Agent name for logging
};

struct ExecutionPlan {
    std::string original_query;            // Original user request
    std::vector<SubTask> tasks;            // Ordered subtask list
    bool is_single_agent = true;           // true → fast-path single Agent
    std::string single_agent_skill;        // Skill for the single-agent path
    std::string single_agent_id;           // Pre-resolved agent ID for single-agent path
    std::string single_agent_name;         // Agent name for single-agent path
};

// ── Configuration ──────────────────────────────────────────────────────────

struct TaskPlannerConfig {
    std::string api_key;
    std::string model    = "deepseek-v4-pro";
    std::string api_url  = "https://api.deepseek.com/v1/chat/completions";
};

// ── TaskPlanner class ──────────────────────────────────────────────────────

class TaskPlanner {
public:
    explicit TaskPlanner(const TaskPlannerConfig& config);

    /**
     * Analyze a user query and produce an execution plan.
     * @param query              User's question / request
     * @param available_skills   skill → description map from the registry
     * @return ExecutionPlan with is_single_agent flag and optional subtask DAG
     */
    ExecutionPlan plan(const std::string& query,
                       const std::unordered_map<std::string, std::string>& available_skills);

    /**
     * Pre-resolve agents for each subtask in the plan using AgentRouter.
     * Should be called after plan() to populate preferred_agent_id fields.
     * For single-agent plans, also resolves single_agent_id.
     *
     * @param plan    ExecutionPlan from plan() — modified in place
     * @param router  AgentRouter to use for skill → agent resolution
     */
    void resolveAgents(ExecutionPlan& plan, AgentRouter& router);

private:
    std::string buildPlanningPrompt(
        const std::string& query,
        const std::unordered_map<std::string, std::string>& available_skills) const;

    ExecutionPlan parsePlanResponse(const std::string& response,
                                    const std::string& query) const;

    TaskPlannerConfig config_;
    std::unique_ptr<LLMClient> llm_client_;
};

} // namespace orchestrator
} // namespace agent_rpc
