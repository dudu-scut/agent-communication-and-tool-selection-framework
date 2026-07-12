#include "agent_rpc/server/user_experience_service.h"
#include "agent_rpc/common/logger.h"
#include <uuid/uuid.h>

namespace agent_rpc {
namespace server {

grpc::Status UserExperienceServiceImpl::InterventionResponse(
    grpc::ServerContext*,
    const agent_communication::InterventionResponseRequest* request,
    agent_communication::InterventionResponseResponse* response) {
    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("OK");
    LOG_INFO("InterventionResponse: trace=" + request->trace_id() + " decision=" + request->decision());
    return grpc::Status::OK;
}

grpc::Status UserExperienceServiceImpl::SandboxQuery(
    grpc::ServerContext*,
    const agent_communication::SandboxQueryRequest* request,
    agent_communication::SandboxQueryResponse* response) {
    (void)request;
    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("OK");
    uuid_t uuid;
    char uuid_str[37];
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuid_str);
    response->set_result("sandbox_" + std::string(uuid_str));
    LOG_INFO("SandboxQuery: ctx=" + std::string(uuid_str));
    return grpc::Status::OK;
}

} // namespace server
} // namespace agent_rpc
