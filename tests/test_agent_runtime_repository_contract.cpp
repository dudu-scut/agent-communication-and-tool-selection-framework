/**
 * @file test_agent_runtime_repository_contract.cpp
 * @brief PR-C3: PG observability, feedback & agent lifecycle — contract + integration.
 *
 * Three layers (all against real PostgreSQL / Redis, no repository mocks):
 *   1. Static source guards: no popen(, no execPsql, no PG_URL in the
 *      runtime code paths; CANARY weighting removed; owner-only services.
 *   2. AgentRuntimeRepository integration (registry upsert/heartbeat,
 *      owner-scoped feedback → route quality, invocation metrics, cost).
 *   3. End-to-end gRPC: admin gating, owner isolation for feedback /
 *      trace detail / cost report, durability across Redis FLUSHDB.
 */

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <hiredis/hiredis.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "agent_rpc/common/postgres_store.h"
#include "agent_rpc/common/query_domain_repository.h"
#include "agent_rpc/common/agent_runtime_repository.h"
#include "agent_rpc/a2a_adapter/a2a_config.h"
#include "agent_rpc/server/rpc_server.h"

#include "agent_service.grpc.pb.h"
#include "agent_lifecycle.grpc.pb.h"
#include "observability.grpc.pb.h"
#include "user.grpc.pb.h"

#ifndef NEXUSAI_RUNTIME_ROOT
#error "NEXUSAI_RUNTIME_ROOT must point at the repository checkout root"
#endif

namespace {

namespace common_ns = agent_rpc::common;
namespace server_ns = agent_rpc::server;

std::string rootPath() {
    return NEXUSAI_RUNTIME_ROOT;
}

std::string readFileOrEmpty(const std::string& path) {
    std::ifstream source{path, std::ios::binary};
    if (!source.is_open()) {
        return {};
    }
    return {std::istreambuf_iterator<char>{source}, std::istreambuf_iterator<char>{}};
}

std::size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    std::size_t count = 0;
    std::size_t position = haystack.find(needle);
    while (position != std::string::npos) {
        ++count;
        position = haystack.find(needle, position + needle.size());
    }
    return count;
}

std::string uniqueSuffix() {
    static std::atomic<std::int64_t> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(ticks) + "-" + std::to_string(counter.fetch_add(1));
}

std::string utcToday() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &utc);
    return buffer;
}

// ============================================================================
// 1. Static source guards (required item 5)
// ============================================================================

TEST(RuntimeFactsContractTest, NoPopenOrPsqlOrPgUrlInRuntimeCode) {
    const std::vector<std::string> guarded_files = {
        "/orchestrator/src/feedback_aggregator.cpp",
        "/orchestrator/include/agent_rpc/orchestrator/feedback_aggregator.h",
        "/common/src/agent_runtime_repository.cpp",
        "/common/include/agent_rpc/common/agent_runtime_repository.h",
        "/server/src/observability_service.cpp",
        "/server/src/agent_lifecycle_service.cpp",
        "/server/src/agent_service.cpp",
    };
    for (const auto& relative : guarded_files) {
        const std::string content = readFileOrEmpty(rootPath() + relative);
        ASSERT_FALSE(content.empty()) << relative;
        EXPECT_EQ(content.find("popen("), std::string::npos) << relative;
        EXPECT_EQ(content.find("execPsql"), std::string::npos) << relative;
        EXPECT_EQ(content.find("PG_URL"), std::string::npos) << relative;
        EXPECT_EQ(content.find("psql"), std::string::npos) << relative;
    }
}

TEST(RuntimeFactsContractTest, RouterDroppedCanaryWeightsAndOwnerLessRedisQuality) {
    const std::string router = readFileOrEmpty(rootPath() + "/orchestrator/src/agent_router.cpp");
    ASSERT_FALSE(router.empty());
    EXPECT_EQ(router.find("CANARY"), std::string::npos);
    EXPECT_EQ(router.find("DEPRECATED"), std::string::npos);
    // Owner-less Redis feedback hashes are no longer the quality source.
    EXPECT_EQ(router.find("feedback:\" + agent_id"), std::string::npos);
    EXPECT_NE(router.find("quality_provider_"), std::string::npos);
}

TEST(RuntimeFactsContractTest, MainRemovedCronAndCanaryTasks) {
    const std::string main = readFileOrEmpty(rootPath() + "/server/src/main.cpp");
    ASSERT_FALSE(main.empty());
    EXPECT_EQ(main.find("CronScheduler::initialize"), std::string::npos);
    EXPECT_EQ(main.find("canary_evaluation"), std::string::npos);
}

