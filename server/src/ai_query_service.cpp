/**
 * @file ai_query_service.cpp
 * @brief AI Query Service implementation
 * 
 * Requirements: 2.1, 2.2, 2.5
 * Task 13: RPC服务扩展
 */

#include "agent_rpc/server/ai_query_service.h"
#include "agent_rpc/server/auth_interceptor.h"
#include "agent_rpc/common/logger.h"
#include "agent_rpc/common/metrics.h"
#include "agent_rpc/common/env_loader.h"
#include "agent_rpc/a2a_adapter/error_mapper.h"
#include <a2a/llm_client.hpp>
#include <a2a/client/a2a_client.hpp>
#include <nlohmann/json.hpp>
#include "agent_rpc/common/trace_context.h"
#include "agent_rpc/common/cost_tracker.h"
#include "agent_rpc/orchestrator/export_service.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <sstream>
#include <iomanip>
#include <algorithm>
#ifdef _WIN32
#include <objbase.h>
#include <rpc.h>
#pragma comment(lib, "rpcrt4.lib")
#else
#include <uuid/uuid.h>
#endif

namespace agent_rpc {
namespace server {

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

    // Initialize circuit breaker for A2A backend
    circuit_breaker_ = common::CircuitBreakerManager::getInstance()
        .getCircuitBreaker("a2a_backend");

    // P4-4: Initialize multi-agent orchestrator if LLM_API_KEY is set
    const char* api_key_env = std::getenv("LLM_API_KEY");
    if (api_key_env && api_key_env[0] != '\0') {
        std::string api_key(api_key_env);
        std::string model = agent_rpc::common::envOrDefault("LLM_MODEL", "deepseek-v4-pro");
        std::string api_url = agent_rpc::common::envOrDefault("LLM_API_URL", "https://api.deepseek.com/v1/chat/completions");

        // Memory: LLM client for cross-agent summary generation
        memory_llm_client_ = std::make_unique<LLMClient>(api_key, model, api_url);

        if (initializeOrchestrator(api_key, model, api_url)) {
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

// B-03: Sanitize raw CURL errors into user-friendly messages
static std::string sanitizeErrorMessage(const std::string& msg) {
    if (msg.find("CURL error") != std::string::npos) {
        if (msg.find("Couldn't connect") != std::string::npos ||
            msg.find("couldn't connect") != std::string::npos) {
            return "Agent service is currently unreachable. Please verify the agent is running and try again later.";
        }
        if (msg.find("timeout") != std::string::npos ||
            msg.find("Timeout") != std::string::npos) {
            return "Agent service did not respond in time. Please try again later.";
        }
        if (msg.find("URL using bad") != std::string::npos ||
            msg.find("missing URL") != std::string::npos) {
            return "Invalid agent endpoint configuration. Please contact the administrator.";
        }
        return "Failed to connect to agent service. Please try again later.";
    }
    return msg;
}

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

    // Auth: reject unauthenticated requests (interceptor sets TLS auth state)
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    auto start_time = std::chrono::steady_clock::now();
    
    // Generate request ID if not provided
    std::string request_id = request->request_id();
    if (request_id.empty()) {
        request_id = generateRequestId();
    }
    
    LOG_INFO("Processing AI query: " + request_id);

    // Memory: resolve user_id from request or auth interceptor
    std::string user_id = request->user_id();
    if (user_id.empty()) {
        user_id = AuthInterceptor::currentUserId();
    }

    // Init distributed trace context for observability
    common::TraceContext::init(user_id, request->context_id());

    // Memory: build enriched request with SystemContext
    agent_communication::AIQueryRequest enriched_req = *request;
    if (!user_id.empty()) {
        enriched_req.set_user_id(user_id);
        auto sys_ctx = memory_service_->buildSystemContext(
            user_id, request->context_id(), /* agent_id will be set by router */ "");
        *enriched_req.mutable_system_context() = sys_ctx;
    }

    // Batch 5: Budget check — skip for internal/empty user_ids
    // [Batch 7 U5] Skip budget check for sandbox queries (context_id prefixed with "sandbox_")
    bool is_sandbox = request->context_id().rfind("sandbox_", 0) == 0;
    if (redis_client_ && !user_id.empty() && !is_sandbox) {
        // Estimate cost: ~100 micro-dollars per 1000 characters (rough heuristic)
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
            updateTaskStatus(request_id, "failed", "", "",
                             BudgetMiddleware::resultMessage(budget_result));
            return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                               BudgetMiddleware::resultMessage(budget_result));
        }
    }

