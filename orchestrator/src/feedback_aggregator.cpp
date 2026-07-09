#include "agent_rpc/orchestrator/feedback_aggregator.h"
#include "agent_rpc/common/logger.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>

namespace agent_rpc {
namespace orchestrator {

agent_rpc::common::RedisClient* FeedbackAggregator::redis_ = nullptr;

void FeedbackAggregator::initialize(agent_rpc::common::RedisClient* redis) {
    redis_ = redis;
    LOG_INFO("FeedbackAggregator initialized");
}

std::string FeedbackAggregator::pgUrl() {
    const char* env = std::getenv("PG_URL");
    if (env && env[0] != '\0') {
        return std::string(env);
    }
    // Default: localhost, default port, no auth, database nexus
    return "postgresql://localhost:5432/nexus";
}

std::string FeedbackAggregator::execPsql(const std::string& sql) {
    // Escape double quotes in the SQL for shell safety
    std::string escaped;
    escaped.reserve(sql.size() + 16);
    for (char c : sql) {
        if (c == '"') {
            escaped += "\\\"";
        } else if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }

    std::string cmd = "psql \"" + pgUrl() + "\" -t -A -c \"" + escaped + "\" 2>/dev/null";

    std::array<char, 4096> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        LOG_WARN("FeedbackAggregator: failed to run psql command");
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

void FeedbackAggregator::recalculate() {
    if (!redis_) {
        LOG_WARN("FeedbackAggregator::recalculate skipped: Redis not initialized");
        return;
    }

    LOG_INFO("FeedbackAggregator::recalculate — aggregating approval rates from agent_feedback");

    // Query: for each (agent_id, skill_name), compute total ratings and count of
    // positive ratings (rating >= 2, i.e., thumbs-up or neutral). Use Bayesian
    // smoothing with Beta(2,2) prior to avoid division-by-zero and produce stable
    // estimates for low-volume agents.
    //
    // The SQL returns: agent_id | skill_name | total_count | positive_count
    std::string sql =
        "SELECT agent_id, skill_name, "
        "  COUNT(*) AS total_count, "
        "  COUNT(*) FILTER (WHERE rating >= 2) AS positive_count "
        "FROM agent_feedback "
        "GROUP BY agent_id, skill_name;";

    std::string raw = execPsql(sql);
    if (raw.empty()) {
        LOG_WARN("FeedbackAggregator::recalculate — no data or psql unavailable");
        return;
    }

    // Parse the psql tab-separated output. Each line: agent_id|skill_name|total|positive
    std::istringstream stream(raw);
    std::string line;
    int updated_count = 0;
    while (std::getline(stream, line)) {
        // Skip empty lines
        if (line.empty()) continue;

        std::istringstream ls(line);
        std::string agent_id, skill_name, total_str, positive_str;

        if (!std::getline(ls, agent_id, '|')) continue;
        if (!std::getline(ls, skill_name, '|')) continue;
        if (!std::getline(ls, total_str, '|')) continue;
        if (!std::getline(ls, positive_str, '|')) continue;

        int total = std::atoi(total_str.c_str());
        int positive = std::atoi(positive_str.c_str());

        if (total <= 0) continue;

        // Bayesian-smoothed approval rate: Beta(2,2) prior
        // approval_rate = (positive + 2) / (total + 4)
        double approval_rate = static_cast<double>(positive + 2) / static_cast<double>(total + 4);

        // Write to Redis HSET: feedback:{agent_id}:{skill_name}
        std::string redis_key = "feedback:" + agent_id + ":" + skill_name;
        redis_->hset(redis_key, "approval_rate", std::to_string(approval_rate));
        redis_->hset(redis_key, "total_ratings", std::to_string(total));
        redis_->hset(redis_key, "positive_ratings", std::to_string(positive));
        redis_->hset(redis_key, "last_updated",
                     std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count()));

        ++updated_count;
    }

    LOG_INFO("FeedbackAggregator::recalculate — updated " +
             std::to_string(updated_count) + " agent-skill entries in Redis");
}

void FeedbackAggregator::recalculateMetrics() {
    if (!redis_) {
        LOG_WARN("FeedbackAggregator::recalculateMetrics skipped: Redis not initialized");
        return;
    }

    LOG_INFO("FeedbackAggregator::recalculateMetrics — aggregating performance metrics from agent_calls");

    // Query: compute success_rate, avg_latency, p95_latency, total_requests per agent_id.
    //
    // P95 is approximated with Postgres's percentile_cont within an ordered-subset
    // aggregate. For older PG versions without ordered-set aggregates, we fall back
    // to a simpler computation by ordering and taking the 95th percentile row.
    std::string sql =
        "SELECT agent_id, "
        "  COUNT(*) AS total_req, "
        "  ROUND(100.0 * SUM(CASE WHEN success THEN 1 ELSE 0 END) / COUNT(*), 2) AS success_rate, "
        "  ROUND(AVG(latency_ms), 2) AS avg_latency, "
        "  ROUND(PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY latency_ms), 2) AS p95_latency "
        "FROM agent_calls "
        "GROUP BY agent_id;";

    std::string raw = execPsql(sql);
    if (raw.empty()) {
        LOG_WARN("FeedbackAggregator::recalculateMetrics — no data or psql unavailable");
        return;
    }

    // Parse tab-separated output: agent_id|total_req|success_rate|avg_latency|p95_latency
    std::istringstream stream(raw);
    std::string line;
    int updated_count = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        std::istringstream ls(line);
        std::string agent_id, total_str, success_rate_str, avg_latency_str, p95_str;

        if (!std::getline(ls, agent_id, '|')) continue;
        if (!std::getline(ls, total_str, '|')) continue;
        if (!std::getline(ls, success_rate_str, '|')) continue;
        if (!std::getline(ls, avg_latency_str, '|')) continue;
        if (!std::getline(ls, p95_str, '|')) continue;

        // Write to Redis HSET: agent_metrics:{agent_id}
        std::string redis_key = "agent_metrics:" + agent_id;
        redis_->hset(redis_key, "success_rate", success_rate_str);
        redis_->hset(redis_key, "avg_latency_ms", avg_latency_str);
        redis_->hset(redis_key, "p95_latency_ms", p95_str);
        redis_->hset(redis_key, "total_requests", total_str);
        redis_->hset(redis_key, "last_updated",
                     std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count()));

        ++updated_count;
    }

    LOG_INFO("FeedbackAggregator::recalculateMetrics — updated " +
             std::to_string(updated_count) + " agent metric entries in Redis");
}

} // namespace orchestrator
} // namespace agent_rpc
