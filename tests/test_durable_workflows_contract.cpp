/**
 * @file test_durable_workflows_contract.cpp
 * @brief PR-D: Replay / Export / Share / Template — contract + integration.
 *
 * Two layers:
 *   1. Static source guards (no placeholder strings, no hardcoded share
 *      host, no "开发中" fake-success placeholders in the PR-D views).
 *   2. End-to-end workflows against a real RpcServer + real PostgreSQL +
 *      real Redis + embedded mock A2A HTTP agent (no repository mocks).
 *
 * Mandatory coverage (TODO 3.4 / PR-D):
 *   - Replay: cross-owner NOT_FOUND; route mode returns old-vs-new route
 *     without executing; exact mode persists a NEW trace leaving the
 *     original untouched; invalid mode rejected.
 *   - Export: cross-owner NOT_FOUND; HTML output escapes hostile payloads.
 *   - Share: raw token returned once / PG stores hash only; public
 *     ReadSharedConversation is read-only and sanitized; expired/revoked
 *     shares refused; revoke reflected in owner list.
 *   - Template: invalid JSON refused; UseTemplate creates a real
 *     conversation + initial message under the CURRENT owner; cross-owner
 *     template use refused.
 */

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/sha.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "agent_rpc/common/postgres_store.h"
#include "agent_rpc/common/query_domain_repository.h"
#include "agent_rpc/a2a_adapter/a2a_config.h"
#include "agent_rpc/server/rpc_server.h"

#include "ai_query.grpc.pb.h"
#include "ai_query.pb.h"
#include "orchestration.grpc.pb.h"
#include "sharing.grpc.pb.h"
#include "user.grpc.pb.h"
#include "user.pb.h"

#ifndef NEXUSAI_WORKFLOWS_ROOT
#error "NEXUSAI_WORKFLOWS_ROOT must point at the repository checkout root"
#endif

namespace {

namespace common_ns = agent_rpc::common;
namespace server_ns = agent_rpc::server;

std::string rootPath() {
    return NEXUSAI_WORKFLOWS_ROOT;
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

std::string sha256Hex(const std::string& input) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    std::ostringstream hex;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        hex << std::setw(2) << std::setfill('0') << std::hex
            << static_cast<int>(digest[i]);
    }
    return hex.str();
}

// ============================================================================
// 1. Static source guards
// ============================================================================

TEST(DurableWorkflowsContractTest, NoPlaceholderStringsInWorkflowServices) {
    const std::string replay = readFileOrEmpty(rootPath() + "/orchestrator/src/replay_service.cpp");
    const std::string export_svc = readFileOrEmpty(rootPath() + "/orchestrator/src/export_service.cpp");
    const std::string sharing = readFileOrEmpty(rootPath() + "/server/src/sharing_service.cpp");
    ASSERT_FALSE(replay.empty());
    ASSERT_FALSE(export_svc.empty());
    ASSERT_FALSE(sharing.empty());

    for (const auto* source : {&replay, &export_svc, &sharing}) {
        EXPECT_EQ(source->find("[placeholder]"), std::string::npos);
        EXPECT_EQ(source->find("not yet implemented"), std::string::npos);
        EXPECT_EQ(source->find("暂无回复"), std::string::npos);
        EXPECT_EQ(source->find("未来版本"), std::string::npos);
    }

    // Replay must read the trace from PostgreSQL scoped by the authenticated
    // owner and only accept exact|route modes.
    EXPECT_NE(replay.find("getTraceById("), std::string::npos);
    EXPECT_NE(replay.find("NOT_FOUND"), std::string::npos);
    // Export must read messages from PostgreSQL and keep HTML escaping.
    EXPECT_NE(export_svc.find("listMessages("), std::string::npos);
    EXPECT_NE(export_svc.find("&lt;"), std::string::npos);
    // Sharing stores only the token hash and never a hardcoded host.
    EXPECT_NE(sharing.find("token_hash"), std::string::npos);
    EXPECT_EQ(sharing.find("nexusai.local"), std::string::npos);
}

TEST(DurableWorkflowsContractTest, ReplayAndExportStatusMessagesNeverEchoExceptionText) {
    // M1 (PR-D deferred): gRPC status messages returned to callers are fixed,
    // sanitized texts. Raw exception detail may appear in the server log
    // (exactly one error.what() per file) but never in the returned status,
    // which would leak internal information to other tenants.
    for (const char* path : {"/orchestrator/src/replay_service.cpp",
                             "/orchestrator/src/export_service.cpp"}) {
        const std::string source = readFileOrEmpty(rootPath() + path);
        ASSERT_FALSE(source.empty()) << path;
        std::size_t occurrences = 0;
        std::size_t position = source.find("error.what()");
        while (position != std::string::npos) {
            ++occurrences;
            position = source.find("error.what()", position + 1);
        }
        EXPECT_EQ(occurrences, 1u) << path << ": error.what() must stay in LOG_ERROR only";
    }
}

