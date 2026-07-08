#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include "agent_rpc/common/redis_client.h"

namespace agent_rpc {
namespace common {

struct ModelPrice {
    double prompt_price_per_1k = 0.0;
    double completion_price_per_1k = 0.0;
};

class CostTracker {
public:
    static CostTracker& instance();

    // Initialize with Redis client for budget counters
    void initialize(RedisClient* redis);

    // Record an LLM API call
    void recordLLMCall(const std::string& trace_id,
                       const std::string& user_id,
                       const std::string& context_id,
                       const std::string& agent_id,
                       const std::string& component,
                       int prompt_tokens,
                       int completion_tokens,
                       const std::string& model,
                       int64_t latency_ms);

    // Static helper: calculate cost for a model + token counts
    static double calculateCost(const std::string& model,
                                int prompt_tokens,
                                int completion_tokens);

    // Convert dollar amount to micro-dollars (int64 for Redis atomic ops)
    static int64_t toMicroDollars(double cost_usd);

    // Get pricing for a model
    static ModelPrice getModelPrice(const std::string& model);

private:
    CostTracker() = default;
    void updateRedisBudget(const std::string& user_id, double cost_usd);

    RedisClient* redis_ = nullptr;
    static std::unordered_map<std::string, ModelPrice> pricing_;
    static std::once_flag pricing_init_;
    static void initPricing();
};

}  // namespace common
}  // namespace agent_rpc
