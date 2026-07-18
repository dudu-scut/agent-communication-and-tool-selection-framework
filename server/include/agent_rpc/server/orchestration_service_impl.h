/**
 * @file orchestration_service_impl.h
 * @brief OrchestrationService gRPC methods (extracted from ai_query_service.cpp)
 *
 * Implements: ExecutePlan, ReplayQuery, ExportConversation
 */

#pragma once

#include "agent_rpc/common/types.h"
#include "agent_rpc/common/memory_service.h"
#include "agent_rpc/orchestrator/task_planner.h"
#include "agent_rpc/orchestrator/task_executor.h"
#include "agent_rpc/orchestrator/agent_router.h"

#include <grpcpp/grpcpp.h>

namespace agent_communication {
class ExecutePlanRequest;
class ExecutePlanResponse;
class ReplayQueryRequest;
class ReplayQueryResponse;
class ExportConversationRequest;
class ExportConversationResponse;
}

namespace agent_rpc {
namespace server {

/**
 * @brief Implements OrchestrationService gRPC methods
 *
 * This class owns the logic for DAG execution, query replay, and conversation export.
 * It is composed into AIQueryServiceImpl, which delegates the override methods here.
 */
class OrchestrationServiceImpl {
public:
    OrchestrationServiceImpl(
        orchestrator::TaskPlanner* planner,
        orchestrator::TaskExecutor* executor,
        orchestrator::AgentRouter* router,
        common::MemoryService* memory,
        common::RpcConfig* config);

    grpc::Status executePlan(
        grpc::ServerContext* context,
        const agent_communication::ExecutePlanRequest* request,
        agent_communication::ExecutePlanResponse* response);

    grpc::Status replayQuery(
        grpc::ServerContext* context,
        const agent_communication::ReplayQueryRequest* request,
        agent_communication::ReplayQueryResponse* response);

    grpc::Status exportConversation(
        grpc::ServerContext* context,
        const agent_communication::ExportConversationRequest* request,
        agent_communication::ExportConversationResponse* response);

private:
    orchestrator::TaskPlanner* task_planner_;
    orchestrator::TaskExecutor* task_executor_;
    orchestrator::AgentRouter* agent_router_;
    common::MemoryService* memory_service_;
    common::RpcConfig* rpc_config_;
};

} // namespace server
} // namespace agent_rpc
