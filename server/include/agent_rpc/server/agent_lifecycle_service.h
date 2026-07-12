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
private:
    common::RedisClient* redis_;
};

}} // namespaces