    // P4-4: Multi-agent orchestrator path
    if (orchestrator_enabled_) {
        auto status = handleMultiAgentQuery(context, &enriched_req, response, request_id);
        // Memory: post-process response (store conversation)
        // NOTE: memory_hints not collected here — sub-agents return plain text via A2A, no structured hints
        if (!user_id.empty()) {
            std::string resp_agent = response->agent_id().empty() ? "multi-agent" : response->agent_id();
            // Detect agent switch and generate summary for future queries
            handleAgentSwitch(user_id, request->context_id(), resp_agent);
            memory_service_->appendMessage(request->context_id(),
                resp_agent, "user", request->question());
            memory_service_->appendMessage(request->context_id(),
                resp_agent, "agent", response->answer());
        }
        return status;
    }

    // Check for cancellation
    if (context->IsCancelled()) {
        updateTaskStatus(request_id, "cancelled");
        return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled");
    }

    // Track task as working
    updateTaskStatus(request_id, "working");

    // Check circuit breaker before calling A2A backend
    if (circuit_breaker_ && !circuit_breaker_->isRequestAllowed()) {
        LOG_WARN("A2A backend circuit breaker open, rejecting query: " + request_id);
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("A2A backend temporarily unavailable (circuit breaker open)");
        updateTaskStatus(request_id, "failed", "", "", "Circuit breaker open");
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "A2A backend circuit breaker open");
    }

