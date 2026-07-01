/**
 * @file ai_query_service.cpp
 * @brief AI Query Service implementation
 * 
 * Requirements: 2.1, 2.2, 2.5
 * Task 13: RPC服务扩展
 */

#include "agent_rpc/server/ai_query_service.h"
#include "agent_rpc/common/logger.h"
#include "agent_rpc/common/metrics.h"
#include "agent_rpc/a2a_adapter/error_mapper.h"

#include <chrono>
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
    bool success = a2a_adapter_->processQuery(*request, response);

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
    a2a_adapter_->processQueryStreaming(*request,
        [this, &context, &writer, &success, &error_message, &request_id](
            const agent_communication::AIStreamEvent& event) {

            // Check for cancellation
            if (context->IsCancelled()) {
                success = false;
                error_message = "Request cancelled";
                updateTaskStatus(request_id, "cancelled");
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

} // namespace server
} // namespace agent_rpc
