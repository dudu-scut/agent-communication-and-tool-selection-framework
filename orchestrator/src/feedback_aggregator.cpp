#include "agent_rpc/orchestrator/feedback_aggregator.h"
#include "agent_rpc/common/agent_runtime_repository.h"
#include "agent_rpc/common/logger.h"

#include <chrono>
#include <string>

namespace agent_rpc {
namespace orchestrator {

agent_rpc::common::RedisClient* FeedbackAggregator::redis_ = nullptr;
agent_rpc::common::AgentRuntimeRepository* FeedbackAggregator::runtime_repository_ = nullptr;

void FeedbackAggregator::initialize(agent_rpc::common::RedisClient* redis) {
    redis_ = redis;
    LOG_INFO("FeedbackAggregator initialized");
}

void FeedbackAggregator::setRuntimeRepository(
    agent_rpc::common::AgentRuntimeRepository* repository) {
    runtime_repository_ = repository;
}

void FeedbackAggregator::recalculate() {
    if (runtime_repository_ == nullptr) {
        LOG_WARN("FeedbackAggregator::recalculate skipped: runtime repository not initialized");
        return;
    }

    LOG_INFO("FeedbackAggregator::recalculate — aggregating owner-scoped route quality from feedback");

    int updated_count = 0;
    try {
        // Every distinct (owner, agent, skill) triple gets its own quality
        // row; one owner's feedback can never shift another owner's routing
        // weights. aggregateRouteQuality applies the Beta(2,2) prior.
        for (const auto& key : runtime_repository_->listFeedbackKeys()) {
            if (runtime_repository_->aggregateRouteQuality(
                    key.owner_id, key.agent_id, key.skill_name)) {
                ++updated_count;
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN(std::string("FeedbackAggregator::recalculate failed: ") + e.what());
        return;
    }

    LOG_INFO("FeedbackAggregator::recalculate — refreshed " +
             std::to_string(updated_count) + " owner/agent/skill quality rows in PostgreSQL");
}

void FeedbackAggregator::recalculateMetrics() {
    if (runtime_repository_ == nullptr) {
        LOG_WARN("FeedbackAggregator::recalculateMetrics skipped: runtime repository not initialized");
        return;
    }

    LOG_INFO("FeedbackAggregator::recalculateMetrics — aggregating invocation metrics from agent_invocations");

    int updated_count = 0;
    try {
        for (const auto& metrics : runtime_repository_->aggregateInvocationMetrics()) {
            ++updated_count;
            if (redis_ == nullptr) {
                continue;  // PG aggregate already computed; Redis cache is optional
            }
            // Cache layer only — PostgreSQL agent_invocations stays the
            // durable source of truth for these numbers.
            std::string redis_key = "agent_metrics:" + metrics.agent_id;
            redis_->hset(redis_key, "success_rate", metrics.success_rate);
            redis_->hset(redis_key, "avg_latency_ms", metrics.avg_latency_ms);
            redis_->hset(redis_key, "total_requests", std::to_string(metrics.total_requests));
            redis_->hset(redis_key, "last_updated",
                         std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch()).count()));
        }
    } catch (const std::exception& e) {
        LOG_WARN(std::string("FeedbackAggregator::recalculateMetrics failed: ") + e.what());
        return;
    }

    LOG_INFO("FeedbackAggregator::recalculateMetrics — refreshed metrics for " +
             std::to_string(updated_count) + " agents");
}

} // namespace orchestrator
} // namespace agent_rpc
