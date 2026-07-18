/**
 * @file ai_query_service.cpp
 * @brief AI Query Service �?core query methods (Query, QueryStream, GetQueryStatus, GetAgentMetrics)
 *
 * Requirements: 2.1, 2.2, 2.5
 * Task 13: RPC服务扩展
 *
 * Delegated modules:
 *   - orchestration_service_impl.cpp  �?ExecutePlan, ReplayQuery, ExportConversation
 *   - multi_agent_handler.cpp         �?multi-agent sync/stream query paths
 *   - query_helpers.cpp               �?task status, metrics, agent-switch, UUID
 */

#include "agent_rpc/server/ai_query_service.h"
#include "agent_rpc/server/auth_interceptor.h"
#include "agent_rpc/common/logger.h"
#include "agent_rpc/common/env_loader.h"
#include "agent_rpc/a2a_adapter/error_mapper.h"
#include "agent_rpc/common/trace_context.h"
#include "agent_rpc/common/cost_tracker.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>

namespace agent_rpc {
namespace server {

// ============================================================================
// Lifecycle
// ============================================================================

AIQueryServiceImpl::AIQueryServiceImpl()
    : a2a_adapter_(std::make_unique<a2a_adapter::A2AAdapter>()) {
}

AIQueryServiceImpl::~AIQueryServiceImpl() {
    shutdown();
}

bool AIQueryServiceImpl::initialize(
    const common::RpcConfig& rpc_config,
    const a2a_adapter::A2AConfig& a2a_config,
    common::RedisClient* redis) {

    if (initialized_) {
        return true;
    }

    rpc_config_ = rpc_config;
    redis_client_ = redis;

    // Initialize MemoryService (Redis-backed)
    memory_service_ = std::make_unique<common::MemoryService>(
        std::shared_ptr<common::RedisClient>(redis, [](common::RedisClient*){}));

    // Initialize A2A adapter
    if (!a2a_adapter_->initialize(a2a_config)) {
        LOG_ERROR("Failed to initialize A2A adapter");
        return false;
    }

    // Wire Redis client to adapter (required for activity feed and autonomy headers)
    if (redis) {
        a2a_adapter_->setRedisClient(
            std::shared_ptr<common::RedisClient>(redis, [](common::RedisClient*){}));
    }

    // Initialize circuit breaker for A2A backend
    circuit_breaker_ = common::CircuitBreakerManager::getInstance()
        .getCircuitBreaker("a2a_backend");

    // P4-4: Initialize multi-agent orchestrator if LLM_API_KEY is set
    const char* api_key_env = std::getenv("LLM_API_KEY");
    if (api_key_env && api_key_env[0] != '\0') {
        std::string api_key(api_key_env);
        std::string model = common::envOrDefault("LLM_MODEL", "deepseek-v4-pro");
        std::string api_url = common::envOrDefault("LLM_API_URL", "https://api.deepseek.com/v1/chat/completions");

        memory_llm_client_ = std::make_unique<LLMClient>(api_key, model, api_url);

        if (MultiAgentHandler::initializeOrchestrator(
                api_key, model, api_url, redis, rpc_config_,
                agent_router_, task_planner_, task_executor_, result_aggregator_)) {

            orchestrator_enabled_ = true;

            // Create composed modules
            multi_agent_handler_ = std::make_unique<MultiAgentHandler>(
                task_planner_.get(), agent_router_.get(),
                task_executor_.get(), result_aggregator_.get(),
                a2a_adapter_.get(), &rpc_config_);
            multi_agent_handler_->setCallbacks(
                [this](const std::string& tid, const std::string& st,
                       const std::string& aid, const std::string& an, const std::string& err) {
                    helpers_.updateTaskStatus(tid, st, aid, an, err);
                },
                [](const std::string& m, int64_t d, bool s) {
                    QueryHelpers::recordMetrics(m, d, s);
                });

            orchestration_impl_ = std::make_unique<OrchestrationServiceImpl>(
                task_planner_.get(), task_executor_.get(),
                agent_router_.get(), memory_service_.get(), &rpc_config_);

            LOG_INFO("Multi-agent orchestrator enabled (LLM: " + model + ")");
        } else {
            LOG_WARN("Multi-agent orchestrator initialization failed, falling back to single-agent mode");
        }
    }

    initialized_ = true;
    LOG_INFO("AIQueryService initialized successfully");
    return true;
}

void AIQueryServiceImpl::shutdown() {
    if (!initialized_) {
        return;
    }
    if (a2a_adapter_) {
        a2a_adapter_->shutdown();
    }
    initialized_ = false;
    LOG_INFO("AIQueryService shutdown");
}

bool AIQueryServiceImpl::isAvailable() const {
    return initialized_ && a2a_adapter_ && a2a_adapter_->isAvailable();
}

// ============================================================================
// Local helper �?sanitize CURL errors (B-03)
// ============================================================================

static std::string sanitizeErrorMessage(const std::string& msg) {
    return QueryHelpers::sanitizeErrorMessage(msg);
}

// ============================================================================
// Query �?synchronous AI query
// ============================================================================

grpc::Status AIQueryServiceImpl::Query(
    grpc::ServerContext* context,
    const agent_communication::AIQueryRequest* request,
    agent_communication::AIQueryResponse* response) {
    
    if (!isAvailable()) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, 
                           "AI Query Service not available");
    }
    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "Invalid request or response");
    }
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    auto start_time = std::chrono::steady_clock::now();
    
    std::string request_id = request->request_id();
    if (request_id.empty()) {
        request_id = QueryHelpers::generateRequestId();
    }
    
    LOG_INFO("Processing AI query: " + request_id);

    // Resolve user_id
    std::string user_id = request->user_id();
    if (user_id.empty()) {
        user_id = AuthInterceptor::currentUserId();
    }

    common::TraceContext::init(user_id, request->context_id());

    // Build enriched request with SystemContext
    agent_communication::AIQueryRequest enriched_req = *request;
    if (!user_id.empty()) {
        enriched_req.set_user_id(user_id);
        auto sys_ctx = memory_service_->buildSystemContext(
            user_id, request->context_id(), "");
        *enriched_req.mutable_system_context() = sys_ctx;
    }

    // Budget check �?skip for sandbox queries
    bool is_sandbox = request->context_id().rfind("sandbox_", 0) == 0;
    if (redis_client_ && !user_id.empty() && !is_sandbox) {
        int64_t estimated_cost = std::max<int64_t>(100,
            static_cast<int64_t>(request->question().size()) * 100 / 1000);
        auto budget_result = BudgetMiddleware::checkAndDeduct(
            redis_client_, user_id, request->context_id(), request_id, estimated_cost);
        if (budget_result != BudgetMiddleware::OK) {
            LOG_WARN("Budget check failed for " + user_id + ": " +
                     BudgetMiddleware::resultMessage(budget_result));
            auto* status = response->mutable_status();
            status->set_code(-1);
            status->set_message(BudgetMiddleware::resultMessage(budget_result));
            helpers_.updateTaskStatus(request_id, "failed", "", "",
                             BudgetMiddleware::resultMessage(budget_result));
            return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                               BudgetMiddleware::resultMessage(budget_result));
        }
    }

    // P4-4: Multi-agent orchestrator path
    if (orchestrator_enabled_) {
        auto status = multi_agent_handler_->handleQuery(
            context, &enriched_req, response, request_id);
        if (!user_id.empty()) {
            std::string resp_agent = response->agent_id().empty() ? "multi-agent" : response->agent_id();
            helpers_.handleAgentSwitch(memory_service_.get(), memory_llm_client_.get(),
                                       user_id, request->context_id(), resp_agent);
            memory_service_->appendMessage(request->context_id(),
                resp_agent, "user", request->question());
            memory_service_->appendMessage(request->context_id(),
                resp_agent, "agent", response->answer());
        }
        return status;
    }

    // Check for cancellation
    if (context->IsCancelled()) {
        helpers_.updateTaskStatus(request_id, "cancelled");
        return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled");
    }

    helpers_.updateTaskStatus(request_id, "working");

    // Circuit breaker check
    if (circuit_breaker_ && !circuit_breaker_->isRequestAllowed()) {
        LOG_WARN("A2A backend circuit breaker open, rejecting query: " + request_id);
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("A2A backend temporarily unavailable (circuit breaker open)");
        helpers_.updateTaskStatus(request_id, "failed", "", "", "Circuit breaker open");
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "A2A backend circuit breaker open");
    }

    // Propagate gRPC deadline to A2A HTTP timeout
    if (context->deadline() != std::chrono::system_clock::time_point::max()) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            context->deadline() - std::chrono::system_clock::now());
        long timeout_sec = std::max(1L, static_cast<long>(remaining.count()));
        a2a_adapter_->setRequestTimeout(timeout_sec);
    }

    // Process query via A2A adapter
    common::TraceContext::current()->startSpan("process_query", "server");
    bool success = a2a_adapter_->processQuery(enriched_req, response);
    common::TraceContext::current()->endSpan();

    if (circuit_breaker_) {
        if (success) circuit_breaker_->recordSuccess();
        else circuit_breaker_->recordFailure();
    }

    response->set_request_id(request_id);
    response->set_task_id(request_id);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    QueryHelpers::recordMetrics("Query", duration.count(), success);

    // Memory: post-process �?store hints + conversation history
    if (success && !user_id.empty()) {
        memory_service_->updateUserMemoryFromHints(
            user_id, {response->memory_hints().begin(), response->memory_hints().end()});
        helpers_.handleAgentSwitch(memory_service_.get(), memory_llm_client_.get(),
                                   user_id, request->context_id(), response->agent_id());
        memory_service_->appendMessage(request->context_id(),
            response->agent_id(), "user", request->question());
        memory_service_->appendMessage(request->context_id(),
            response->agent_id(), "agent", response->answer());
    }

    if (success) {
        helpers_.updateTaskStatus(request_id, "completed",
                         response->agent_id(), response->agent_name());
        auto* tc = common::TraceContext::current();
        if (tc) {
            std::string component = is_sandbox ? "sandbox" : "server_query";
            common::CostTracker::instance().recordLLMCall(
                tc->traceId(), user_id, request->context_id(),
                response->agent_id(), component,
                0, 0, "unknown", duration.count());
        }
        LOG_INFO("AI query completed: " + request_id +
                " in " + std::to_string(duration.count()) + "ms");
    } else {
        helpers_.updateTaskStatus(request_id, "failed", "", "",
                         response->status().message());
        LOG_ERROR("AI query failed: " + request_id + " - " + response->status().message());
    }

    // Persist trace spans to Redis for ObservabilityService::GetTraceDetail
    {
        auto* trace = common::TraceContext::current();
        if (trace && redis_client_) {
            const auto& spans = trace->completedSpans();
            if (!spans.empty()) {
                std::string trace_id = trace->traceId();
                std::string redis_key = "trace:" + trace_id + ":spans";
                int64_t epoch_ms = trace->epochMs();
                auto steady_ref = trace->startSteady();
                for (const auto& span : spans) {
                    nlohmann::json j;
                    j["trace_id"] = trace_id;
                    j["span_id"] = span.span_id;
                    j["parent_span_id"] = span.parent_span_id;
                    j["component"] = span.component;
                    j["start_time"] = epoch_ms +
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            span.start_time - steady_ref).count();
                    j["end_time"] = epoch_ms +
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            span.end_time - steady_ref).count();
                    j["duration_ms"] = span.duration_ms;
                    j["status"] = span.status;
                    j["error_message"] = span.error_message;
                    j["metadata_json"] = span.metadata_json;
                    redis_client_->rpush(redis_key, j.dump());
                }
                redis_client_->expire(redis_key, 604800);  // TTL 7 days
            }
        }
    }

    if (success) {
        return grpc::Status::OK;
    } else {
        grpc::StatusCode grpc_code = a2a_adapter::ErrorMapper::mapIntToGrpcStatus(
            response->status().code());
        return grpc::Status(grpc_code, sanitizeErrorMessage(response->status().message()));
    }
}

