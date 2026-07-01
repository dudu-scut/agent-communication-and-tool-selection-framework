/**
 * @file ai_query_service.h
 * @brief AI Query Service implementation for gRPC
 * 
 * Requirements: 2.1, 2.2, 2.5
 * Task 13: RPC服务扩展
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
#include "agent_rpc/orchestrator/result_aggregator.h"

#include "ai_query.grpc.pb.h"
#include "ai_query.pb.h"

#include <grpcpp/grpcpp.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <atomic>
#include <unordered_map>

namespace agent_rpc {
namespace server {

/**
 * @brief AI Query Service implementation
 * 
 * Implements the AIQueryService gRPC service, bridging RPC requests
 * to the A2A protocol via the A2AAdapter.
 */
class AIQueryServiceImpl final : public agent_communication::AIQueryService::Service {
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
    // gRPC Service Methods
    // ========================================================================
    
    /**
     * @brief Synchronous AI query
     * @param context gRPC server context
     * @param request AI query request
     * @param response AI query response
     * @return gRPC status
     */
    grpc::Status Query(
        grpc::ServerContext* context,
        const agent_communication::AIQueryRequest* request,
        agent_communication::AIQueryResponse* response) override;
    
    /**
     * @brief Streaming AI query
     * @param context gRPC server context
     * @param request AI query request
     * @param writer Stream writer for events
     * @return gRPC status
     */
    grpc::Status QueryStream(
        grpc::ServerContext* context,
        const agent_communication::AIQueryRequest* request,
        grpc::ServerWriter<agent_communication::AIStreamEvent>* writer) override;
    
    /**
     * @brief Get query status
     * @param context gRPC server context
     * @param request Status request
     * @param response Status response
     * @return gRPC status
     */
    grpc::Status GetQueryStatus(
        grpc::ServerContext* context,
        const agent_communication::QueryStatusRequest* request,
        agent_communication::QueryStatusResponse* response) override;
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    /**
     * @brief Get the A2A adapter
     */
    a2a_adapter::A2AAdapter* getA2AAdapter() { return a2a_adapter_.get(); }

    /**
     * @brief Get the AgentRouter (P0-2: for registry unification)
     * @return Pointer to AgentRouter, or nullptr if orchestrator not enabled
     */
    orchestrator::AgentRouter* getAgentRouter() { return agent_router_.get(); }

    /**
     * @brief Get the MemoryService for memory system integration
     */
    common::MemoryService* getMemoryService() { return memory_service_.get(); }

private:
    std::string generateRequestId();
    void recordMetrics(const std::string& method, int64_t duration_ms, bool success);

    // ========================================================================
    // Task Status Tracking (P2-1)
    // ========================================================================

    struct TaskStatus {
        std::string task_id;
        std::string state;       // submitted | working | completed | failed | cancelled
        std::chrono::steady_clock::time_point created_at;
        std::chrono::steady_clock::time_point updated_at;
        std::string agent_id;
        std::string agent_name;
        std::string error_message;
    };

    void updateTaskStatus(const std::string& task_id, const std::string& state,
                          const std::string& agent_id = "",
                          const std::string& agent_name = "",
                          const std::string& error_msg = "");
    void cleanupExpiredTasks();

    // Memory: detect agent switch and generate cross-agent summary
    void handleAgentSwitch(const std::string& user_id,
                           const std::string& context_id,
                           const std::string& current_agent_id);

    // Memory: build context string from request's SystemContext for sub-agent injection
    std::string buildMemoryContext(const agent_communication::AIQueryRequest* request) const;

    mutable std::mutex task_status_mutex_;
    std::unordered_map<std::string, TaskStatus> task_status_cache_;
    std::atomic<uint64_t> status_query_count_{0};
    
    std::unique_ptr<a2a_adapter::A2AAdapter> a2a_adapter_;
    std::shared_ptr<common::CircuitBreaker> circuit_breaker_;
    common::RpcConfig rpc_config_;
    std::atomic<bool> initialized_{false};
    std::atomic<uint64_t> request_counter_{0};
    std::unique_ptr<common::MemoryService> memory_service_;

    // ========================================================================
    // Multi-Agent Orchestration (P4-4)
    // ========================================================================

    bool initializeOrchestrator(const std::string& api_key,
                                const std::string& model,
                                const std::string& api_url);

    grpc::Status handleMultiAgentQuery(
        grpc::ServerContext* context,
        const agent_communication::AIQueryRequest* request,
        agent_communication::AIQueryResponse* response,
        const std::string& request_id);

    grpc::Status handleMultiAgentQueryStream(
        grpc::ServerContext* context,
        const agent_communication::AIQueryRequest* request,
        grpc::ServerWriter<agent_communication::AIStreamEvent>* writer,
        const std::string& request_id);

    std::unique_ptr<orchestrator::AgentRouter> agent_router_;
    std::unique_ptr<orchestrator::TaskPlanner> task_planner_;
    std::unique_ptr<orchestrator::TaskExecutor> task_executor_;
    std::unique_ptr<orchestrator::ResultAggregator> result_aggregator_;
    std::atomic<bool> orchestrator_enabled_{false};

    // Memory: LLM client for cross-agent summary generation
    std::unique_ptr<LLMClient> memory_llm_client_;
    std::mutex memory_llm_mutex_;
    std::set<std::string> summary_in_progress_;  // context_ids with ongoing summary generation
};

} // namespace server
} // namespace agent_rpc
