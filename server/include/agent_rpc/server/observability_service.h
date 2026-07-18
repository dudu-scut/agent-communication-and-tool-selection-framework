/**
 * @file observability_service.h
 * @brief ObservabilityService gRPC implementation — GetTraceDetail & GetCostReport
 *
 * Reads trace spans persisted by AIQueryServiceImpl and cost data written
 * by CostTracker to serve the frontend Dashboard / Monitor views.
 */

#pragma once

#include "observability.grpc.pb.h"
#include "observability.pb.h"

#include <agent_rpc/common/redis_client.h>
#include <agent_rpc/common/logger.h>

#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>

namespace agent_rpc {
namespace server {

class ObservabilityServiceImpl final
    : public agent_communication::ObservabilityService::Service {
public:
    explicit ObservabilityServiceImpl(common::RedisClient* redis_client);
    ~ObservabilityServiceImpl() override = default;

    grpc::Status GetTraceDetail(
        grpc::ServerContext* context,
        const agent_communication::GetTraceDetailRequest* request,
        agent_communication::GetTraceDetailResponse* response) override;

    grpc::Status GetCostReport(
        grpc::ServerContext* context,
        const agent_communication::GetCostReportRequest* request,
        agent_communication::GetCostReportResponse* response) override;

private:
    common::RedisClient* redis_client_;
};

} // namespace server
} // namespace agent_rpc