// ============================================================================
// QueryStream �?streaming AI query
// ============================================================================

grpc::Status AIQueryServiceImpl::QueryStream(
    grpc::ServerContext* context,
    const agent_communication::AIQueryRequest* request,
    grpc::ServerWriter<agent_communication::AIStreamEvent>* writer) {
    
    if (!isAvailable()) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                           "AI Query Service not available");
    }
    if (!request || !writer) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "Invalid request or writer");
    }
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    auto start_time = std::chrono::steady_clock::now();
    
    std::string request_id = request->request_id();
    if (request_id.empty()) {
        request_id = QueryHelpers::generateRequestId();
    }
    
    LOG_INFO("Processing streaming AI query: " + request_id);

    std::string user_id = request->user_id();
    if (user_id.empty()) {
        user_id = AuthInterceptor::currentUserId();
    }

    common::TraceContext::init(user_id, request->context_id());

    agent_communication::AIQueryRequest enriched_req = *request;
    if (!user_id.empty()) {
        enriched_req.set_user_id(user_id);
        auto sys_ctx = memory_service_->buildSystemContext(
            user_id, request->context_id(), "");
        *enriched_req.mutable_system_context() = sys_ctx;
    }

    // P4-4: Multi-agent orchestrator path
    if (orchestrator_enabled_) {
        return multi_agent_handler_->handleQueryStream(
            context, &enriched_req, writer, request_id);
    }

    // Circuit breaker check
    if (circuit_breaker_ && !circuit_breaker_->isRequestAllowed()) {
        LOG_WARN("A2A backend circuit breaker open, rejecting streaming query: " + request_id);
        helpers_.updateTaskStatus(request_id, "failed", "", "", "Circuit breaker open");
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "A2A backend circuit breaker open");
    }

    helpers_.updateTaskStatus(request_id, "working");

    bool success = true;
    std::string error_message;
    std::string streamed_content;

    // Propagate gRPC deadline to A2A HTTP timeout
    if (context->deadline() != std::chrono::system_clock::time_point::max()) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            context->deadline() - std::chrono::system_clock::now());
        long timeout_sec = std::max(1L, static_cast<long>(remaining.count()));
        a2a_adapter_->setRequestTimeout(timeout_sec);
    }

    common::TraceContext::current()->startSpan("process_query_stream", "server");
    a2a_adapter_->processQueryStreaming(enriched_req,
        [this, &context, &writer, &success, &error_message, &request_id, &streamed_content](
            const agent_communication::AIStreamEvent& event) {

            if (context->IsCancelled()) {
                success = false;
                error_message = "Request cancelled";
                helpers_.updateTaskStatus(request_id, "cancelled");
                a2a_adapter_->cancelTask(request_id);
                return;
            }

            if (event.event_type() == "partial") {
                streamed_content += event.content();
            }

            if (!writer->Write(event)) {
                success = false;
                error_message = "Failed to write stream event";
            }
        });
    common::TraceContext::current()->endSpan();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    
    QueryHelpers::recordMetrics("QueryStream", duration.count(), success);

    if (circuit_breaker_) {
        if (success) circuit_breaker_->recordSuccess();
        else circuit_breaker_->recordFailure();
    }

    // Memory: store user question + accumulated agent response
    if (success && !user_id.empty()) {
        memory_service_->setLastAgent(request->context_id(), "default");
        memory_service_->appendMessage(request->context_id(),
            "default", "user", request->question());
        if (!streamed_content.empty()) {
            memory_service_->appendMessage(request->context_id(),
                "default", "agent", streamed_content);
        }
    }

    if (success) {
        helpers_.updateTaskStatus(request_id, "completed");
        bool qs_is_sandbox = request->context_id().rfind("sandbox_", 0) == 0;
        std::string qs_component = qs_is_sandbox ? "sandbox" : "server_stream";
        common::CostTracker::instance().recordLLMCall(
            common::TraceContext::current() ? common::TraceContext::current()->traceId() : "",
            user_id, request->context_id(), "", qs_component,
            0, 0, "unknown", duration.count());
        LOG_INFO("Streaming AI query completed: " + request_id +
                " in " + std::to_string(duration.count()) + "ms");
    } else {
        if (error_message != "Request cancelled") {
            helpers_.updateTaskStatus(request_id, "failed", "", "", error_message);
        }
        LOG_ERROR("Streaming AI query failed: " + request_id +
                 " - " + error_message);
    }

    // Persist trace spans to Redis for ObservabilityService::GetTraceDetail
    {
        auto* trace = common::TraceContext::current();
        if (trace && redis_client_) {
            const auto& spans = trace->completedSpans();
            if (!spans.empty()) {
                std::string trace_id = trace->traceId();
                std::string redis_key = "trace:" + trace_id + ":spans";
                int64_t epoch_ms = trace->epochMs();
                auto steady_ref = trace->startSteady();
                for (const auto& span : spans) {
                    nlohmann::json j;
                    j["trace_id"] = trace_id;
                    j["span_id"] = span.span_id;
                    j["parent_span_id"] = span.parent_span_id;
                    j["component"] = span.component;
                    j["start_time"] = epoch_ms +
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            span.start_time - steady_ref).count();
                    j["end_time"] = epoch_ms +
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            span.end_time - steady_ref).count();
                    j["duration_ms"] = span.duration_ms;
                    j["status"] = span.status;
                    j["error_message"] = span.error_message;
                    j["metadata_json"] = span.metadata_json;
                    redis_client_->rpush(redis_key, j.dump());
                }
                redis_client_->expire(redis_key, 604800);  // TTL 7 days
            }
        }
    }

    if (success) {
        return grpc::Status::OK;
    } else {
        return grpc::Status(grpc::StatusCode::INTERNAL, error_message);
    }
}

