#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "agent_rpc/common/postgres_budget_repository.h"

#ifndef NEXUSAI_BUDGET_MIGRATION_SOURCE
#error "NEXUSAI_BUDGET_MIGRATION_SOURCE must point at V012__postgres_budget.sql"
#endif

#ifndef NEXUSAI_BUDGET_REPOSITORY_HEADER
#error "NEXUSAI_BUDGET_REPOSITORY_HEADER must point at postgres_budget_repository.h"
#endif

namespace {

std::string readFile(const char* path) {
    std::ifstream source{path, std::ios::binary};
    EXPECT_TRUE(source.is_open()) << "cannot read " << path;
    return {std::istreambuf_iterator<char>{source}, std::istreambuf_iterator<char>{}};
}

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

std::string tableDefinition(const std::string& source, const std::string& table_name) {
    const std::string marker = "CREATE TABLE IF NOT EXISTS " + table_name;
    const std::size_t start = source.find(marker);
    EXPECT_NE(start, std::string::npos) << "missing table " << table_name;
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t end = source.find(");", start);
    EXPECT_NE(end, std::string::npos) << "unterminated table " << table_name;
    if (end == std::string::npos) {
        return source.substr(start);
    }
    return source.substr(start, end - start);
}

TEST(PostgresBudgetRepositoryContractTest, MigrationDefinesDurableBudgetTablesAndIndexes) {
    const std::string migration = uppercase(readFile(NEXUSAI_BUDGET_MIGRATION_SOURCE));
    ASSERT_FALSE(migration.empty());
    EXPECT_EQ(migration.find("FOREIGN KEY"), std::string::npos);
    EXPECT_EQ(migration.find(" REFERENCES "), std::string::npos);

    for (const std::string table_name : {"BUDGET_RESERVATIONS", "BUDGET_COUNTERS", "BUDGET_POLICIES"}) {
        const std::string table = tableDefinition(migration, table_name);
        ASSERT_FALSE(table.empty()) << table_name;
        EXPECT_NE(table.find("OWNER_ID TEXT NOT NULL"), std::string::npos) << table_name;
        EXPECT_NE(table.find("CREATED_AT TIMESTAMPTZ NOT NULL DEFAULT NOW()"), std::string::npos)
            << table_name;
        EXPECT_NE(table.find("UPDATED_AT TIMESTAMPTZ NOT NULL DEFAULT NOW()"), std::string::npos)
            << table_name;
    }

    EXPECT_NE(migration.find("CREATE TABLE IF NOT EXISTS BUDGET_RESERVATIONS"), std::string::npos);
    EXPECT_NE(migration.find("CREATE TABLE IF NOT EXISTS BUDGET_COUNTERS"), std::string::npos);
    EXPECT_NE(migration.find("CREATE TABLE IF NOT EXISTS BUDGET_POLICIES"), std::string::npos);
    const std::string reservations = tableDefinition(migration, "BUDGET_RESERVATIONS");
    ASSERT_FALSE(reservations.empty());
    EXPECT_NE(reservations.find("REQUEST_ID TEXT PRIMARY KEY"), std::string::npos);
    EXPECT_NE(reservations.find("STATUS TEXT NOT NULL DEFAULT 'RESERVED'"), std::string::npos);
    EXPECT_NE(reservations.find("CHECK (LENGTH(STATUS) > 0)"), std::string::npos);
    EXPECT_EQ(migration.find("UQ_BUDGET_RESERVATIONS_OWNER_REQUEST"), std::string::npos);
    EXPECT_EQ(migration.find("UQ_BUDGET_COUNTERS_BUCKET"), std::string::npos);
    EXPECT_EQ(migration.find("UQ_BUDGET_POLICIES_OWNER"), std::string::npos);
    EXPECT_NE(migration.find("IDX_BUDGET_COUNTERS_OWNER_SESSION"), std::string::npos);
    EXPECT_NE(migration.find("CHECK (ESTIMATED_TOKENS >= 0)"), std::string::npos);
    EXPECT_NE(migration.find("CHECK (GLOBAL_LIMIT >= 0)"), std::string::npos);
    EXPECT_NE(migration.find("CHECK (USER_DAILY_LIMIT >= 0)"), std::string::npos);
    EXPECT_NE(migration.find("CHECK (USER_MONTHLY_LIMIT >= 0)"), std::string::npos);
    EXPECT_NE(migration.find("CHECK (SESSION_LIMIT >= 0)"), std::string::npos);
}

TEST(PostgresBudgetRepositoryContractTest, PublicApiUsesPostgresStoreAndExposesReservationResult) {
    const std::string header = readFile(NEXUSAI_BUDGET_REPOSITORY_HEADER);
    ASSERT_FALSE(header.empty());
    EXPECT_NE(header.find("PostgresStore"), std::string::npos);
    EXPECT_NE(header.find("struct BudgetLimits"), std::string::npos);
    EXPECT_NE(header.find("global"), std::string::npos);
    EXPECT_NE(header.find("user_daily"), std::string::npos);
    EXPECT_NE(header.find("user_monthly"), std::string::npos);
    EXPECT_NE(header.find("session"), std::string::npos);
    EXPECT_NE(header.find("reserve"), std::string::npos);
    EXPECT_NE(header.find("accepted"), std::string::npos);
    EXPECT_NE(header.find("idempotent"), std::string::npos);
    EXPECT_NE(header.find("reason"), std::string::npos);
    EXPECT_EQ(header.find("redis"), std::string::npos);
    EXPECT_EQ(header.find("popen"), std::string::npos);
}

class PostgresBudgetRepositoryIntegrationTest : public ::testing::Test {
protected:
    struct Context {
        std::unique_ptr<agent_rpc::common::PostgresStore> store;
        std::unique_ptr<agent_rpc::common::PostgresBudgetRepository> repository;
    };

