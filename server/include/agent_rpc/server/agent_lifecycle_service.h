#pragma once
#include "agent_lifecycle.grpc.pb.h"
#include "agent_lifecycle.pb.h"
#include "agent_rpc/common/redis_client.h"

namespace agent_rpc::common {
class AgentRuntimeRepository;
class QueryDomainRepository;
}  // namespace agent_rpc::common

namespace agent_rpc { namespace server {

class AgentLifecycleServiceImpl final : public agent_communication::AgentLifecycleService::Service {
public:
    // Auth-disabled semantics (M1): SubmitFeedback requires an authenticated
    // owner — when auth is disabled there is no identity, so the RPC returns
    // UNAUTHENTICATED and no owner-less feedback is ever written.
    explicit AgentLifecycleServiceImpl(common::RedisClient* redis);
    ~AgentLifecycleServiceImpl() override = default;

    // PostgreSQL is the durable feedback store; Redis stays an optional cache.
    // Both may be null in minimal test setups (the RPC then degrades to a
    // database-independent NOT_FOUND instead of crashing).
    void setAgentRuntimeRepository(common::AgentRuntimeRepository* repository);
    void setQueryDomainRepository(common::QueryDomainRepository* repository);

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
    common::AgentRuntimeRepository* runtime_repository_ = nullptr;
    common::QueryDomainRepository* query_repository_ = nullptr;
};

}} // namespaces
