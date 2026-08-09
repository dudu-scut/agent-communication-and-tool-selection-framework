#pragma once

#include "agent_rpc/common/postgres_store.h"

#include <cstdint>
#include <string>

namespace agent_rpc::common {

// A value of zero means that the corresponding bucket is unlimited.
struct BudgetLimits {
    std::int64_t global = 0;
    std::int64_t user_daily = 0;
    std::int64_t user_monthly = 0;
    std::int64_t session = 0;
};

struct BudgetReservationResult {
    bool accepted = false;
    bool idempotent = false;
    std::string reason;
};

using BudgetReserveResult = BudgetReservationResult;
using ReserveResult = BudgetReservationResult;

struct BudgetUsage {
    std::int64_t global = 0;
    std::int64_t user_daily = 0;
    std::int64_t user_monthly = 0;
    std::int64_t session = 0;
};

using BudgetUsageSnapshot = BudgetUsage;

// Persists token reservations and all four counters in PostgreSQL.  The
// caller supplies the default limits; an owner policy, when present, takes
// precedence for that owner.
class PostgresBudgetRepository final {
public:
    explicit PostgresBudgetRepository(PostgresStore& store);

    BudgetReservationResult reserve(const std::string& owner_id,
                                    const std::string& context_id,
                                    const std::string& request_id,
                                    std::int64_t estimated_tokens,
                                    const BudgetLimits& limits);

    bool setOwnerPolicy(const std::string& owner_id, const BudgetLimits& limits);

    BudgetUsage usage(const std::string& owner_id, const std::string& context_id);

    BudgetUsage getUsage(const std::string& owner_id, const std::string& context_id) {
        return usage(owner_id, context_id);
    }

private:
    PostgresStore& store_;
};

using BudgetRepository = PostgresBudgetRepository;

}  // namespace agent_rpc::common
