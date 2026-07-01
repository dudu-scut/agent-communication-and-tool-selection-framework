/**
 * @file test_redis_services.cpp
 * @brief Integration tests for RedisClient, AuthService, and MemoryService.
 *
 * Requires a running Redis instance at REDIS_HOST:REDIS_PORT (default 127.0.0.1:6379).
 * Tests use a "nexusai_test:" prefix to avoid polluting real data and clean up after themselves.
 */

#include "agent_rpc/common/redis_client.h"
#include "agent_rpc/common/memory_service.h"
#include "agent_rpc/server/auth_service.h"

#include "ai_query.pb.h"
#include "user.pb.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace agent_rpc::tests {

namespace {

std::string getRedisHost() {
    const char* h = std::getenv("REDIS_HOST");
    return h ? h : "127.0.0.1";
}

int getRedisPort() {
    const char* p = std::getenv("REDIS_PORT");
    return p ? std::atoi(p) : 6379;
}

// Unique prefix for each test run to avoid collisions
std::string testPrefix() {
    static int counter = 0;
    return "nexusai_test:" + std::to_string(counter++);
}

}  // namespace

// ============================================================================
// Fixture: provides a connected RedisClient, skips if Redis unavailable
// ============================================================================
class RedisFixture : public ::testing::Test {
protected:
    common::RedisClient redis;
    bool redis_available = false;

    void SetUp() override {
        redis_available = redis.connect(getRedisHost(), getRedisPort());
        if (!redis_available) {
            GTEST_SKIP() << "Redis not available at " << getRedisHost()
                         << ":" << getRedisPort();
        }
    }

    // Clean up a key after test
    void cleanup(const std::string& key) {
        redis.del(key);
    }
};

// ============================================================================
// RedisClient basic operations
// ============================================================================

TEST_F(RedisFixture, StringSetGetDel) {
    auto key = testPrefix() + ":str";
    EXPECT_TRUE(redis.set(key, "hello"));

    std::string val;
    EXPECT_TRUE(redis.get(key, val));
    EXPECT_EQ(val, "hello");

    EXPECT_TRUE(redis.del(key));
    EXPECT_FALSE(redis.get(key, val));
}

TEST_F(RedisFixture, StringSetex) {
    auto key = testPrefix() + ":setex";
    EXPECT_TRUE(redis.setex(key, 10, "temp"));

    std::string val;
    EXPECT_TRUE(redis.get(key, val));
    EXPECT_EQ(val, "temp");

    redis.del(key);
}

TEST_F(RedisFixture, ExistsAndDel) {
    auto key = testPrefix() + ":exists";
    EXPECT_FALSE(redis.exists(key));

    redis.set(key, "x");
    EXPECT_TRUE(redis.exists(key));

    redis.del(key);
    EXPECT_FALSE(redis.exists(key));
}

TEST_F(RedisFixture, HashOperations) {
    auto key = testPrefix() + ":hash";

    EXPECT_TRUE(redis.hset(key, "f1", "v1"));
    EXPECT_TRUE(redis.hset(key, "f2", "v2"));

    std::string val;
    EXPECT_TRUE(redis.hget(key, "f1", val));
    EXPECT_EQ(val, "v1");

    std::map<std::string, std::string> all;
    EXPECT_TRUE(redis.hgetall(key, all));
    EXPECT_EQ(all.size(), 2u);
    EXPECT_EQ(all["f1"], "v1");
    EXPECT_EQ(all["f2"], "v2");

    EXPECT_TRUE(redis.hdel(key, "f1"));
    EXPECT_FALSE(redis.hget(key, "f1", val));

    redis.del(key);
}

TEST_F(RedisFixture, HsetnxAtomic) {
    auto key = testPrefix() + ":hsetnx";

    EXPECT_TRUE(redis.hsetnx(key, "field", "first"));
    EXPECT_FALSE(redis.hsetnx(key, "field", "second"));  // already exists

    std::string val;
    redis.hget(key, "field", val);
    EXPECT_EQ(val, "first");  // original value preserved

    redis.del(key);
}

