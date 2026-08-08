#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <string>

#include "agent_rpc/common/query_domain_repository.h"

#ifndef NEXUSAI_QUERY_DOMAIN_REPOSITORY_HEADER
#error "NEXUSAI_QUERY_DOMAIN_REPOSITORY_HEADER must point at query_domain_repository.h"
#endif

#ifndef NEXUSAI_QUERY_DOMAIN_REPOSITORY_SOURCE
#error "NEXUSAI_QUERY_DOMAIN_REPOSITORY_SOURCE must point at query_domain_repository.cpp"
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

TEST(QueryDomainRepositoryContractTest, HeaderExposesPostgresBackedDomainOperations) {
    const std::string header = readFile(NEXUSAI_QUERY_DOMAIN_REPOSITORY_HEADER);
    ASSERT_FALSE(header.empty());
    EXPECT_NE(header.find("PostgresStore"), std::string::npos);
    EXPECT_NE(header.find("class QueryDomainRepository"), std::string::npos);

    for (const std::string operation : {
             "createConversation", "getConversationById", "listConversations", "appendMessage",
             "listMessages", "createQueryLog", "getQueryLogById", "createTrace", "getTraceById",
             "appendTokenUsageLedger", "listTokenUsageLedgerByOwner", "createFeedback",
             "getRouteQuality", "upsertRouteQuality", "updateQueryLog", "updateTrace"}) {
        EXPECT_NE(header.find(operation), std::string::npos) << operation;
    }
}

TEST(QueryDomainRepositoryContractTest, SourceUsesBoundParametersAndExplicitOwnerFilters) {
    const std::string source = readFile(NEXUSAI_QUERY_DOMAIN_REPOSITORY_SOURCE);
    ASSERT_FALSE(source.empty());

    EXPECT_NE(source.find("execParams"), std::string::npos);
    EXPECT_NE(source.find("$1"), std::string::npos);
    EXPECT_NE(source.find("$2"), std::string::npos);
    EXPECT_NE(source.find("$3"), std::string::npos);
    EXPECT_EQ(source.find("popen"), std::string::npos);
    EXPECT_EQ(uppercase(source).find("REDIS"), std::string::npos);

    // Every read is tenant scoped. The repository has seven read methods:
    // conversation get/list, message list, query-log get, trace get, token list,
    // and route-quality get.
    const std::regex owner_filter{R"(WHERE\s+[^;]*OWNER_ID\s*=\s*\$[0-9])",
                                  std::regex::icase};
    EXPECT_GE(std::distance(std::sregex_iterator(source.begin(), source.end(), owner_filter),
                            std::sregex_iterator()),
              7);

    // Message sequence conflicts are the sole DO NOTHING idempotency path.
    const std::string upper_source = uppercase(source);
    EXPECT_NE(upper_source.find("ON CONFLICT (OWNER_ID, CONVERSATION_ID, SEQUENCE_NO) DO NOTHING"),
              std::string::npos);
    const std::regex do_nothing_pattern{"ON CONFLICT[^;]*DO NOTHING"};
    EXPECT_EQ(std::distance(std::sregex_iterator(upper_source.begin(), upper_source.end(),
                                                 do_nothing_pattern),
                            std::sregex_iterator()),
              1);

    // Owner-scoped updates are plain UPDATEs; they must never upsert.
    EXPECT_NE(upper_source.find("UPDATE QUERY_LOGS SET"), std::string::npos);
    EXPECT_NE(upper_source.find("UPDATE TRACES SET"), std::string::npos);
}

class QueryDomainRepositoryIntegrationTest : public ::testing::Test {
protected:
    using PostgresStore = agent_rpc::common::PostgresStore;
    using QueryDomainRepository = agent_rpc::common::QueryDomainRepository;

    struct Context {
        std::unique_ptr<PostgresStore> store;
        std::unique_ptr<QueryDomainRepository> repository;
    };

    // The repository source path macro doubles as the anchor for locating the
    // durable-domain migration inside the same checkout.
    static std::string durableDomainMigrationPath() {
        const std::string source_path{NEXUSAI_QUERY_DOMAIN_REPOSITORY_SOURCE};
        const std::string suffix = "/common/src/query_domain_repository.cpp";
        if (source_path.size() <= suffix.size() ||
            source_path.compare(source_path.size() - suffix.size(), suffix.size(), suffix) != 0) {
            return {};
        }
        return source_path.substr(0, source_path.size() - suffix.size()) +
               "/db/migrations/V011__durable_domain.sql";
    }

    static std::unique_ptr<Context> makeContext() {
        try {
            auto config = agent_rpc::common::PostgresConfig::fromEnvironment();
            config.pool_size = 1;
            auto context = std::make_unique<Context>();
            context->store = std::make_unique<PostgresStore>(std::move(config));
            const std::string migration_path = durableDomainMigrationPath();
            std::ifstream migration_source{migration_path, std::ios::binary};
            if (migration_path.empty() || !migration_source.is_open()) {
                return nullptr;
            }
            const std::string migration{std::istreambuf_iterator<char>{migration_source},
                                        std::istreambuf_iterator<char>{}};
            context->store->executeTransaction([&migration](pqxx::work& transaction) {
                transaction.exec(migration);
            });
            context->repository =
                std::make_unique<QueryDomainRepository>(*context->store);
            return context;
        } catch (const std::exception&) {
            return nullptr;
        }
    }

