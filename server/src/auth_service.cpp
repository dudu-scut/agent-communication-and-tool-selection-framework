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

    if (request->username().size() < 3 || request->password().size() < 6) {
        response->mutable_status()->set_code(400);
        response->mutable_status()->set_message("Username min 3 chars, password min 6 chars");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Username min 3 chars, password min 6 chars");
    }

    if (request->username().size() > 64 || request->password().size() > 128) {
        response->mutable_status()->set_code(400);
        response->mutable_status()->set_message("Username or password too long");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Input too long");
    }

    // Check if username already exists (Redis: check key existence)
    auto key = userKey(request->username());

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

    // Atomic registration: HSETNX on user_id field — only succeeds if key doesn't exist
    if (!redis_->hsetnx(key, "user_id", user_id)) {
        response->mutable_status()->set_code(409);
        response->mutable_status()->set_message("Username already exists");
        return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                            "Username already exists");
    }

    // Store remaining user fields (check critical writes)
    if (!redis_->hset(key, "display_name", display_name) ||
        !redis_->hset(key, "password_hash", password_hash) ||
        !redis_->hset(key, "created_at", std::to_string(created_ts))) {
        LOG_ERROR("Failed to store user fields for: " + request->username());
        redis_->del(key);  // Roll back partial registration
        response->mutable_status()->set_code(500);
        response->mutable_status()->set_message("Internal server error");
        return grpc::Status(grpc::StatusCode::INTERNAL,
                            "Failed to store user data");
    }

    // Reverse index: nexusai:uid:{user_id} → username (for token validation)
    if (!redis_->set(usernameIdxKey(user_id), request->username())) {
        LOG_ERROR("Failed to create reverse index for user: " + user_id);
    }

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

    // Redis TTL handles expiry automatically — if key expired, hget returns false
    auto tkey = tokenKey(token);
    if (!redis_->hget(tkey, "user_id", user_id)) {
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
    // Fix #2: Use OpenSSL CSPRNG instead of mt19937 for cryptographically secure salt
    constexpr size_t kSaltLen = 32;
    unsigned char random_bytes[kSaltLen];
    if (RAND_bytes(random_bytes, kSaltLen) != 1) {
        // Fallback to mt19937 only if CSPRNG fails (extremely unlikely)
        static thread_local std::mt19937 rng(std::random_device{}());
        static constexpr const char kHexChars[] = "0123456789abcdef";
        std::string salt(kSaltLen, '0');
        for (auto& c : salt) {
            c = kHexChars[rng() % 16];
        }
        return salt;
    }
    static constexpr const char kHexChars[] = "0123456789abcdef";
    std::string salt(kSaltLen * 2, '0');
    for (size_t i = 0; i < kSaltLen; ++i) {
        salt[i * 2] = kHexChars[random_bytes[i] >> 4];
        salt[i * 2 + 1] = kHexChars[random_bytes[i] & 0x0F];
    }
    return salt;
}

std::string AuthServiceImpl::hashPassword(const std::string& password,
                                            const std::string& salt) {
    // Fix #2: Upgrade from single SHA-256 to iterated SHA-256 (PBKDF2-style).
    // 10,000 iterations provides meaningful protection against brute-force
    // while remaining compatible with existing OpenSSL SHA-256.
    constexpr int kIterations = 10000;
    std::string input = salt + password;

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),
           input.size(), digest);

    // Iteratively re-hash the digest to increase computational cost
    for (int i = 1; i < kIterations; ++i) {
        // Combine previous digest with original input for each iteration
        unsigned char temp[SHA256_DIGEST_LENGTH + 64];  // digest + part of original
        memcpy(temp, digest, SHA256_DIGEST_LENGTH);
        size_t copy_len = std::min(sizeof(temp) - SHA256_DIGEST_LENGTH, input.size());
        memcpy(temp + SHA256_DIGEST_LENGTH, input.c_str(), copy_len);

        SHA256(temp, SHA256_DIGEST_LENGTH + copy_len, digest);
    }

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(digest[i]);
    }

    // Store iteration count in the hash format for future compatibility
    return salt + ":" + std::to_string(kIterations) + ":" + oss.str();
}

bool AuthServiceImpl::verifyPassword(const std::string& password,
                                       const std::string& stored_hash) {
    // Parse stored hash: "salt:hexdigest" (old format) or "salt:iterations:hexdigest" (new format)
    auto sep1 = stored_hash.find(':');
    if (sep1 == std::string::npos) return false;

    std::string salt = stored_hash.substr(0, sep1);
    auto sep2 = stored_hash.find(':', sep1 + 1);

    int iterations = 1;  // Default: single SHA-256 (old format)
    std::string expected;

    if (sep2 != std::string::npos) {
        // New format: salt:iterations:hexdigest
        iterations = std::stoi(stored_hash.substr(sep1 + 1, sep2 - sep1 - 1));
        expected = stored_hash.substr(sep2 + 1);
    } else {
        // Old format: salt:hexdigest
        expected = stored_hash.substr(sep1 + 1);
    }

    std::string input = salt + password;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),
           input.size(), digest);

    // Apply the same iteration count as when the hash was stored
    for (int i = 1; i < iterations; ++i) {
        unsigned char temp[SHA256_DIGEST_LENGTH + 64];
        memcpy(temp, digest, SHA256_DIGEST_LENGTH);
        size_t copy_len = std::min(sizeof(temp) - SHA256_DIGEST_LENGTH, input.size());
        memcpy(temp + SHA256_DIGEST_LENGTH, input.c_str(), copy_len);
        SHA256(temp, SHA256_DIGEST_LENGTH + copy_len, digest);
    }

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(digest[i]);
    }

    return oss.str() == expected;
}

}  // namespace server
}  // namespace agent_rpc