TEST(RuntimeFactsContractTest, LifecycleAndObservabilityAreOwnerScoped) {
    const std::string lifecycle =
        readFileOrEmpty(rootPath() + "/server/src/agent_lifecycle_service.cpp");
    ASSERT_FALSE(lifecycle.empty());
    EXPECT_NE(lifecycle.find("AuthInterceptor::currentUserId()"), std::string::npos);
    EXPECT_NE(lifecycle.find("insertFeedback("), std::string::npos);
    EXPECT_NE(lifecycle.find("aggregateRouteQuality("), std::string::npos);
    EXPECT_NE(lifecycle.find("NOT_FOUND"), std::string::npos);

    const std::string observability =
        readFileOrEmpty(rootPath() + "/server/src/observability_service.cpp");
    ASSERT_FALSE(observability.empty());
    EXPECT_NE(observability.find("AuthInterceptor::currentUserId()"), std::string::npos);
    EXPECT_NE(observability.find("dailyCostReport("), std::string::npos);
    EXPECT_NE(observability.find("set_estimated("), std::string::npos);
    // The request-body user_id must not drive the cost query anymore.
    EXPECT_EQ(observability.find("request->user_id()"), std::string::npos);
}

TEST(RuntimeFactsContractTest, MigrationIsAppendOnlyAndIdempotent) {
    const std::string migration =
        readFileOrEmpty(rootPath() + "/db/migrations/V013__runtime_facts.sql");
    ASSERT_FALSE(migration.empty());
    EXPECT_NE(migration.find("ADD COLUMN IF NOT EXISTS trace_id"), std::string::npos);
    EXPECT_NE(migration.find("ADD COLUMN IF NOT EXISTS skill_name"), std::string::npos);
    EXPECT_NE(migration.find("CREATE TABLE IF NOT EXISTS agent_invocations"), std::string::npos);
    EXPECT_NE(migration.find("uq_agent_route_quality_owner_agent_skill"), std::string::npos);
    // Append-only migrations never drop tables or columns of earlier versions.
    EXPECT_EQ(migration.find("DROP TABLE"), std::string::npos);
    EXPECT_EQ(migration.find("DROP COLUMN"), std::string::npos);
}

// ============================================================================
// 2. Repository integration (real PostgreSQL)
// ============================================================================

class AgentRuntimeRepositoryTest : public ::testing::Test {
protected:
    struct Context {
        std::unique_ptr<common_ns::PostgresStore> store;
        std::unique_ptr<common_ns::QueryDomainRepository> domain;
        std::unique_ptr<common_ns::AgentRuntimeRepository> runtime;
    };

    static std::unique_ptr<Context> makeContext() {
        try {
            auto config = common_ns::PostgresConfig::fromEnvironment();
            config.pool_size = 4;
            auto context = std::make_unique<Context>();
            context->store = std::make_unique<common_ns::PostgresStore>(std::move(config));
            for (const std::string& name :
                 {"V010__local_auth.sql", "V011__durable_domain.sql",
                  "V012__postgres_budget.sql", "V013__runtime_facts.sql"}) {
                const std::string migration =
                    readFileOrEmpty(rootPath() + "/db/migrations/" + name);
                if (migration.empty()) {
                    return nullptr;
                }
                context->store->executeTransaction([&migration](pqxx::work& transaction) {
                    transaction.exec(migration);
                });
            }
            context->domain =
                std::make_unique<common_ns::QueryDomainRepository>(*context->store);
            context->runtime =
                std::make_unique<common_ns::AgentRuntimeRepository>(*context->store);
            return context;
        } catch (const std::exception&) {
            return nullptr;
        }
    }

    std::int64_t countRows(common_ns::PostgresStore& store, const std::string& table,
                           const std::string& column, const std::string& value) {
        std::int64_t count = 0;
        store.executeTransaction([&](pqxx::work& transaction) {
            const auto result = transaction.exec_params(
                "SELECT COUNT(*) FROM " + table + " WHERE " + column + " = $1", value);
            if (!result.empty()) {
                count = result[0][0].as<std::int64_t>();
            }
        });
        return count;
    }
};

TEST_F(AgentRuntimeRepositoryTest, RegistryUpsertSurvivesReRegistrationAndStatusChanges) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string agent_id = "c3-agent-" + uniqueSuffix();
    common_ns::AgentRegistryRecord record;
    record.id = "registry-" + agent_id;
    record.owner_id = "system";
    record.agent_id = agent_id;
    record.display_name = "Echo Agent";
    record.capabilities = "{\"skills\":[\"echo\"]}";
    record.health_status = "healthy";

    ASSERT_TRUE(context->runtime->upsertAgentRegistry(record));
    auto stored = context->runtime->getAgent(agent_id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->display_name, "Echo Agent");
    EXPECT_EQ(stored->owner_id, "system");

    // Restart = re-register: still exactly one durable row for the agent.
    ASSERT_TRUE(context->runtime->upsertAgentRegistry(record));
    EXPECT_EQ(countRows(*context->store, "agent_registry", "agent_id", agent_id), 1);

    ASSERT_TRUE(context->runtime->updateAgentHeartbeat(agent_id));
    ASSERT_TRUE(context->runtime->markAgentStatus(agent_id, "offline"));
    stored = context->runtime->getAgent(agent_id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->health_status, "offline");

    bool listed = false;
    for (const auto& entry : context->runtime->listAgents()) {
        if (entry.agent_id == agent_id) {
            listed = true;
        }
    }
    EXPECT_TRUE(listed);
}

