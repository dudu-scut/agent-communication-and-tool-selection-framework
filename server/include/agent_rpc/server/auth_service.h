#pragma once

#include "agent_rpc/common/redis_client.h"
#include "user.grpc.pb.h"
#include "user.pb.h"

#include <string>

namespace agent_rpc {
namespace server {

class AuthServiceImpl final : public agent_communication::auth::UserService::Service {
public:
    explicit AuthServiceImpl(common::RedisClient* redis);
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

    // Redis key helpers
    static std::string userKey(const std::string& username) {
        return "nexusai:user:" + username;
    }
    static std::string usernameIdxKey(const std::string& user_id) {
        return "nexusai:uid:" + user_id;
    }
    static std::string tokenKey(const std::string& token) {
        return "nexusai:token:" + token;
    }

    common::RedisClient* redis_;  // not owned

    static constexpr int kTokenTtlHours = 24;
    static constexpr int kTokenTtlSeconds = kTokenTtlHours * 3600;
};

}  // namespace server
}  // namespace agent_rpc