// ============================================================================
// GetQueryStatus
// ============================================================================

grpc::Status AIQueryServiceImpl::GetQueryStatus(
    grpc::ServerContext* context,
    const agent_communication::QueryStatusRequest* request,
    agent_communication::QueryStatusResponse* response) {
    
    if (!isAvailable()) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                           "AI Query Service not available");
    }
    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "Invalid request or response");
    }
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }
    if (context->IsCancelled()) {
        return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled");
    }
    if (request->task_id().empty() && request->context_id().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "task_id or context_id is required");
    }

    LOG_INFO("Getting query status for task: " + request->task_id());

    {
        std::lock_guard<std::mutex> lock(helpers_.task_status_mutex);
        auto it = helpers_.task_status_cache.find(request->task_id());
        if (it != helpers_.task_status_cache.end()) {
            const auto& ts = it->second;
            auto* status = response->mutable_status();
            status->set_code(0);
            status->set_message("OK");
            response->set_task_state(ts.state);

            if (!ts.agent_id.empty()) {
                auto* hist = response->add_history();
                hist->set_message_id(ts.task_id);
                hist->set_role("agent");
                hist->set_content(ts.agent_name);
                hist->set_timestamp(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        ts.updated_at.time_since_epoch()).count());
            }
            return grpc::Status::OK;
        }
    }

    if (!request->context_id().empty()) {
        std::lock_guard<std::mutex> lock(helpers_.task_status_mutex);
        for (const auto& [id, ts] : helpers_.task_status_cache) {
            auto* status = response->mutable_status();
            status->set_code(0);
            status->set_message("OK");
            response->set_task_state(ts.state);
            return grpc::Status::OK;
        }
    }

    auto* status = response->mutable_status();
    status->set_code(0);
    status->set_message("Task not found or expired");
    response->set_task_state("unknown");
    return grpc::Status::OK;
}