TEST_F(AgentRuntimeRepositoryTest, FeedbackRouteQualityIsOwnerAndSkillScoped) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string owner_a = "c3-owner-a-" + uniqueSuffix();
    const std::string owner_b = "c3-owner-b-" + uniqueSuffix();
    const std::string agent_id = "c3-quality-agent-" + uniqueSuffix();
    const std::string skill = "echo";

    common_ns::RuntimeFeedbackRecord feedback;
    feedback.owner_id = owner_a;
    feedback.query_log_id = "ql-a";
    feedback.trace_id = "trace-a";
    feedback.agent_id = agent_id;
    feedback.skill_name = skill;

    feedback.id = "c3-fb-1-" + uniqueSuffix();
    feedback.rating = 5;
    ASSERT_TRUE(context->runtime->insertFeedback(feedback));
    feedback.id = "c3-fb-2-" + uniqueSuffix();
    feedback.rating = 1;
    ASSERT_TRUE(context->runtime->insertFeedback(feedback));

    ASSERT_TRUE(context->runtime->aggregateRouteQuality(owner_a, agent_id, skill));
    const auto quality = context->runtime->getRouteQuality(owner_a, agent_id, skill);
    ASSERT_TRUE(quality.has_value());
    EXPECT_EQ(quality->sample_count, 2);
    // Beta(2,2) smoothing: (positive=1 + 2) / (total=2 + 4) = 0.5
    EXPECT_NEAR(std::stod(quality->routing_weight), 0.5, 1e-6);
    const auto rate = context->runtime->feedbackApprovalRate(owner_a, agent_id, skill);
    ASSERT_TRUE(rate.has_value());
    EXPECT_NEAR(*rate, 0.5, 1e-6);

    // Owner B has no feedback: no quality row, no borrowed approval rate.
    EXPECT_FALSE(context->runtime->getRouteQuality(owner_b, agent_id, skill).has_value());
    EXPECT_FALSE(context->runtime->feedbackApprovalRate(owner_b, agent_id, skill).has_value());
    // Same owner, different skill: equally isolated.
    EXPECT_FALSE(
        context->runtime->getRouteQuality(owner_a, agent_id, "other-skill").has_value());
}

TEST_F(AgentRuntimeRepositoryTest, InvocationFactsAggregatePerAgent) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string owner = "c3-inv-owner-" + uniqueSuffix();
    const std::string agent_id = "c3-inv-agent-" + uniqueSuffix();
    common_ns::AgentInvocationRecord invocation;
    invocation.owner_id = owner;
    invocation.query_log_id = "ql-inv";
    invocation.agent_id = agent_id;
    invocation.skill_name = "echo";
    invocation.latency_ms = 100;

    for (int index = 0; index < 2; ++index) {
        invocation.id = "c3-inv-s-" + std::to_string(index) + "-" + uniqueSuffix();
        invocation.status = "success";
        ASSERT_TRUE(context->runtime->recordInvocation(invocation));
    }
    invocation.id = "c3-inv-f-" + uniqueSuffix();
    invocation.status = "failed";
    ASSERT_TRUE(context->runtime->recordInvocation(invocation));

    EXPECT_EQ(context->runtime->listInvocationsByOwner(owner).size(), 3u);

    bool found = false;
    for (const auto& metrics : context->runtime->aggregateInvocationMetrics()) {
        if (metrics.agent_id == agent_id) {
            found = true;
            EXPECT_EQ(metrics.total_requests, 3);
            EXPECT_NEAR(std::stod(metrics.success_rate), 66.67, 0.01);
            EXPECT_NEAR(std::stod(metrics.avg_latency_ms), 100.0, 0.01);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(AgentRuntimeRepositoryTest, DailyCostReportIsOwnerScopedAndFlaggedEstimated) {
    auto context = makeContext();
    if (!context) {
        GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
    }

    const std::string owner_a = "c3-cost-a-" + uniqueSuffix();
    const std::string owner_b = "c3-cost-b-" + uniqueSuffix();
    const std::string request_id = "c3-cost-req-" + uniqueSuffix();

    common_ns::TokenUsageLedgerRecord usage;
    usage.id = "c3-cost-entry-" + uniqueSuffix();
    usage.owner_id = owner_a;
    usage.query_log_id = request_id;
    usage.model = "test-model";
    usage.prompt_tokens = 100;
    usage.completion_tokens = 50;
    usage.estimated = true;  // provider usage unavailable → estimate only
    usage.cost_usd = "0.001234";
    ASSERT_TRUE(context->domain->appendTokenUsageLedger(usage));

    const std::string today = utcToday();
    const auto days = context->runtime->dailyCostReport(owner_a, today, today);
    ASSERT_EQ(days.size(), 1u);
    EXPECT_EQ(days[0].prompt_tokens, 100);
    EXPECT_EQ(days[0].completion_tokens, 50);
    EXPECT_EQ(days[0].request_count, 1);
    EXPECT_TRUE(days[0].estimated);
    EXPECT_NEAR(std::stod(days[0].cost_usd), 0.001234, 1e-9);

    // Another owner sees nothing.
    EXPECT_TRUE(context->runtime->dailyCostReport(owner_b, today, today).empty());
}

// ============================================================================
// 3. End-to-end gRPC (real RpcServer + PostgreSQL + Redis, mock A2A agent)
// ============================================================================

class MockA2AHttpServer {
public:
    bool start() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            return false;
        }
        int reuse = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            return false;
        }
        socklen_t length = sizeof(address);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        if (::listen(listen_fd_, 8) != 0) {
            return false;
        }
        running_.store(true);
        thread_ = std::thread([this] { acceptLoop(); });
        return true;
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
    }

    int port() const {
        return port_;
    }

