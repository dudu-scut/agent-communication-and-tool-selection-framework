#pragma once

#include <grpcpp/grpcpp.h>
#include <string>

namespace agent_rpc {
namespace server {

class AuthServiceImpl;

/**
 * @brief gRPC server interceptor for token-based authentication.
 *
 * Extracts and validates the "authorization" metadata from incoming RPCs.
 * Stores user identity in thread-local storage for downstream handlers.
 * Whitelisted methods (UserService.Register/Login, health checks,
 * agent registration/heartbeat) bypass authentication.
 *
 * Handlers call currentUserId() / isAuthenticated() to check auth state.
 */
class AuthInterceptor : public grpc::experimental::Interceptor {
public:
    AuthInterceptor(AuthServiceImpl* auth_service,
                    grpc::ServerContextBase* context,
                    const std::string& method_path);

    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override;

    // Check if a method path is exempt from authentication
    static bool isWhitelisted(const std::string& method);

    // ---- Thread-local auth state (set by interceptor, read by handlers) ----

    struct AuthContext {
        std::string user_id;
        std::string username;
        bool authenticated = false;
    };

    static const AuthContext& currentAuth();
    static bool isAuthenticated();
    static std::string currentUserId();

private:
    static std::string extractBearerToken(
        const std::multimap<grpc::string_ref, grpc::string_ref>& metadata);

    AuthServiceImpl* auth_service_;
    grpc::ServerContextBase* context_;
    std::string method_path_;

    static thread_local AuthContext tls_auth_;
};

}  // namespace server
}  // namespace agent_rpc
