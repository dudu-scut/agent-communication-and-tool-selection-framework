#include "agent_rpc/server/agent_lifecycle_service.h"
#include "agent_rpc/common/logger.h"
#include <json/json.h>

namespace agent_rpc { namespace server {

AgentLifecycleServiceImpl::AgentLifecycleServiceImpl(common::RedisClient* redis) : redis_(redis) {}

grpc::Status AgentLifecycleServiceImpl::SubmitFeedback(
    grpc::ServerContext*, const agent_communication::SubmitFeedbackRequest* request,
    agent_communication::SubmitFeedbackResponse* response) {
    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("OK");

    // Store in Redis hash using available hset/hget operations
    if (redis_) {
        std::string key = "feedback:" + request->agent_id() + ":" + request->skill_name();
        redis_->hset(key, "last_rating", std::to_string(request->rating()));
        redis_->hset(key, "last_updated", std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));
        // Use counter keys for likes/dislikes aggregation
        std::string counter_suffix = request->rating() >= 3 ? "likes" : "dislikes";
        std::string counter_key = key + ":" + counter_suffix;
        int64_t dummy;
        redis_->incrby(counter_key, 1, dummy);
        redis_->incrby(key + ":total", 1, dummy);
        redis_->expire(key, 86400 * 30); // 30 day TTL
    }

    LOG_INFO("SubmitFeedback: agent=" + request->agent_id() + " skill=" + request->skill_name() +
             " rating=" + std::to_string(request->rating()));
    return grpc::Status::OK;
}

grpc::Status AgentLifecycleServiceImpl::GetAgentCompare(
    grpc::ServerContext*, const agent_communication::GetAgentCompareRequest* /*request*/,
    agent_communication::GetAgentCompareResponse* response) {
    // Placeholder: returns empty comparison until agent metrics aggregation is wired up
    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("Not yet implemented — agent metrics aggregation pending");
    return grpc::Status::OK;
}

grpc::Status AgentLifecycleServiceImpl::SetAutonomyLevel(
    grpc::ServerContext*, const agent_communication::SetAutonomyLevelRequest* request,
    agent_communication::SetAutonomyLevelResponse* response) {
    // Store autonomy level in Redis
    if (redis_) {
        std::string key = "autonomy:" + request->user_id() + ":" + request->agent_id();
        redis_->set(key, std::to_string(request->level()));
        redis_->expire(key, 86400 * 365); // 1 year TTL
        response->mutable_status()->set_code(0);
        response->mutable_status()->set_message("OK");
    } else {
        response->mutable_status()->set_code(1);
        response->mutable_status()->set_message("Redis not available");
    }
    LOG_INFO("SetAutonomyLevel: user=" + request->user_id() + " agent=" + request->agent_id() +
             " level=" + std::to_string(request->level()));
    return grpc::Status::OK;
}

grpc::Status AgentLifecycleServiceImpl::UndoAction(
    grpc::ServerContext*, const agent_communication::UndoActionRequest* /*request*/,
    agent_communication::UndoActionResponse* response) {
    // Placeholder: undo requires action history persistence which is not yet implemented
    response->mutable_status()->set_code(1);
    response->mutable_status()->set_message("Undo not yet implemented — action history persistence pending");
    return grpc::Status::OK;
}

}} // namespaces