private:
    void acceptLoop() {
        while (running_.load()) {
            pollfd watched{listen_fd_, POLLIN, 0};
            const int ready = ::poll(&watched, 1, 200);
            if (ready <= 0) {
                continue;
            }
            const int client = ::accept(listen_fd_, nullptr, nullptr);
            if (client < 0) {
                continue;
            }
            handleConnection(client);
            ::close(client);
        }
    }

    static bool sendAll(int fd, const std::string& payload) {
        std::size_t sent = 0;
        while (sent < payload.size()) {
            const ssize_t written =
                ::send(fd, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
            if (written <= 0) {
                return false;
            }
            sent += static_cast<std::size_t>(written);
        }
        return true;
    }

    void handleConnection(int client) {
        std::string buffer;
        char chunk[4096];
        std::size_t header_end = std::string::npos;
        while (header_end == std::string::npos && buffer.size() < (1u << 20)) {
            const ssize_t received = ::recv(client, chunk, sizeof(chunk), 0);
            if (received <= 0) {
                return;
            }
            buffer.append(chunk, static_cast<std::size_t>(received));
            header_end = buffer.find("\r\n\r\n");
        }
        if (header_end == std::string::npos) {
            return;
        }
        const std::string body = buffer.substr(header_end + 4);
        if (body.find("message/stream") != std::string::npos) {
            sendAll(client,
                    "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                    "Cache-Control: no-cache\r\nConnection: close\r\n\r\n"
                    "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"type\":\"status\","
                    "\"status\":{\"state\":\"completed\",\"message\":{\"role\":\"agent\","
                    "\"parts\":[{\"type\":\"text\",\"text\":\"mock\"}]}}}}\n\n");
        } else {
            const std::string payload =
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"type\":\"message\","
                "\"message\":{\"message_id\":\"mock-1\",\"role\":\"agent\","
                "\"parts\":[{\"type\":\"text\",\"text\":\"mock-answer\"}]}}}";
            sendAll(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                            "Content-Length: " + std::to_string(payload.size()) +
                                "\r\nConnection: close\r\n\r\n" + payload);
        }
    }

    int listen_fd_ = -1;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

class AgentRuntimeE2ETest : public ::testing::Test {
protected:
    struct Handles {
        std::unique_ptr<common_ns::PostgresStore> store;
        std::unique_ptr<common_ns::QueryDomainRepository> domain;
        std::unique_ptr<common_ns::AgentRuntimeRepository> runtime;
    };

    struct User {
        std::string id;
        std::string token;
        std::string role;
    };

    static int nextPort() {
        static std::atomic<int> port{52460};
        return port.fetch_add(1);
    }

    void SetUp() override {
        ::unsetenv("LLM_API_KEY");
        admin_username_ = "c3-admin-" + uniqueSuffix();
        ::setenv("NEXUSAI_ADMIN_USERNAME", admin_username_.c_str(), 1);
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
            server_.reset();
        }
        mock_.stop();
        ::unsetenv("NEXUSAI_ADMIN_USERNAME");
    }

    bool applyMigrations(common_ns::PostgresStore& store) {
        for (const std::string& name :
             {"V010__local_auth.sql", "V011__durable_domain.sql",
              "V012__postgres_budget.sql", "V013__runtime_facts.sql"}) {
            const std::string migration =
                readFileOrEmpty(rootPath() + "/db/migrations/" + name);
            if (migration.empty()) {
                return false;
            }
            store.executeTransaction([&migration](pqxx::work& transaction) {
                transaction.exec(migration);
            });
        }
        return true;
    }

    void startServer() {
        ASSERT_TRUE(mock_.start()) << "mock A2A server failed to start";
        try {
            auto config = common_ns::PostgresConfig::fromEnvironment();
            config.pool_size = 6;
            handles_ = std::make_unique<Handles>();
            handles_->store = std::make_unique<common_ns::PostgresStore>(std::move(config));
        } catch (const std::exception&) {
            GTEST_SKIP() << "PostgreSQL test DSN is unavailable";
        }
        if (!handles_->store->healthCheck()) {
            GTEST_SKIP() << "PostgreSQL health check failed";
        }
        try {
            ASSERT_TRUE(applyMigrations(*handles_->store));
        } catch (const std::exception& error) {
            GTEST_SKIP() << "migrations failed: " << error.what();
        }
        handles_->domain =
            std::make_unique<common_ns::QueryDomainRepository>(*handles_->store);
        handles_->runtime =
            std::make_unique<common_ns::AgentRuntimeRepository>(*handles_->store);

        server_ = std::make_unique<server_ns::RpcServer>();
        common_ns::RpcConfig rpc_config;
        rpc_config.server_address = "127.0.0.1:" + std::to_string(nextPort());
        rpc_config.enable_service_registry = false;

        agent_rpc::a2a_adapter::A2AConfig a2a_config;
        a2a_config.orchestrator_url = "http://127.0.0.1:" + std::to_string(mock_.port());
        a2a_config.orchestrator_port = mock_.port();
        a2a_config.max_retries = 1;
        a2a_config.retry_delay_ms = 20;
        a2a_config.request_timeout_seconds = 15;
        server_->setA2AConfig(a2a_config);

        if (!server_->initialize(rpc_config)) {
            GTEST_SKIP() << "RpcServer refused to initialize (environment unavailable)";
        }
        ASSERT_TRUE(server_->start());

        channel_ = grpc::CreateChannel(rpc_config.server_address,
                                       grpc::InsecureChannelCredentials());
        const auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
        ASSERT_TRUE(channel_->WaitForConnected(deadline)) << "channel never connected";

        user_stub_ = agent_communication::auth::UserService::NewStub(channel_);
        agent_stub_ = agent_communication::AgentCommunicationService::NewStub(channel_);
        lifecycle_stub_ = agent_communication::AgentLifecycleService::NewStub(channel_);
        observability_stub_ = agent_communication::ObservabilityService::NewStub(channel_);
    }

    User registerUser(const std::string& username) {
        agent_communication::auth::RegisterRequest register_request;
        register_request.set_username(username);
        register_request.set_password("runtime-facts-password");
        register_request.set_display_name(username);
        agent_communication::auth::RegisterResponse register_response;
        grpc::ClientContext register_context;
        const auto register_status =
            user_stub_->Register(&register_context, register_request, &register_response);
        EXPECT_TRUE(register_status.ok()) << register_status.error_message();

        agent_communication::auth::LoginRequest login_request;
        login_request.set_username(username);
        login_request.set_password("runtime-facts-password");
        agent_communication::auth::LoginResponse login_response;
        grpc::ClientContext login_context;
        const auto login_status =
            user_stub_->Login(&login_context, login_request, &login_response);
        EXPECT_TRUE(login_status.ok()) << login_status.error_message();
        EXPECT_FALSE(login_response.token().empty());
        return {login_response.user_id(), login_response.token(), login_response.role()};
    }

    void applyAuth(grpc::ClientContext& context, const User& user) {
        context.AddMetadata("authorization", "Bearer " + user.token);
    }

    // Seeds a completed query-log + trace pair owned by `owner`.
    std::string seedTrace(const std::string& owner) {
        const std::string request_id = "c3-e2e-req-" + uniqueSuffix();
        common_ns::QueryLogRecord log;
        log.id = request_id;
        log.owner_id = owner;
        log.conversation_id = "c3-e2e-conv-" + uniqueSuffix();
        log.request_text = "runtime facts question";
        log.status = "completed";
        EXPECT_TRUE(handles_->domain->createQueryLog(log));

        common_ns::TraceRecord trace;
        trace.id = "trace-" + request_id;
        trace.owner_id = owner;
        trace.query_log_id = request_id;
        trace.trace_payload =
            "{\"status\":\"completed\",\"request_id\":\"" + request_id + "\","
            "\"spans\":[{\"name\":\"route\",\"component\":\"router\","
            "\"duration_ms\":12,\"status\":\"ok\"},"
            "{\"name\":\"agent_call\",\"component\":\"agent_call\","
            "\"duration_ms\":340,\"status\":\"ok\"}]}";
        trace.status = "completed";
        EXPECT_TRUE(handles_->domain->createTrace(trace));
        return request_id;
    }

    std::int64_t countRows(const std::string& table, const std::string& column,
                           const std::string& value) {
        std::int64_t count = 0;
        handles_->store->executeTransaction([&](pqxx::work& transaction) {
            const auto result = transaction.exec_params(
                "SELECT COUNT(*) FROM " + table + " WHERE " + column + " = $1", value);
            if (!result.empty()) {
                count = result[0][0].as<std::int64_t>();
            }
        });
        return count;
    }

    static bool redisAvailable() {
        const char* host = std::getenv("REDIS_HOST") ? std::getenv("REDIS_HOST") : "127.0.0.1";
        const int port = std::getenv("REDIS_PORT") ? std::atoi(std::getenv("REDIS_PORT")) : 6379;
        redisContext* context = redisConnect(host, port);
        if (context == nullptr || context->err) {
            if (context) {
                redisFree(context);
            }
            return false;
        }
        redisFree(context);
        return true;
    }

    static bool flushRedisDb() {
        const char* host = std::getenv("REDIS_HOST") ? std::getenv("REDIS_HOST") : "127.0.0.1";
        const int port = std::getenv("REDIS_PORT") ? std::atoi(std::getenv("REDIS_PORT")) : 6379;
        redisContext* context = redisConnect(host, port);
        if (context == nullptr || context->err) {
            if (context) {
                redisFree(context);
            }
            return false;
        }
        auto* reply = static_cast<redisReply*>(redisCommand(context, "FLUSHDB"));
        const bool ok = reply != nullptr && reply->type == REDIS_REPLY_STATUS;
        freeReplyObject(reply);
        redisFree(context);
        return ok;
    }

    static agent_communication::RegisterAgentRequest makeRegisterRequest(
        const std::string& service_name) {
        agent_communication::RegisterAgentRequest request;
        auto* info = request.mutable_agent_info();
        info->set_service_name(service_name);
        info->set_host("127.0.0.1");
        info->set_port(19000);
        info->set_version("1.0.0");
        info->add_skills("echo");
        request.set_heartbeat_interval(30);
        return request;
    }

    std::string admin_username_;
    MockA2AHttpServer mock_;
    std::unique_ptr<Handles> handles_;
    std::unique_ptr<server_ns::RpcServer> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<agent_communication::auth::UserService::Stub> user_stub_;
    std::unique_ptr<agent_communication::AgentCommunicationService::Stub> agent_stub_;
    std::unique_ptr<agent_communication::AgentLifecycleService::Stub> lifecycle_stub_;
    std::unique_ptr<agent_communication::ObservabilityService::Stub> observability_stub_;
};

