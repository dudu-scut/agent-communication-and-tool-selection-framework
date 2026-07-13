#pragma once
#include "sharing.grpc.pb.h"
#include "sharing.pb.h"

namespace agent_rpc { namespace server {

class SharingServiceImpl final : public agent_communication::SharingService::Service {
public:
    SharingServiceImpl() = default;
    ~SharingServiceImpl() override = default;

    grpc::Status ShareSession(
        grpc::ServerContext* context,
        const agent_communication::ShareSessionRequest* request,
        agent_communication::ShareSessionResponse* response) override;

    grpc::Status ObserveSession(
        grpc::ServerContext* context,
        const agent_communication::ObserveSessionRequest* request,
        grpc::ServerWriter<agent_communication::AIStreamEvent>* writer) override;

    grpc::Status SaveTemplate(
        grpc::ServerContext* context,
        const agent_communication::SaveTemplateRequest* request,
        agent_communication::SaveTemplateResponse* response) override;

    grpc::Status UseTemplate(
        grpc::ServerContext* context,
        const agent_communication::UseTemplateRequest* request,
        agent_communication::UseTemplateResponse* response) override;
};

}} // namespaces