    static const std::string& runSuffix() {
        static const std::string suffix = [] {
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            return std::to_string(ticks);
        }();
        return suffix;
    }

    static std::string owner() {
        static std::size_t sequence = 0;
        return "query-domain-owner-" + std::to_string(++sequence) + "-" + runSuffix();
    }

    static std::string entityId(const std::string& label) {
        static std::size_t sequence = 0;
        return "query-domain-" + label + "-" + std::to_string(++sequence) + "-" + runSuffix();
    }

    static agent_rpc::common::QueryLogRecord makeQueryLog(const std::string& owner_id,
                                                          const std::string& conversation_id) {
        agent_rpc::common::QueryLogRecord query_log{};
        query_log.id = entityId("log");
        query_log.owner_id = owner_id;
        query_log.conversation_id = conversation_id;
        query_log.request_text = "contract request";
        query_log.response_text = "";
        query_log.model = "";
        query_log.status = "running";
        return query_log;
    }

    static agent_rpc::common::TraceRecord makeTrace(const std::string& owner_id,
                                                    const std::string& query_log_id) {
        agent_rpc::common::TraceRecord trace{};
        trace.id = entityId("trace");
        trace.owner_id = owner_id;
        trace.query_log_id = query_log_id;
        trace.trace_payload = "{\"stage\":\"start\"}";
        trace.status = "running";
        return trace;
    }
};

TEST_F(QueryDomainRepositoryIntegrationTest, UpdateQueryLogAndTracePersistForOwnedRows) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string owner_id = owner();
    auto query_log = makeQueryLog(owner_id, entityId("conversation"));
    ASSERT_TRUE(context->repository->createQueryLog(query_log));
    auto trace = makeTrace(owner_id, query_log.id);
    ASSERT_TRUE(context->repository->createTrace(trace));

    query_log.response_text = "final answer";
    query_log.model = "contract-model";
    query_log.status = "completed";
    EXPECT_TRUE(context->repository->updateQueryLog(query_log));

    trace.trace_payload = "{\"stage\":\"done\"}";
    trace.status = "completed";
    EXPECT_TRUE(context->repository->updateTrace(trace));

    const auto updated_log = context->repository->getQueryLogById(owner_id, query_log.id);
    ASSERT_TRUE(updated_log.has_value());
    EXPECT_EQ(updated_log->status, "completed");
    EXPECT_EQ(updated_log->response_text, "final answer");
    EXPECT_EQ(updated_log->model, "contract-model");

    const auto updated_trace = context->repository->getTraceById(owner_id, trace.id);
    ASSERT_TRUE(updated_trace.has_value());
    EXPECT_EQ(updated_trace->status, "completed");
    EXPECT_NE(updated_trace->trace_payload.find("done"), std::string::npos);
}

TEST_F(QueryDomainRepositoryIntegrationTest, UpdateMissingRowsReturnsFalseWithoutUpsert) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string owner_id = owner();
    auto missing_log = makeQueryLog(owner_id, entityId("conversation"));
    auto missing_trace = makeTrace(owner_id, missing_log.id);
    missing_log.status = "completed";
    missing_trace.status = "completed";

    EXPECT_FALSE(context->repository->updateQueryLog(missing_log));
    EXPECT_FALSE(context->repository->updateTrace(missing_trace));

    // A failed update must never insert the missing row.
    EXPECT_FALSE(context->repository->getQueryLogById(owner_id, missing_log.id).has_value());
    EXPECT_FALSE(context->repository->getTraceById(owner_id, missing_trace.id).has_value());
}

TEST_F(QueryDomainRepositoryIntegrationTest, CrossOwnerUpdateReturnsFalseAndLeavesRowUntouched) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string victim_owner = owner();
    const std::string intruder_owner = owner();
    auto query_log = makeQueryLog(victim_owner, entityId("conversation"));
    ASSERT_TRUE(context->repository->createQueryLog(query_log));
    auto trace = makeTrace(victim_owner, query_log.id);
    ASSERT_TRUE(context->repository->createTrace(trace));

    auto intruder_log = query_log;
    intruder_log.owner_id = intruder_owner;
    intruder_log.status = "hijacked";
    intruder_log.response_text = "intruder";
    EXPECT_FALSE(context->repository->updateQueryLog(intruder_log));

    auto intruder_trace = trace;
    intruder_trace.owner_id = intruder_owner;
    intruder_trace.status = "hijacked";
    intruder_trace.trace_payload = "{\"stage\":\"intruder\"}";
    EXPECT_FALSE(context->repository->updateTrace(intruder_trace));

    const auto untouched_log = context->repository->getQueryLogById(victim_owner, query_log.id);
    ASSERT_TRUE(untouched_log.has_value());
    EXPECT_EQ(untouched_log->status, "running");
    EXPECT_EQ(untouched_log->response_text, "");

    const auto untouched_trace = context->repository->getTraceById(victim_owner, trace.id);
    ASSERT_TRUE(untouched_trace.has_value());
    EXPECT_EQ(untouched_trace->status, "running");

    // No row may be created for the intruder owner.
    EXPECT_FALSE(context->repository->getQueryLogById(intruder_owner, query_log.id).has_value());
    EXPECT_FALSE(context->repository->getTraceById(intruder_owner, trace.id).has_value());
}

}  // namespace