// Required item 3 (half): role flows through Register/Login/ValidateToken.
TEST_F(AgentRuntimeE2ETest, AuthResponsesCarryRole) {
    startServer();
    const auto admin = registerUser(admin_username_);
    EXPECT_EQ(admin.role, "ADMIN");
    const auto plain = registerUser("c3-user-" + uniqueSuffix());
    EXPECT_EQ(plain.role, "USER");

    agent_communication::auth::ValidateTokenRequest validate_request;
    validate_request.set_token(admin.token);
    agent_communication::auth::ValidateTokenResponse validate_response;
    grpc::ClientContext validate_context;
    const auto status =
        user_stub_->ValidateToken(&validate_context, validate_request, &validate_response);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_TRUE(validate_response.valid());
    EXPECT_EQ(validate_response.role(), "ADMIN");
}

// Required item 1: non-admin users cannot manage the agent registry.
TEST_F(AgentRuntimeE2ETest, NonAdminRegisterAgentIsDenied) {
    startServer();
    const auto plain = registerUser("c3-plain-" + uniqueSuffix());
    const std::string service_name = "c3-denied-" + uniqueSuffix();

    agent_communication::RegisterAgentResponse response;
    grpc::ClientContext context;
    applyAuth(context, plain);
    const auto status = agent_stub_->RegisterAgent(
        &context, makeRegisterRequest(service_name), &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);

    // No token at all → UNAUTHENTICATED, not PERMISSION_DENIED.
    agent_communication::RegisterAgentResponse anon_response;
    grpc::ClientContext anon_context;
    const auto anon_status = agent_stub_->RegisterAgent(
        &anon_context, makeRegisterRequest("c3-anon-" + uniqueSuffix()), &anon_response);
    EXPECT_EQ(anon_status.error_code(), grpc::StatusCode::UNAUTHENTICATED);

    // Denied registrations never reach the durable registry.
    EXPECT_EQ(countRows("agent_registry", "agent_id",
                        service_name + "-127.0.0.1-19000"), 0);
}

