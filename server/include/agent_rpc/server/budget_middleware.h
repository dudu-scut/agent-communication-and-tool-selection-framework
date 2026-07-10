#pragma once

#include "agent_rpc/common/redis_client.h"
#include <string>

namespace agent_rpc {
namespace server {

/**
 * @brief Token Budget Middleware (Batch 5)
 *
 * Four-level budget check before allowing an AI query to proceed:
 *   1. Global budget
 *   2. User daily budget
 *   3. User monthly budget
 *   4. Per-session budget
 *
 * Each level is a Redis INCRBY key with a TTL. Budget limits are loaded
 * from environment variables (micro-dollar defaults below).
 */
class BudgetMiddleware {
public:
    enum Result {
        OK,
        REQUEST_OVER,       // Estimated cost exceeds per-request limit
        SESSION_OVER,       // Session budget exhausted
        USER_DAILY_OVER,    // User daily budget exhausted
        USER_MONTHLY_OVER,  // User monthly budget exhausted
        GLOBAL_OVER         // Global budget exhausted
    };

    /**
     * @brief Atomically check all budget levels and deduct if OK.
     * @param redis        Redis client instance
     * @param user_id      User identifier
     * @param context_id   Conversation/session identifier
     * @param trace_id     Trace identifier for logging
     * @param estimated_cost_micro  Estimated cost in micro-dollars
     * @return Result code (OK if all checks passed)
     */
    static Result checkAndDeduct(
        agent_rpc::common::RedisClient* redis,
        const std::string& user_id,
        const std::string& context_id,
        const std::string& trace_id,
        int64_t estimated_cost_micro);

    /**
     * @brief Return a human-readable message for a result code.
     */
    static std::string resultMessage(Result r);
};

} // namespace server
} // namespace agent_rpc
