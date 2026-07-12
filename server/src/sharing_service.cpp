#include "agent_rpc/server/sharing_service.h"
#include "agent_rpc/common/logger.h"
#include <uuid/uuid.h>

namespace agent_rpc { namespace server {

grpc::Status SharingServiceImpl::ShareSession(
    grpc::ServerContext*, const agent_communication::ShareSessionRequest* request,
    agent_communication::ShareSessionResponse* response) {
    uuid_t uuid; char uuid_str[37];
    uuid_generate(uuid); uuid_unparse_lower(uuid, uuid_str);
    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("OK");
    response->set_share_id(std::string(uuid_str));
    response->set_share_url("https://nexusai.local/share/" + std::string(uuid_str));
    LOG_INFO("ShareSession: ctx=" + request->context_id() + " mode=" + request->mode());
    return grpc::Status::OK;
}

grpc::Status SharingServiceImpl::SaveTemplate(
    grpc::ServerContext*, const agent_communication::SaveTemplateRequest* request,
    agent_communication::SaveTemplateResponse* response) {
    uuid_t uuid; char uuid_str[37];
    uuid_generate(uuid); uuid_unparse_lower(uuid, uuid_str);
    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("OK");
    response->set_template_id(std::string(uuid_str));
    LOG_INFO("SaveTemplate: name=" + request->name());
    return grpc::Status::OK;
}

grpc::Status SharingServiceImpl::UseTemplate(
    grpc::ServerContext*, const agent_communication::UseTemplateRequest* request,
    agent_communication::UseTemplateResponse* response) {
    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("OK");
    response->set_context_id("ctx-from-template-" + request->template_id());
    LOG_INFO("UseTemplate: template_id=" + request->template_id());
    return grpc::Status::OK;
}

}} // namespaces
