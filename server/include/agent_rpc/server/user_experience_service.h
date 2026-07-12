#pragma once

#include "user_experience.grpc.pb.h"
#include "user_experience.pb.h"

namespace agent_rpc {
namespace server {

class UserExperienceServiceImpl final : public agent_communication::UserExperienceService::Service {
public:
    UserExperienceServiceImpl() = default;
    ~UserExperienceServiceImpl() override = default;

    grpc::Status InterventionResponse(
        grpc::ServerContext* context,
        const agent_communication::InterventionResponseRequest* request,
        agent_communication::InterventionResponseResponse* response) override;

    grpc::Status SandboxQuery(
        grpc::ServerContext* context,
        const agent_communication::SandboxQueryRequest* request,
        agent_communication::SandboxQueryResponse* response) override;
};

} // namespace server
} // namespace agent_rpc