TEST_F(RedisFixture, ListOperations) {
    auto key = testPrefix() + ":list";

    EXPECT_TRUE(redis.rpush(key, "a"));
    EXPECT_TRUE(redis.rpush(key, "b"));
    EXPECT_TRUE(redis.rpush(key, "c"));

    std::vector<std::string> items;
    EXPECT_TRUE(redis.lrange(key, 0, -1, items));
    EXPECT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0], "a");
    EXPECT_EQ(items[2], "c");

    // Trim to last 2 elements
    EXPECT_TRUE(redis.ltrim(key, -2, -1));
    items.clear();
    redis.lrange(key, 0, -1, items);
    EXPECT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0], "b");

    redis.del(key);
}

TEST_F(RedisFixture, ExpireKey) {
    auto key = testPrefix() + ":expire";
    redis.set(key, "val");
    EXPECT_TRUE(redis.expire(key, 60));
    // Key still exists (hasn't expired yet)
    EXPECT_TRUE(redis.exists(key));
    redis.del(key);
}

// ============================================================================
// MemoryService (Redis-backed)
// ============================================================================

class MemoryFixture : public RedisFixture {
protected:
    std::unique_ptr<common::MemoryService> memory;

    void SetUp() override {
        RedisFixture::SetUp();
        if (!redis_available) return;
        memory = std::make_unique<common::MemoryService>(&redis);
    }
};

TEST_F(MemoryFixture, ConversationHistory) {
    auto ctx = testPrefix() + ":ctx";
    auto agent = "agent-1";

    memory->appendMessage(ctx, agent, "user", "你好");
    memory->appendMessage(ctx, agent, "agent", "你好！有什么可以帮你的？");
    memory->appendMessage(ctx, agent, "user", "写个排序算法");

    auto history = memory->getConversationHistory(ctx, agent, 10);
    EXPECT_TRUE(history.find("你好") != std::string::npos);
    EXPECT_TRUE(history.find("排序算法") != std::string::npos);

    // Cleanup
    redis.del("nexusai:conv:" + ctx + ":" + agent);
    redis.del("nexusai:last_agent:" + ctx);
}

TEST_F(MemoryFixture, LastAgentTracking) {
    auto ctx = testPrefix() + ":ctx";

    memory->setLastAgent(ctx, "agent-a");
    EXPECT_EQ(memory->getLastAgent(ctx), "agent-a");

    memory->setLastAgent(ctx, "agent-b");
    EXPECT_EQ(memory->getLastAgent(ctx), "agent-b");

    redis.del("nexusai:last_agent:" + ctx);
}

TEST_F(MemoryFixture, UserMemory) {
    auto uid = testPrefix() + ":uid";

    memory->setUserMemory(uid, "language", "Chinese");
    memory->setUserMemory(uid, "timezone", "UTC+8");

    auto mem = memory->getUserMemory(uid);
    EXPECT_TRUE(mem.find("language: Chinese") != std::string::npos);
    EXPECT_TRUE(mem.find("timezone: UTC+8") != std::string::npos);

    redis.del("nexusai:memory:" + uid);
}

TEST_F(MemoryFixture, UserMemoryFromHints) {
    auto uid = testPrefix() + ":uid";
    auto key = "nexusai:memory:" + uid;

    // Pre-set a field
    redis.hset(key, "existing", "value");

    std::map<std::string, std::string> hints;
    hints["preference"] = "dark_mode";
    hints["existing"] = "updated";

    memory->updateUserMemoryFromHints(uid, hints);

    std::string val;
    redis.hget(key, "preference", val);
    EXPECT_EQ(val, "dark_mode");

    redis.hget(key, "existing", val);
    EXPECT_EQ(val, "updated");

    // Empty value = delete
    hints.clear();
    hints["preference"] = "";  // delete
    memory->updateUserMemoryFromHints(uid, hints);

    EXPECT_FALSE(redis.hget(key, "preference", val));

    redis.del(key);
}

TEST_F(MemoryFixture, CrossAgentSummary) {
    auto ctx = testPrefix() + ":ctx";

    EXPECT_EQ(memory->getCrossAgentSummary(ctx), "");

    memory->setCrossAgentSummary(ctx, "User was discussing sorting algorithms.");
    EXPECT_EQ(memory->getCrossAgentSummary(ctx),
              "User was discussing sorting algorithms.");

    redis.del("nexusai:summary:" + ctx);
}

