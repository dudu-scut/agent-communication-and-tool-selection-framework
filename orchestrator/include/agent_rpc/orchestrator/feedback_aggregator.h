#pragma once

#include "agent_rpc/common/redis_client.h"

#include <string>

namespace agent_rpc {
namespace orchestrator {

/**
 * @brief Feedback aggregator for agent approval rates and performance metrics.
 *
 * Periodically aggregates feedback ratings from agent_feedback table (PG)
 * and call metrics from agent_calls table (PG), then stores the results
 * in Redis for fast lookup by the routing layer.
 */
class FeedbackAggregator {
public:
    /**
     * @brief Initialize the aggregator with a Redis client.
     * @param redis Pointer to the shared RedisClient instance
     */
    static void initialize(agent_rpc::common::RedisClient* redis);

    /**
     * @brief Aggregate approval rates from agent_feedback into Redis.
     *
     * Reads from PostgreSQL agent_feedback table, computes Bayesian-smoothed
     * approval_rate per (agent_id, skill_name), and writes to Redis HSET
     * under key "feedback:{agent_id}:{skill_name}".
     *
     * Scheduled to run hourly.
     */
    static void recalculate();

    /**
     * @brief Aggregate performance metrics from agent_calls into Redis.
     *
     * Reads from PostgreSQL agent_calls table, computes success_rate, avg_latency,
     * p95_latency, total_requests per agent_id, and writes to Redis HSET
     * under key "agent_metrics:{agent_id}".
     *
     * Scheduled to run hourly.
     */
    static void recalculateMetrics();

private:
    /** Shared Redis client pointer (set once via initialize). */
    static agent_rpc::common::RedisClient* redis_;

    /** Default PostgreSQL connection string (overridable via env PG_URL). */
    static std::string pgUrl();

    /**
     * @brief Execute a SQL query via psql and return the result as a string.
     * @param sql SQL query to execute
     * @return Query result (stdout from psql), or empty on failure
     */
    static std::string execPsql(const std::string& sql);
};

} // namespace orchestrator
} // namespace agent_rpc
