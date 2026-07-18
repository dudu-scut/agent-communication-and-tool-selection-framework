/**
 * @file multi_agent_handler.cpp
 * @brief Multi-agent query handling implementation
 *
 * Extracted from ai_query_service.cpp:
 *   - handleMultiAgentQuery (sync multi-agent path)
 *   - handleMultiAgentQueryStream (streaming multi-agent path)
 *   - initializeOrchestrator (static factory for orchestrator components)
 */

#include "agent_rpc/server/multi_agent_handler.h"
#include "agent_rpc/server/query_helpers.h"
#include "agent_rpc/common/logger.h"
#include "agent_rpc/common/env_loader.h"
#include "agent_rpc/common/trace_context.h"
#include "agent_rpc/common/memory_service.h"

#include <a2a/client/a2a_client.hpp>
#include <a2a/llm_client.hpp>
#include <nlohmann/json.hpp>

#include "ai_query.grpc.pb.h"
#include "ai_query.pb.h"

namespace agent_rpc {
namespace server {

MultiAgentHandler::MultiAgentHandler(
    orchestrator::TaskPlanner* planner,
    orchestrator::AgentRouter* router,
    orchestrator::TaskExecutor* executor,
    orchestrator::ResultAggregator* aggregator,
    a2a_adapter::A2AAdapter* adapter,
    common::RpcConfig* config)
    : task_planner_(planner)
    , agent_router_(router)
    , task_executor_(executor)
    , result_aggregator_(aggregator)
    , a2a_adapter_(adapter)
    , rpc_config_(config) {
}

void MultiAgentHandler::setCallbacks(StatusUpdateFn status_fn, MetricsRecordFn metrics_fn) {
    update_status_ = std::move(status_fn);
    record_metrics_ = std::move(metrics_fn);
}

// ============================================================================
// Static factory: create orchestrator components
// ============================================================================

bool MultiAgentHandler::initializeOrchestrator(
    const std::string& api_key,
    const std::string& model,
    const std::string& api_url,
    common::RedisClient* redis_client,
    const common::RpcConfig& rpc_config,
    std::unique_ptr<orchestrator::AgentRouter>& out_router,
    std::unique_ptr<orchestrator::TaskPlanner>& out_planner,
    std::unique_ptr<orchestrator::TaskExecutor>& out_executor,
    std::unique_ptr<orchestrator::ResultAggregator>& out_aggregator) {

    try {
        // AgentRouter: skill-based routing
        out_router = std::make_unique<orchestrator::AgentRouter>();
        out_router->initialize(orchestrator::RoutingStrategy::SKILL_MATCH);

        // Wire LLM client into AgentRouter for Tier 0 intent classification (P1-1)
        auto router_llm = std::make_unique<LLMClient>(api_key, model, api_url);
        out_router->setLLMClient(std::move(router_llm));

        // Wire Redis client for feedback-driven routing (Batch 2)
        if (redis_client) {
            out_router->setRedisClient(redis_client);
        }

        // TaskPlanner: decides single vs multi-agent, decomposes into DAG
        orchestrator::TaskPlannerConfig planner_config;
        planner_config.api_key = api_key;
        planner_config.model = model;
        planner_config.api_url = api_url;
        out_planner = std::make_unique<orchestrator::TaskPlanner>(planner_config);

        // TaskExecutor: DAG execution engine
        orchestrator::ExecutorConfig exec_config;
        exec_config.subtask_timeout_seconds = rpc_config.timeout_seconds;
        exec_config.global_timeout_seconds = rpc_config.timeout_seconds * 2;
        out_executor = std::make_unique<orchestrator::TaskExecutor>(*out_router, exec_config);

        // ResultAggregator: merges subtask results
        orchestrator::AggregatorConfig agg_config;
        agg_config.api_key = api_key;
        agg_config.model = model;
        agg_config.api_url = api_url;
        agg_config.default_strategy = "llm_synthesize";
        out_aggregator = std::make_unique<orchestrator::ResultAggregator>(agg_config);

        return true;

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Orchestrator init failed: ") + e.what());
        return false;
    }
}

// ============================================================================
// Synchronous multi-agent query
// ============================================================================

grpc::Status MultiAgentHandler::handleQuery(
    grpc::ServerContext* context,
    const agent_communication::AIQueryRequest* request,
    agent_communication::AIQueryResponse* response,
    const std::string& request_id) {

    // Fix #15: Propagate gRPC deadline to A2A call timeouts
    auto gpr_deadline = context->deadline();
    int effective_timeout_seconds = rpc_config_->timeout_seconds;
    if (gpr_deadline != std::chrono::system_clock::time_point::max()) {
        auto now = std::chrono::system_clock::now();
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            gpr_deadline - now);
        if (remaining.count() > 0 && remaining.count() < effective_timeout_seconds) {
            effective_timeout_seconds = static_cast<int>(remaining.count());
        }
    }

    auto start_time = std::chrono::steady_clock::now();
    std::string question = request->question();

