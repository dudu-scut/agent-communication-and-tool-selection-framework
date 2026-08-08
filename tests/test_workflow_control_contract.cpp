/**
 * @file test_workflow_control_contract.cpp
 * @brief PR-E: Sandbox / Compare / Intervention / Undo / Autonomy contract.
 *
 * Two layers:
 *   1. Static source guards (no random-UUID fake success, no request-body
 *      user_id in SetAutonomyLevel, sandbox skips the long-term memory
 *      path, frontend views call the real RPCs).
 *   2. End-to-end workflows against a real RpcServer + real PostgreSQL +
 *      real Redis + embedded mock A2A HTTP agent (no repository mocks).
 *
 * Mandatory coverage (TODO 3.5 / PR-E):
 *   - Sandbox: real execution persists sandbox_runs + query/trace/cost;
 *     long-term memory untouched; cross-owner/unauthenticated refused.
 *   - Compare: up to 3 agents; per-agent results persisted independently;
 *     a single agent failure stays visible; >3 agents refused.
 *   - Intervention: owner-only single CAS transition pending -> decision;
 *     MODIFY requires text; duplicate responses conflict; reversible
 *     decisions write undo_actions.
 *   - Undo: CAS undone_at marks the row and the inverse payload runs
 *     exactly once; duplicate undo conflicts; expired/cross-owner refused.
 *   - Autonomy: only 1..4; request-body user_id ignored; the stored level
 *     really decides whether an intervention is created.
 */

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include "agent_rpc/common/postgres_store.h"
#include "agent_rpc/common/query_domain_repository.h"
#include "agent_rpc/common/agent_runtime_repository.h"
#include "agent_rpc/a2a_adapter/a2a_config.h"
#include "agent_rpc/server/rpc_server.h"

#include "agent_lifecycle.grpc.pb.h"
#include "ai_query.grpc.pb.h"
#include "user.grpc.pb.h"
#include "user.pb.h"
#include "user_experience.grpc.pb.h"

#ifndef NEXUSAI_WORKFLOW_CONTROL_ROOT
#error "NEXUSAI_WORKFLOW_CONTROL_ROOT must point at the repository checkout root"
#endif

