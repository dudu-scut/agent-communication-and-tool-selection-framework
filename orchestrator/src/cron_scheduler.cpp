#include "agent_rpc/orchestrator/cron_scheduler.h"
#include "agent_rpc/common/redis_client.h"
#include "agent_rpc/common/logger.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

namespace agent_rpc {
namespace orchestrator {

// Static members
CronScheduler::ExecuteFn CronScheduler::execute_fn_;
agent_rpc::common::RedisClient* CronScheduler::redis_ = nullptr;

void CronScheduler::setExecuteFn(ExecuteFn fn) {
    execute_fn_ = std::move(fn);
}

void CronScheduler::initialize(agent_rpc::common::RedisClient* redis) {
    redis_ = redis;
}

void CronScheduler::checkAndFire() {
    if (!redis_) {
        LOG_WARN("CronScheduler: Redis not initialized, skipping checkAndFire");
        return;
    }

    auto now = std::chrono::system_clock::now();
    int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    // Read the scheduled task ID list from Redis SET "scheduled_tasks"
    // Since RedisClient has no SMEMBERS/SCAN, we use a simpler approach:
    // a single list "scheduled_tasks" which stores task IDs as JSON,
    // or we iterate known task IDs from sequential scanning.
    //
    // Practical approach: store master list in Redis LIST "scheduled_tasks:ids"
    // Each element is a task ID as string.
    std::vector<std::string> task_ids;
    redis_->lrange("scheduled_tasks:ids", 0, -1, task_ids);

    if (task_ids.empty()) {
        // Check for legacy single-task key as fallback
        std::string legacy_id;
        if (redis_->get("scheduled_tasks:active_id", legacy_id) && !legacy_id.empty()) {
            task_ids.push_back(legacy_id);
        }
    }

    for (const auto& id_str : task_ids) {
        std::string key = "scheduled_task:" + id_str;

        // Read task hash
        std::map<std::string, std::string> fields;
        if (!redis_->hgetall(key, fields)) {
            continue;
        }

        // Check enabled flag
        auto enabled_it = fields.find("enabled");
        if (enabled_it == fields.end() || enabled_it->second != "1") {
            continue;
        }

        // Check next_run_at
        auto next_it = fields.find("next_run_at");
        if (next_it == fields.end() || next_it->second.empty()) {
            continue;
        }

        int64_t next_run_ts = 0;
        try {
            next_run_ts = std::stoll(next_it->second);
        } catch (...) {
            continue;
        }

        if (now_ts < next_run_ts) {
            continue; // Not due yet
        }

        // Task is due — extract fields
        auto name_it = fields.find("name");
        auto query_it = fields.find("query_template");
        auto agent_it = fields.find("agent_id");
        auto ctx_it = fields.find("context_id");

        int64_t task_id = 0;
        try { task_id = std::stoll(id_str); } catch (...) {}

        std::string task_name = (name_it != fields.end()) ? name_it->second : "";
        std::string query_template = (query_it != fields.end()) ? query_it->second : "";
        std::string agent_id = (agent_it != fields.end()) ? agent_it->second : "";
        std::string context_id = (ctx_it != fields.end()) ? ctx_it->second : "";

        LOG_INFO("CronScheduler: firing task " + id_str +
                 " (" + task_name + ")");

        // Fire the task via registered callback
        if (execute_fn_) {
            try {
                execute_fn_(task_id, task_name, query_template, agent_id, context_id);

                // Record success in task_result key
                std::string result_key = "task_result:" + id_str + ":" +
                                         std::to_string(now_ts);
                redis_->hset(result_key, "task_id", id_str);
                redis_->hset(result_key, "status", "completed");
                redis_->hset(result_key, "completed_at", std::to_string(now_ts));
                redis_->expire(result_key, 86400 * 7); // 7-day TTL

                LOG_INFO("CronScheduler: task " + id_str + " completed");

            } catch (const std::exception& e) {
                LOG_ERROR("CronScheduler: task " + id_str +
                          " failed: " + e.what());

                // Record failure
                std::string result_key = "task_result:" + id_str + ":" +
                                         std::to_string(now_ts);
                redis_->hset(result_key, "task_id", id_str);
                redis_->hset(result_key, "status", "failed");
                redis_->hset(result_key, "error", e.what());
                redis_->hset(result_key, "completed_at", std::to_string(now_ts));
                redis_->expire(result_key, 86400 * 7);
            }
        }

        // Update last_run_at and calculate next_run_at
        // Default interval: 1 hour (3600 seconds)
        int64_t default_interval = 3600;
        int64_t next_run = now_ts + default_interval;

        auto interval_it = fields.find("interval_seconds");
        if (interval_it != fields.end()) {
            try {
                default_interval = std::stoll(interval_it->second);
                next_run = now_ts + default_interval;
            } catch (...) {}
        }

        redis_->hset(key, "last_run_at", std::to_string(now_ts));
        redis_->hset(key, "next_run_at", std::to_string(next_run));
    }
}

} // namespace orchestrator
} // namespace agent_rpc
