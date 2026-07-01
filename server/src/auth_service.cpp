#include "agent_rpc/server/auth_service.h"
#include "agent_rpc/common/logger.h"

#include <openssl/sha.h>

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <rpc.h>
#pragma comment(lib, "rpcrt4.lib")
#else
#include <uuid/uuid.h>
#endif

namespace agent_rpc {
namespace server {

namespace {

std::string generateUuid() {
#ifdef _WIN32
    UUID uuid;
    UuidCreate(&uuid);
    RPC_CSTR szUuid = nullptr;
    UuidToStringA(&uuid, &szUuid);
    std::string result(reinterpret_cast<const char*>(szUuid));
    RpcStringFreeA(&szUuid);
    return result;
#else
    uuid_t uuid;
    uuid_generate(uuid);
    char buf[37];
    uuid_unparse_lower(uuid, buf);
    return std::string(buf);
#endif
}

}  // namespace

AuthServiceImpl::AuthServiceImpl(common::RedisClient* redis)
    : redis_(redis) {}

// ============================================================================
// Register
// ============================================================================

grpc::Status AuthServiceImpl::Register(
    grpc::ServerContext* context,
    const agent_communication::auth::RegisterRequest* request,
    agent_communication::auth::RegisterResponse* response) {
    (void)context;

    if (request->username().empty() || request->password().empty()) {
        response->mutable_status()->set_code(400);
        response->mutable_status()->set_message("Username and password required");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Username and password required");
    }

    if (request->username().size() > 64 || request->password().size() > 128) {
        response->mutable_status()->set_code(400);
        response->mutable_status()->set_message("Username or password too long");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Input too long");
    }

    // Check if username already exists (Redis: check key existence)
    auto key = userKey(request->username());
    if (redis_->exists(key)) {
        response->mutable_status()->set_code(409);
        response->mutable_status()->set_message("Username already exists");
        return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                            "Username already exists");
    }

    auto user_id = generateUuid();
    auto salt = generateSalt();
    auto password_hash = hashPassword(request->password(), salt);
    auto display_name = request->display_name().empty()
                            ? request->username()
                            : request->display_name();
    auto now = std::chrono::system_clock::now();
    auto created_ts = std::chrono::duration_cast<std::chrono::seconds>(
                          now.time_since_epoch())
                          .count();

    // Store user in Redis hash: nexusai:user:{username}
    redis_->hset(key, "user_id", user_id);
    redis_->hset(key, "display_name", display_name);
    redis_->hset(key, "password_hash", password_hash);
    redis_->hset(key, "created_at", std::to_string(created_ts));

    // Reverse index: nexusai:uid:{user_id} → username (for token validation)
    redis_->set(usernameIdxKey(user_id), request->username());

    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("Registration successful");
    response->set_user_id(user_id);
    response->set_username(request->username());

    LOG_INFO("User registered: " + request->username() + " (" + user_id + ")");
    return grpc::Status::OK;
}

// ============================================================================
// Login
// ============================================================================

grpc::Status AuthServiceImpl::Login(
    grpc::ServerContext* context,
    const agent_communication::auth::LoginRequest* request,
    agent_communication::auth::LoginResponse* response) {
    (void)context;

    if (request->username().empty() || request->password().empty()) {
        response->mutable_status()->set_code(400);
        response->mutable_status()->set_message("Username and password required");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Username and password required");
    }

    // Look up user from Redis
    auto key = userKey(request->username());
    std::string password_hash, user_id;
    if (!redis_->hget(key, "password_hash", password_hash) ||
        !redis_->hget(key, "user_id", user_id)) {
        response->mutable_status()->set_code(401);
        response->mutable_status()->set_message("Invalid credentials");
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                            "Invalid credentials");
    }

    if (!verifyPassword(request->password(), password_hash)) {
        response->mutable_status()->set_code(401);
        response->mutable_status()->set_message("Invalid credentials");
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                            "Invalid credentials");
    }

    // Generate token and store in Redis with TTL
    auto token = generateToken();
    auto now = std::chrono::system_clock::now();
    auto expires_at = now + std::chrono::hours(kTokenTtlHours);
    auto expires_ts = std::chrono::duration_cast<std::chrono::seconds>(
                          expires_at.time_since_epoch())
                          .count();

    auto tkey = tokenKey(token);
    redis_->hset(tkey, "user_id", user_id);
    redis_->hset(tkey, "expires_at", std::to_string(expires_ts));
    redis_->expire(tkey, kTokenTtlSeconds);

    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("Login successful");
    response->set_user_id(user_id);
    response->set_username(request->username());
    response->set_token(token);
    response->set_expires_at(expires_ts);

    LOG_INFO("User logged in: " + request->username());
    return grpc::Status::OK;
}

// ============================================================================
// ValidateToken (gRPC handler)
// ============================================================================

grpc::Status AuthServiceImpl::ValidateToken(
    grpc::ServerContext* context,
    const agent_communication::auth::ValidateTokenRequest* request,
    agent_communication::auth::ValidateTokenResponse* response) {
    (void)context;

    std::string user_id, username;
    bool valid = validateToken(request->token(), user_id, username);

    response->set_valid(valid);
    if (valid) {
        response->mutable_status()->set_code(0);
        response->mutable_status()->set_message("Token valid");
        response->set_user_id(user_id);
        response->set_username(username);
    } else {
        response->mutable_status()->set_code(401);
        response->mutable_status()->set_message("Token invalid or expired");
    }
    return grpc::Status::OK;
}

// ============================================================================
// Internal token validation (for interceptor)
// ============================================================================

bool AuthServiceImpl::validateToken(const std::string& token,
                                     std::string& user_id,
                                     std::string& username) {
    if (token.empty()) return false;

    // Look up token from Redis
    auto tkey = tokenKey(token);
    std::string expires_str;
    if (!redis_->hget(tkey, "user_id", user_id) ||
        !redis_->hget(tkey, "expires_at", expires_str)) {
        return false;
    }

    // Check expiry
    auto expires_ts = std::stoll(expires_str);
    auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    if (now_ts > expires_ts) {
        redis_->del(tkey);  // Clean up expired token
        return false;
    }

    // Reverse lookup: user_id → username via index key
    redis_->get(usernameIdxKey(user_id), username);
    return true;
}

// ============================================================================
// Helpers
// ============================================================================

std::string AuthServiceImpl::generateToken() {
    return generateUuid();
}

std::string AuthServiceImpl::generateSalt() {
    static thread_local std::mt19937 rng(std::random_device{}());
    static constexpr const char kHexChars[] = "0123456789abcdef";

    std::string salt(32, '0');
    for (auto& c : salt) {
        c = kHexChars[rng() % 16];
    }
    return salt;
}

std::string AuthServiceImpl::hashPassword(const std::string& password,
                                            const std::string& salt) {
    std::string input = salt + password;

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),
           input.size(), digest);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(digest[i]);
    }

    return salt + ":" + oss.str();
}

bool AuthServiceImpl::verifyPassword(const std::string& password,
                                       const std::string& stored_hash) {
    auto sep = stored_hash.find(':');
    if (sep == std::string::npos) return false;

    std::string salt = stored_hash.substr(0, sep);
    std::string expected = stored_hash.substr(sep + 1);

    std::string input = salt + password;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),
           input.size(), digest);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(digest[i]);
    }

    return oss.str() == expected;
}

}  // namespace server
}  // namespace agent_rpc