// Required item 3: admin registration persists to PG and survives restart
// (re-registration); Redis only carries liveness.
TEST_F(AgentRuntimeE2ETest, AdminRegistrationPersistsAndSurvivesReRegistration) {
    startServer();
    const auto admin = registerUser(admin_username_);
    const std::string service_name = "c3-echo-" + uniqueSuffix();
    const std::string agent_id = service_name + "-127.0.0.1-19000";

    // 1) register
    {
        agent_communication::RegisterAgentResponse response;
        grpc::ClientContext context;
        applyAuth(context, admin);
        const auto status =
            agent_stub_->RegisterAgent(&context, makeRegisterRequest(service_name), &response);
        ASSERT_TRUE(status.ok()) << status.error_message();
    }
    EXPECT_EQ(countRows("agent_registry", "agent_id", agent_id), 1);
    auto record = handles_->runtime->getAgent(agent_id);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->health_status, "healthy");

    // 2) restart → unregister then register again: still one durable row.
    {
        agent_communication::UnregisterAgentRequest unregister_request;
        unregister_request.set_agent_id(agent_id);
        agent_communication::UnregisterAgentResponse unregister_response;
        grpc::ClientContext context;
        applyAuth(context, admin);
        const auto status =
            agent_stub_->UnregisterAgent(&context, unregister_request, &unregister_response);
        ASSERT_TRUE(status.ok()) << status.error_message();
    }
    record = handles_->runtime->getAgent(agent_id);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->health_status, "offline");

    {
        agent_communication::RegisterAgentResponse response;
        grpc::ClientContext context;
        applyAuth(context, admin);
        const auto status =
            agent_stub_->RegisterAgent(&context, makeRegisterRequest(service_name), &response);
        ASSERT_TRUE(status.ok()) << status.error_message();
    }
    EXPECT_EQ(countRows("agent_registry", "agent_id", agent_id), 1);
    record = handles_->runtime->getAgent(agent_id);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->health_status, "healthy");

    // 3) heartbeat keeps the durable row fresh and requires authentication.
    {
        agent_communication::HeartbeatRequest heartbeat_request;
        heartbeat_request.set_agent_id(agent_id);
        agent_communication::HeartbeatResponse heartbeat_response;
        grpc::ClientContext context;
        applyAuth(context, admin);
        const auto status =
            agent_stub_->Heartbeat(&context, heartbeat_request, &heartbeat_response);
        ASSERT_TRUE(status.ok()) << status.error_message();
    }

    agent_communication::HeartbeatRequest anon_heartbeat;
    anon_heartbeat.set_agent_id(agent_id);
    agent_communication::HeartbeatResponse anon_response;
    grpc::ClientContext anon_context;
    const auto anon_status =
        agent_stub_->Heartbeat(&anon_context, anon_heartbeat, &anon_response);
    EXPECT_EQ(anon_status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
}

