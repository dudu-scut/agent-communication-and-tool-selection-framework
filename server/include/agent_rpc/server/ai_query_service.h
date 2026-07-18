/**
 * @file ai_query_service.h
 * @brief AI Query Service implementation for gRPC
 * 
 * Requirements: 2.1, 2.2, 2.5
 * Task 13: RPC服务扩展
 *
 * Architecture: This class composes three helper modules:
 *   - OrchestrationServiceImpl  (ExecutePlan, ReplayQuery, ExportConversation)
 *   - MultiAgentHandler         (multi-agent sync/stream query handling)
 *   - QueryHelpers              (task status, metrics, agent-switch, UUID)
 */

#pragma once

#include "agent_rpc/common/types.h"
#include "agent_rpc/common/logger.h"
#include "agent_rpc/common/metrics.h"
#include "agent_rpc/common/circuit_breaker.h"
#include "agent_rpc/common/memory_service.h"
#include "agent_rpc/a2a_adapter/a2a_adapter.h"
#include "agent_rpc/a2a_adapter/a2a_config.h"
#include "agent_rpc/orchestrator/agent_router.h"
#include "agent_rpc/orchestrator/task_planner.h"
#include "agent_rpc/orchestrator/task_executor.h"
#include "agent_rpc/server/budget_middleware.h"
#include "agent_rpc/orchestrator/result_aggregator.h"
#include "agent_rpc/server/orchestration_service_impl.h"
#include "agent_rpc/server/multi_agent_handler.h"
#include "agent_rpc/server/query_helpers.h"

#include "ai_query.grpc.pb.h"
#include "ai_query.pb.h"
#include "orchestration.grpc.pb.h"
#include "orchestration.pb.h"

#include <grpcpp/grpcpp.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <atomic>

namespace agent_rpc {
namespace server {

/**
 * @brief AI Query Service implementation
 * 
 * Implements the AIQueryService gRPC service, bridging RPC requests
 * to the A2A protocol via the A2AAdapter.
 *
 * Also implements OrchestrationService (ExecutePlan, ReplayQuery, ExportConversation)
 * via delegation to OrchestrationServiceImpl.
 */
class AIQueryServiceImpl final : public agent_communication::AIQueryService::Service,
                                  public agent_communication::OrchestrationService::Service {
public:
    AIQueryServiceImpl();
    ~AIQueryServiceImpl();
    
    /**
     * @brief Initialize the service with configuration
     * @param rpc_config RPC configuration
     * @param a2a_config A2A adapter configuration
     * @return true if initialization successful
     */
    bool initialize(const common::RpcConfig& rpc_config,
                   const a2a_adapter::A2AConfig& a2a_config,
                   common::RedisClient* redis = nullptr);
    
    /**
     * @brief Shutdown the service
     */
    void shutdown();
    
    /**
     * @brief Check if service is available
     */
    bool isAvailable() const;
    
    // ========================================================================
    // AIQueryService gRPC Methods
    // ========================================================================
    
    grpc::Status Query(
        grpc::ServerContext* context,
        const agent_communication::AIQueryRequest* request,
        agent_communication::AIQueryResponse* response) override;
    
    grpc::Status QueryStream(
        grpc::ServerContext* context,
        const agent_communication::AIQueryRequest* request,
        grpc::ServerWriter<agent_communication::AIStreamEvent>* writer) override;
    
    grpc::Status GetQueryStatus(
        grpc::ServerContext* context,
        const agent_communication::QueryStatusRequest* request,
        agent_communication::QueryStatusResponse* response) override;

    grpc::Status GetAgentMetrics(
        grpc::ServerContext* context,
        const agent_communication::GetAgentMetricsRequest* request,
        agent_communication::GetAgentMetricsResponse* response) override;

    // ========================================================================
    // OrchestrationService gRPC Methods (delegated to OrchestrationServiceImpl)
    // ========================================================================

    grpc::Status ExecutePlan(
        grpc::ServerContext* context,
        const agent_communication::ExecutePlanRequest* request,
        agent_communication::ExecutePlanResponse* response) override;

    grpc::Status ReplayQuery(
        grpc::ServerContext* context,
        const agent_communication::ReplayQueryRequest* request,
        agent_communication::ReplayQueryResponse* response) override;

    grpc::Status ExportConversation(
        grpc::ServerContext* context,
        const agent_communication::ExportConversationRequest* request,
        agent_communication::ExportConversationResponse* response) override;
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    a2a_adapter::A2AAdapter* getA2AAdapter() { return a2a_adapter_.get(); }

    orchestrator::AgentRouter* getAgentRouter() { return agent_router_.get(); }

    common::MemoryService* getMemoryService() { return memory_service_.get(); }

private:
    // ========================================================================
    // Core dependencies
    // ========================================================================
    std::unique_ptr<a2a_adapter::A2AAdapter> a2a_adapter_;
    std::shared_ptr<common::CircuitBreaker> circuit_breaker_;
    common::RpcConfig rpc_config_;
    std::atomic<bool> initialized_{false};
    std::unique_ptr<common::MemoryService> memory_service_;
    common::RedisClient* redis_client_ = nullptr;

    // ========================================================================
    // Multi-Agent Orchestration (P4-4)
    // ========================================================================
    std::unique_ptr<orchestrator::AgentRouter> agent_router_;
    std::unique_ptr<orchestrator::TaskPlanner> task_planner_;
    std::unique_ptr<orchestrator::TaskExecutor> task_executor_;
    std::unique_ptr<orchestrator::ResultAggregator> result_aggregator_;
    std::atomic<bool> orchestrator_enabled_{false};

    // Memory: LLM client for cross-agent summary generation
    std::unique_ptr<LLMClient> memory_llm_client_;

    // ========================================================================
    // Composed modules
    // ========================================================================
    std::unique_ptr<OrchestrationServiceImpl> orchestration_impl_;
    std::unique_ptr<MultiAgentHandler> multi_agent_handler_;
    QueryHelpers helpers_;
};

} // namespace server
} // namespace agent_rpc