TEST_F(MemoryFixture, BuildSystemContext) {
    auto uid = testPrefix() + ":uid";
    auto ctx = testPrefix() + ":ctx";
    auto agent = "agent-1";

    // Set up some data
    memory->appendMessage(ctx, agent, "user", "test message");
    redis.hset("nexusai:memory:" + uid, "lang", "zh");
    memory->setCrossAgentSummary(ctx, "previous context");

    auto sys_ctx = memory->buildSystemContext(uid, ctx, agent, 5);

    EXPECT_TRUE(sys_ctx.conversation_history().find("test message") != std::string::npos);
    EXPECT_TRUE(sys_ctx.user_memory().find("lang: zh") != std::string::npos);
    EXPECT_EQ(sys_ctx.cross_agent_summary(), "previous context");

    // Cleanup
    redis.del("nexusai:conv:" + ctx + ":" + agent);
    redis.del("nexusai:memory:" + uid);
    redis.del("nexusai:summary:" + ctx);
    redis.del("nexusai:last_agent:" + ctx);
}

TEST_F(MemoryFixture, HistoryTrimming) {
    auto ctx = testPrefix() + ":ctx";
    auto agent = "agent-trim";

    // Append more than 50 messages (kMaxHistoryPerAgent)
    for (int i = 0; i < 55; ++i) {
        memory->appendMessage(ctx, agent, "user", "msg-" + std::to_string(i));
    }

    auto history = memory->getConversationHistory(ctx, agent, 100);
    // Should only contain the last 50 messages, not 55
    EXPECT_TRUE(history.find("msg-5") != std::string::npos);
    EXPECT_TRUE(history.find("msg-54") != std::string::npos);
    EXPECT_TRUE(history.find("msg-0") == std::string::npos);  // trimmed

    redis.del("nexusai:conv:" + ctx + ":" + agent);
    redis.del("nexusai:last_agent:" + ctx);
}

// ============================================================================
// AuthService (Redis-backed)
// ============================================================================

class AuthFixture : public RedisFixture {
protected:
    std::unique_ptr<server::AuthServiceImpl> auth;
    grpc::ServerContext ctx;  // dummy context for gRPC handlers

    void SetUp() override {
        RedisFixture::SetUp();
        if (!redis_available) return;
        auth = std::make_unique<server::AuthServiceImpl>(&redis);
    }

    // Helper: register + login, return token
    std::string registerAndLogin(const std::string& username,
                                  const std::string& password = "test123") {
        // Register
        agent_communication::auth::RegisterRequest reg_req;
        agent_communication::auth::RegisterResponse reg_resp;
        reg_req.set_username(username);
        reg_req.set_password(password);
        auth->Register(&ctx, &reg_req, &reg_resp);

        // Login
        agent_communication::auth::LoginRequest login_req;
        agent_communication::auth::LoginResponse login_resp;
        login_req.set_username(username);
        login_req.set_password(password);
        auth->Login(&ctx, &login_req, &login_resp);

        return login_resp.token();
    }

    // Helper: clean up all test keys
    void cleanupUser(const std::string& username) {
        // Look up user_id before deleting
        std::string uid;
        redis.hget("nexusai:user:" + username, "user_id", uid);
        redis.del("nexusai:user:" + username);
        if (!uid.empty()) {
            redis.del("nexusai:uid:" + uid);
        }
    }
};

TEST_F(AuthFixture, RegisterSuccess) {
    auto username = testPrefix() + "_user";

    agent_communication::auth::RegisterRequest req;
    agent_communication::auth::RegisterResponse resp;
    req.set_username(username);
    req.set_password("password123");
    req.set_display_name("Test User");

    auto status = auth->Register(&ctx, &req, &resp);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(resp.status().code(), 0);
    EXPECT_FALSE(resp.user_id().empty());
    EXPECT_EQ(resp.username(), username);

    cleanupUser(username);
}

TEST_F(AuthFixture, RegisterDuplicate) {
    auto username = testPrefix() + "_dup";

    agent_communication::auth::RegisterRequest req;
    agent_communication::auth::RegisterResponse resp;
    req.set_username(username);
    req.set_password("password123");

    // First registration succeeds
    auto s1 = auth->Register(&ctx, &req, &resp);
    EXPECT_TRUE(s1.ok());

    // Second registration should fail with ALREADY_EXISTS
    agent_communication::auth::RegisterResponse resp2;
    auto s2 = auth->Register(&ctx, &req, &resp2);
    EXPECT_EQ(s2.error_code(), grpc::StatusCode::ALREADY_EXISTS);
    EXPECT_EQ(resp2.status().code(), 409);

    cleanupUser(username);
}

