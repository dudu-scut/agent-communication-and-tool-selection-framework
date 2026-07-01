#pragma once

#include "user.grpc.pb.h"
#include "user.pb.h"

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace agent_rpc {
namespace server {

class AuthServiceImpl final : public agent_communication::auth::UserService::Service {
public:
    AuthServiceImpl();
    ~AuthServiceImpl() override = default;

    // gRPC RPC handlers
    grpc::Status Register(
        grpc::ServerContext* context,
        const agent_communication::auth::RegisterRequest* request,
        agent_communication::auth::RegisterResponse* response) override;

    grpc::Status Login(
        grpc::ServerContext* context,
        const agent_communication::auth::LoginRequest* request,
        agent_communication::auth::LoginResponse* response) override;

    grpc::Status ValidateToken(
        grpc::ServerContext* context,
        const agent_communication::auth::ValidateTokenRequest* request,
        agent_communication::auth::ValidateTokenResponse* response) override;

    // Internal: validate token by string (for interceptor)
    bool validateToken(const std::string& token,
                       std::string& user_id,
                       std::string& username);

private:
    static std::string generateToken();
    static std::string hashPassword(const std::string& password,
                                    const std::string& salt);
    static std::string generateSalt();
    static bool verifyPassword(const std::string& password,
                               const std::string& stored_hash);

    struct UserInfo {
        std::string user_id;
        std::string username;
        std::string display_name;
        std::string password_hash;  // "salt:sha256hex"
        std::chrono::system_clock::time_point created_at;
    };

    struct TokenInfo {
        std::string user_id;
        std::chrono::system_clock::time_point expires_at;
    };

    std::mutex users_mutex_;
    std::unordered_map<std::string, UserInfo> users_;  // username → info

    std::mutex tokens_mutex_;
    std::unordered_map<std::string, TokenInfo> tokens_;  // token → info

    static constexpr int kTokenTtlHours = 24;
};

}  // namespace server
}  // namespace agent_rpc
