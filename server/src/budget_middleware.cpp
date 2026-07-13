#include "agent_rpc/server/budget_middleware.h"
#include "agent_rpc/common/logger.h"
#include <cstdlib>
#include <charconv>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace agent_rpc {
namespace server {

namespace {

// Default budget limits in micro-dollars
// (equivalent to $0.001/request, $0.05/session, $1/day, $30/month, $1000/global)
constexpr int64_t kDefaultRequestBudgetMicro   = 1'000;
constexpr int64_t kDefaultSessionBudgetMicro   = 50'000;
constexpr int64_t kDefaultUserDailyBudgetMicro = 1'000'000;
constexpr int64_t kDefaultUserMonthlyBudgetMicro = 30'000'000;
constexpr int64_t kDefaultGlobalBudgetMicro    = 1'000'000'000; // $1,000

int64_t loadLimitMicro(const char* env_name, int64_t default_micro) {
    const char* val = std::getenv(env_name);
    if (!val) return default_micro;
    try {
        // Input is in dollars — convert to micro-dollars
        double dollars = std::stod(val);
        return static_cast<int64_t>(dollars * 1'000'000.0);
    } catch (...) {
        return default_micro;
    }
}

// Redis key helpers
std::string sessionKey(const std::string& user_id, const std::string& context_id) {
    return "budget:session:" + user_id + ":" + context_id;
}

std::string dailyKey(const std::string& user_id) {
    std::time_t now = std::time(nullptr);
    std::tm tm_buf;
    localtime_r(&now, &tm_buf);
    std::ostringstream ss;
    ss << "budget:daily:" << user_id << ":"
       << (tm_buf.tm_year + 1900) << "-"
       << std::setw(2) << std::setfill('0') << (tm_buf.tm_mon + 1) << "-"
       << std::setw(2) << std::setfill('0') << tm_buf.tm_mday;
    return ss.str();
}

std::string monthlyKey(const std::string& user_id) {
    std::time_t now = std::time(nullptr);
    std::tm tm_buf;
    localtime_r(&now, &tm_buf);
    std::ostringstream ss;
    ss << "budget:monthly:" << user_id << ":"
       << (tm_buf.tm_year + 1900) << "-"
       << std::setw(2) << std::setfill('0') << (tm_buf.tm_mon + 1);
    return ss.str();
}

std::string globalKey() {
    return "budget:global";
}

} // anonymous namespace

BudgetMiddleware::Result BudgetMiddleware::checkAndDeduct(
    agent_rpc::common::RedisClient* redis,
    const std::string& user_id,
    const std::string& context_id,
    const std::string& /*trace_id*/,
    int64_t estimated_cost_micro)
{
    if (!redis) {
        // No Redis — budget check is a no-op (allow)
        return OK;
    }

    if (!redis->isConnected()) {
        LOG_WARN("BudgetMiddleware: Redis not connected, allowing request");
        return OK;
    }

    // Load limits once per check
    int64_t req_limit   = loadLimitMicro("BUDGET_REQUEST_LIMIT",   kDefaultRequestBudgetMicro);
    int64_t sess_limit  = loadLimitMicro("BUDGET_SESSION_LIMIT",   kDefaultSessionBudgetMicro);
    int64_t daily_limit = loadLimitMicro("BUDGET_DAILY_LIMIT",     kDefaultUserDailyBudgetMicro);
    int64_t monthly_limit = loadLimitMicro("BUDGET_MONTHLY_LIMIT", kDefaultUserMonthlyBudgetMicro);
    int64_t global_limit = loadLimitMicro("BUDGET_GLOBAL_LIMIT",   kDefaultGlobalBudgetMicro);

    // 1. Per-request check (no Redis needed)
    if (estimated_cost_micro > req_limit) {
        LOG_WARN("Budget: request cost exceeds per-request limit");
        return REQUEST_OVER;
    }

    // Track which tiers were incremented, so we can rollback on failure
    bool global_incr = false, daily_incr = false, monthly_incr = false, session_incr = false;

    // 2. Global check
    {
        std::string gkey = globalKey();
        int64_t global_total;
        redis->incrby(gkey, estimated_cost_micro, global_total);
        global_incr = true;
        if (global_total == estimated_cost_micro) {
            redis->expire(gkey, 86400 * 365); // 1 year
        }
        if (global_total > global_limit) {
            LOG_WARN("Budget: global budget exceeded (" +
                     std::to_string(global_total) + " > " +
                     std::to_string(global_limit) + ")");
            // Rollback global counter before returning
            redis->incrby(gkey, -estimated_cost_micro, global_total);
            return GLOBAL_OVER;
        }
    }

    // 3. User daily check
    {
        std::string dkey = dailyKey(user_id);
        int64_t daily_total;
        redis->incrby(dkey, estimated_cost_micro, daily_total);
        daily_incr = true;
        if (daily_total == estimated_cost_micro) {
            redis->expire(dkey, 86400); // 24h TTL
        }
        if (daily_total > daily_limit) {
            LOG_WARN("Budget: user daily budget exceeded for " + user_id);
            // Rollback: decrement global + daily
            if (global_incr) { std::string gkey = globalKey(); int64_t dummy; redis->incrby(gkey, -estimated_cost_micro, dummy); }
            redis->incrby(dkey, -estimated_cost_micro, daily_total);
            return USER_DAILY_OVER;
        }
    }

    // 4. User monthly check
    {
        std::string mkey = monthlyKey(user_id);
        int64_t monthly_total;
        redis->incrby(mkey, estimated_cost_micro, monthly_total);
        monthly_incr = true;
        if (monthly_total == estimated_cost_micro) {
            redis->expire(mkey, 86400 * 31); // 31 days TTL
        }
        if (monthly_total > monthly_limit) {
            LOG_WARN("Budget: user monthly budget exceeded for " + user_id);
            // Rollback: decrement global + daily + monthly
            if (global_incr) { std::string gkey = globalKey(); int64_t dummy; redis->incrby(gkey, -estimated_cost_micro, dummy); }
            if (daily_incr) { std::string dkey = dailyKey(user_id); int64_t dummy; redis->incrby(dkey, -estimated_cost_micro, dummy); }
            redis->incrby(mkey, -estimated_cost_micro, monthly_total);
            return USER_MONTHLY_OVER;
        }
    }

    // 5. Session check
    {
        std::string skey = sessionKey(user_id, context_id);
        int64_t session_total;
        redis->incrby(skey, estimated_cost_micro, session_total);
        session_incr = true;
        if (session_total == estimated_cost_micro) {
            redis->expire(skey, 3600); // 1h TTL
        }
        if (session_total > sess_limit) {
            LOG_WARN("Budget: session budget exceeded for " + user_id + "/" + context_id);
            // Rollback: decrement all tiers
            if (global_incr) { std::string gkey = globalKey(); int64_t dummy; redis->incrby(gkey, -estimated_cost_micro, dummy); }
            if (daily_incr) { std::string dkey = dailyKey(user_id); int64_t dummy; redis->incrby(dkey, -estimated_cost_micro, dummy); }
            if (monthly_incr) { std::string mkey = monthlyKey(user_id); int64_t dummy; redis->incrby(mkey, -estimated_cost_micro, dummy); }
            redis->incrby(skey, -estimated_cost_micro, session_total);
            return SESSION_OVER;
        }
    }

    return OK;
}

std::string BudgetMiddleware::resultMessage(Result r) {
    switch (r) {
        case OK:               return "OK";
        case REQUEST_OVER:     return "Request exceeds per-request budget limit";
        case SESSION_OVER:     return "Session budget exhausted";
        case USER_DAILY_OVER:  return "Daily budget limit reached";
        case USER_MONTHLY_OVER: return "Monthly budget limit reached";
        case GLOBAL_OVER:      return "Global budget exhausted";
        default:               return "Unknown budget result";
    }
}

} // namespace server
} // namespace agent_rpc
