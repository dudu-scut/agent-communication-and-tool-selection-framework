#include "agent_rpc/server/auth_interceptor.h"
#include "agent_rpc/server/auth_service.h"
#include "agent_rpc/common/logger.h"

namespace agent_rpc {
namespace server {

thread_local AuthInterceptor::AuthContext AuthInterceptor::tls_auth_;

AuthInterceptor::AuthInterceptor(AuthServiceImpl* auth_service,
                                  grpc::ServerContextBase* context,
                                  const std::string& method_path)
    : auth_service_(auth_service), context_(context), method_path_(method_path) {}

void AuthInterceptor::Intercept(
    grpc::experimental::InterceptorBatchMethods* methods) {

    if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {

        // Reset thread-local auth state for each new RPC
        tls_auth_ = AuthContext{};

        if (!isWhitelisted(method_path_)) {
            auto* metadata = methods->GetRecvInitialMetadata();
            if (metadata) {
                std::string token = extractBearerToken(*metadata);
                if (!token.empty()) {
                    std::string user_id, username;
                    if (auth_service_->validateToken(token, user_id, username)) {
                        tls_auth_.authenticated = true;
                        tls_auth_.user_id = user_id;
                        tls_auth_.username = username;
                    }
                }
            }
        } else {
            // Whitelisted methods are always considered authenticated
            tls_auth_.authenticated = true;
        }
    }

    methods->Proceed();
}

bool AuthInterceptor::isWhitelisted(const std::string& method) {
    return method == "/agent_communication.auth.UserService/Register" ||
           method == "/agent_communication.auth.UserService/Login" ||
           method == "/agent_communication.auth.UserService/ValidateToken" ||
           method == "/grpc.health.v1.Health/Check" ||
           method == "/grpc.health.v1.Health/Watch" ||
           method == "/agent_communication.HealthService/Check" ||
           method == "/agent_communication.HealthService/Watch" ||
           method == "/agent_communication.AgentCommunicationService/RegisterAgent" ||
           method == "/agent_communication.AgentCommunicationService/UnregisterAgent" ||
           method == "/agent_communication.AgentCommunicationService/Heartbeat";
}

const AuthInterceptor::AuthContext& AuthInterceptor::currentAuth() {
    return tls_auth_;
}

bool AuthInterceptor::isAuthenticated() {
    return tls_auth_.authenticated;
}

std::string AuthInterceptor::currentUserId() {
    return tls_auth_.user_id;
}

std::string AuthInterceptor::extractBearerToken(
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata) {
    auto it = metadata.find("authorization");
    if (it == metadata.end()) return "";

    std::string value(it->second.data(), it->second.size());
    const std::string prefix = "Bearer ";
    if (value.rfind(prefix, 0) == 0) {
        return value.substr(prefix.size());
    }
    return value;
}

}  // namespace server
}  // namespace agent_rpc
