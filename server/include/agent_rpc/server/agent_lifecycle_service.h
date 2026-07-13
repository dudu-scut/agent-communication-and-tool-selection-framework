#pragma once
#include "agent_lifecycle.grpc.pb.h"
#include "agent_lifecycle.pb.h"
#include "agent_rpc/common/redis_client.h"

namespace agent_rpc { namespace server {

class AgentLifecycleServiceImpl final : public agent_communication::AgentLifecycleService::Service {
public:
    explicit AgentLifecycleServiceImpl(common::RedisClient* redis);
    ~AgentLifecycleServiceImpl() override = default;

    grpc::Status SubmitFeedback(
        grpc::ServerContext* context,
        const agent_communication::SubmitFeedbackRequest* request,
        agent_communication::SubmitFeedbackResponse* response) override;

    grpc::Status GetAgentCompare(
        grpc::ServerContext* context,
        const agent_communication::GetAgentCompareRequest* request,
        agent_communication::GetAgentCompareResponse* response) override;

    grpc::Status SetAutonomyLevel(
        grpc::ServerContext* context,
        const agent_communication::SetAutonomyLevelRequest* request,
        agent_communication::SetAutonomyLevelResponse* response) override;

    grpc::Status UndoAction(
        grpc::ServerContext* context,
        const agent_communication::UndoActionRequest* request,
        agent_communication::UndoActionResponse* response) override;

private:
    common::RedisClient* redis_;
};

}} // namespaces