    // Process query via A2A adapter
    // P2-2: Propagate gRPC deadline to A2A HTTP timeout
    if (context->deadline() != std::chrono::system_clock::time_point::max()) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            context->deadline() - std::chrono::system_clock::now());
        long timeout_sec = std::max(1L, static_cast<long>(remaining.count()));
        a2a_adapter_->setRequestTimeout(timeout_sec);
    }

    // Trace span
    common::TraceContext::current()->startSpan("process_query", "server");
    bool success = a2a_adapter_->processQuery(enriched_req, response);
    common::TraceContext::current()->endSpan();

    // Record circuit breaker result
    if (circuit_breaker_) {
        if (success) circuit_breaker_->recordSuccess();
        else circuit_breaker_->recordFailure();
    }

    // Ensure request_id and task_id are set in response
    response->set_request_id(request_id);
    response->set_task_id(request_id);

    // Calculate duration
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    // Record metrics
    recordMetrics("Query", duration.count(), success);

    // Memory: post-process — store hints + conversation history
    if (success && !user_id.empty()) {
        memory_service_->updateUserMemoryFromHints(
            user_id, {response->memory_hints().begin(), response->memory_hints().end()});
        // Detect agent switch for future summary generation
        handleAgentSwitch(user_id, request->context_id(), response->agent_id());
        memory_service_->appendMessage(request->context_id(),
            response->agent_id(), "user", request->question());
        memory_service_->appendMessage(request->context_id(),
            response->agent_id(), "agent", response->answer());
    }

    if (success) {
        updateTaskStatus(request_id, "completed",
                         response->agent_id(), response->agent_name());
        auto* tc = common::TraceContext::current();
        if (tc) {
            // [Batch 7 U5] Use "sandbox" component for sandbox queries (skips budget update)
            std::string component = is_sandbox ? "sandbox" : "server_query";
            common::CostTracker::instance().recordLLMCall(
                tc->traceId(), user_id, request->context_id(),
                response->agent_id(), component,
                0, 0, "unknown", duration.count());
        }
        LOG_INFO("AI query completed: " + request_id +
                " in " + std::to_string(duration.count()) + "ms");
        return grpc::Status::OK;
    } else {
        updateTaskStatus(request_id, "failed", "", "",
                         response->status().message());
        // Map the adapter's error code to proper gRPC status
        grpc::StatusCode grpc_code = a2a_adapter::ErrorMapper::mapIntToGrpcStatus(
            response->status().code());
        LOG_ERROR("AI query failed: " + request_id);
        return grpc::Status(grpc_code, sanitizeErrorMessage(response->status().message()));
    }
}

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

    // Auth: reject unauthenticated requests (interceptor sets TLS auth state)
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    auto start_time = std::chrono::steady_clock::now();
    
    std::string request_id = request->request_id();
    if (request_id.empty()) {
        request_id = generateRequestId();
    }
    
    LOG_INFO("Processing streaming AI query: " + request_id);

    // Memory: resolve user_id from request or auth interceptor
    std::string user_id = request->user_id();
    if (user_id.empty()) {
        user_id = AuthInterceptor::currentUserId();
    }

    // Init distributed trace context for observability
    common::TraceContext::init(user_id, request->context_id());

    // Memory: build enriched request with SystemContext
    agent_communication::AIQueryRequest enriched_req = *request;
    if (!user_id.empty()) {
        enriched_req.set_user_id(user_id);
        auto sys_ctx = memory_service_->buildSystemContext(
            user_id, request->context_id(), "");
        *enriched_req.mutable_system_context() = sys_ctx;
    }

    // P4-4: Multi-agent orchestrator path
    if (orchestrator_enabled_) {
        return handleMultiAgentQueryStream(context, &enriched_req, writer, request_id);
    }

    // Check circuit breaker before calling A2A backend
    if (circuit_breaker_ && !circuit_breaker_->isRequestAllowed()) {
        LOG_WARN("A2A backend circuit breaker open, rejecting streaming query: " + request_id);
        updateTaskStatus(request_id, "failed", "", "", "Circuit breaker open");
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "A2A backend circuit breaker open");
    }

    // Track task as working
    updateTaskStatus(request_id, "working");

    bool success = true;
    std::string error_message;
    std::string streamed_content;  // Memory: accumulate agent response

    // Process streaming query
    // P2-2: Propagate gRPC deadline to A2A HTTP timeout
    if (context->deadline() != std::chrono::system_clock::time_point::max()) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            context->deadline() - std::chrono::system_clock::now());
        long timeout_sec = std::max(1L, static_cast<long>(remaining.count()));
        a2a_adapter_->setRequestTimeout(timeout_sec);
    }

    // Trace span for streaming query
    common::TraceContext::current()->startSpan("process_query_stream", "server");
    a2a_adapter_->processQueryStreaming(enriched_req,
        [this, &context, &writer, &success, &error_message, &request_id, &streamed_content](
            const agent_communication::AIStreamEvent& event) {

            // Check for cancellation
            if (context->IsCancelled()) {
                success = false;
                error_message = "Request cancelled";
                updateTaskStatus(request_id, "cancelled");
                // P2-2: Cancel the downstream A2A task
                a2a_adapter_->cancelTask(request_id);
                return;
            }

            // Memory: accumulate partial content for Tier 1 storage
            if (event.event_type() == "partial") {
                streamed_content += event.content();
            }

            // Write event to stream
            if (!writer->Write(event)) {
                success = false;
                error_message = "Failed to write stream event";
            }
        });
    common::TraceContext::current()->endSpan();

    // Calculate duration
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    
    // Record metrics
    recordMetrics("QueryStream", duration.count(), success);

    // Record circuit breaker result
    if (circuit_breaker_) {
        if (success) circuit_breaker_->recordSuccess();
        else circuit_breaker_->recordFailure();
    }

    // Memory: store user question + accumulated agent response to Tier 1
    if (success && !user_id.empty()) {
        // Use "default" as agent bucket for single-agent streaming
        memory_service_->setLastAgent(request->context_id(), "default");
        memory_service_->appendMessage(request->context_id(),
            "default", "user", request->question());
        if (!streamed_content.empty()) {
            memory_service_->appendMessage(request->context_id(),
                "default", "agent", streamed_content);
        }
    }

    if (success) {
        updateTaskStatus(request_id, "completed");
        // Record LLM call cost for observability
        // [Batch 7 U5] Use "sandbox" component for sandbox queries (skips budget update)
        bool qs_is_sandbox = request->context_id().rfind("sandbox_", 0) == 0;
        std::string qs_component = qs_is_sandbox ? "sandbox" : "server_stream";
        common::CostTracker::instance().recordLLMCall(
            common::TraceContext::current() ? common::TraceContext::current()->traceId() : "",
            user_id, request->context_id(), "", qs_component,
            0, 0, "unknown", duration.count());
        LOG_INFO("Streaming AI query completed: " + request_id +
                " in " + std::to_string(duration.count()) + "ms");
        return grpc::Status::OK;
    } else {
        // Don't overwrite "cancelled" state with "failed"
        if (error_message != "Request cancelled") {
            updateTaskStatus(request_id, "failed", "", "", error_message);
        }
        LOG_ERROR("Streaming AI query failed: " + request_id +
                 " - " + error_message);
        return grpc::Status(grpc::StatusCode::INTERNAL, error_message);
    }
}

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

    // Auth: reject unauthenticated requests
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    // Check for cancellation
    if (context->IsCancelled()) {
        return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled");
    }
    
    if (request->task_id().empty() && request->context_id().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "task_id or context_id is required");
    }

    LOG_INFO("Getting query status for task: " + request->task_id());

    // Look up task status from cache
    {
        std::lock_guard<std::mutex> lock(task_status_mutex_);
        auto it = task_status_cache_.find(request->task_id());
        if (it != task_status_cache_.end()) {
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

    // Also check by context_id (all tasks under a conversation)
    if (!request->context_id().empty()) {
        std::lock_guard<std::mutex> lock(task_status_mutex_);
        for (const auto& [id, ts] : task_status_cache_) {
            auto* status = response->mutable_status();
            status->set_code(0);
            status->set_message("OK");
            response->set_task_state(ts.state);
            return grpc::Status::OK;
        }
    }

    // Task not found in cache
    auto* status = response->mutable_status();
    status->set_code(0);
    status->set_message("Task not found or expired");
    response->set_task_state("unknown");

    return grpc::Status::OK;
}

std::string AIQueryServiceImpl::generateRequestId() {
#ifdef _WIN32
    UUID uuid;
    UuidCreate(&uuid);
    RPC_CSTR szUuid = nullptr;
    UuidToStringA(&uuid, &szUuid);
    std::string uuid_str(reinterpret_cast<const char*>(szUuid));
    RpcStringFreeA(&szUuid);
    return uuid_str;
#else
    uuid_t uuid;
    uuid_generate(uuid);

    char uuid_str[37];
    uuid_unparse_lower(uuid, uuid_str);

    return std::string(uuid_str);
#endif
}

void AIQueryServiceImpl::recordMetrics(
    const std::string& method, 
    int64_t duration_ms, 
    bool success) {
    
    auto& metrics = common::Metrics::getInstance();
    metrics.recordRpcRequest("AIQueryService", method, duration_ms);
    
    if (success) {
        metrics.recordRpcResponse("AIQueryService", method, 0);
    } else {
        metrics.recordRpcError("AIQueryService", method, "Error");
    }
}

void AIQueryServiceImpl::updateTaskStatus(
    const std::string& task_id,
    const std::string& state,
    const std::string& agent_id,
    const std::string& agent_name,
    const std::string& error_msg) {

    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(task_status_mutex_);

    auto it = task_status_cache_.find(task_id);
    if (it != task_status_cache_.end()) {
        // Update existing entry
        it->second.state = state;
        it->second.updated_at = now;
        if (!agent_id.empty()) it->second.agent_id = agent_id;
        if (!agent_name.empty()) it->second.agent_name = agent_name;
        if (!error_msg.empty()) it->second.error_message = error_msg;
    } else {
        // Insert new entry
        task_status_cache_[task_id] = TaskStatus{
            task_id, state, now, now, agent_id, agent_name, error_msg
        };
    }

    // Periodic cleanup every 100 status updates
    uint64_t count = status_query_count_.fetch_add(1);
    if (count % 100 == 0 && count > 0) {
        // Inline cleanup while we hold the lock
        auto cutoff = now - std::chrono::minutes(5);
        for (auto entry = task_status_cache_.begin();
             entry != task_status_cache_.end(); ) {
            if (entry->second.updated_at < cutoff) {
                entry = task_status_cache_.erase(entry);
            } else {
                ++entry;
            }
        }
    }
}

void AIQueryServiceImpl::cleanupExpiredTasks() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::minutes(5);

    std::lock_guard<std::mutex> lock(task_status_mutex_);
    for (auto it = task_status_cache_.begin(); it != task_status_cache_.end(); ) {
        if (it->second.updated_at < cutoff) {
            it = task_status_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Multi-Agent Orchestration (P4-4)
// ============================================================================

void AIQueryServiceImpl::handleAgentSwitch(
    const std::string& user_id,
    const std::string& context_id,
    const std::string& current_agent_id) {

    if (user_id.empty() || context_id.empty() || current_agent_id.empty()) return;

    std::string last_agent = memory_service_->getLastAgent(context_id);
    if (last_agent.empty() || last_agent == current_agent_id) {
        // No switch or first call — appendMessage will update last_agent
        return;
    }

    // Agent switched — generate summary asynchronously to avoid blocking response
    LOG_INFO("Agent switch detected: " + last_agent + " → " + current_agent_id +
             " (context: " + context_id + ")");

    std::string old_history = memory_service_->getConversationHistory(
        context_id, last_agent, 20);

    if (!old_history.empty() && memory_llm_client_) {
        // Prevent concurrent summary generation for the same context
        {
            std::lock_guard<std::mutex> lock(memory_llm_mutex_);
            if (!summary_in_progress_.insert(context_id).second) {
                return;  // Another thread already generating for this context
            }
        }

        (void)std::async(std::launch::async,
            [this, context_id, old_history]() {
                std::lock_guard<std::mutex> lock(memory_llm_mutex_);
                try {
                    std::string summary = memory_llm_client_->chat(
                        "你是一个对话摘要助手。请用2-3句话简洁总结以下用户与助手的对话要点，"
                        "保留关键信息和上下文，以便下一个助手能够无缝接续对话。直接输出摘要，不要加前缀。",
                        old_history);
                    memory_service_->setCrossAgentSummary(context_id, summary);
                    LOG_INFO("Cross-agent summary generated for context: " + context_id);
                } catch (const std::exception& e) {
                    LOG_WARN("Failed to generate cross-agent summary: " + std::string(e.what()));
                }
                summary_in_progress_.erase(context_id);
            });
    }
    // last_agent updated by subsequent appendMessage
}

std::string AIQueryServiceImpl::buildMemoryContext(
    const agent_communication::AIQueryRequest* request) const {
    std::string memory_ctx;
    if (request->has_system_context()) {
        const auto& sys_ctx = request->system_context();
        if (!sys_ctx.user_memory().empty()) {
            memory_ctx += "[User Context]\n" + sys_ctx.user_memory() + "\n";
        }
        if (!sys_ctx.cross_agent_summary().empty()) {
            memory_ctx += "[Prior Context]\n" + sys_ctx.cross_agent_summary() + "\n";
        }
    }
    return memory_ctx;
}

bool AIQueryServiceImpl::initializeOrchestrator(
    const std::string& api_key,
    const std::string& model,
    const std::string& api_url) {

    try {
        // AgentRouter: skill-based routing
        agent_router_ = std::make_unique<orchestrator::AgentRouter>();
        agent_router_->initialize(orchestrator::RoutingStrategy::SKILL_MATCH);

        // Wire LLM client into AgentRouter for Tier 0 intent classification (P1-1)
        auto router_llm = std::make_unique<LLMClient>(api_key, model, api_url);
        agent_router_->setLLMClient(std::move(router_llm));

        // Wire Redis client into AgentRouter for feedback-driven routing (Batch 2)
        if (redis_client_) {
            agent_router_->setRedisClient(redis_client_);
        }

        // TaskPlanner: decides single vs multi-agent, decomposes into DAG
        // (creates its own LLMClient internally from config)
        orchestrator::TaskPlannerConfig planner_config;
        planner_config.api_key = api_key;
        planner_config.model = model;
        planner_config.api_url = api_url;
        task_planner_ = std::make_unique<orchestrator::TaskPlanner>(planner_config);

        // TaskExecutor: DAG execution engine (needs AgentRouter for skill→agent resolution)
        orchestrator::ExecutorConfig exec_config;
        exec_config.subtask_timeout_seconds = rpc_config_.timeout_seconds;
        exec_config.global_timeout_seconds = rpc_config_.timeout_seconds * 2;
        task_executor_ = std::make_unique<orchestrator::TaskExecutor>(*agent_router_, exec_config);

        // ResultAggregator: merges subtask results
        orchestrator::AggregatorConfig agg_config;
        agg_config.api_key = api_key;
        agg_config.model = model;
        agg_config.api_url = api_url;
        agg_config.default_strategy = "llm_synthesize";
        result_aggregator_ = std::make_unique<orchestrator::ResultAggregator>(agg_config);

        orchestrator_enabled_ = true;
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Orchestrator init failed: ") + e.what());
        return false;
    }
}

grpc::Status AIQueryServiceImpl::handleMultiAgentQuery(
    grpc::ServerContext* context,
    const agent_communication::AIQueryRequest* request,
    agent_communication::AIQueryResponse* response,
    const std::string& request_id) {

    // Fix #15: Propagate gRPC deadline to A2A call timeouts
    auto gpr_deadline = context->deadline();
    int effective_timeout_seconds = rpc_config_.timeout_seconds;
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

    // Pre-resolve agents for all subtasks (eliminates redundant routing in executor)
    task_planner_->resolveAgents(plan, *agent_router_);

    // Single-agent fast path: use pre-resolved agent URL to bypass redundant routing
    if (plan.is_single_agent) {
        bool success = false;

        // Try to use pre-resolved agent URL (eliminates re-routing inside adapter)
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
            // Fallback: adapter does its own routing
            success = a2a_adapter_->processQuery(*request, response);
        }

        if (success) {
            updateTaskStatus(request_id, "completed",
                             plan.single_agent_id, plan.single_agent_name);
        }
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        response->set_processing_time_ms(duration.count());
        recordMetrics("Query", duration.count(), success);
        return success ? grpc::Status::OK
                       : grpc::Status(grpc::StatusCode::INTERNAL,
                                      sanitizeErrorMessage(response->status().message()));
    }

    // Multi-agent path
    LOG_INFO("Multi-agent plan: " + std::to_string(plan.tasks.size()) + " subtasks");
    updateTaskStatus(request_id, "working");

    // Memory: build context to inject into sub-agent prompts
    std::string memory_ctx = buildMemoryContext(request);

    // Step 2: Build AgentCallFn — receives pre-resolved agent_url, sends A2A call
    auto call_agent = [this, &memory_ctx](const std::string& agent_url,
                             const std::string& prompt) -> std::string {
        // Inject memory context into sub-agent prompt
        std::string enriched_prompt = prompt;
        if (!memory_ctx.empty()) {
            enriched_prompt = memory_ctx + "\n" + prompt;
        }

        a2a::A2AClient client(agent_url);
        client.set_timeout(rpc_config_.timeout_seconds);

        a2a::AgentMessage msg = a2a::AgentMessage::create()
            .with_role(a2a::MessageRole::User)
            .with_text(enriched_prompt);

        auto params = a2a::MessageSendParams::create()
            .with_message(msg);

        auto a2a_response = client.send_message(params);
        if (a2a_response.is_task()) {
            const auto& task = a2a_response.as_task();
            for (const auto& artifact : task.artifacts()) {
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
        // Step 3: Execute DAG
        auto results = task_executor_->execute(plan, call_agent);

        // Step 4: Aggregate results
        auto aggregated = result_aggregator_->aggregate(plan, results);

        // Populate response
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

        updateTaskStatus(request_id, "completed", "", "multi-agent");
        recordMetrics("Query", duration.count(), true);

        LOG_INFO("Multi-agent query completed in " +
             std::to_string(duration.count()) + "ms (" +
             std::to_string(plan.tasks.size()) + " subtasks)");

        return grpc::Status::OK;

    } catch (const std::exception& e) {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        LOG_ERROR("Multi-agent query failed: " + request_id + " - " + e.what());
        updateTaskStatus(request_id, "failed", "", "", e.what());
        recordMetrics("Query", duration.count(), false);
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           sanitizeErrorMessage(std::string("Multi-agent orchestration failed: ") + e.what()));
    }
}

grpc::Status AIQueryServiceImpl::handleMultiAgentQueryStream(
    grpc::ServerContext* context,
    const agent_communication::AIQueryRequest* request,
    grpc::ServerWriter<agent_communication::AIStreamEvent>* writer,
    const std::string& request_id) {

    (void)context;  // TODO: use for deadline/cancellation propagation
    auto start_time = std::chrono::steady_clock::now();
    std::string question = request->question();
    std::string context_id = request->context_id();

    // Send "thinking" status event immediately so frontend gets feedback
    // during the potentially slow LLM planning call
    {
        agent_communication::AIStreamEvent thinking_event;
        thinking_event.set_event_type("status");
        thinking_event.set_content("thinking");
        thinking_event.set_task_state("planning");
        thinking_event.set_context_id(context_id);
        writer->Write(thinking_event);
    }

    // Step 1: Plan — decide single vs multi-agent
    // Wrapped in try-catch: LLM call may fail (timeout, unreachable API, etc.)
    orchestrator::ExecutionPlan plan;
    try {
        plan = task_planner_->plan(question, agent_router_->getAllSkillDescriptions());
    } catch (const std::exception& e) {
        LOG_ERROR("Planning failed for query: " + request_id + " - " + e.what());
        plan.is_single_agent = true;
    }

    // Pre-resolve agents for all subtasks
    task_planner_->resolveAgents(plan, *agent_router_);

    // Single-agent fast path: use pre-resolved agent URL to bypass redundant routing
    if (plan.is_single_agent) {
        auto write_cb = [writer](const agent_communication::AIStreamEvent& event) {
            writer->Write(event);
        };

        // Try to use pre-resolved agent URL
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
            updateTaskStatus(request_id, "failed", "", "", e.what());
            recordMetrics("QueryStream", duration.count(), false);
            return grpc::Status(grpc::StatusCode::INTERNAL,
                               sanitizeErrorMessage(std::string("Agent streaming failed: ") + e.what()));
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
        updateTaskStatus(request_id, "completed",
                         plan.single_agent_id, plan.single_agent_name);
        recordMetrics("QueryStream", duration.count(), true);
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

    updateTaskStatus(request_id, "working");

    // Memory: build context to inject into sub-agent prompts
    std::string memory_ctx = buildMemoryContext(request);

    // Step 2: AgentCallFn — receives pre-resolved agent_url, sends A2A call
    auto call_agent = [this, &memory_ctx](const std::string& agent_url,
                             const std::string& prompt) -> std::string {
        // Inject memory context into sub-agent prompt
        std::string enriched_prompt = prompt;
        if (!memory_ctx.empty()) {
            enriched_prompt = memory_ctx + "\n" + prompt;
        }

        a2a::A2AClient client(agent_url);
        client.set_timeout(rpc_config_.timeout_seconds);
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
        // Step 3: Execute with progress callback emitting stream events
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

        // Step 4: Aggregate
        auto aggregated = result_aggregator_->aggregate(plan, results);

        // Emit final answer
        agent_communication::AIStreamEvent answer_event;
        answer_event.set_event_type("partial");
        answer_event.set_content(aggregated.final_answer);
        answer_event.set_context_id(context_id);
        writer->Write(answer_event);

        // Complete
        agent_communication::AIStreamEvent complete_event;
        complete_event.set_event_type("complete");
        complete_event.set_context_id(context_id);
        if (auto* tc = common::TraceContext::current()) {
            complete_event.set_trace_summary(tc->traceSummary());
        }
        writer->Write(complete_event);

        // Memory: store user question and aggregated answer to Tier 1
        std::string user_id = request->user_id();
        if (!user_id.empty()) {
            handleAgentSwitch(user_id, context_id, "multi-agent");
            memory_service_->appendMessage(context_id, "multi-agent", "user", question);
            memory_service_->appendMessage(context_id, "multi-agent", "agent", aggregated.final_answer);
        }

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        updateTaskStatus(request_id, "completed", "", "multi-agent");
        recordMetrics("QueryStream", duration.count(), true);

        LOG_INFO("Multi-agent stream completed in " +
                 std::to_string(duration.count()) + "ms (" +
                 std::to_string(plan.tasks.size()) + " subtasks)");

        return grpc::Status::OK;

    } catch (const std::exception& e) {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        LOG_ERROR("Multi-agent stream failed: " + request_id + " - " + e.what());
        updateTaskStatus(request_id, "failed", "", "", e.what());
        recordMetrics("QueryStream", duration.count(), false);

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
// Agent Metrics (Batch 2)
// ============================================================================

grpc::Status AIQueryServiceImpl::GetAgentMetrics(
    grpc::ServerContext* context,
    const agent_communication::GetAgentMetricsRequest* request,
    agent_communication::GetAgentMetricsResponse* response) {

    (void)context; // unused, reserved for future auth/deadline propagation

    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "Invalid request or response");
    }

    // Auth: reject unauthenticated requests
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    const std::string& agent_id = request->agent_id();
    if (agent_id.empty()) {
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("agent_id is required");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "agent_id is required");
    }

    LOG_INFO("GetAgentMetrics for agent: " + agent_id);

    // Read from Redis HSET: agent_metrics:{agent_id}
    std::string redis_key = "agent_metrics:" + agent_id;
    std::map<std::string, std::string> metrics_data;

    if (!redis_client_ || !redis_client_->hgetall(redis_key, metrics_data)) {
        LOG_WARN("No metrics found for agent: " + agent_id);
        auto* status = response->mutable_status();
        status->set_code(0);
        status->set_message("No metrics available for this agent");
        // Return empty metrics with OK status
        auto* metrics = response->mutable_metrics();
        metrics->set_agent_id(agent_id);
        return grpc::Status::OK;
    }

    // Populate AgentMetrics from Redis hash fields
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

    // approval_rate comes from feedback aggregation
    metrics->set_approval_rate(get_double("approval_rate"));

    auto* status = response->mutable_status();
    status->set_code(0);
    status->set_message("OK");

    return grpc::Status::OK;
}

// ============================================================================
// [Batch 4 U4] ExecutePlan — User-modified DAG Execution
// ============================================================================

grpc::Status AIQueryServiceImpl::ExecutePlan(
    grpc::ServerContext* context,
    const agent_communication::ExecutePlanRequest* request,
    agent_communication::ExecutePlanResponse* response) {

    if (!isAvailable()) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                           "AI Query Service not available");
    }

    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "Invalid request or response");
    }

    // Auth: reject unauthenticated requests
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    // Check for cancellation
    if (context->IsCancelled()) {
        return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled");
    }

    // Orchestrator must be enabled for DAG execution
    if (!orchestrator_enabled_) {
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("Multi-agent orchestrator is not enabled");
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                           "Multi-agent orchestrator is not enabled");
    }

    std::string trace_id = generateRequestId();
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
    if (!user_id.empty()) {
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
        client.set_timeout(rpc_config_.timeout_seconds);

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
        // Resolve agents from the DAG — use agent_id directly as URL
        for (auto& task : plan.tasks) {
            if (!task.preferred_agent_id.empty()) {
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
// Batch 6: Export Conversation
// ============================================================================

grpc::Status AIQueryServiceImpl::ExportConversation(
    grpc::ServerContext* context,
    const agent_communication::ExportConversationRequest* request,
    agent_communication::ExportConversationResponse* response) {

    (void)context; // unused, reserved for future auth/deadline propagation

    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "Invalid request or response");
    }

    // Auth: reject unauthenticated requests
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    const std::string& context_id = request->context_id();
    if (context_id.empty()) {
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("context_id is required");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "context_id is required");
    }

    LOG_INFO("ExportConversation: context=" + context_id +
             " format=" + request->format());

    // Generate Markdown conversation
    std::string markdown = orchestrator::ExportService::toMarkdown(context_id);

    // Determine output format
    std::string format = request->format();
    if (format.empty()) {
        format = "markdown"; // default
    }

    std::string file_content;
    std::string mime_type;

    if (format == "html") {
        file_content = orchestrator::ExportService::toHTML(markdown);
        mime_type = "text/html; charset=utf-8";
    } else {
        // Default: Markdown
        file_content = markdown;
        mime_type = "text/markdown; charset=utf-8";
    }

    response->set_file_data(file_content);
    response->set_mime_type(mime_type);

    auto* status = response->mutable_status();
    status->set_code(0);
    status->set_message("OK");

    LOG_INFO("ExportConversation completed: context=" + context_id +
             " size=" + std::to_string(file_content.size()) + " bytes");
    return grpc::Status::OK;
}