// ============================================================================
// GetAgentMetrics (Batch 2)
// ============================================================================

grpc::Status AIQueryServiceImpl::GetAgentMetrics(
    grpc::ServerContext* context,
    const agent_communication::GetAgentMetricsRequest* request,
    agent_communication::GetAgentMetricsResponse* response) {

    (void)context;

    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "Invalid request or response");
    }
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    const std::string& agent_id = request->agent_id();
    if (agent_id.empty()) {
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("agent_id is required");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "agent_id is required");
    }

    LOG_INFO("GetAgentMetrics for agent: " + agent_id);

    std::string redis_key = "agent_metrics:" + agent_id;
    std::map<std::string, std::string> metrics_data;

    if (!redis_client_ || !redis_client_->hgetall(redis_key, metrics_data)) {
        LOG_WARN("No metrics found for agent: " + agent_id);
        auto* status = response->mutable_status();
        status->set_code(0);
        status->set_message("No metrics available for this agent");
        auto* metrics = response->mutable_metrics();
        metrics->set_agent_id(agent_id);
        return grpc::Status::OK;
    }

    auto* metrics = response->mutable_metrics();
    metrics->set_agent_id(agent_id);

    auto get_double = [&](const std::string& field) -> double {
        auto it = metrics_data.find(field);
        if (it != metrics_data.end() && !it->second.empty()) {
            try { return std::stod(it->second); } catch (...) {}
        }
        return 0.0;
    };
    auto get_int = [&](const std::string& field) -> int32_t {
        auto it = metrics_data.find(field);
        if (it != metrics_data.end() && !it->second.empty()) {
            try { return std::stoi(it->second); } catch (...) {}
        }
        return 0;
    };

    metrics->set_success_rate(get_double("success_rate"));
    metrics->set_avg_latency_ms(get_double("avg_latency_ms"));
    metrics->set_p95_latency_ms(get_double("p95_latency_ms"));
    metrics->set_total_requests(get_int("total_requests"));
    metrics->set_approval_rate(get_double("approval_rate"));

    auto* status = response->mutable_status();
    status->set_code(0);
    status->set_message("OK");
    return grpc::Status::OK;
}