TEST(DurableWorkflowsContractTest, NoHardcodedShareHostAnywhere) {
    for (const char* path : {"/server/src/sharing_service.cpp",
                             "/frontend/src/views/ShareView.vue",
                             "/frontend/src/views/TemplateMarket.vue",
                             "/frontend/src/services/grpc-client.ts",
                             "/gateway/proxy/server.mjs"}) {
        const std::string source = readFileOrEmpty(rootPath() + path);
        ASSERT_FALSE(source.empty()) << path;
        EXPECT_EQ(source.find("nexusai.local"), std::string::npos) << path;
    }
}

TEST(DurableWorkflowsContractTest, PrDViewsHaveNoFakeDevelopmentPlaceholders) {
    for (const char* path : {"/frontend/src/views/ShareView.vue",
                             "/frontend/src/views/TemplateMarket.vue"}) {
        const std::string source = readFileOrEmpty(rootPath() + path);
        ASSERT_FALSE(source.empty()) << path;
        EXPECT_EQ(source.find("开发中"), std::string::npos) << path;
        EXPECT_EQ(source.find("敬请期待"), std::string::npos) << path;
    }
    // The AdminView replay tab must call the real ReplayQuery RPC.
    const std::string admin = readFileOrEmpty(rootPath() + "/frontend/src/views/AdminView.vue");
    ASSERT_FALSE(admin.empty());
    EXPECT_NE(admin.find("replayQuery"), std::string::npos);
    EXPECT_EQ(admin.find("Replay功能开发中"), std::string::npos);
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

class DurableWorkflowsE2ETest : public ::testing::Test {
protected:
    struct Handles {
        std::unique_ptr<common_ns::PostgresStore> store;
        std::unique_ptr<common_ns::QueryDomainRepository> domain;
    };

    struct User {
        std::string id;
        std::string username;
        std::string token;
    };

    static int nextPort() {
        static std::atomic<int> port{53160};
        // PR-F Minor #3 analogue: a deterministic counter can collide with a
        // socket still in TIME_WAIT (or another listener) from a previous
        // run, so probe each candidate with a real bind and skip busy ones.
        for (;;) {
            const int candidate = port.fetch_add(1);
            const int probe = ::socket(AF_INET, SOCK_STREAM, 0);
            if (probe < 0) {
                return candidate;
            }
            int reuse = 1;
            ::setsockopt(probe, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(static_cast<uint16_t>(candidate));
            const bool is_free =
                ::bind(probe, reinterpret_cast<sockaddr*>(&address),
                       sizeof(address)) == 0;
            ::close(probe);
            if (is_free) {
                return candidate;
            }
        }
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
             {"V010__local_auth.sql", "V011__durable_domain.sql", "V012__postgres_budget.sql"}) {
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

    void startWorkflow() {
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
        query_stub_ = agent_communication::AIQueryService::NewStub(channel_);
        orchestration_stub_ = agent_communication::OrchestrationService::NewStub(channel_);
        sharing_stub_ = agent_communication::SharingService::NewStub(channel_);
    }

    User registerUser(const std::string& label) {
        const std::string username = "dwf-" + label + "-" + uniqueSuffix();
        agent_communication::auth::RegisterRequest register_request;
        register_request.set_username(username);
        register_request.set_password("durable-workflows-password");
        register_request.set_display_name(username);
        agent_communication::auth::RegisterResponse register_response;
        grpc::ClientContext register_context;
        const auto register_status =
            user_stub_->Register(&register_context, register_request, &register_response);
        EXPECT_TRUE(register_status.ok()) << register_status.error_message();

        agent_communication::auth::LoginRequest login_request;
        login_request.set_username(username);
        login_request.set_password("durable-workflows-password");
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

    // Runs a real durable Query for the user and returns the request id
    // (== query log id); the trace row id is "trace-" + request_id.
    std::string runCompletedQuery(const User& user, const std::string& context_id,
                                  const std::string& question) {
        const std::string request_id = "dwf-req-" + uniqueSuffix();
        agent_communication::AIQueryRequest request;
        request.set_request_id(request_id);
        request.set_context_id(context_id);
        request.set_question(question);
        agent_communication::AIQueryResponse response;
        grpc::ClientContext context;
        applyAuth(context, user);
        const auto status = query_stub_->Query(&context, request, &response);
        EXPECT_TRUE(status.ok()) << status.error_message();
        EXPECT_EQ(response.status().code(), 0);
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

    MockA2AHttpServer mock_;
    std::unique_ptr<Handles> handles_;
    std::unique_ptr<server_ns::RpcServer> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<agent_communication::auth::UserService::Stub> user_stub_;
    std::unique_ptr<agent_communication::AIQueryService::Stub> query_stub_;
    std::unique_ptr<agent_communication::OrchestrationService::Stub> orchestration_stub_;
    std::unique_ptr<agent_communication::SharingService::Stub> sharing_stub_;
};

// ============================================================================
// Auth gate
// ============================================================================

TEST_F(DurableWorkflowsE2ETest, UnauthenticatedWorkflowCallsAreRejected) {
    startWorkflow();

    {
        agent_communication::ReplayQueryRequest request;
        request.set_trace_id("trace-anything");
        request.set_mode("exact");
        agent_communication::ReplayQueryResponse response;
        grpc::ClientContext context;
        const auto status = orchestration_stub_->ReplayQuery(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    }
    {
        agent_communication::ExportConversationRequest request;
        request.set_context_id("ctx-anything");
        agent_communication::ExportConversationResponse response;
        grpc::ClientContext context;
        const auto status = orchestration_stub_->ExportConversation(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    }
    {
        agent_communication::ListSharesRequest request;
        agent_communication::ListSharesResponse response;
        grpc::ClientContext context;
        const auto status = sharing_stub_->ListShares(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    }
    {
        agent_communication::SaveTemplateRequest request;
        request.set_name("t");
        agent_communication::SaveTemplateResponse response;
        grpc::ClientContext context;
        const auto status = sharing_stub_->SaveTemplate(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    }
}

// ============================================================================
// Replay
// ============================================================================

TEST_F(DurableWorkflowsE2ETest, ReplayCrossOwnerOrUnknownTraceIsNotFound) {
    startWorkflow();
    const auto user_a = registerUser("replay-a");
    const auto user_b = registerUser("replay-b");
    const std::string context_id = "dwf-ctx-replay-" + uniqueSuffix();
    const std::string request_id = runCompletedQuery(user_a, context_id, "replay me");
    const std::string trace_id = "trace-" + request_id;

    // Cross-owner replay must look like NOT_FOUND (no existence leak).
    agent_communication::ReplayQueryRequest request;
    request.set_trace_id(trace_id);
    request.set_mode("route");
    agent_communication::ReplayQueryResponse response;
    grpc::ClientContext context;
    applyAuth(context, user_b);
    const auto status = orchestration_stub_->ReplayQuery(&context, request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);

    // Unknown trace for the real owner is also NOT_FOUND.
    agent_communication::ReplayQueryRequest unknown_request;
    unknown_request.set_trace_id("trace-dwf-does-not-exist-" + uniqueSuffix());
    unknown_request.set_mode("route");
    agent_communication::ReplayQueryResponse unknown_response;
    grpc::ClientContext unknown_context;
    applyAuth(unknown_context, user_a);
    const auto unknown_status =
        orchestration_stub_->ReplayQuery(&unknown_context, unknown_request, &unknown_response);
    EXPECT_EQ(unknown_status.error_code(), grpc::StatusCode::NOT_FOUND);
}

TEST_F(DurableWorkflowsE2ETest, ReplayInvalidModeIsRejected) {
    startWorkflow();
    const auto user_a = registerUser("replay-mode");
    const std::string context_id = "dwf-ctx-mode-" + uniqueSuffix();
    const std::string request_id = runCompletedQuery(user_a, context_id, "mode check");

    agent_communication::ReplayQueryRequest request;
    request.set_trace_id("trace-" + request_id);
    request.set_mode("banana");
    agent_communication::ReplayQueryResponse response;
    grpc::ClientContext context;
    applyAuth(context, user_a);
    const auto status = orchestration_stub_->ReplayQuery(&context, request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);

    // Empty trace id is also rejected.
    agent_communication::ReplayQueryRequest empty_request;
    empty_request.set_mode("exact");
    agent_communication::ReplayQueryResponse empty_response;
    grpc::ClientContext empty_context;
    applyAuth(empty_context, user_a);
    const auto empty_status =
        orchestration_stub_->ReplayQuery(&empty_context, empty_request, &empty_response);
    EXPECT_EQ(empty_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(DurableWorkflowsE2ETest, ReplayRouteComparesRoutesWithoutExecuting) {
    startWorkflow();
    const auto user_a = registerUser("replay-route");
    const std::string context_id = "dwf-ctx-route-" + uniqueSuffix();
    const std::string request_id = runCompletedQuery(user_a, context_id, "route comparison");
    const std::string trace_id = "trace-" + request_id;

    const auto logs_before = countRows("query_logs", "owner_id", user_a.id);
    const auto traces_before = countRows("traces", "owner_id", user_a.id);

    agent_communication::ReplayQueryRequest request;
    request.set_trace_id(trace_id);
    request.set_mode("route");
    agent_communication::ReplayQueryResponse response;
    grpc::ClientContext context;
    applyAuth(context, user_a);
    const auto status = orchestration_stub_->ReplayQuery(&context, request, &response);
    ASSERT_TRUE(status.ok()) << status.error_message();

    // Original vs new route are both present; no new ids in route mode.
    EXPECT_FALSE(response.original().empty());
    EXPECT_FALSE(response.replayed().empty());
    EXPECT_TRUE(response.new_trace_id().empty());
    EXPECT_TRUE(response.new_request_id().empty());

    // Route mode must not persist any new execution records.
    EXPECT_EQ(countRows("query_logs", "owner_id", user_a.id), logs_before);
    EXPECT_EQ(countRows("traces", "owner_id", user_a.id), traces_before);
}

TEST_F(DurableWorkflowsE2ETest, ReplayExactPersistsNewTraceWithoutTouchingOriginal) {
    startWorkflow();
    const auto user_a = registerUser("replay-exact");
    const std::string context_id = "dwf-ctx-exact-" + uniqueSuffix();
    const std::string request_id = runCompletedQuery(user_a, context_id, "exact replay target");
    const std::string trace_id = "trace-" + request_id;

    const auto original_before = handles_->domain->getTraceById(user_a.id, trace_id);
    ASSERT_TRUE(original_before.has_value());
    const auto logs_before = countRows("query_logs", "owner_id", user_a.id);
    const auto traces_before = countRows("traces", "owner_id", user_a.id);

    agent_communication::ReplayQueryRequest request;
    request.set_trace_id(trace_id);
    request.set_mode("exact");
    agent_communication::ReplayQueryResponse response;
    grpc::ClientContext context;
    applyAuth(context, user_a);
    const auto status = orchestration_stub_->ReplayQuery(&context, request, &response);
    ASSERT_TRUE(status.ok()) << status.error_message();

    // A brand-new request/trace pair was persisted; the original is intact.
    EXPECT_FALSE(response.new_request_id().empty());
    EXPECT_FALSE(response.new_trace_id().empty());
    EXPECT_NE(response.new_trace_id(), trace_id);
    EXPECT_NE(response.new_request_id(), request_id);
    EXPECT_FALSE(response.replayed().empty());

    EXPECT_EQ(countRows("query_logs", "owner_id", user_a.id), logs_before + 1);
    EXPECT_EQ(countRows("traces", "owner_id", user_a.id), traces_before + 1);

    const auto original_after = handles_->domain->getTraceById(user_a.id, trace_id);
    ASSERT_TRUE(original_after.has_value());
    EXPECT_EQ(original_after->trace_payload, original_before->trace_payload);
    EXPECT_EQ(original_after->status, original_before->status);
    EXPECT_EQ(original_after->query_log_id, request_id);

    // The new trace is queryable and linked back to the original trace.
    const auto replayed_trace =
        handles_->domain->getTraceById(user_a.id, response.new_trace_id());
    ASSERT_TRUE(replayed_trace.has_value());
    EXPECT_EQ(replayed_trace->owner_id, user_a.id);
    EXPECT_NE(replayed_trace->trace_payload.find(trace_id), std::string::npos)
        << "new trace must carry a queryable association to the original trace";
    EXPECT_EQ(replayed_trace->query_log_id, response.new_request_id());

    // The replay executed through the durable pipeline with a terminal state.
    const auto replayed_log =
        handles_->domain->getQueryLogById(user_a.id, response.new_request_id());
    ASSERT_TRUE(replayed_log.has_value());
    EXPECT_EQ(replayed_log->status, "completed");
    EXPECT_EQ(replayed_log->request_text, "exact replay target");
}

// ============================================================================
// Export
// ============================================================================

TEST_F(DurableWorkflowsE2ETest, ExportCrossOwnerOrMissingConversationIsNotFound) {
    startWorkflow();
    const auto user_a = registerUser("export-a");
    const auto user_b = registerUser("export-b");
    const std::string context_id = "dwf-ctx-export-" + uniqueSuffix();
    runCompletedQuery(user_a, context_id, "export me");

    agent_communication::ExportConversationRequest request;
    request.set_context_id(context_id);
    request.set_format("markdown");
    agent_communication::ExportConversationResponse response;
    grpc::ClientContext context;
    applyAuth(context, user_b);
    const auto status = orchestration_stub_->ExportConversation(&context, request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);

    agent_communication::ExportConversationRequest missing_request;
    missing_request.set_context_id("dwf-ctx-missing-" + uniqueSuffix());
    agent_communication::ExportConversationResponse missing_response;
    grpc::ClientContext missing_context;
    applyAuth(missing_context, user_a);
    const auto missing_status =
        orchestration_stub_->ExportConversation(&missing_context, missing_request,
                                                &missing_response);
    EXPECT_EQ(missing_status.error_code(), grpc::StatusCode::NOT_FOUND);
}

TEST_F(DurableWorkflowsE2ETest, ExportHtmlEscapesHostileMessagePayloads) {
    startWorkflow();
    const auto user_a = registerUser("export-xss");
    const std::string context_id = "dwf-ctx-xss-" + uniqueSuffix();
    const std::string hostile = "<script>alert('xss-\"dwf\")</script><img onerror=x>";
    ASSERT_TRUE(handles_->domain->ensureConversation(user_a.id, context_id, "xss"));
    ASSERT_TRUE(handles_->domain
                    ->appendMessageAutoSequence(user_a.id, context_id, "user", hostile)
                    .has_value());
    ASSERT_TRUE(handles_->domain
                    ->appendMessageAutoSequence(user_a.id, context_id, "assistant",
                                                "reply & <b>bold</b>")
                    .has_value());

    // HTML export must escape every hostile byte.
    agent_communication::ExportConversationRequest html_request;
    html_request.set_context_id(context_id);
    html_request.set_format("html");
    agent_communication::ExportConversationResponse html_response;
    grpc::ClientContext html_context;
    applyAuth(html_context, user_a);
    const auto html_status =
        orchestration_stub_->ExportConversation(&html_context, html_request, &html_response);
    ASSERT_TRUE(html_status.ok()) << html_status.error_message();
    EXPECT_NE(html_response.mime_type().find("text/html"), std::string::npos);
    const std::string html = html_response.file_data();
    EXPECT_EQ(html.find("<script>"), std::string::npos) << "raw script tag must be escaped";
    EXPECT_EQ(html.find("onerror=x>"), std::string::npos);
    EXPECT_NE(html.find("&lt;script&gt;"), std::string::npos);
    EXPECT_NE(html.find("alert(&#39;xss-"), std::string::npos);

    // Markdown export carries the real conversation text (raw by design).
    agent_communication::ExportConversationRequest md_request;
    md_request.set_context_id(context_id);
    md_request.set_format("markdown");
    agent_communication::ExportConversationResponse md_response;
    grpc::ClientContext md_context;
    applyAuth(md_context, user_a);
    const auto md_status =
        orchestration_stub_->ExportConversation(&md_context, md_request, &md_response);
    ASSERT_TRUE(md_status.ok()) << md_status.error_message();
    EXPECT_NE(md_response.file_data().find(hostile), std::string::npos);
    EXPECT_NE(md_response.file_data().find("reply & <b>bold</b>"), std::string::npos);
}

// ============================================================================
// Share
// ============================================================================

TEST_F(DurableWorkflowsE2ETest, ShareTokenIsHighEntropyAndStoredOnlyAsHash) {
    startWorkflow();
    const auto user_a = registerUser("share-create");
    const std::string context_id = "dwf-ctx-share-" + uniqueSuffix();
    runCompletedQuery(user_a, context_id, "share this conversation");

    agent_communication::ShareSessionRequest request;
    request.set_context_id(context_id);
    request.set_mode("view");
    request.set_expiry_days(7);
    agent_communication::ShareSessionResponse response;
    grpc::ClientContext context;
    applyAuth(context, user_a);
    const auto status = sharing_stub_->ShareSession(&context, request, &response);
    ASSERT_TRUE(status.ok()) << status.error_message();

    // High entropy raw token, returned exactly once at creation.
    EXPECT_GE(response.token().size(), 64u);
    EXPECT_FALSE(response.share_id().empty());
    EXPECT_FALSE(response.expires_at().empty());
    // Relative link only — no hardcoded host.
    EXPECT_EQ(response.share_url().rfind("/share/", 0), 0u);
    EXPECT_EQ(response.share_url().find("://"), std::string::npos);

    // PostgreSQL stores only the SHA-256 hash, never the raw token.
    const std::string expected_hash = sha256Hex(response.token());
    std::string stored_hash;
    std::int64_t raw_hits = 0;
    handles_->store->executeTransaction([&](pqxx::work& transaction) {
        const auto rows = transaction.exec_params(
            "SELECT token_hash FROM shares WHERE id = $1", response.share_id());
        if (!rows.empty()) {
            stored_hash = rows[0][0].as<std::string>();
        }
        raw_hits = transaction.exec_params(
            "SELECT COUNT(*) FROM shares WHERE token_hash = $1", response.token())[0][0]
                       .as<std::int64_t>();
    });
    EXPECT_EQ(stored_hash, expected_hash);
    EXPECT_EQ(raw_hits, 0);

    // Sharing another user's conversation or an unknown one is NOT_FOUND.
    const auto user_b = registerUser("share-intruder");
    agent_communication::ShareSessionRequest foreign_request;
    foreign_request.set_context_id(context_id);
    foreign_request.set_mode("view");
    agent_communication::ShareSessionResponse foreign_response;
    grpc::ClientContext foreign_context;
    applyAuth(foreign_context, user_b);
    const auto foreign_status =
        sharing_stub_->ShareSession(&foreign_context, foreign_request, &foreign_response);
    EXPECT_EQ(foreign_status.error_code(), grpc::StatusCode::NOT_FOUND);

    // Only read-only "view" is supported.
    agent_communication::ShareSessionRequest interact_request;
    interact_request.set_context_id(context_id);
    interact_request.set_mode("interact");
    agent_communication::ShareSessionResponse interact_response;
    grpc::ClientContext interact_context;
    applyAuth(interact_context, user_a);
    const auto interact_status =
        sharing_stub_->ShareSession(&interact_context, interact_request, &interact_response);
    EXPECT_EQ(interact_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(DurableWorkflowsE2ETest, ReadSharedConversationIsPublicReadOnlyAndSanitized) {
    startWorkflow();
    const auto user_a = registerUser("share-read");
    const std::string context_id = "dwf-ctx-read-" + uniqueSuffix();
    runCompletedQuery(user_a, context_id, "shared question");

    agent_communication::ShareSessionRequest share_request;
    share_request.set_context_id(context_id);
    share_request.set_mode("view");
    agent_communication::ShareSessionResponse share_response;
    grpc::ClientContext share_context;
    applyAuth(share_context, user_a);
    ASSERT_TRUE(sharing_stub_->ShareSession(&share_context, share_request, &share_response).ok());

    // Public read: NO authorization metadata at all.
    agent_communication::ReadSharedConversationRequest read_request;
    read_request.set_token(share_response.token());
    agent_communication::ReadSharedConversationResponse read_response;
    grpc::ClientContext read_context;  // deliberately anonymous
    const auto read_status =
        sharing_stub_->ReadSharedConversation(&read_context, read_request, &read_response);
    ASSERT_TRUE(read_status.ok()) << read_status.error_message();
    ASSERT_GE(read_response.messages_size(), 2);
    EXPECT_EQ(read_response.messages(0).role(), "user");
    EXPECT_EQ(read_response.messages(0).content(), "shared question");

    // Sanitization: no owner identity leaks anywhere in the payload.
    const std::string dumped = read_response.DebugString();
    EXPECT_EQ(dumped.find(user_a.id), std::string::npos);
    EXPECT_EQ(dumped.find(user_a.username), std::string::npos);

    // Unknown token is an explicit NOT_FOUND.
    agent_communication::ReadSharedConversationRequest bad_request;
    bad_request.set_token("dwf-bogus-token-" + uniqueSuffix());
    agent_communication::ReadSharedConversationResponse bad_response;
    grpc::ClientContext bad_context;
    const auto bad_status =
        sharing_stub_->ReadSharedConversation(&bad_context, bad_request, &bad_response);
    EXPECT_EQ(bad_status.error_code(), grpc::StatusCode::NOT_FOUND);
}

TEST_F(DurableWorkflowsE2ETest, RevokedShareIsRefusedAndListReflectsState) {
    startWorkflow();
    const auto user_a = registerUser("share-revoke");
    const std::string context_id = "dwf-ctx-revoke-" + uniqueSuffix();
    runCompletedQuery(user_a, context_id, "revocable share");

    agent_communication::ShareSessionRequest share_request;
    share_request.set_context_id(context_id);
    share_request.set_mode("view");
    agent_communication::ShareSessionResponse share_response;
    grpc::ClientContext share_context;
    applyAuth(share_context, user_a);
    ASSERT_TRUE(sharing_stub_->ShareSession(&share_context, share_request, &share_response).ok());

    // Owner list shows the active share (raw token absent — hash only).
    agent_communication::ListSharesRequest list_request;
    agent_communication::ListSharesResponse list_response;
    grpc::ClientContext list_context;
    applyAuth(list_context, user_a);
    ASSERT_TRUE(sharing_stub_->ListShares(&list_context, list_request, &list_response).ok());
    ASSERT_EQ(list_response.shares_size(), 1);
    EXPECT_EQ(list_response.shares(0).share_id(), share_response.share_id());
    EXPECT_EQ(list_response.shares(0).conversation_id(), context_id);
    EXPECT_FALSE(list_response.shares(0).revoked());
    EXPECT_EQ(list_response.DebugString().find(share_response.token()), std::string::npos);

    // Revoke (owner-scoped; another owner cannot revoke it).
    const auto user_b = registerUser("share-revoke-b");
    agent_communication::RevokeShareRequest foreign_revoke;
    foreign_revoke.set_share_id(share_response.share_id());
    agent_communication::RevokeShareResponse foreign_revoke_response;
    grpc::ClientContext foreign_revoke_context;
    applyAuth(foreign_revoke_context, user_b);
    const auto foreign_status = sharing_stub_->RevokeShare(
        &foreign_revoke_context, foreign_revoke, &foreign_revoke_response);
    EXPECT_EQ(foreign_status.error_code(), grpc::StatusCode::NOT_FOUND);

    agent_communication::RevokeShareRequest revoke_request;
    revoke_request.set_share_id(share_response.share_id());
    agent_communication::RevokeShareResponse revoke_response;
    grpc::ClientContext revoke_context;
    applyAuth(revoke_context, user_a);
    ASSERT_TRUE(sharing_stub_->RevokeShare(&revoke_context, revoke_request, &revoke_response).ok());

    // List now reflects the revoked state.
    agent_communication::ListSharesResponse after_list;
    grpc::ClientContext after_context;
    applyAuth(after_context, user_a);
    ASSERT_TRUE(sharing_stub_->ListShares(&after_context, list_request, &after_list).ok());
    ASSERT_EQ(after_list.shares_size(), 1);
    EXPECT_TRUE(after_list.shares(0).revoked());
    EXPECT_FALSE(after_list.shares(0).revoked_at().empty());

    // Public read of a revoked share is refused with a clear error.
    agent_communication::ReadSharedConversationRequest read_request;
    read_request.set_token(share_response.token());
    agent_communication::ReadSharedConversationResponse read_response;
    grpc::ClientContext read_context;
    const auto read_status =
        sharing_stub_->ReadSharedConversation(&read_context, read_request, &read_response);
    EXPECT_EQ(read_status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
}

TEST_F(DurableWorkflowsE2ETest, ExpiredShareIsRefused) {
    startWorkflow();
    const auto user_a = registerUser("share-expiry");
    const std::string context_id = "dwf-ctx-expiry-" + uniqueSuffix();
    ASSERT_TRUE(handles_->domain->ensureConversation(user_a.id, context_id, "expiry"));
    ASSERT_TRUE(handles_->domain
                    ->appendMessageAutoSequence(user_a.id, context_id, "user", "expired secret")
                    .has_value());

    // Insert a share that already expired yesterday (hash of a known token).
    const std::string raw_token = "dwf-expired-token-" + uniqueSuffix();
    const std::string share_id = "dwf-expired-share-" + uniqueSuffix();
    handles_->store->executeTransaction([&](pqxx::work& transaction) {
        transaction.exec_params(
            "INSERT INTO shares (id, owner_id, conversation_id, token_hash, permission,"
            " expires_at) VALUES ($1, $2, $3, $4, 'view', NOW() - INTERVAL '1 day')",
            share_id, user_a.id, context_id, sha256Hex(raw_token));
    });

    agent_communication::ReadSharedConversationRequest read_request;
    read_request.set_token(raw_token);
    agent_communication::ReadSharedConversationResponse read_response;
    grpc::ClientContext read_context;
    const auto read_status =
        sharing_stub_->ReadSharedConversation(&read_context, read_request, &read_response);
    EXPECT_EQ(read_status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
    EXPECT_NE(read_status.error_message().find("expired"), std::string::npos);
}

// ============================================================================
// Template
// ============================================================================

TEST_F(DurableWorkflowsE2ETest, TemplateWithInvalidDefinitionIsRejected) {
    startWorkflow();
    const auto user_a = registerUser("template-invalid");

    {
        agent_communication::SaveTemplateRequest request;
        request.set_name("broken-json");
        request.set_dag_json("{this is not json");
        agent_communication::SaveTemplateResponse response;
        grpc::ClientContext context;
        applyAuth(context, user_a);
        const auto status = sharing_stub_->SaveTemplate(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    }
    {
        agent_communication::SaveTemplateRequest request;
        request.set_name("missing-fields");
        request.set_dag_json(R"({"unrelated": 1})");
        agent_communication::SaveTemplateResponse response;
        grpc::ClientContext context;
        applyAuth(context, user_a);
        const auto status = sharing_stub_->SaveTemplate(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    }
    {
        agent_communication::SaveTemplateRequest request;
        request.set_name("");
        request.set_dag_json(R"({"initial_message": "hi"})");
        agent_communication::SaveTemplateResponse response;
        grpc::ClientContext context;
        applyAuth(context, user_a);
        const auto status = sharing_stub_->SaveTemplate(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    }

    // Nothing was persisted.
    agent_communication::ListTemplatesRequest list_request;
    agent_communication::ListTemplatesResponse list_response;
    grpc::ClientContext list_context;
    applyAuth(list_context, user_a);
    ASSERT_TRUE(sharing_stub_->ListTemplates(&list_context, list_request, &list_response).ok());
    EXPECT_EQ(list_response.templates_size(), 0);
}

TEST_F(DurableWorkflowsE2ETest, OversizedTemplateDefinitionIsRejectedWithoutCrash) {
    startWorkflow();
    const auto user_a = registerUser("template-oversized");

    // 1) Flat payload well above the 64 KiB cap.
    {
        agent_communication::SaveTemplateRequest request;
        request.set_name("oversized-flat");
        request.set_dag_json(std::string(70000, 'a'));
        agent_communication::SaveTemplateResponse response;
        grpc::ClientContext context;
        applyAuth(context, user_a);
        const auto status = sharing_stub_->SaveTemplate(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
        EXPECT_NE(status.error_message().find("64 KiB"), std::string::npos);
    }
    // 2) Deeply nested payload above the cap: recursive descent parsing must
    //    never even start — the size guard rejects it first.
    {
        std::string nested(70000, '[');
        agent_communication::SaveTemplateRequest request;
        request.set_name("oversized-nested");
        request.set_dag_json(nested);
        agent_communication::SaveTemplateResponse response;
        grpc::ClientContext context;
        applyAuth(context, user_a);
        const auto status = sharing_stub_->SaveTemplate(&context, request, &response);
        EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
        EXPECT_NE(status.error_message().find("64 KiB"), std::string::npos);
    }

    // 3) The process survived both hostile payloads: a normal save still
    //    succeeds and exactly one template exists for the owner.
    agent_communication::SaveTemplateRequest ok_request;
    ok_request.set_name("after-attack-" + uniqueSuffix());
    ok_request.set_dag_json(R"({"initial_message": "hello"})");
    agent_communication::SaveTemplateResponse ok_response;
    grpc::ClientContext ok_context;
    applyAuth(ok_context, user_a);
    ASSERT_TRUE(sharing_stub_->SaveTemplate(&ok_context, ok_request, &ok_response).ok());

    agent_communication::ListTemplatesRequest list_request;
    agent_communication::ListTemplatesResponse list_response;
    grpc::ClientContext list_context;
    applyAuth(list_context, user_a);
    ASSERT_TRUE(sharing_stub_->ListTemplates(&list_context, list_request, &list_response).ok());
    EXPECT_EQ(list_response.templates_size(), 1);
}

TEST_F(DurableWorkflowsE2ETest, UseTemplateCreatesRealConversationForCurrentOwnerOnly) {
    startWorkflow();
    const auto user_a = registerUser("template-use-a");
    const auto user_b = registerUser("template-use-b");

    agent_communication::SaveTemplateRequest save_request;
    save_request.set_name("standup-" + uniqueSuffix());
    save_request.set_description("Daily standup kickoff");
    save_request.set_dag_json(R"({"initial_message": "Start my daily standup summary"})");
    agent_communication::SaveTemplateResponse save_response;
    grpc::ClientContext save_context;
    applyAuth(save_context, user_a);
    ASSERT_TRUE(sharing_stub_->SaveTemplate(&save_context, save_request, &save_response).ok());
    ASSERT_FALSE(save_response.template_id().empty());

    // List + get reflect the stored template for its owner only.
    agent_communication::ListTemplatesRequest list_request;
    agent_communication::ListTemplatesResponse list_response;
    grpc::ClientContext list_context;
    applyAuth(list_context, user_a);
    ASSERT_TRUE(sharing_stub_->ListTemplates(&list_context, list_request, &list_response).ok());
    ASSERT_EQ(list_response.templates_size(), 1);
    EXPECT_EQ(list_response.templates(0).name(), save_request.name());
    EXPECT_NE(list_response.templates(0).definition().find("initial_message"),
              std::string::npos);

    agent_communication::GetTemplateRequest get_request;
    get_request.set_template_id(save_response.template_id());
    agent_communication::GetTemplateResponse get_response;
    grpc::ClientContext get_context;
    applyAuth(get_context, user_b);
    const auto get_status = sharing_stub_->GetTemplate(&get_context, get_request, &get_response);
    EXPECT_EQ(get_status.error_code(), grpc::StatusCode::NOT_FOUND);

    // Cross-owner use is refused and creates nothing for the intruder.
    agent_communication::UseTemplateRequest foreign_use;
    foreign_use.set_template_id(save_response.template_id());
    agent_communication::UseTemplateResponse foreign_response;
    grpc::ClientContext foreign_context;
    applyAuth(foreign_context, user_b);
    const auto foreign_status =
        sharing_stub_->UseTemplate(&foreign_context, foreign_use, &foreign_response);
    EXPECT_EQ(foreign_status.error_code(), grpc::StatusCode::NOT_FOUND);
    EXPECT_EQ(handles_->domain->listConversations(user_b.id).size(), 0u);

    // Owner use creates a REAL conversation with the initial message.
    agent_communication::UseTemplateRequest use_request;
    use_request.set_template_id(save_response.template_id());
    agent_communication::UseTemplateResponse use_response;
    grpc::ClientContext use_context;
    applyAuth(use_context, user_a);
    ASSERT_TRUE(sharing_stub_->UseTemplate(&use_context, use_request, &use_response).ok());
    ASSERT_FALSE(use_response.context_id().empty());

    const auto conversation =
        handles_->domain->getConversationById(user_a.id, use_response.context_id());
    ASSERT_TRUE(conversation.has_value());
    EXPECT_EQ(conversation->owner_id, user_a.id);

    const auto messages =
        handles_->domain->listMessages(user_a.id, use_response.context_id());
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].role, "user");
    EXPECT_EQ(messages[0].content, "Start my daily standup summary");
    EXPECT_EQ(messages[0].sequence_no, 1);
}

}  // namespace
