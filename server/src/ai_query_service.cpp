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
#include "agent_rpc/a2a_adapter/error_mapper.h"
#include <a2a/llm_client.hpp>
#include <a2a/client/a2a_client.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <iomanip>
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
    const a2a_adapter::A2AConfig& a2a_config) {
    
    if (initialized_) {
        return true;
    }
    
    rpc_config_ = rpc_config;
    
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
        std::string model = std::getenv("LLM_MODEL") ? std::getenv("LLM_MODEL") : "deepseek-v4-pro";
        std::string api_url = std::getenv("LLM_API_URL") ? std::getenv("LLM_API_URL")
            : "https://api.deepseek.com/v1/chat/completions";
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

    // Memory: build enriched request with SystemContext
    agent_communication::AIQueryRequest enriched_req = *request;
    if (!user_id.empty()) {
        enriched_req.set_user_id(user_id);
        auto sys_ctx = memory_service_.buildSystemContext(
            user_id, request->context_id(), /* agent_id will be set by router */ "");
        *enriched_req.mutable_system_context() = sys_ctx;
    }

    // P4-4: Multi-agent orchestrator path
    if (orchestrator_enabled_) {
        auto status = handleMultiAgentQuery(context, &enriched_req, response, request_id);
        // Memory: post-process response (store hints + conversation)
        if (!user_id.empty()) {
            memory_service_.updateUserMemoryFromHints(
                user_id, {response->memory_hints().begin(), response->memory_hints().end()});
            memory_service_.appendMessage(request->context_id(),
                response->agent_id(), "user", request->question());
            memory_service_.appendMessage(request->context_id(),
                response->agent_id(), "agent", response->answer());
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

    bool success = a2a_adapter_->processQuery(enriched_req, response);

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
        memory_service_.updateUserMemoryFromHints(
            user_id, {response->memory_hints().begin(), response->memory_hints().end()});
        memory_service_.appendMessage(request->context_id(),
            response->agent_id(), "user", request->question());
        memory_service_.appendMessage(request->context_id(),
            response->agent_id(), "agent", response->answer());
    }

    if (success) {
        updateTaskStatus(request_id, "completed",
                         response->agent_id(), response->agent_name());
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
        return grpc::Status(grpc_code, response->status().message());
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

    // Memory: build enriched request with SystemContext
    agent_communication::AIQueryRequest enriched_req = *request;
    if (!user_id.empty()) {
        enriched_req.set_user_id(user_id);
        auto sys_ctx = memory_service_.buildSystemContext(
            user_id, request->context_id(), "");
        *enriched_req.mutable_system_context() = sys_ctx;
    }

    // P4-4: Multi-agent orchestrator path
    if (orchestrator_enabled_) {
        auto status = handleMultiAgentQueryStream(context, &enriched_req, writer, request_id);
        // Memory: append conversation history (streaming has no memory_hints)
        if (!user_id.empty()) {
            memory_service_.appendMessage(request->context_id(),
                "", "user", request->question());
        }
        return status;
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

    // Process streaming query
    // P2-2: Propagate gRPC deadline to A2A HTTP timeout
    if (context->deadline() != std::chrono::system_clock::time_point::max()) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            context->deadline() - std::chrono::system_clock::now());
        long timeout_sec = std::max(1L, static_cast<long>(remaining.count()));
        a2a_adapter_->setRequestTimeout(timeout_sec);
    }

    a2a_adapter_->processQueryStreaming(enriched_req,
        [this, &context, &writer, &success, &error_message, &request_id](
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

            // Write event to stream
            if (!writer->Write(event)) {
                success = false;
                error_message = "Failed to write stream event";
            }
        });
    
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

    // Memory: append conversation history for streaming (no memory_hints available)
    if (success && !user_id.empty()) {
        memory_service_.appendMessage(request->context_id(),
            "", "user", request->question());
    }

    if (success) {
        updateTaskStatus(request_id, "completed");
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

    (void)context;  // TODO: use for deadline propagation
    auto start_time = std::chrono::steady_clock::now();
    std::string question = request->question();

    // Step 1: Plan — decide single vs multi-agent
    auto plan = task_planner_->plan(question, agent_router_->getAllSkillDescriptions());

    // Single-agent fast path: fall back to normal A2A adapter flow
    if (plan.is_single_agent) {
        std::vector<std::string> skills;
        if (!plan.single_agent_skill.empty()) {
            skills.push_back(plan.single_agent_skill);
        }
        // Delegate to normal query with skill hint
        bool success = a2a_adapter_->processQuery(*request, response);
        if (success) {
            updateTaskStatus(request_id, "completed",
                             response->agent_id(), response->agent_name());
        }
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        response->set_processing_time_ms(duration.count());
        recordMetrics("Query", duration.count(), success);
        return success ? grpc::Status::OK
                       : grpc::Status(grpc::StatusCode::INTERNAL,
                                      response->status().message());
    }

    // Multi-agent path
    LOG_INFO("Multi-agent plan: " + std::to_string(plan.tasks.size()) + " subtasks");
    updateTaskStatus(request_id, "working");

    // Step 2: Build AgentCallFn — resolves skill → agent → A2A call
    auto call_agent = [this](const std::string& skill,
                             const std::string& prompt) -> std::string {
        auto agent = agent_router_->selectAgent(prompt, {skill});
        if (!agent.has_value()) {
            throw std::runtime_error("No agent available for skill: " + skill);
        }

        a2a::A2AClient client(agent->url);
        client.set_timeout(rpc_config_.timeout_seconds);

        a2a::AgentMessage msg = a2a::AgentMessage::create()
            .with_role(a2a::MessageRole::User)
            .with_text(prompt);

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

    // Step 1: Plan
    auto plan = task_planner_->plan(question, agent_router_->getAllSkillDescriptions());

    // Single-agent fast path
    if (plan.is_single_agent) {
        // Delegate to normal streaming
        a2a_adapter_->processQueryStreaming(*request,
            [writer](const agent_communication::AIStreamEvent& event) {
                writer->Write(event);
            });

        agent_communication::AIStreamEvent complete;
        complete.set_event_type("complete");
        complete.set_context_id(context_id);
        writer->Write(complete);

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        updateTaskStatus(request_id, "completed");
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
        plan_json["tasks"].push_back(tj);
    }

    agent_communication::AIStreamEvent plan_event;
    plan_event.set_event_type("plan");
    plan_event.set_content(plan_json.dump());
    plan_event.set_context_id(context_id);
    writer->Write(plan_event);

    updateTaskStatus(request_id, "working");

    // Step 2: AgentCallFn (same as sync path)
    auto call_agent = [this](const std::string& skill,
                             const std::string& prompt) -> std::string {
        auto agent = agent_router_->selectAgent(prompt, {skill});
        if (!agent.has_value()) {
            throw std::runtime_error("No agent available for skill: " + skill);
        }
        a2a::A2AClient client(agent->url);
        client.set_timeout(rpc_config_.timeout_seconds);
        a2a::AgentMessage msg = a2a::AgentMessage::create()
            .with_role(a2a::MessageRole::User)
            .with_text(prompt);
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
    writer->Write(complete_event);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    updateTaskStatus(request_id, "completed", "", "multi-agent");
    recordMetrics("QueryStream", duration.count(), true);

    LOG_INFO("Multi-agent stream completed in " +
             std::to_string(duration.count()) + "ms (" +
             std::to_string(plan.tasks.size()) + " subtasks)");

    return grpc::Status::OK;
}

} // namespace server
} // namespace agent_rpc
