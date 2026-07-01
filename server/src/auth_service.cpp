#include "agent_rpc/server/auth_service.h"
#include "agent_rpc/common/logger.h"

#include <openssl/sha.h>

#include <algorithm>
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

AuthServiceImpl::AuthServiceImpl() = default;

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

    {
        std::lock_guard<std::mutex> lock(users_mutex_);
        if (users_.count(request->username())) {
            response->mutable_status()->set_code(409);
            response->mutable_status()->set_message("Username already exists");
            return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                                "Username already exists");
        }
    }

    auto user_id = generateUuid();
    auto salt = generateSalt();
    auto password_hash = hashPassword(request->password(), salt);

    UserInfo info;
    info.user_id = user_id;
    info.username = request->username();
    info.display_name =
        request->display_name().empty() ? request->username() : request->display_name();
    info.password_hash = password_hash;
    info.created_at = std::chrono::system_clock::now();

    {
        std::lock_guard<std::mutex> lock(users_mutex_);
        // Double-check after re-acquiring lock
        if (users_.count(request->username())) {
            response->mutable_status()->set_code(409);
            response->mutable_status()->set_message("Username already exists");
            return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                                "Username already exists");
        }
        users_[request->username()] = std::move(info);
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

    UserInfo user_copy;
    {
        std::lock_guard<std::mutex> lock(users_mutex_);
        auto it = users_.find(request->username());
        if (it == users_.end()) {
            response->mutable_status()->set_code(401);
            response->mutable_status()->set_message("Invalid credentials");
            return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                                "Invalid credentials");
        }
        user_copy = it->second;
    }

    if (!verifyPassword(request->password(), user_copy.password_hash)) {
        response->mutable_status()->set_code(401);
        response->mutable_status()->set_message("Invalid credentials");
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                            "Invalid credentials");
    }

    auto token = generateToken();
    auto now = std::chrono::system_clock::now();
    auto expires_at = now + std::chrono::hours(kTokenTtlHours);

    {
        std::lock_guard<std::mutex> lock(tokens_mutex_);
        tokens_[token] = TokenInfo{user_copy.user_id, expires_at};
    }

    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("Login successful");
    response->set_user_id(user_copy.user_id);
    response->set_username(user_copy.username);
    response->set_token(token);
    response->set_expires_at(
        std::chrono::duration_cast<std::chrono::seconds>(
            expires_at.time_since_epoch())
            .count());

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

    TokenInfo token_info;
    {
        std::lock_guard<std::mutex> lock(tokens_mutex_);
        auto it = tokens_.find(token);
        if (it == tokens_.end()) return false;

        if (std::chrono::system_clock::now() > it->second.expires_at) {
            tokens_.erase(it);
            return false;
        }
        token_info = it->second;
    }

    {
        std::lock_guard<std::mutex> lock(users_mutex_);
        for (const auto& [uname, info] : users_) {
            if (info.user_id == token_info.user_id) {
                user_id = info.user_id;
                username = info.username;
                return true;
            }
        }
    }
    return false;
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