// ============================================================================
// Query Replay (Batch 5)
// ============================================================================

grpc::Status AIQueryServiceImpl::ReplayQuery(
    grpc::ServerContext* context,
    const agent_communication::ReplayQueryRequest* request,
    agent_communication::ReplayQueryResponse* response) {

    (void)context; // unused, reserved for future auth/deadline propagation

    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "Invalid request or response");
    }

    // Auth: reject unauthenticated requests
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                           "Valid authentication token required");
    }

    const std::string& trace_id = request->trace_id();
    const std::string& mode = request->mode();

    if (trace_id.empty()) {
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("trace_id is required");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "trace_id is required");
    }

    LOG_INFO("ReplayQuery: trace=" + trace_id + " mode=" + mode);

    auto* status = response->mutable_status();
    status->set_code(0);
    status->set_message("OK");

    if (mode == "route") {
        auto result = orchestrator::ReplayService::replayRoute(trace_id);
        response->set_original(result.original_response);
        response->set_replayed(result.replayed_response);
    } else {
        // Default to "exact"
        auto result = orchestrator::ReplayService::replayExact(trace_id);
        response->set_original(result.original_response);
        response->set_replayed(result.replayed_response);
    }

    LOG_INFO("ReplayQuery completed: trace=" + trace_id);
    return grpc::Status::OK;
}

} // namespace server
} // namespace agent_rpc