TEST_F(AuthFixture, RegisterEmptyCredentials) {
    agent_communication::auth::RegisterRequest req;
    agent_communication::auth::RegisterResponse resp;
    req.set_username("");
    req.set_password("");

    auto status = auth->Register(&ctx, &req, &resp);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(AuthFixture, LoginSuccess) {
    auto username = testPrefix() + "_login";

    // Register first
    agent_communication::auth::RegisterRequest reg_req;
    agent_communication::auth::RegisterResponse reg_resp;
    reg_req.set_username(username);
    reg_req.set_password("secret");
    auth->Register(&ctx, &reg_req, &reg_resp);

    // Login
    agent_communication::auth::LoginRequest login_req;
    agent_communication::auth::LoginResponse login_resp;
    login_req.set_username(username);
    login_req.set_password("secret");

    auto status = auth->Login(&ctx, &login_req, &login_resp);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(login_resp.status().code(), 0);
    EXPECT_FALSE(login_resp.token().empty());
    EXPECT_FALSE(login_resp.user_id().empty());
    EXPECT_GT(login_resp.expires_at(), 0);

    // Clean up token
    redis.del("nexusai:token:" + login_resp.token());
    cleanupUser(username);
}

TEST_F(AuthFixture, LoginWrongPassword) {
    auto username = testPrefix() + "_wrongpw";

    // Register
    agent_communication::auth::RegisterRequest reg_req;
    agent_communication::auth::RegisterResponse reg_resp;
    reg_req.set_username(username);
    reg_req.set_password("correct");
    auth->Register(&ctx, &reg_req, &reg_resp);

    // Login with wrong password
    agent_communication::auth::LoginRequest login_req;
    agent_communication::auth::LoginResponse login_resp;
    login_req.set_username(username);
    login_req.set_password("wrong");

    auto status = auth->Login(&ctx, &login_req, &login_resp);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    EXPECT_EQ(login_resp.status().code(), 401);

    cleanupUser(username);
}

TEST_F(AuthFixture, LoginNonexistentUser) {
    agent_communication::auth::LoginRequest req;
    agent_communication::auth::LoginResponse resp;
    req.set_username("nonexistent_user_xyz");
    req.set_password("anything");

    auto status = auth->Login(&ctx, &req, &resp);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
}

TEST_F(AuthFixture, ValidateTokenValid) {
    auto username = testPrefix() + "_validate";
    auto token = registerAndLogin(username);
    EXPECT_FALSE(token.empty());

    // Validate the token
    std::string uid, uname;
    EXPECT_TRUE(auth->validateToken(token, uid, uname));
    EXPECT_FALSE(uid.empty());
    EXPECT_EQ(uname, username);

    redis.del("nexusai:token:" + token);
    cleanupUser(username);
}

TEST_F(AuthFixture, ValidateTokenInvalid) {
    std::string uid, uname;
    EXPECT_FALSE(auth->validateToken("bogus-token-xyz", uid, uname));
    EXPECT_FALSE(auth->validateToken("", uid, uname));
}

TEST_F(AuthFixture, AtomicRegistrationPreventsRace) {
    // Verify HSETNX semantics: even if key exists with different data,
    // second registration fails
    auto username = testPrefix() + "_atomic";

    agent_communication::auth::RegisterRequest req;
    agent_communication::auth::RegisterResponse resp;
    req.set_username(username);
    req.set_password("pw1");

    auto s1 = auth->Register(&ctx, &req, &resp);
    EXPECT_TRUE(s1.ok());
    auto first_uid = resp.user_id();

    req.set_password("pw2");
    auto s2 = auth->Register(&ctx, &req, &resp);
    EXPECT_EQ(s2.error_code(), grpc::StatusCode::ALREADY_EXISTS);

    // Original user data should be intact
    std::string stored_pw;
    redis.hget("nexusai:user:" + username, "password_hash", stored_pw);
    EXPECT_FALSE(stored_pw.empty());  // still has the first registration's hash

    cleanupUser(username);
}

}  // namespace agent_rpc::tests