// Required item 6 + 2: feedback must reference the caller's own trace and
// only shifts the caller's route quality.
TEST_F(AgentRuntimeE2ETest, SubmitFeedbackValidatesTraceOwnershipAndIsolatesQuality) {
    startServer();
    const auto owner_a = registerUser("c3-fb-a-" + uniqueSuffix());
    const auto owner_b = registerUser("c3-fb-b-" + uniqueSuffix());
    const std::string request_id = seedTrace(owner_a.id);
    const std::string agent_id = "c3-fb-agent";
    const std::string skill = "echo";

    // B cannot rate A's trace.
    {
        agent_communication::SubmitFeedbackRequest request;
        request.set_trace_id(request_id);
        request.set_agent_id(agent_id);
        request.set_skill_name(skill);
        request.set_rating(5);
        agent_communication::SubmitFeedbackResponse response;
        grpc::ClientContext context;
        applyAuth(context, owner_b);
        const auto status = lifecycle_stub_->SubmitFeedback(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
    }
    EXPECT_EQ(countRows("feedback", "owner_id", owner_b.id), 0);

    // B cannot rate a trace that does not exist either.
    {
        agent_communication::SubmitFeedbackRequest request;
        request.set_trace_id("no-such-trace-" + uniqueSuffix());
        request.set_agent_id(agent_id);
        request.set_skill_name(skill);
        request.set_rating(4);
        agent_communication::SubmitFeedbackResponse response;
        grpc::ClientContext context;
        applyAuth(context, owner_b);
        const auto status = lifecycle_stub_->SubmitFeedback(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
    }

    // Out-of-range ratings are rejected before touching storage.
    {
        agent_communication::SubmitFeedbackRequest request;
        request.set_trace_id(request_id);
        request.set_agent_id(agent_id);
        request.set_skill_name(skill);
        request.set_rating(9);
        agent_communication::SubmitFeedbackResponse response;
        grpc::ClientContext context;
        applyAuth(context, owner_a);
        const auto status = lifecycle_stub_->SubmitFeedback(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    }

    // A rates their own trace (bare id spelling) → durable + aggregated.
    {
        agent_communication::SubmitFeedbackRequest request;
        request.set_trace_id(request_id);
        request.set_agent_id(agent_id);
        request.set_skill_name(skill);
        request.set_rating(5);
        request.set_comment("great");
        agent_communication::SubmitFeedbackResponse response;
        grpc::ClientContext context;
        applyAuth(context, owner_a);
        const auto status = lifecycle_stub_->SubmitFeedback(&context, request, &response);
        ASSERT_TRUE(status.ok()) << status.error_message();
    }
    EXPECT_EQ(countRows("feedback", "owner_id", owner_a.id), 1);
    const auto quality =
        handles_->runtime->getRouteQuality(owner_a.id, agent_id, skill);
    ASSERT_TRUE(quality.has_value());
    EXPECT_EQ(quality->sample_count, 1);
    // One 5-star rating: (1 positive + 2) / (1 total + 4) = 0.6
    EXPECT_NEAR(std::stod(quality->routing_weight), 0.6, 1e-6);

    // B's quality for the same agent/skill stays untouched.
    EXPECT_FALSE(handles_->runtime->getRouteQuality(owner_b.id, agent_id, skill).has_value());
    EXPECT_FALSE(
        handles_->runtime->feedbackApprovalRate(owner_b.id, agent_id, skill).has_value());
}

// Required item 2: trace detail and cost reports never cross owners.
TEST_F(AgentRuntimeE2ETest, TraceDetailAndCostReportAreOwnerScoped) {
    startServer();
    const auto owner_a = registerUser("c3-obs-a-" + uniqueSuffix());
    const auto owner_b = registerUser("c3-obs-b-" + uniqueSuffix());
    const std::string request_id = seedTrace(owner_a.id);

    // Cost ledger entry for A only (estimate-only accounting).
    common_ns::TokenUsageLedgerRecord usage;
    usage.id = "c3-obs-entry-" + uniqueSuffix();
    usage.owner_id = owner_a.id;
    usage.query_log_id = request_id;
    usage.model = "test-model";
    usage.prompt_tokens = 200;
    usage.completion_tokens = 80;
    usage.estimated = true;
    usage.cost_usd = "0.004567";
    ASSERT_TRUE(handles_->domain->appendTokenUsageLedger(usage));

    const std::string today = utcToday();

    // B asks for A's trace → NOT_FOUND.
    {
        agent_communication::GetTraceDetailRequest request;
        request.set_trace_id(request_id);
        agent_communication::GetTraceDetailResponse response;
        grpc::ClientContext context;
        applyAuth(context, owner_b);
        const auto status =
            observability_stub_->GetTraceDetail(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
    }

    // A reads their own trace → spans from the PG payload.
    {
        agent_communication::GetTraceDetailRequest request;
        request.set_trace_id(request_id);
        agent_communication::GetTraceDetailResponse response;
        grpc::ClientContext context;
        applyAuth(context, owner_a);
        const auto status =
            observability_stub_->GetTraceDetail(&context, request, &response);
        ASSERT_TRUE(status.ok()) << status.error_message();
        ASSERT_EQ(response.spans_size(), 2);
        EXPECT_EQ(response.spans(0).component(), "router");
        EXPECT_FALSE(response.trace_summary().empty());
    }

    // B spoofs user_id=A in the request body → still only B's (empty) report.
    {
        agent_communication::GetCostReportRequest request;
        request.set_user_id(owner_a.id);  // spoofed, must be ignored
        request.set_start_date(today);
        request.set_end_date(today);
        agent_communication::GetCostReportResponse response;
        grpc::ClientContext context;
        applyAuth(context, owner_b);
        const auto status =
            observability_stub_->GetCostReport(&context, request, &response);
        ASSERT_TRUE(status.ok()) << status.error_message();
        EXPECT_EQ(response.records_size(), 0);
        EXPECT_DOUBLE_EQ(response.total_cost_usd(), 0.0);
    }

    // A reads their own report → estimated flag preserved, never fabricated.
    {
        agent_communication::GetCostReportRequest request;
        request.set_start_date(today);
        request.set_end_date(today);
        agent_communication::GetCostReportResponse response;
        grpc::ClientContext context;
        applyAuth(context, owner_a);
        const auto status =
            observability_stub_->GetCostReport(&context, request, &response);
        ASSERT_TRUE(status.ok()) << status.error_message();
        ASSERT_EQ(response.records_size(), 1);
        EXPECT_EQ(response.records(0).date(), today);
        EXPECT_TRUE(response.records(0).estimated());
        EXPECT_EQ(response.records(0).total_prompt_tokens(), 200);
        EXPECT_NEAR(response.total_cost_usd(), 0.004567, 1e-9);
    }
}

// Required item 4: Redis FLUSHDB must not destroy observability facts.
TEST_F(AgentRuntimeE2ETest, ObservabilitySurvivesRedisFlush) {
    startServer();
    if (!redisAvailable()) {
        GTEST_SKIP() << "Redis is unavailable";
    }
    const auto owner = registerUser("c3-flush-" + uniqueSuffix());
    const std::string request_id = seedTrace(owner.id);

    common_ns::TokenUsageLedgerRecord usage;
    usage.id = "c3-flush-entry-" + uniqueSuffix();
    usage.owner_id = owner.id;
    usage.query_log_id = request_id;
    usage.model = "test-model";
    usage.prompt_tokens = 42;
    usage.completion_tokens = 7;
    usage.estimated = true;
    usage.cost_usd = "0.000042";
    ASSERT_TRUE(handles_->domain->appendTokenUsageLedger(usage));

    // Registry fact too: survives the flush and stays readable from PG.
    const std::string agent_id = "c3-flush-agent-" + uniqueSuffix();
    common_ns::AgentRegistryRecord registry_record;
    registry_record.id = "registry-" + agent_id;
    registry_record.owner_id = "system";
    registry_record.agent_id = agent_id;
    registry_record.display_name = "Flush Agent";
    registry_record.capabilities = "{}";
    registry_record.health_status = "healthy";
    ASSERT_TRUE(handles_->runtime->upsertAgentRegistry(registry_record));

    ASSERT_TRUE(flushRedisDb()) << "FLUSHDB failed";

    // Trace detail still served from PostgreSQL.
    {
        agent_communication::GetTraceDetailRequest request;
        request.set_trace_id(request_id);
        agent_communication::GetTraceDetailResponse response;
        grpc::ClientContext context;
        applyAuth(context, owner);
        const auto status =
            observability_stub_->GetTraceDetail(&context, request, &response);
        ASSERT_TRUE(status.ok()) << status.error_message();
        EXPECT_EQ(response.spans_size(), 2);
    }

    // Cost report still served from PostgreSQL.
    {
        const std::string today = utcToday();
        agent_communication::GetCostReportRequest request;
        request.set_start_date(today);
        request.set_end_date(today);
        agent_communication::GetCostReportResponse response;
        grpc::ClientContext context;
        applyAuth(context, owner);
        const auto status =
            observability_stub_->GetCostReport(&context, request, &response);
        ASSERT_TRUE(status.ok()) << status.error_message();
        EXPECT_GE(response.records_size(), 1);
    }

    // Registry row still readable from PG after the flush.
    const auto stored = handles_->runtime->getAgent(agent_id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->display_name, "Flush Agent");
}

}  // namespace