    // Step 1: Plan — decide single vs multi-agent
    orchestrator::ExecutionPlan plan;
    try {
        plan = task_planner_->plan(question, agent_router_->getAllSkillDescriptions());
    } catch (const std::exception& e) {
        LOG_ERROR("Planning failed for sync query: " + request_id + " - " + e.what());
        plan.is_single_agent = true;
    }

    // Pre-resolve agents for all subtasks
    task_planner_->resolveAgents(plan, *agent_router_);

    // Single-agent fast path
    if (plan.is_single_agent) {
        bool success = false;

        std::string agent_url;
        if (!plan.single_agent_id.empty()) {
            auto agent = agent_router_->getAgent(plan.single_agent_id);
            if (agent.has_value() && agent->is_healthy) {
                agent_url = agent->url;
            }
        }

        if (!agent_url.empty()) {
            success = a2a_adapter_->processQueryDirect(*request, response, agent_url);
        } else {
            success = a2a_adapter_->processQuery(*request, response);
        }

        if (success) {
            update_status_(request_id, "completed",
                          plan.single_agent_id, plan.single_agent_name, "");
        }
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        response->set_processing_time_ms(duration.count());
        record_metrics_("Query", duration.count(), success);
        return success ? grpc::Status::OK
                       : grpc::Status(grpc::StatusCode::INTERNAL,
                                      QueryHelpers::sanitizeErrorMessage(response->status().message()));
    }

    // Multi-agent path
    LOG_INFO("Multi-agent plan: " + std::to_string(plan.tasks.size()) + " subtasks");
    update_status_(request_id, "working", "", "", "");

    std::string memory_ctx = QueryHelpers::buildMemoryContext(request);

    auto call_agent = buildCallAgent(request);

    try {
        auto results = task_executor_->execute(plan, call_agent);
        auto aggregated = result_aggregator_->aggregate(plan, results);

        response->set_request_id(request_id);
        response->set_task_id(request_id);
        response->set_answer(aggregated.final_answer);
        response->set_context_id(request->context_id());

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        response->set_processing_time_ms(duration.count());

        auto* status = response->mutable_status();
        status->set_code(0);
        status->set_message("OK");

        update_status_(request_id, "completed", "", "multi-agent", "");
        record_metrics_("Query", duration.count(), true);

        LOG_INFO("Multi-agent query completed in " +
             std::to_string(duration.count()) + "ms (" +
             std::to_string(plan.tasks.size()) + " subtasks)");

        return grpc::Status::OK;

    } catch (const std::exception& e) {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        LOG_ERROR("Multi-agent query failed: " + request_id + " - " + e.what());
        update_status_(request_id, "failed", "", "", e.what());
        record_metrics_("Query", duration.count(), false);
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           QueryHelpers::sanitizeErrorMessage(
                               std::string("Multi-agent orchestration failed: ") + e.what()));
    }
}

// ============================================================================
// Streaming multi-agent query
// ============================================================================