namespace {

namespace common_ns = agent_rpc::common;
namespace server_ns = agent_rpc::server;

std::string rootPath() {
    return NEXUSAI_WORKFLOW_CONTROL_ROOT;
}

std::string readFileOrEmpty(const std::string& path) {
    std::ifstream source{path, std::ios::binary};
    if (!source.is_open()) {
        return {};
    }
    return {std::istreambuf_iterator<char>{source}, std::istreambuf_iterator<char>{}};
}

std::string uniqueSuffix() {
    static std::atomic<std::int64_t> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(ticks) + "-" + std::to_string(counter.fetch_add(1));
}

// ============================================================================
// 1. Static source guards
// ============================================================================

TEST(WorkflowControlContractTest, NoRandomUuidFakeSuccessInUserExperienceService) {
    const std::string source =
        readFileOrEmpty(rootPath() + "/server/src/user_experience_service.cpp");
    ASSERT_FALSE(source.empty());
    // The old placeholder generated random UUIDs and returned code=0 without
    // persisting anything. That fake-success path must be gone.
    EXPECT_EQ(source.find("uuid_generate"), std::string::npos);
    EXPECT_EQ(source.find("uuid_unparse"), std::string::npos);
    // Real behavior: sandbox executions persist sandbox_runs through the
    // repository and interventions are resolved by an owner-scoped CAS.
    EXPECT_NE(source.find("createSandboxRun"), std::string::npos);
    EXPECT_NE(source.find("resolveIntervention"), std::string::npos);
    EXPECT_NE(source.find("currentUserId()"), std::string::npos);
}

TEST(WorkflowControlContractTest, SetAutonomyLevelIgnoresRequestBodyUserId) {
    const std::string source =
        readFileOrEmpty(rootPath() + "/server/src/agent_lifecycle_service.cpp");
    ASSERT_FALSE(source.empty());
    // Owner must never come from the request body; the old Redis key was
    // built from request->user_id() and must be gone.
    EXPECT_EQ(source.find("request->user_id()"), std::string::npos);
    EXPECT_EQ(source.find("\"autonomy:\""), std::string::npos);
    // Real behavior: autonomy levels live in PostgreSQL (owner-scoped
    // upsert), undo is a single CAS, compare reads persisted runs.
    EXPECT_NE(source.find("upsertAutonomySetting"), std::string::npos);
    EXPECT_NE(source.find("markUndoActionUndone"), std::string::npos);
    EXPECT_NE(source.find("listCompareRunsByOwner"), std::string::npos);
    EXPECT_NE(source.find("currentUserId()"), std::string::npos);
}

TEST(WorkflowControlContractTest, SandboxFlagGuardsLongTermMemoryPath) {
    const std::string source =
        readFileOrEmpty(rootPath() + "/server/src/ai_query_service.cpp");
    ASSERT_FALSE(source.empty());
    // The single Query-path memory write block must be guarded by the
    // sandbox flag so sandbox runs never touch long-term memory.
    EXPECT_NE(source.find("!request->sandbox()"), std::string::npos);
    EXPECT_NE(source.find("updateUserMemoryFromHints"), std::string::npos);
}

TEST(WorkflowControlContractTest, PrEViewsCallRealRpcsWithoutPlaceholders) {
    for (const char* path : {"/frontend/src/views/AgentSandbox.vue",
                             "/frontend/src/views/CompareView.vue"}) {
        const std::string source = readFileOrEmpty(rootPath() + path);
        ASSERT_FALSE(source.empty()) << path;
        EXPECT_EQ(source.find("开发中"), std::string::npos) << path;
        EXPECT_EQ(source.find("敬请期待"), std::string::npos) << path;
        EXPECT_EQ(source.find("Coming Soon"), std::string::npos) << path;
    }
    const std::string sandbox =
        readFileOrEmpty(rootPath() + "/frontend/src/views/AgentSandbox.vue");
    const std::string compare =
        readFileOrEmpty(rootPath() + "/frontend/src/views/CompareView.vue");
    EXPECT_NE(sandbox.find("sandboxQuery"), std::string::npos);
    EXPECT_NE(compare.find("compareAgents"), std::string::npos);
}

// ============================================================================
// 2. Embedded mock A2A agent (same shape as the durable pipeline tests)
// ============================================================================

class MockA2AHttpServer {
public:
    ~MockA2AHttpServer() {
        stop();
    }

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
        if (::listen(listen_fd_, 16) != 0) {
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
        if (listen_fd_ >= 0) {
            ::shutdown(listen_fd_, SHUT_RDWR);
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
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
        const std::string body =
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"type\":\"message\","
            "\"message\":{\"message_id\":\"mock-1\",\"role\":\"agent\","
            "\"parts\":[{\"type\":\"text\",\"text\":\"mock-answer\"}]}}}";
        sendAll(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                        "Content-Length: " + std::to_string(body.size()) +
                            "\r\nConnection: close\r\n\r\n" + body);
    }

    int listen_fd_ = -1;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// ============================================================================
// 3. End-to-end fixture: real RpcServer + PG + Redis + mock A2A
// ============================================================================

class WorkflowControlE2ETest : public ::testing::Test {
protected:
    struct Handles {
        std::unique_ptr<common_ns::PostgresStore> store;
        std::unique_ptr<common_ns::QueryDomainRepository> domain;
        std::unique_ptr<common_ns::AgentRuntimeRepository> runtime;
    };

    struct User {
        std::string id;
        std::string username;
        std::string token;
    };

    static int nextPort() {
        // Per-process base (ephemeral-ish range) avoids bind collisions with
        // TIME_WAIT sockets left behind by earlier test runs.
        static std::atomic<int> port{40000 + (static_cast<int>(::getpid()) % 20000)};
        return port.fetch_add(1);
    }

    void SetUp() override {
        ::unsetenv("LLM_API_KEY");
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
            server_.reset();
        }
        mock_.stop();
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
            config.pool_size = 8;
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
        ASSERT_TRUE(channel_->WaitForConnected(deadline)) << "rpc channel never connected";

        user_stub_ = agent_communication::auth::UserService::NewStub(channel_);
        lifecycle_stub_ = agent_communication::AgentLifecycleService::NewStub(channel_);
        ux_stub_ = agent_communication::UserExperienceService::NewStub(channel_);
    }

    User registerUser(const std::string& label) {
        const std::string username = "wfc-" + label + "-" + uniqueSuffix();
        agent_communication::auth::RegisterRequest register_request;
        register_request.set_username(username);
        register_request.set_password("workflow-control-password");
        register_request.set_display_name(username);
        agent_communication::auth::RegisterResponse register_response;
        grpc::ClientContext register_context;
        const auto register_status =
            user_stub_->Register(&register_context, register_request, &register_response);
        EXPECT_TRUE(register_status.ok()) << register_status.error_message();

        agent_communication::auth::LoginRequest login_request;
        login_request.set_username(username);
        login_request.set_password("workflow-control-password");
        agent_communication::auth::LoginResponse login_response;
        grpc::ClientContext login_context;
        const auto login_status =
            user_stub_->Login(&login_context, login_request, &login_response);
        EXPECT_TRUE(login_status.ok()) << login_status.error_message();
        EXPECT_FALSE(login_response.token().empty());
        return {login_response.user_id(), username, login_response.token()};
    }

    void applyAuth(grpc::ClientContext& context, const User& user) {
        context.AddMetadata("authorization", "Bearer " + user.token);
    }

    // Seeds a durable agent_registry row (platform-scoped, owner "system").
    void seedAgent(const std::string& agent_id, const std::string& health) {
        common_ns::AgentRegistryRecord record;
        record.id = "agentreg-" + agent_id + "-" + uniqueSuffix();
        record.owner_id = "system";
        record.agent_id = agent_id;
        record.display_name = agent_id;
        record.capabilities = "{\"skills\":[\"echo\"]}";
        record.health_status = health;
        ASSERT_TRUE(handles_->runtime->upsertAgentRegistry(record));
        if (health != "healthy") {
            ASSERT_TRUE(handles_->runtime->markAgentStatus(agent_id, health));
        }
    }

    void setAutonomy(const User& user, const std::string& agent_id, int level,
                     grpc::StatusCode expected = grpc::StatusCode::OK) {
        agent_communication::SetAutonomyLevelRequest request;
        request.set_agent_id(agent_id);
        request.set_level(level);
        agent_communication::SetAutonomyLevelResponse response;
        grpc::ClientContext context;
        applyAuth(context, user);
        const auto status = lifecycle_stub_->SetAutonomyLevel(&context, request, &response);
        EXPECT_EQ(status.error_code(), expected) << status.error_message();
    }

    // Runs SandboxQuery and returns the gRPC status; fills response.
    grpc::Status runSandbox(const User& user, const std::string& agent_id,
                            const std::string& query_text,
                            agent_communication::SandboxQueryResponse* response) {
        agent_communication::SandboxQueryRequest request;
        request.set_agent_id(agent_id);
        request.set_query_text(query_text);
        grpc::ClientContext context;
        applyAuth(context, user);
        return ux_stub_->SandboxQuery(&context, request, response);
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

    std::string scalarByColumn(const std::string& table, const std::string& column,
                                const std::string& key_column, const std::string& key_value) {
        std::string value;
        handles_->store->executeTransaction([&](pqxx::work& transaction) {
            const auto result = transaction.exec_params(
                "SELECT " + column + " FROM " + table + " WHERE " + key_column +
                    " = $1 LIMIT 1",
                key_value);
            if (!result.empty() && !result[0][0].is_null()) {
                value = result[0][0].as<std::string>();
            }
        });
        return value;
    }

    // Creates a pending interventions row directly (producer-independent
    // fixture for undo/expiry tests).
    std::string seedIntervention(const std::string& owner_id, const std::string& original) {
        const std::string id = "intv-seed-" + uniqueSuffix();
        handles_->store->executeTransaction([&](pqxx::work& transaction) {
            transaction.exec_params(
                "INSERT INTO interventions (id, owner_id, query_log_id, state, "
                "original_request, edited_request, decision) "
                "VALUES ($1, $2, '', 'pending', $3, '', '')",
                id, owner_id, original);
        });
        return id;
    }

    // Creates an undo_actions row directly (expired-undo test).
    std::string seedUndoAction(const std::string& owner_id, const std::string& payload,
                               bool expired) {
        const std::string id = "undo-seed-" + uniqueSuffix();
        handles_->store->executeTransaction([&](pqxx::work& transaction) {
            transaction.exec_params(
                "INSERT INTO undo_actions (id, owner_id, resource_type, resource_id, "
                "action_payload, version, expires_at) "
                "VALUES ($1, $2, 'intervention', 'intv-x', $3::jsonb, 1, "
                "NOW() + ($4 || ' hours')::interval)",
                id, owner_id, payload, expired ? std::string("-1") : std::string("24"));
        });
        return id;
    }

    MockA2AHttpServer mock_;
    std::unique_ptr<Handles> handles_;
    std::unique_ptr<server_ns::RpcServer> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<agent_communication::auth::UserService::Stub> user_stub_;
    std::unique_ptr<agent_communication::AgentLifecycleService::Stub> lifecycle_stub_;
    std::unique_ptr<agent_communication::UserExperienceService::Stub> ux_stub_;
};

// ============================================================================
// Auth gate
// ============================================================================

TEST_F(WorkflowControlE2ETest, UnauthenticatedWorkflowControlCallsAreRejected) {
    startServer();

    {
        agent_communication::SandboxQueryRequest request;
        request.set_query_text("q");
        request.set_agent_id("a");
        agent_communication::SandboxQueryResponse response;
        grpc::ClientContext context;
        const auto status = ux_stub_->SandboxQuery(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    }
    {
        agent_communication::InterventionResponseRequest request;
        request.set_intervention_id("intv-x");
        request.set_decision("PROCEED");
        agent_communication::InterventionResponseResponse response;
        grpc::ClientContext context;
        const auto status = ux_stub_->InterventionResponse(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    }
    {
        agent_communication::SetAutonomyLevelRequest request;
        request.set_agent_id("a");
        request.set_level(3);
        agent_communication::SetAutonomyLevelResponse response;
        grpc::ClientContext context;
        const auto status = lifecycle_stub_->SetAutonomyLevel(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    }
    {
        agent_communication::UndoActionRequest request;
        request.set_action_id("undo-x");
        agent_communication::UndoActionResponse response;
        grpc::ClientContext context;
        const auto status = lifecycle_stub_->UndoAction(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    }
    {
        agent_communication::CompareAgentsRequest request;
        request.set_question("q");
        request.add_agent_ids("a");
        agent_communication::CompareAgentsResponse response;
        grpc::ClientContext context;
        const auto status = lifecycle_stub_->CompareAgents(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    }
    {
        agent_communication::GetAgentCompareRequest request;
        agent_communication::GetAgentCompareResponse response;
        grpc::ClientContext context;
        const auto status = lifecycle_stub_->GetAgentCompare(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    }
}

// ============================================================================
// Sandbox
// ============================================================================

TEST_F(WorkflowControlE2ETest, SandboxExecutesThroughPipelineAndPersists) {
    startServer();
    const auto user = registerUser("sandbox-exec");
    const std::string agent_id = "sandbox-agent-" + uniqueSuffix();
    setAutonomy(user, agent_id, 4);  // level >= 3 => executes without asking

    agent_communication::SandboxQueryResponse response;
    const auto status = runSandbox(user, agent_id, "sandbox me", &response);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_FALSE(response.intervention_required());
    EXPECT_EQ(response.result(), "mock-answer");
    ASSERT_FALSE(response.request_id().empty());

    // sandbox_runs persisted for the owner, linked to the pipeline query log.
    EXPECT_EQ(countRows("sandbox_runs", "owner_id", user.id), 1);
    const std::string run_status =
        scalarByColumn("sandbox_runs", "status", "query_log_id", response.request_id());
    EXPECT_EQ(run_status, "completed");
    const std::string run_response =
        scalarByColumn("sandbox_runs", "response_text", "query_log_id", response.request_id());
    EXPECT_EQ(run_response, "mock-answer");

    // The durable pipeline persisted query log, trace and cost rows.
    const std::string conversation =
        scalarByColumn("query_logs", "conversation_id", "id", response.request_id());
    EXPECT_NE(conversation.find("sandbox-"), std::string::npos)
        << "sandbox must run in its own conversation";
    EXPECT_EQ(scalarByColumn("query_logs", "status", "id", response.request_id()),
              "completed");
    EXPECT_EQ(countRows("traces", "id", "trace-" + response.request_id()), 1);
    EXPECT_EQ(scalarByColumn("traces", "status", "id", "trace-" + response.request_id()),
              "completed");
    EXPECT_EQ(countRows("token_usage_ledger", "id", "usage-" + response.request_id()), 1);

    // Long-term memory untouched: the sandbox conversation carries no
    // memory summary and the memory write path is guarded (static test).
    const std::string memory_summary =
        scalarByColumn("conversations", "memory_summary", "id", conversation);
    EXPECT_TRUE(memory_summary.empty());
}

TEST_F(WorkflowControlE2ETest, SandboxValidationRejectsEmptyInput) {
    startServer();
    const auto user = registerUser("sandbox-validation");

    agent_communication::SandboxQueryResponse empty_text;
    grpc::ClientContext text_context;
    applyAuth(text_context, user);
    agent_communication::SandboxQueryRequest text_request;
    text_request.set_agent_id("a");
    const auto text_status =
        ux_stub_->SandboxQuery(&text_context, text_request, &empty_text);
    EXPECT_EQ(text_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);

    agent_communication::SandboxQueryResponse empty_agent;
    grpc::ClientContext agent_context;
    applyAuth(agent_context, user);
    agent_communication::SandboxQueryRequest agent_request;
    agent_request.set_query_text("hello");
    const auto agent_status =
        ux_stub_->SandboxQuery(&agent_context, agent_request, &empty_agent);
    EXPECT_EQ(agent_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);

    EXPECT_EQ(countRows("sandbox_runs", "owner_id", user.id), 0);
}

// ============================================================================
// Autonomy + intervention linkage
// ============================================================================

TEST_F(WorkflowControlE2ETest, AutonomyLevelValidationAndSpoofedUserIdIgnored) {
    startServer();
    const auto user = registerUser("autonomy-validation");
    const std::string agent_id = "autonomy-agent-" + uniqueSuffix();

    setAutonomy(user, agent_id, 0, grpc::StatusCode::INVALID_ARGUMENT);
    setAutonomy(user, agent_id, 5, grpc::StatusCode::INVALID_ARGUMENT);
    setAutonomy(user, agent_id, -1, grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_EQ(countRows("autonomy_settings", "owner_id", user.id), 0);

    // Spoofed body user_id must be ignored; the row belongs to the
    // authenticated owner only.
    agent_communication::SetAutonomyLevelRequest spoofed;
    spoofed.set_user_id("victim-" + uniqueSuffix());
    spoofed.set_agent_id(agent_id);
    spoofed.set_level(3);
    agent_communication::SetAutonomyLevelResponse spoofed_response;
    grpc::ClientContext spoofed_context;
    applyAuth(spoofed_context, user);
    const auto spoofed_status =
        lifecycle_stub_->SetAutonomyLevel(&spoofed_context, spoofed, &spoofed_response);
    ASSERT_TRUE(spoofed_status.ok()) << spoofed_status.error_message();

    EXPECT_EQ(countRows("autonomy_settings", "owner_id", user.id), 1);
    EXPECT_EQ(countRows("autonomy_settings", "owner_id", spoofed.user_id()), 0);
    EXPECT_EQ(scalarByColumn("autonomy_settings", "level", "owner_id", user.id), "3");

    // Upsert: changing the level updates the same row, never duplicates it.
    setAutonomy(user, agent_id, 4);
    EXPECT_EQ(countRows("autonomy_settings", "owner_id", user.id), 1);
    EXPECT_EQ(scalarByColumn("autonomy_settings", "level", "owner_id", user.id), "4");
}

TEST_F(WorkflowControlE2ETest, AutonomyLevelDecidesWhetherInterventionIsCreated) {
    startServer();
    const auto user = registerUser("autonomy-gate");

    // level <= 2: a pending intervention is created, nothing executes.
    const std::string gated_agent = "gate-low-" + uniqueSuffix();
    setAutonomy(user, gated_agent, 1);
    agent_communication::SandboxQueryResponse gated;
    const auto gated_status = runSandbox(user, gated_agent, "needs confirmation", &gated);
    ASSERT_TRUE(gated_status.ok()) << gated_status.error_message();
    EXPECT_TRUE(gated.intervention_required());
    ASSERT_FALSE(gated.intervention_id().empty());
    EXPECT_TRUE(gated.request_id().empty());
    EXPECT_TRUE(gated.result().empty());
    EXPECT_EQ(countRows("sandbox_runs", "owner_id", user.id), 0);
    EXPECT_EQ(scalarByColumn("interventions", "state", "id", gated.intervention_id()),
              "pending");
    EXPECT_EQ(scalarByColumn("interventions", "original_request", "id",
                             gated.intervention_id()),
              "needs confirmation");
    EXPECT_EQ(scalarByColumn("interventions", "owner_id", "id", gated.intervention_id()),
              user.id);

    // level >= 3: executes immediately, no intervention created.
    const std::string free_agent = "gate-high-" + uniqueSuffix();
    setAutonomy(user, free_agent, 3);
    agent_communication::SandboxQueryResponse free_run;
    const auto free_status = runSandbox(user, free_agent, "runs freely", &free_run);
    ASSERT_TRUE(free_status.ok()) << free_status.error_message();
    EXPECT_FALSE(free_run.intervention_required());
    EXPECT_FALSE(free_run.request_id().empty());
    EXPECT_EQ(countRows("sandbox_runs", "owner_id", user.id), 1);

    // No autonomy row at all defaults to confirmation (conservative).
    const std::string unknown_agent = "gate-default-" + uniqueSuffix();
    agent_communication::SandboxQueryResponse default_run;
    const auto default_status =
        runSandbox(user, unknown_agent, "default gate", &default_run);
    ASSERT_TRUE(default_status.ok()) << default_status.error_message();
    EXPECT_TRUE(default_run.intervention_required());
    EXPECT_EQ(countRows("sandbox_runs", "owner_id", user.id), 1);
}

// ============================================================================
// Intervention CAS
// ============================================================================

TEST_F(WorkflowControlE2ETest, InterventionProceedExecutesDeferredSandboxRun) {
    startServer();
    const auto user = registerUser("intv-proceed");
    const std::string agent_id = "intv-agent-" + uniqueSuffix();
    setAutonomy(user, agent_id, 2);

    agent_communication::SandboxQueryResponse gated;
    ASSERT_TRUE(runSandbox(user, agent_id, "deferred question", &gated).ok());
    ASSERT_TRUE(gated.intervention_required());

    agent_communication::InterventionResponseRequest request;
    request.set_intervention_id(gated.intervention_id());
    request.set_decision("PROCEED");
    agent_communication::InterventionResponseResponse response;
    grpc::ClientContext context;
    applyAuth(context, user);
    const auto status = ux_stub_->InterventionResponse(&context, request, &response);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(response.new_state(), "PROCEED");
    ASSERT_FALSE(response.executed_request_id().empty());

    // The deferred execution ran through the pipeline and was recorded as a
    // sandbox run.
    EXPECT_EQ(scalarByColumn("interventions", "state", "id", gated.intervention_id()),
              "PROCEED");
    EXPECT_EQ(countRows("sandbox_runs", "owner_id", user.id), 1);
    EXPECT_EQ(scalarByColumn("sandbox_runs", "status", "query_log_id",
                             response.executed_request_id()),
              "completed");
    EXPECT_EQ(scalarByColumn("query_logs", "request_text", "id",
                             response.executed_request_id()),
              "deferred question");

    // Duplicate response on an already-resolved record is a real conflict.
    agent_communication::InterventionResponseResponse repeat_response;
    grpc::ClientContext repeat_context;
    applyAuth(repeat_context, user);
    const auto repeat_status =
        ux_stub_->InterventionResponse(&repeat_context, request, &repeat_response);
    EXPECT_EQ(repeat_status.error_code(), grpc::StatusCode::ALREADY_EXISTS);
}

TEST_F(WorkflowControlE2ETest, InterventionModifyRequiresTextAndWritesUndoAction) {
    startServer();
    const auto user = registerUser("intv-modify");
    const std::string agent_id = "intv-modify-agent-" + uniqueSuffix();
    setAutonomy(user, agent_id, 1);

    agent_communication::SandboxQueryResponse gated;
    ASSERT_TRUE(runSandbox(user, agent_id, "original question", &gated).ok());
    ASSERT_TRUE(gated.intervention_required());

    // MODIFY without text is rejected and leaves the record pending.
    {
        agent_communication::InterventionResponseRequest empty_modify;
        empty_modify.set_intervention_id(gated.intervention_id());
        empty_modify.set_decision("MODIFY");
        agent_communication::InterventionResponseResponse empty_response;
        grpc::ClientContext empty_context;
        applyAuth(empty_context, user);
        const auto empty_status =
            ux_stub_->InterventionResponse(&empty_context, empty_modify, &empty_response);
        EXPECT_EQ(empty_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
        EXPECT_EQ(scalarByColumn("interventions", "state", "id", gated.intervention_id()),
                  "pending");
    }

    // MODIFY with text executes the edited request and records a reversible
    // undo action.
    agent_communication::InterventionResponseRequest modify;
    modify.set_intervention_id(gated.intervention_id());
    modify.set_decision("MODIFY");
    modify.set_modification_text("edited question");
    agent_communication::InterventionResponseResponse response;
    grpc::ClientContext context;
    applyAuth(context, user);
    const auto status = ux_stub_->InterventionResponse(&context, modify, &response);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(response.new_state(), "MODIFY");
    ASSERT_FALSE(response.executed_request_id().empty());
    ASSERT_FALSE(response.undo_action_id().empty());

    EXPECT_EQ(scalarByColumn("query_logs", "request_text", "id",
                             response.executed_request_id()),
              "edited question");
    EXPECT_EQ(scalarByColumn("interventions", "edited_request", "id",
                             gated.intervention_id()),
              "edited question");
    EXPECT_EQ(countRows("undo_actions", "id", response.undo_action_id()), 1);
    EXPECT_EQ(scalarByColumn("undo_actions", "owner_id", "id", response.undo_action_id()),
              user.id);
    EXPECT_TRUE(scalarByColumn("undo_actions", "undone_at", "id",
                               response.undo_action_id()).empty());
}

TEST_F(WorkflowControlE2ETest, InterventionAbortAndSkipDoNotExecute) {
    startServer();
    const auto user = registerUser("intv-abort");
    const std::string agent_id = "intv-abort-agent-" + uniqueSuffix();
    setAutonomy(user, agent_id, 1);

    agent_communication::SandboxQueryResponse gated;
    ASSERT_TRUE(runSandbox(user, agent_id, "abort me", &gated).ok());

    agent_communication::InterventionResponseRequest abort;
    abort.set_intervention_id(gated.intervention_id());
    abort.set_decision("ABORT");
    agent_communication::InterventionResponseResponse response;
    grpc::ClientContext context;
    applyAuth(context, user);
    const auto status = ux_stub_->InterventionResponse(&context, abort, &response);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(response.new_state(), "ABORT");
    EXPECT_TRUE(response.executed_request_id().empty());
    EXPECT_EQ(countRows("sandbox_runs", "owner_id", user.id), 0);
    EXPECT_EQ(scalarByColumn("interventions", "state", "id", gated.intervention_id()),
              "ABORT");
    // ABORT is terminal: resolving again is a conflict.
    agent_communication::InterventionResponseResponse again_response;
    grpc::ClientContext again_context;
    applyAuth(again_context, user);
    const auto again_status =
        ux_stub_->InterventionResponse(&again_context, abort, &again_response);
    EXPECT_EQ(again_status.error_code(), grpc::StatusCode::ALREADY_EXISTS);
}

TEST_F(WorkflowControlE2ETest, InterventionRejectsForeignUnknownAndInvalidInput) {
    startServer();
    const auto user_a = registerUser("intv-owner");
    const auto user_b = registerUser("intv-intruder");
    const std::string agent_id = "intv-secure-agent-" + uniqueSuffix();
    setAutonomy(user_a, agent_id, 1);

    agent_communication::SandboxQueryResponse gated;
    ASSERT_TRUE(runSandbox(user_a, agent_id, "protected question", &gated).ok());

    // Cross-owner: looks like NOT_FOUND (no existence leak).
    {
        agent_communication::InterventionResponseRequest foreign;
        foreign.set_intervention_id(gated.intervention_id());
        foreign.set_decision("PROCEED");
        agent_communication::InterventionResponseResponse foreign_response;
        grpc::ClientContext foreign_context;
        applyAuth(foreign_context, user_b);
        const auto foreign_status =
            ux_stub_->InterventionResponse(&foreign_context, foreign, &foreign_response);
        EXPECT_EQ(foreign_status.error_code(), grpc::StatusCode::NOT_FOUND);
    }
    // Unknown id for the true owner is also NOT_FOUND.
    {
        agent_communication::InterventionResponseRequest unknown;
        unknown.set_intervention_id("intv-does-not-exist-" + uniqueSuffix());
        unknown.set_decision("PROCEED");
        agent_communication::InterventionResponseResponse unknown_response;
        grpc::ClientContext unknown_context;
        applyAuth(unknown_context, user_a);
        const auto unknown_status =
            ux_stub_->InterventionResponse(&unknown_context, unknown, &unknown_response);
        EXPECT_EQ(unknown_status.error_code(), grpc::StatusCode::NOT_FOUND);
    }
    // Invalid decision and missing id are INVALID_ARGUMENT.
    {
        agent_communication::InterventionResponseRequest invalid;
        invalid.set_intervention_id(gated.intervention_id());
        invalid.set_decision("BANANA");
        agent_communication::InterventionResponseResponse invalid_response;
        grpc::ClientContext invalid_context;
        applyAuth(invalid_context, user_a);
        const auto invalid_status =
            ux_stub_->InterventionResponse(&invalid_context, invalid, &invalid_response);
        EXPECT_EQ(invalid_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);

        agent_communication::InterventionResponseRequest missing_id;
        missing_id.set_decision("PROCEED");
        agent_communication::InterventionResponseResponse missing_response;
        grpc::ClientContext missing_context;
        applyAuth(missing_context, user_a);
        const auto missing_status =
            ux_stub_->InterventionResponse(&missing_context, missing_id, &missing_response);
        EXPECT_EQ(missing_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    }
    // The record remains untouched for the owner.
    EXPECT_EQ(scalarByColumn("interventions", "state", "id", gated.intervention_id()),
              "pending");
}

// ============================================================================
// Undo
// ============================================================================

TEST_F(WorkflowControlE2ETest, UndoRestoresInterventionExactlyOnce) {
    startServer();
    const auto user = registerUser("undo-once");
    const auto intruder = registerUser("undo-intruder");
    const std::string agent_id = "undo-agent-" + uniqueSuffix();
    setAutonomy(user, agent_id, 1);

    agent_communication::SandboxQueryResponse gated;
    ASSERT_TRUE(runSandbox(user, agent_id, "undoable question", &gated).ok());

    agent_communication::InterventionResponseRequest skip;
    skip.set_intervention_id(gated.intervention_id());
    skip.set_decision("SKIP");
    agent_communication::InterventionResponseResponse skip_response;
    grpc::ClientContext skip_context;
    applyAuth(skip_context, user);
    ASSERT_TRUE(ux_stub_
                    ->InterventionResponse(&skip_context, skip, &skip_response)
                    .ok());
    ASSERT_EQ(skip_response.new_state(), "SKIP");
    ASSERT_FALSE(skip_response.undo_action_id().empty());
    EXPECT_EQ(countRows("sandbox_runs", "owner_id", user.id), 0);

    // Cross-owner undo is NOT_FOUND.
    {
        agent_communication::UndoActionRequest foreign;
        foreign.set_action_id(skip_response.undo_action_id());
        agent_communication::UndoActionResponse foreign_response;
        grpc::ClientContext foreign_context;
        applyAuth(foreign_context, intruder);
        const auto foreign_status =
            lifecycle_stub_->UndoAction(&foreign_context, foreign, &foreign_response);
        EXPECT_EQ(foreign_status.error_code(), grpc::StatusCode::NOT_FOUND);
    }

    // Owner undo: CAS marks undone_at and the inverse payload (restore the
    // intervention to pending) executes exactly once.
    {
        agent_communication::UndoActionRequest undo;
        undo.set_action_id(skip_response.undo_action_id());
        agent_communication::UndoActionResponse undo_response;
        grpc::ClientContext undo_context;
        applyAuth(undo_context, user);
        const auto undo_status =
            lifecycle_stub_->UndoAction(&undo_context, undo, &undo_response);
        ASSERT_TRUE(undo_status.ok()) << undo_status.error_message();
        EXPECT_TRUE(undo_response.success());
    }
    EXPECT_FALSE(scalarByColumn("undo_actions", "undone_at", "id",
                                skip_response.undo_action_id()).empty());
    EXPECT_EQ(scalarByColumn("interventions", "state", "id", gated.intervention_id()),
              "pending");

    // Duplicate undo is a real conflict; the inverse never runs twice.
    {
        agent_communication::UndoActionRequest repeat;
        repeat.set_action_id(skip_response.undo_action_id());
        agent_communication::UndoActionResponse repeat_response;
        grpc::ClientContext repeat_context;
        applyAuth(repeat_context, user);
        const auto repeat_status =
            lifecycle_stub_->UndoAction(&repeat_context, repeat, &repeat_response);
        EXPECT_EQ(repeat_status.error_code(), grpc::StatusCode::ALREADY_EXISTS);
    }
    // Unknown action id is NOT_FOUND; missing id is INVALID_ARGUMENT.
    {
        agent_communication::UndoActionRequest unknown;
        unknown.set_action_id("undo-does-not-exist-" + uniqueSuffix());
        agent_communication::UndoActionResponse unknown_response;
        grpc::ClientContext unknown_context;
        applyAuth(unknown_context, user);
        const auto unknown_status =
            lifecycle_stub_->UndoAction(&unknown_context, unknown, &unknown_response);
        EXPECT_EQ(unknown_status.error_code(), grpc::StatusCode::NOT_FOUND);

        agent_communication::UndoActionRequest missing;
        agent_communication::UndoActionResponse missing_response;
        grpc::ClientContext missing_context;
        applyAuth(missing_context, user);
        const auto missing_status =
            lifecycle_stub_->UndoAction(&missing_context, missing, &missing_response);
        EXPECT_EQ(missing_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    }
}

TEST_F(WorkflowControlE2ETest, ExpiredUndoActionIsRefused) {
    startServer();
    const auto user = registerUser("undo-expired");
    const std::string payload =
        "{\"operation\":\"restore_intervention\",\"intervention_id\":\"intv-x\"}";
    const std::string expired_id = seedUndoAction(user.id, payload, /*expired=*/true);

    agent_communication::UndoActionRequest request;
    request.set_action_id(expired_id);
    agent_communication::UndoActionResponse response;
    grpc::ClientContext context;
    applyAuth(context, user);
    const auto status = lifecycle_stub_->UndoAction(&context, request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::FAILED_PRECONDITION);
    EXPECT_TRUE(scalarByColumn("undo_actions", "undone_at", "id", expired_id).empty());
}

// ============================================================================
// Compare
// ============================================================================

TEST_F(WorkflowControlE2ETest, ComparePersistsIndependentPerAgentResults) {
    startServer();
    const auto user = registerUser("compare-run");
    const std::string suffix = uniqueSuffix();
    const std::string healthy_one = "cmp-a-" + suffix;
    const std::string healthy_two = "cmp-b-" + suffix;
    const std::string offline = "cmp-c-" + suffix;
    seedAgent(healthy_one, "healthy");
    seedAgent(healthy_two, "healthy");
    seedAgent(offline, "offline");

    agent_communication::CompareAgentsRequest request;
    request.set_question("compare this");
    request.add_agent_ids(healthy_one);
    request.add_agent_ids(healthy_two);
    request.add_agent_ids(offline);
    agent_communication::CompareAgentsResponse response;
    grpc::ClientContext context;
    applyAuth(context, user);
    const auto status = lifecycle_stub_->CompareAgents(&context, request, &response);
    ASSERT_TRUE(status.ok()) << status.error_message();
    ASSERT_FALSE(response.run_id().empty());
    // One agent failed but the run is NOT masked as an overall success.
    EXPECT_EQ(response.run_status(), "partial");
    ASSERT_EQ(response.results_size(), 3);

    int completed = 0;
    int failed = 0;
    for (const auto& result : response.results()) {
        if (result.agent_id() == offline) {
            EXPECT_EQ(result.status(), "failed");
            EXPECT_FALSE(result.error().empty());
            EXPECT_TRUE(result.answer().empty());
            ++failed;
        } else {
            EXPECT_EQ(result.status(), "completed") << result.agent_id();
            EXPECT_EQ(result.answer(), "mock-answer");
            ASSERT_FALSE(result.request_id().empty());
            EXPECT_EQ(countRows("query_logs", "id", result.request_id()), 1);
            EXPECT_EQ(scalarByColumn("query_logs", "status", "id", result.request_id()),
                      "completed");
            ++completed;
        }
    }
    EXPECT_EQ(completed, 2);
    EXPECT_EQ(failed, 1);

    // The durable compare_runs row stores the whole picture for the owner.
    EXPECT_EQ(countRows("compare_runs", "owner_id", user.id), 1);
    EXPECT_EQ(scalarByColumn("compare_runs", "status", "id", response.run_id()),
              "partial");
    const std::string results_json =
        scalarByColumn("compare_runs", "results::text", "id", response.run_id());
    const auto parsed = nlohmann::json::parse(results_json, nullptr, false);
    ASSERT_TRUE(parsed.is_array());
    EXPECT_EQ(parsed.size(), 3u);
}

TEST_F(WorkflowControlE2ETest, CompareRejectsTooManyAgentsAndBadInput) {
    startServer();
    const auto user = registerUser("compare-validation");

    agent_communication::CompareAgentsRequest too_many;
    too_many.set_question("q");
    for (int i = 0; i < 4; ++i) {
        too_many.add_agent_ids("agent-" + std::to_string(i));
    }
    agent_communication::CompareAgentsResponse too_many_response;
    grpc::ClientContext too_many_context;
    applyAuth(too_many_context, user);
    const auto too_many_status =
        lifecycle_stub_->CompareAgents(&too_many_context, too_many, &too_many_response);
    EXPECT_EQ(too_many_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);

    agent_communication::CompareAgentsRequest none;
    none.set_question("q");
    agent_communication::CompareAgentsResponse none_response;
    grpc::ClientContext none_context;
    applyAuth(none_context, user);
    const auto none_status =
        lifecycle_stub_->CompareAgents(&none_context, none, &none_response);
    EXPECT_EQ(none_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);

    agent_communication::CompareAgentsRequest empty_question;
    empty_question.add_agent_ids("agent-x");
    agent_communication::CompareAgentsResponse empty_question_response;
    grpc::ClientContext empty_question_context;
    applyAuth(empty_question_context, user);
    const auto empty_question_status = lifecycle_stub_->CompareAgents(
        &empty_question_context, empty_question, &empty_question_response);
    EXPECT_EQ(empty_question_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);

    agent_communication::CompareAgentsRequest duplicates;
    duplicates.set_question("q");
    duplicates.add_agent_ids("agent-dup");
    duplicates.add_agent_ids("agent-dup");
    agent_communication::CompareAgentsResponse duplicates_response;
    grpc::ClientContext duplicates_context;
    applyAuth(duplicates_context, user);
    const auto duplicates_status =
        lifecycle_stub_->CompareAgents(&duplicates_context, duplicates, &duplicates_response);
    EXPECT_EQ(duplicates_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);

    EXPECT_EQ(countRows("compare_runs", "owner_id", user.id), 0);
}

TEST_F(WorkflowControlE2ETest, CompareUnknownAgentFailsIndependently) {
    startServer();
    const auto user = registerUser("compare-unknown");
    const std::string healthy = "cmp-known-" + uniqueSuffix();
    seedAgent(healthy, "healthy");

    agent_communication::CompareAgentsRequest request;
    request.set_question("known vs unknown");
    request.add_agent_ids(healthy);
    request.add_agent_ids("cmp-ghost-" + uniqueSuffix());
    agent_communication::CompareAgentsResponse response;
    grpc::ClientContext context;
    applyAuth(context, user);
    const auto status = lifecycle_stub_->CompareAgents(&context, request, &response);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(response.run_status(), "partial");
    ASSERT_EQ(response.results_size(), 2);
    for (const auto& result : response.results()) {
        if (result.agent_id() == healthy) {
            EXPECT_EQ(result.status(), "completed");
        } else {
            EXPECT_EQ(result.status(), "failed");
            EXPECT_FALSE(result.error().empty());
        }
    }
}

TEST_F(WorkflowControlE2ETest, GetAgentCompareReturnsPersistedRunsOwnerScoped) {
    startServer();
    const auto user_a = registerUser("compare-list-a");
    const auto user_b = registerUser("compare-list-b");
    const std::string healthy = "cmp-list-" + uniqueSuffix();
    seedAgent(healthy, "healthy");

    // Empty state is real: no runs yet.
    {
        agent_communication::GetAgentCompareRequest request;
        agent_communication::GetAgentCompareResponse response;
        grpc::ClientContext context;
        applyAuth(context, user_a);
        const auto status = lifecycle_stub_->GetAgentCompare(&context, request, &response);
        ASSERT_TRUE(status.ok()) << status.error_message();
        EXPECT_EQ(response.runs_size(), 0);
    }

    agent_communication::CompareAgentsRequest run_request;
    run_request.set_question("list me");
    run_request.add_agent_ids(healthy);
    agent_communication::CompareAgentsResponse run_response;
    grpc::ClientContext run_context;
    applyAuth(run_context, user_a);
    ASSERT_TRUE(lifecycle_stub_->CompareAgents(&run_context, run_request, &run_response).ok());

    {
        agent_communication::GetAgentCompareRequest request;
        agent_communication::GetAgentCompareResponse response;
        grpc::ClientContext context;
        applyAuth(context, user_a);
        const auto status = lifecycle_stub_->GetAgentCompare(&context, request, &response);
        ASSERT_TRUE(status.ok()) << status.error_message();
        ASSERT_EQ(response.runs_size(), 1);
        EXPECT_EQ(response.runs(0).run_id(), run_response.run_id());
        EXPECT_EQ(response.runs(0).request_text(), "list me");
        EXPECT_EQ(response.runs(0).status(), "completed");
    }
    // Another owner never sees foreign runs.
    {
        agent_communication::GetAgentCompareRequest request;
        agent_communication::GetAgentCompareResponse response;
        grpc::ClientContext context;
        applyAuth(context, user_b);
        ASSERT_TRUE(lifecycle_stub_->GetAgentCompare(&context, request, &response).ok());
        EXPECT_EQ(response.runs_size(), 0);
    }
}

}  // namespace
