/**
 * @file orchestration_service_impl.cpp
 * @brief OrchestrationService gRPC method implementations
 *
 * Extracted from ai_query_service.cpp:
 *   - ExecutePlan  (Batch 4 U4: user-modified DAG execution)
 *   - ReplayQuery  (Batch 5: exact / route-only replay)
 *   - ExportConversation (Batch 6: Markdown / HTML export)
 */

#include "agent_rpc/server/orchestration_service_impl.h"
#include "agent_rpc/server/auth_interceptor.h"
#include "agent_rpc/server/query_helpers.h"
#include "agent_rpc/common/logger.h"
#include "agent_rpc/orchestrator/export_service.h"
#include "agent_rpc/orchestrator/replay_service.h"

#include <a2a/client/a2a_client.hpp>

#include "orchestration.grpc.pb.h"
#include "orchestration.pb.h"

namespace agent_rpc {
namespace server {

OrchestrationServiceImpl::OrchestrationServiceImpl(
    orchestrator::TaskPlanner* planner,
    orchestrator::TaskExecutor* executor,
    orchestrator::AgentRouter* router,
    common::MemoryService* memory,
    common::RpcConfig* config)
    : task_planner_(planner)
    , task_executor_(executor)
    , agent_router_(router)
    , memory_service_(memory)
    , rpc_config_(config) {
}

// ============================================================================
// ExecutePlan — User-modified DAG Execution (Batch 4 U4)
// ============================================================================

grpc::Status OrchestrationServiceImpl::executePlan(
    grpc::ServerContext* context,
    const agent_communication::ExecutePlanRequest* request,
    agent_communication::ExecutePlanResponse* response) {

    // Auth: reject unauthenticated requests
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    if (context->IsCancelled()) {
        return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled");
    }

    std::string trace_id = QueryHelpers::generateRequestId();
    response->set_trace_id(trace_id);

    const auto& dag = request->dag();
    if (dag.nodes_size() == 0) {
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("DAG must contain at least one node");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "DAG must contain at least one node");
    }

    LOG_INFO("ExecutePlan: " + std::to_string(dag.nodes_size()) +
             " nodes (trace: " + trace_id + ")");

    // Convert protobuf DAG → internal ExecutionPlan
    orchestrator::ExecutionPlan plan;
    plan.original_query = "ExecutePlan (user-modified DAG)";
    plan.is_single_agent = false;
    plan.tasks.clear();

    for (int i = 0; i < dag.nodes_size(); ++i) {
        const auto& node = dag.nodes(i);
        orchestrator::SubTask st;
        st.id = node.id();
        st.description = node.description();
        st.preferred_agent_id = node.agent_id();
        for (int j = 0; j < node.dependencies_size(); ++j) {
            st.depends_on.push_back(node.dependencies(j));
        }
        plan.tasks.push_back(std::move(st));
    }

    // Memory: build context
    std::string user_id = request->user_id();
    std::string memory_ctx;
    if (!user_id.empty() && memory_service_) {
        auto sys_ctx = memory_service_->buildSystemContext(
            user_id, request->context_id(), "");
        if (!sys_ctx.user_memory().empty()) {
            memory_ctx = "[User Context]\n" + sys_ctx.user_memory() + "\n";
        }
        if (!sys_ctx.cross_agent_summary().empty()) {
            memory_ctx += "[Prior Context]\n" + sys_ctx.cross_agent_summary() + "\n";
        }
    }

    // Build call_agent lambda
    auto call_agent = [this, &memory_ctx](const std::string& agent_url,
                             const std::string& prompt) -> std::string {
        std::string enriched_prompt = prompt;
        if (!memory_ctx.empty()) {
            enriched_prompt = memory_ctx + "\n" + prompt;
        }

        a2a::A2AClient client(agent_url);
        client.set_timeout(rpc_config_->timeout_seconds);

        a2a::AgentMessage msg = a2a::AgentMessage::create()
            .with_role(a2a::MessageRole::User)
            .with_text(enriched_prompt);

        auto params = a2a::MessageSendParams::create().with_message(msg);
        auto a2a_response = client.send_message(params);
        if (a2a_response.is_task()) {
            for (const auto& artifact : a2a_response.as_task().artifacts()) {
                if (artifact.content().has_value()) {
                    return artifact.content().value();
                }
            }
        } else if (a2a_response.is_message()) {
            return a2a_response.as_message().get_text();
        }
        return "";
    };

    try {
        // Resolve agents from the DAG
        for (auto& task : plan.tasks) {
            if (!task.preferred_agent_id.empty() && agent_router_) {
                auto agent = agent_router_->getAgent(task.preferred_agent_id);
                if (agent.has_value() && agent->is_healthy) {
                    // preferred_agent_id remains as-is; the call_agent
                    // lambda receives the agent_url from the executor
                }
            }
        }

        auto results = task_executor_->execute(plan, call_agent);

        auto* status = response->mutable_status();
        status->set_code(0);
        status->set_message("OK");

        LOG_INFO("ExecutePlan completed: " + trace_id +
                 " (" + std::to_string(plan.tasks.size()) + " subtasks)");
        return grpc::Status::OK;

    } catch (const std::exception& e) {
        LOG_ERROR("ExecutePlan failed: " + trace_id + " - " + e.what());
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message(std::string("DAG execution failed: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("DAG execution failed: ") + e.what());
    }
}

// ============================================================================
// ReplayQuery (Batch 5)
// ============================================================================

grpc::Status OrchestrationServiceImpl::replayQuery(
    grpc::ServerContext* context,
    const agent_communication::ReplayQueryRequest* request,
    agent_communication::ReplayQueryResponse* response) {

    (void)context;

    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    // PR-D: the owner always comes from the authenticated session. The
    // durable ReplayService loads the original trace from PostgreSQL
    // (owner-scoped; cross-owner or unknown traces are NOT_FOUND) and either
    // compares routes (mode=route, no execution) or re-executes under a NEW
    // request id (mode=exact) without ever modifying the original records.
    return orchestrator::ReplayService::handleReplayRequest(
        AuthInterceptor::currentUserId(), request, response);
}

// ============================================================================
// ExportConversation (Batch 6)
// ============================================================================

grpc::Status OrchestrationServiceImpl::exportConversation(
    grpc::ServerContext* context,
    const agent_communication::ExportConversationRequest* request,
    agent_communication::ExportConversationResponse* response) {

    (void)context;

    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    // PR-D: owner-scoped export straight from PostgreSQL conversation
    // messages; missing or foreign conversations are NOT_FOUND and HTML
    // output is fully escaped.
    return orchestrator::ExportService::handleExportRequest(
        AuthInterceptor::currentUserId(), request, response);
}

} // namespace server
} // namespace agent_rpc