grpc::Status MultiAgentHandler::handleQueryStream(
    grpc::ServerContext* context,
    const agent_communication::AIQueryRequest* request,
    grpc::ServerWriter<agent_communication::AIStreamEvent>* writer,
    const std::string& request_id) {

    (void)context;
    auto start_time = std::chrono::steady_clock::now();
    std::string question = request->question();
    std::string context_id = request->context_id();

    // Send "thinking" status event
    {
        agent_communication::AIStreamEvent thinking_event;
        thinking_event.set_event_type("status");
        thinking_event.set_content("thinking");
        thinking_event.set_task_state("planning");
        thinking_event.set_context_id(context_id);
        writer->Write(thinking_event);
    }

    // Step 1: Plan
    orchestrator::ExecutionPlan plan;
    try {
        plan = task_planner_->plan(question, agent_router_->getAllSkillDescriptions());
    } catch (const std::exception& e) {
        LOG_ERROR("Planning failed for query: " + request_id + " - " + e.what());
        plan.is_single_agent = true;
    }

    task_planner_->resolveAgents(plan, *agent_router_);

    // Single-agent fast path
    if (plan.is_single_agent) {
        auto write_cb = [writer](const agent_communication::AIStreamEvent& event) {
            writer->Write(event);
        };

        std::string agent_url;
        if (!plan.single_agent_id.empty()) {
            auto agent = agent_router_->getAgent(plan.single_agent_id);
            if (agent.has_value() && agent->is_healthy) {
                agent_url = agent->url;
            }
        }

        try {
            if (!agent_url.empty()) {
                LOG_INFO("Single-agent stream: routing to " + plan.single_agent_skill +
                         " via " + agent_url);
                a2a_adapter_->processQueryStreamingDirect(*request, write_cb, agent_url);
            } else {
                LOG_INFO("Single-agent stream: no pre-resolved agent, using adapter routing");
                a2a_adapter_->processQueryStreaming(*request, write_cb);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Single-agent streaming failed: " + request_id + " - " + e.what());
            agent_communication::AIStreamEvent error_event;
            error_event.set_event_type("error");
            error_event.set_content(std::string("Agent communication failed: ") + e.what());
            error_event.set_context_id(context_id);
            writer->Write(error_event);

            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time);
            update_status_(request_id, "failed", "", "", e.what());
            record_metrics_("QueryStream", duration.count(), false);
            return grpc::Status(grpc::StatusCode::INTERNAL,
                               QueryHelpers::sanitizeErrorMessage(
                                   std::string("Agent streaming failed: ") + e.what()));
        }

        agent_communication::AIStreamEvent complete;
        complete.set_event_type("complete");
        complete.set_context_id(context_id);
        if (auto* tc = common::TraceContext::current()) {
            complete.set_trace_summary(tc->traceSummary());
        }
        writer->Write(complete);

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        update_status_(request_id, "completed",
                      plan.single_agent_id, plan.single_agent_name, "");
        record_metrics_("QueryStream", duration.count(), true);
        return grpc::Status::OK;
    }

    // Emit plan event
    nlohmann::json plan_json;
    plan_json["original_query"] = plan.original_query;
    plan_json["tasks"] = nlohmann::json::array();
    for (const auto& t : plan.tasks) {
        nlohmann::json tj;
        tj["id"] = t.id;
        tj["description"] = t.description;
        tj["skill"] = t.required_skill;
        tj["depends_on"] = t.depends_on;
        if (!t.preferred_agent_id.empty()) {
            tj["agent_id"] = t.preferred_agent_id;
            tj["agent_name"] = t.preferred_agent_name;
        }
        plan_json["tasks"].push_back(tj);
    }

    agent_communication::AIStreamEvent plan_event;
    plan_event.set_event_type("plan");
    plan_event.set_content(plan_json.dump());
    plan_event.set_context_id(context_id);
    writer->Write(plan_event);

    update_status_(request_id, "working", "", "", "");

    auto call_agent = buildCallAgent(request);

    try {
        orchestrator::ProgressCallback progress_cb =
            [writer, &context_id](const orchestrator::SubTaskEvent& event) {
                agent_communication::AIStreamEvent stream_event;
                stream_event.set_context_id(context_id);
                if (event.type == orchestrator::SubTaskEventType::START) {
                    stream_event.set_event_type("subtask_start");
                    stream_event.set_task_state(event.subtask_id);
                    stream_event.set_content(event.detail);
                } else if (event.type == orchestrator::SubTaskEventType::COMPLETE) {
                    stream_event.set_event_type("subtask_complete");
                    stream_event.set_task_state(event.subtask_id);
                    stream_event.set_content(event.detail);
                } else if (event.type == orchestrator::SubTaskEventType::FAILED) {
                    stream_event.set_event_type("subtask_complete");
                    stream_event.set_task_state(event.subtask_id);
                    stream_event.set_content("FAILED: " + event.detail);
                }
                writer->Write(stream_event);
            };

        auto results = task_executor_->execute(plan, call_agent, progress_cb);
        auto aggregated = result_aggregator_->aggregate(plan, results);

        agent_communication::AIStreamEvent answer_event;
        answer_event.set_event_type("partial");
        answer_event.set_content(aggregated.final_answer);
        answer_event.set_context_id(context_id);
        writer->Write(answer_event);

        agent_communication::AIStreamEvent complete_event;
        complete_event.set_event_type("complete");
        complete_event.set_context_id(context_id);
        if (auto* tc = common::TraceContext::current()) {
            complete_event.set_trace_summary(tc->traceSummary());
        }
        writer->Write(complete_event);

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        update_status_(request_id, "completed", "", "multi-agent", "");
        record_metrics_("QueryStream", duration.count(), true);

        LOG_INFO("Multi-agent stream completed in " +
                 std::to_string(duration.count()) + "ms (" +
                 std::to_string(plan.tasks.size()) + " subtasks)");

        return grpc::Status::OK;

    } catch (const std::exception& e) {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        LOG_ERROR("Multi-agent stream failed: " + request_id + " - " + e.what());
        update_status_(request_id, "failed", "", "", e.what());
        record_metrics_("QueryStream", duration.count(), false);

        agent_communication::AIStreamEvent error_event;
        error_event.set_event_type("error");
        error_event.set_content(std::string("Multi-agent orchestration failed: ") + e.what());
        error_event.set_context_id(context_id);
        writer->Write(error_event);

        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Multi-agent orchestration failed: ") + e.what());
    }
}

// ============================================================================
// Private helpers
// ============================================================================

std::function<std::string(const std::string&, const std::string&)>
MultiAgentHandler::buildCallAgent(const agent_communication::AIQueryRequest* request) {
    std::string memory_ctx = QueryHelpers::buildMemoryContext(request);

    return [this, memory_ctx](const std::string& agent_url,
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
}

} // namespace server
} // namespace agent_rpc
