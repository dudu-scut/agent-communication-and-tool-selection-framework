/**
 * @file multi_agent_handler.h
 * @brief Multi-agent orchestration query handling (extracted from ai_query_service.cpp)
 *
 * Contains: handleMultiAgentQuery, handleMultiAgentQueryStream, initializeOrchestrator
 */

#pragma once

#include "agent_rpc/common/types.h"
#include "agent_rpc/a2a_adapter/a2a_adapter.h"
#include "agent_rpc/orchestrator/agent_router.h"
#include "agent_rpc/orchestrator/task_planner.h"
#include "agent_rpc/orchestrator/task_executor.h"
#include "agent_rpc/orchestrator/result_aggregator.h"

#include <grpcpp/grpcpp.h>
#include <functional>
#include <memory>
#include <string>

namespace agent_communication {
class AIQueryRequest;
class AIQueryResponse;
class AIStreamEvent;
}

namespace agent_rpc {
namespace common { class RedisClient; }
namespace server {

/**
 * @brief Handles multi-agent query orchestration
 *
 * Encapsulates the logic for planning, executing, and aggregating
 * multi-agent queries (both sync and streaming).
 */
class MultiAgentHandler {
public:
    using StatusUpdateFn = std::function<void(const std::string& task_id,
                                               const std::string& state,
                                               const std::string& agent_id,
                                               const std::string& agent_name,
                                               const std::string& error_msg)>;
    using MetricsRecordFn = std::function<void(const std::string& method,
                                                int64_t duration_ms,
                                                bool success)>;

    MultiAgentHandler(
        orchestrator::TaskPlanner* planner,
        orchestrator::AgentRouter* router,
        orchestrator::TaskExecutor* executor,
        orchestrator::ResultAggregator* aggregator,
        a2a_adapter::A2AAdapter* adapter,
        common::RpcConfig* config);

    void setCallbacks(StatusUpdateFn status_fn, MetricsRecordFn metrics_fn);

    /**
     * @brief Static factory: create and initialize orchestrator components
     */
    static bool initializeOrchestrator(
        const std::string& api_key,
        const std::string& model,
        const std::string& api_url,
        common::RedisClient* redis_client,
        const common::RpcConfig& rpc_config,
        std::unique_ptr<orchestrator::AgentRouter>& out_router,
        std::unique_ptr<orchestrator::TaskPlanner>& out_planner,
        std::unique_ptr<orchestrator::TaskExecutor>& out_executor,
        std::unique_ptr<orchestrator::ResultAggregator>& out_aggregator);

    /**
     * @brief Handle a synchronous multi-agent query
     */
    grpc::Status handleQuery(
        grpc::ServerContext* context,
        const agent_communication::AIQueryRequest* request,
        agent_communication::AIQueryResponse* response,
        const std::string& request_id);

    /**
     * @brief Handle a streaming multi-agent query
     */
    grpc::Status handleQueryStream(
        grpc::ServerContext* context,
        const agent_communication::AIQueryRequest* request,
        grpc::ServerWriter<agent_communication::AIStreamEvent>* writer,
        const std::string& request_id);

private:
    orchestrator::TaskPlanner* task_planner_;
    orchestrator::AgentRouter* agent_router_;
    orchestrator::TaskExecutor* task_executor_;
    orchestrator::ResultAggregator* result_aggregator_;
    a2a_adapter::A2AAdapter* a2a_adapter_;
    common::RpcConfig* rpc_config_;

    StatusUpdateFn update_status_;
    MetricsRecordFn record_metrics_;

    orchestrator::ExecutionPlan planQuery(const std::string& question);
    std::function<std::string(const std::string&, const std::string&)>
        buildCallAgent(const agent_communication::AIQueryRequest* request);
};

} // namespace server
} // namespace agent_rpc