    static std::unique_ptr<Context> makeContext() {
        try {
            auto config = agent_rpc::common::PostgresConfig::fromEnvironment();
            config.pool_size = 1;
            auto context = std::make_unique<Context>();
            context->store = std::make_unique<agent_rpc::common::PostgresStore>(std::move(config));
            std::ifstream migration_source{NEXUSAI_BUDGET_MIGRATION_SOURCE, std::ios::binary};
            if (!migration_source.is_open()) {
                return nullptr;
            }
            const std::string migration{std::istreambuf_iterator<char>{migration_source},
                                       std::istreambuf_iterator<char>{}};
            context->store->executeTransaction([&migration](pqxx::work& transaction) {
                transaction.exec(migration);
            });
            context->repository = std::make_unique<agent_rpc::common::PostgresBudgetRepository>(*context->store);
            return context;
        } catch (const std::exception&) {
            return nullptr;
        }
    }

    static std::string owner() {
        static std::size_t sequence = 0;
        return "budget-contract-owner-" + std::to_string(++sequence);
    }

    static agent_rpc::common::BudgetLimits generousLimits() {
        return agent_rpc::common::BudgetLimits{
            .global = 1000,
            .user_daily = 1000,
            .user_monthly = 1000,
            .session = 1000,
        };
    }
};

TEST_F(PostgresBudgetRepositoryIntegrationTest, ReserveSucceedsAndIsIdempotent) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string owner_id = owner();
    const auto first = context->repository->reserve(owner_id, "session-a", "request-a", 25,
                                                     generousLimits());
    ASSERT_TRUE(first.accepted);
    EXPECT_FALSE(first.idempotent);

    const auto second = context->repository->reserve(owner_id, "session-a", "request-a", 25,
                                                      generousLimits());
    EXPECT_TRUE(second.accepted);
    EXPECT_TRUE(second.idempotent);

    const auto usage = context->repository->usage(owner_id, "session-a");
    EXPECT_EQ(usage.global, 25);
    EXPECT_EQ(usage.user_daily, 25);
    EXPECT_EQ(usage.user_monthly, 25);
    EXPECT_EQ(usage.session, 25);
}

TEST_F(PostgresBudgetRepositoryIntegrationTest, SameRequestIdAcrossOwnersIsRejectedWithoutIdempotency) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string first_owner = owner();
    const std::string second_owner = owner();
    const auto limits = generousLimits();
    ASSERT_TRUE(context->repository->reserve(first_owner, "session-cross-owner", "request-shared", 25,
                                              limits)
                    .accepted);

    const auto rejected = context->repository->reserve(second_owner, "session-cross-owner", "request-shared",
                                                       25, limits);
    EXPECT_FALSE(rejected.accepted);
    EXPECT_FALSE(rejected.idempotent);
    EXPECT_EQ(rejected.reason, "request unavailable");

    const auto second_usage = context->repository->usage(second_owner, "session-cross-owner");
    EXPECT_EQ(second_usage.user_daily, 0);
    EXPECT_EQ(second_usage.user_monthly, 0);
    EXPECT_EQ(second_usage.session, 0);
}

TEST_F(PostgresBudgetRepositoryIntegrationTest, RejectionDoesNotPartiallyConsumeAnyBucket) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string owner_id = owner();
    auto limits = generousLimits();
    limits.user_daily = 5;
    ASSERT_TRUE(context->repository->reserve(owner_id, "session-b", "request-b1", 4, limits).accepted);
    const auto before = context->repository->usage(owner_id, "session-b");

    const auto rejected = context->repository->reserve(owner_id, "session-b", "request-b2", 2, limits);
    EXPECT_FALSE(rejected.accepted);
    EXPECT_FALSE(rejected.idempotent);
    EXPECT_NE(rejected.reason.find("daily"), std::string::npos);

    const auto after = context->repository->usage(owner_id, "session-b");
    EXPECT_EQ(after.global, before.global);
    EXPECT_EQ(after.user_daily, before.user_daily);
    EXPECT_EQ(after.user_monthly, before.user_monthly);
    EXPECT_EQ(after.session, before.session);
}

TEST_F(PostgresBudgetRepositoryIntegrationTest, OwnerPolicyOverridesRequestLimits) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string owner_id = owner();
    auto policy = generousLimits();
    policy.user_daily = 3;
    ASSERT_TRUE(context->repository->setOwnerPolicy(owner_id, policy));

    const auto accepted = context->repository->reserve(owner_id, "session-c", "request-c1", 3,
                                                        generousLimits());
    EXPECT_TRUE(accepted.accepted);
    const auto rejected = context->repository->reserve(owner_id, "session-c", "request-c2", 1,
                                                        generousLimits());
    EXPECT_FALSE(rejected.accepted);
    EXPECT_NE(rejected.reason.find("daily"), std::string::npos);
}

TEST_F(PostgresBudgetRepositoryIntegrationTest, RejectsEmptyNulAndNegativeInputs) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const auto limits = generousLimits();
    EXPECT_THROW(context->repository->reserve("", "session", "request", 1, limits), std::invalid_argument);
    EXPECT_THROW(context->repository->reserve("owner", "", "request", 1, limits), std::invalid_argument);
    EXPECT_THROW(context->repository->reserve("owner", "session", "", 1, limits), std::invalid_argument);
    EXPECT_THROW(context->repository->reserve(std::string{"owner\0bad", 9}, "session", "request", 1, limits),
                 std::invalid_argument);
    EXPECT_THROW(context->repository->reserve("owner", "session", "request", -1, limits),
                 std::invalid_argument);
}

}  // namespace