// ============================================================================
// OrchestrationService delegation
// ============================================================================

grpc::Status AIQueryServiceImpl::ExecutePlan(
    grpc::ServerContext* context,
    const agent_communication::ExecutePlanRequest* request,
    agent_communication::ExecutePlanResponse* response) {

    if (!isAvailable()) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "AI Query Service not available");
    }
    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid request or response");
    }
    if (!orchestration_impl_) {
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("Multi-agent orchestrator is not enabled");
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                           "Multi-agent orchestrator is not enabled");
    }
    return orchestration_impl_->executePlan(context, request, response);
}

grpc::Status AIQueryServiceImpl::ReplayQuery(
    grpc::ServerContext* context,
    const agent_communication::ReplayQueryRequest* request,
    agent_communication::ReplayQueryResponse* response) {

    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid request or response");
    }
    if (orchestration_impl_) {
        return orchestration_impl_->replayQuery(context, request, response);
    }
    auto* status = response->mutable_status();
    status->set_code(-1);
    status->set_message("Orchestration service not available");
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Orchestration service not available");
}

grpc::Status AIQueryServiceImpl::ExportConversation(
    grpc::ServerContext* context,
    const agent_communication::ExportConversationRequest* request,
    agent_communication::ExportConversationResponse* response) {

    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid request or response");
    }
    if (orchestration_impl_) {
        return orchestration_impl_->exportConversation(context, request, response);
    }
    auto* status = response->mutable_status();
    status->set_code(-1);
    status->set_message("Orchestration service not available");
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Orchestration service not available");
}

} // namespace server
} // namespace agent_rpc