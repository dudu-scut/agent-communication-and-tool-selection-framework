#include "agent_rpc/server/agent_lifecycle_service.h"
#include "agent_rpc/common/logger.h"

namespace agent_rpc { namespace server {

AgentLifecycleServiceImpl::AgentLifecycleServiceImpl(common::RedisClient* redis) : redis_(redis) {}

grpc::Status AgentLifecycleServiceImpl::SubmitFeedback(
    grpc::ServerContext*, const agent_communication::SubmitFeedbackRequest* request,
    agent_communication::SubmitFeedbackResponse* response) {
    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("OK");
    // Store in Redis for aggregation (if available)
    if (redis_) {
        std::string key = "feedback:" + request->agent_id() + ":" + request->skill_name();
        redis_->set(key, std::to_string(request->rating()));
    }
    LOG_INFO("SubmitFeedback: agent=" + request->agent_id() + " skill=" + request->skill_name() +
             " rating=" + std::to_string(request->rating()));
    return grpc::Status::OK;
}

}} // namespaces
