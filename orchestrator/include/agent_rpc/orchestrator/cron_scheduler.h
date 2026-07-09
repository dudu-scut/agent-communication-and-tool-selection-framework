#pragma once

#include <string>
#include <functional>
#include <memory>

namespace agent_rpc {
namespace common {
class RedisClient;
} // namespace common

namespace orchestrator {

/**
 * @brief Cron Scheduler (Batch 6)
 *
 * Checks for due scheduled tasks and fires them via a registered callback.
 * Tasks are stored in Redis hashes with the key pattern "scheduled_task:{id}".
 * Task results are stored in Redis hashes "task_result:{id}".
 *
 * The execute callback is set at startup (see main.cpp) and connects to
 * AIQueryService::Query for actual execution.
 */
class CronScheduler {
public:
    /**
     * @brief Callback type for executing a scheduled task.
     *
     * Parameters: task_id, task_name, query_template, agent_id, context_id
     */
    using ExecuteFn = std::function<void(
        int64_t task_id,
        const std::string& task_name,
        const std::string& query_template,
        const std::string& agent_id,
        const std::string& context_id)>;

    /**
     * @brief Set the execution callback for firing due tasks.
     */
    static void setExecuteFn(ExecuteFn fn);

    /**
     * @brief Scan Redis for due scheduled tasks and fire them.
     *
     * Reads all keys matching "scheduled_task:*", checks enabled AND
     * next_run_at <= NOW(), and fires each due task via the registered
     * ExecuteFn. After firing, updates last_run_at and calculates a new
     * next_run_at.
     *
     * Requires RedisClient to be set via initialize().
     */
    static void checkAndFire();

    /**
     * @brief Initialize with a Redis client pointer.
     *
     * Must be called once before any checkAndFire() call.
     * The pointer is not owned -- the caller must ensure it outlives
     * the scheduler's usage.
     */
    static void initialize(agent_rpc::common::RedisClient* redis);

private:
    static ExecuteFn execute_fn_;
    static agent_rpc::common::RedisClient* redis_;
};

} // namespace orchestrator
} // namespace agent_rpc
