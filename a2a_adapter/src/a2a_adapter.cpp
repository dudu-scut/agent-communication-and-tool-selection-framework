/**
 * @file a2a_adapter.cpp
 * @brief Implementation of main A2A adapter
 * 
 * Requirements: 8.1, 8.2, 8.5
 */

#include "agent_rpc/a2a_adapter/a2a_adapter.h"
#include "agent_rpc/a2a_adapter/error_mapper.h"
#include "agent_rpc/common/circuit_breaker.h"
#include "agent_rpc/common/trace_context.h"
#include "agent_rpc/common/redis_client.h"
#include "ai_query.pb.h"
#include <a2a/core/exception.hpp>
#include <nlohmann/json.hpp>
#include <chrono>

namespace agent_rpc {
namespace a2a_adapter {

// 使用 nlohmann/json
using json = nlohmann::json;

A2AAdapter::A2AAdapter()
    : request_adapter_(std::make_unique<RequestAdapter>())
    , response_adapter_(std::make_unique<ResponseAdapter>()) {
}

A2AAdapter::~A2AAdapter() {
    shutdown();
}

bool A2AAdapter::initialize(const A2AConfig& config) {
    if (initialized_) {
        return true;
    }
    
    // Validate and store configuration
    config_ = config;
    if (!config_.validate()) {
        // Configuration had invalid values, defaults were applied
        // Log warning here if logger is available
    }
    
    // Create A2A client
    try {
        a2a_client_ = std::make_unique<a2a::A2AClient>(config_.orchestrator_url);
        a2a_client_->set_timeout(config_.request_timeout_seconds);
        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        // Log error here
        return false;
    }
}

void A2AAdapter::shutdown() {
    if (!initialized_) {
        return;
    }
    
    a2a_client_.reset();
    initialized_ = false;
}

bool A2AAdapter::processQuery(
    const agent_communication::AIQueryRequest& request,
    agent_communication::AIQueryResponse* response) {
    
    if (!response) {
        return false;
    }
    
    if (!initialized_) {
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("A2A adapter not initialized");
        return false;
    }
    
    if (!a2a_client_) {
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("A2A client not available");
        return false;
    }
    
    auto start_time = std::chrono::steady_clock::now();

    // Circuit breaker: check if orchestrator is healthy before attempting call
    auto cb = common::CircuitBreakerManager::getInstance().getCircuitBreaker("a2a_orchestrator");

    try {
        // Convert RPC request to A2A format
        a2a::MessageSendParams params = request_adapter_->convertToA2A(request);

        // Check circuit breaker before making the call
        if (!cb->isRequestAllowed()) {
            auto* status = response->mutable_status();
            status->set_code(static_cast<int>(grpc::StatusCode::UNAVAILABLE));
            status->set_message("Circuit breaker is OPEN — orchestrator is unavailable");
            return false;
        }

        // [Batch 1] Inject trace headers into A2A HTTP call
        auto* trace = agent_rpc::common::TraceContext::current();
        if (trace) {
            trace->startSpan("agent_call", "a2a_adapter");
            a2a_client_->add_header("x-trace-id", trace->traceId());
            a2a_client_->add_header("x-delegation-depth", std::to_string(trace->depth()));
        }

        // [Batch 3] Inject autonomy-level header
        injectAutonomyHeader(request, "orchestrator");

        // Send message via A2A client
        a2a::A2AResponse a2a_response = a2a_client_->send_message(params);

        // [Batch 1] Record agent call result
        if (trace) {
            trace->endSpan();
            a2a_client_->clear_headers();
        }

        // Record success
        cb->recordSuccess();

        // Convert A2A response to RPC format
        response_adapter_->convertFromA2A(a2a_response, request.request_id(), "", response);

        // Calculate processing time
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        response->set_processing_time_ms(duration.count());

        // Success if we got any valid response (Task or Message)
        return true;

    } catch (const a2a::A2AException& e) {
        // Record failure to circuit breaker
        cb->recordFailure();
        // Handle A2A protocol errors via ErrorMapper
        auto* status = response->mutable_status();
        grpc::StatusCode grpc_code = ErrorMapper::mapToGrpcStatus(
            static_cast<a2a::ErrorCode>(e.error_code()));
        status->set_code(static_cast<int>(grpc_code));
        std::string error_msg = e.what();
        if (error_msg.empty()) {
            error_msg = ErrorMapper::getErrorDescription(
                static_cast<a2a::ErrorCode>(e.error_code()));
        }
        status->set_message(error_msg);
        return false;
    } catch (const std::exception& e) {
        // Record failure to circuit breaker
        cb->recordFailure();
        // Handle network and general errors via ErrorMapper
        auto* status = response->mutable_status();
        grpc::StatusCode grpc_code = ErrorMapper::mapNetworkException(e);
        status->set_code(static_cast<int>(grpc_code));
        std::string error_msg = e.what();
        if (error_msg.empty()) {
            error_msg = "Unknown error occurred while processing query";
        }
        status->set_message(error_msg);
        return false;
    }
}

void A2AAdapter::processQueryAsync(
    const agent_communication::AIQueryRequest& request,
    std::function<void(const agent_communication::AIQueryResponse&)> callback) {
    
    if (!initialized_ || !callback) {
        return;
    }
    
    // For now, implement as synchronous call
    // TODO: Implement true async with thread pool
    agent_communication::AIQueryResponse response;
    processQuery(request, &response);
    callback(response);
}

void A2AAdapter::processQueryStreaming(
    const agent_communication::AIQueryRequest& request,
    std::function<void(const agent_communication::AIStreamEvent&)> callback) {
    
    if (!initialized_ || !callback || !config_.enable_streaming) {
        return;
    }
    
    // Circuit breaker: check if orchestrator is healthy before streaming call
    auto streaming_cb = common::CircuitBreakerManager::getInstance().getCircuitBreaker("a2a_orchestrator");
    if (!streaming_cb->isRequestAllowed()) {
        agent_communication::AIStreamEvent cb_event;
        response_adapter_->buildStreamEvent(
            "Circuit breaker is OPEN — orchestrator is unavailable",
            request.context_id(), "error", &cb_event);
        callback(cb_event);
        // End streaming trace span if it was started
        auto* trace = agent_rpc::common::TraceContext::current();
        if (trace) {
            trace->endSpan();
        }
        return;
    }

    try {
        // Convert RPC request to A2A format
        a2a::MessageSendParams params = request_adapter_->convertToA2A(request);
        std::string context_id = params.context_id().value_or("");

        // Use streaming API
        // 注意：http_client按双换行符切分数据，每次回调收到完整的SSE事件
        // [Batch 1] Inject trace headers into A2A HTTP streaming call
        auto* trace = agent_rpc::common::TraceContext::current();
        if (trace) {
            trace->startSpan("agent_call_streaming", "a2a_adapter");
            a2a_client_->add_header("x-trace-id", trace->traceId());
            a2a_client_->add_header("x-delegation-depth", std::to_string(trace->depth()));
        }

        // [Batch 3] Inject autonomy-level header for streaming call
        injectAutonomyHeader(request, "orchestrator");

        a2a_client_->send_message_streaming(params, 
            [this, &callback, &context_id](const std::string& event_line) {
                // 跳过空行
                if (event_line.empty() || event_line == "\n" || event_line == "\r\n") {
                    return;
                }
                
                // 解析SSE格式: "data: {...}\n" 或 "data: {...}"
                std::string event_data = event_line;
                
                // 移除行尾换行符
                while (!event_data.empty() && 
                       (event_data.back() == '\n' || event_data.back() == '\r')) {
                    event_data.pop_back();
                }
                
                // 提取data:后面的内容
                const std::string data_prefix = "data: ";
                if (event_data.find(data_prefix) == 0) {
                    event_data = event_data.substr(data_prefix.length());
                }
                
                // 跳过空数据
                if (event_data.empty()) {
                    return;
                }
                
                // 解析 JSON 响应，捕获所有 JSON 异常（包括 UTF-8 错误）
                json j;
                try {
                    j = json::parse(event_data);
                } catch (const json::exception& e) {
                    // JSON 解析失败（包括 UTF-8 错误），跳过这个事件
                    return;
                }
                
                try {
                    // 检查是否有错误
                    if (j.contains("error")) {
                        agent_communication::AIStreamEvent event;
                        std::string error_msg = j["error"].value("message", "Unknown error");
                        response_adapter_->buildStreamEvent(
                            error_msg, context_id, "error", &event);
                        callback(event);
                        return;
                    }
                    
                    // 检查是否有结果
                    if (j.contains("result")) {
                        auto& result = j["result"];
                        std::string type = result.value("type", "");
                        
                        if (type == "chunk") {
                            // 流式内容块
                            std::string content = result.value("content", "");
                            agent_communication::AIStreamEvent event;
                            response_adapter_->buildStreamEvent(
                                content, context_id, "partial", &event);
                            callback(event);
                        } else if (type == "stream_start") {
                            // 流开始事件
                            agent_communication::AIStreamEvent event;
                            response_adapter_->buildStreamEvent(
                                "", context_id, "status", &event);
                            event.set_task_state("processing");
                            callback(event);
                        } else if (type == "stream_end") {
                            // 流结束事件 - 不在这里发送 complete，让外层处理
                        } else if (type == "intent") {
                            // 意图识别事件
                            agent_communication::AIStreamEvent event;
                            std::string intent = result.value("intent", "");
                            response_adapter_->buildStreamEvent(
                                "Intent: " + intent, context_id, "status", &event);
                            callback(event);
                        } else if (type == "status") {
                            // A2A 标准状态事件
                            if (result.contains("status")) {
                                auto& status_obj = result["status"];
                                std::string state = status_obj.value("state", "");
                                if (state == "working") {
                                    agent_communication::AIStreamEvent event;
                                    response_adapter_->buildStreamEvent(
                                        "", context_id, "status", &event);
                                    event.set_task_state("processing");
                                    callback(event);
                                } else if (state == "completed") {
                                    // 提取完成消息中的文本内容
                                    if (status_obj.contains("message")) {
                                        auto& message = status_obj["message"];
                                        if (message.contains("parts")) {
                                            std::string content;
                                            for (auto& part : message["parts"]) {
                                                if (part.value("type", "") == "text") {
                                                    content += part.value("text", "");
                                                }
                                            }
                                            if (!content.empty()) {
                                                agent_communication::AIStreamEvent event;
                                                response_adapter_->buildStreamEvent(
                                                    content, context_id, "partial", &event);
                                                callback(event);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    // 处理结果时出错，跳过
                }
            });
        
        // [Batch 1] End streaming trace span
        if (trace) {
            trace->endSpan();
            a2a_client_->clear_headers();
        }

        // Record streaming success to circuit breaker
        streaming_cb->recordSuccess();

        // Send completion event
        agent_communication::AIStreamEvent complete_event;
        response_adapter_->buildStreamEvent(
            "", context_id, "complete", &complete_event);
        callback(complete_event);

    } catch (const std::exception& e) {
        // Record streaming failure to circuit breaker
        streaming_cb->recordFailure();

        // Send error event
        agent_communication::AIStreamEvent error_event;
        response_adapter_->buildStreamEvent(
            e.what(), request.context_id(), "error", &error_event);
        callback(error_event);
    }
}

void A2AAdapter::setRedisClient(std::shared_ptr<common::RedisClient> redis) {
    redis_ = std::move(redis);
}

void A2AAdapter::injectAutonomyHeader(
    const agent_communication::AIQueryRequest& request,
    const std::string& agent_id,
    a2a::A2AClient* client) {
    if (!redis_) return;
    std::string user_id = request.user_id();
    if (user_id.empty()) return;

    // Use provided client, or fall back to the shared a2a_client_
    a2a::A2AClient* target = client ? client : a2a_client_.get();
    if (!target) return;

    std::string autonomy_key = "autonomy:" + user_id + ":" + agent_id;
    std::string level_str;
    int level = 1;
    if (redis_->get(autonomy_key, level_str)) {
        try { level = std::stoi(level_str); } catch (...) { level = 1; }
    }
    if (level > 1) {
        target->add_header("x-autonomy-level", std::to_string(level));
    }
}

bool A2AAdapter::cancelTask(const std::string& task_id) {
    if (!initialized_ || !a2a_client_ || task_id.empty()) {
        return false;
    }

    try {
        a2a_client_->cancel_task(task_id);
        return true;
    } catch (...) {
        return false;
    }
}

void A2AAdapter::setRequestTimeout(long seconds) {
    if (a2a_client_ && seconds > 0) {
        a2a_client_->set_timeout(seconds);
    }
}

bool A2AAdapter::isAvailable() const {
    return initialized_;
}

bool A2AAdapter::processQueryDirect(
    const agent_communication::AIQueryRequest& request,
    agent_communication::AIQueryResponse* response,
    const std::string& agent_url) {

    if (!response || !initialized_) {
        if (response) {
            auto* status = response->mutable_status();
            status->set_code(-1);
            status->set_message("A2A adapter not initialized");
        }
        return false;
    }

    auto start_time = std::chrono::steady_clock::now();

    // Circuit breaker: check if the target agent is healthy
    auto direct_cb = common::CircuitBreakerManager::getInstance().getCircuitBreaker("direct_agent:" + agent_url);

    try {
        a2a::MessageSendParams params = request_adapter_->convertToA2A(request);

        // Check circuit breaker before making the direct call
        if (!direct_cb->isRequestAllowed()) {
            if (response) {
                auto* status = response->mutable_status();
                status->set_code(static_cast<int>(grpc::StatusCode::UNAVAILABLE));
                status->set_message("Circuit breaker is OPEN — agent " + agent_url + " is unavailable");
            }
            return false;
        }

        a2a::A2AClient client(agent_url);
        client.set_timeout(config_.request_timeout_seconds);

        // [Batch 1] Inject trace headers into direct A2A HTTP call
        auto* trace = agent_rpc::common::TraceContext::current();
        if (trace) {
            trace->startSpan("agent_call_direct", "a2a_adapter");
            client.add_header("x-trace-id", trace->traceId());
            client.add_header("x-delegation-depth", std::to_string(trace->depth()));
        }

        // [Batch 3] Inject autonomy-level header for direct call
        injectAutonomyHeader(request, "direct_agent", &client);

        a2a::A2AResponse a2a_response = client.send_message(params);

        // [Batch 1] End trace span
        if (trace) {
            trace->endSpan();
        }

        response_adapter_->convertFromA2A(a2a_response, request.request_id(), "", response);

        // Record direct call success to circuit breaker
        direct_cb->recordSuccess();

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        response->set_processing_time_ms(duration.count());
        return true;

    } catch (const a2a::A2AException& e) {
        // Record failure to circuit breaker
        direct_cb->recordFailure();
        auto* status = response->mutable_status();
        grpc::StatusCode grpc_code = ErrorMapper::mapToGrpcStatus(
            static_cast<a2a::ErrorCode>(e.error_code()));
        status->set_code(static_cast<int>(grpc_code));
        std::string error_msg = e.what();
        if (error_msg.empty()) {
            error_msg = ErrorMapper::getErrorDescription(
                static_cast<a2a::ErrorCode>(e.error_code()));
        }
        status->set_message(error_msg);
        return false;
    } catch (const std::exception& e) {
        // Record failure to circuit breaker
        direct_cb->recordFailure();
        auto* status = response->mutable_status();
        grpc::StatusCode grpc_code = ErrorMapper::mapNetworkException(e);
        status->set_code(static_cast<int>(grpc_code));
        status->set_message(e.what());
        return false;
    }
}

void A2AAdapter::processQueryStreamingDirect(
    const agent_communication::AIQueryRequest& request,
    std::function<void(const agent_communication::AIStreamEvent&)> callback,
    const std::string& agent_url) {

    if (!initialized_ || !callback) {
        return;
    }

    // Circuit breaker: check if the target agent is healthy for streaming direct
    auto streaming_direct_cb = common::CircuitBreakerManager::getInstance().getCircuitBreaker("streaming_direct:" + agent_url);
    if (!streaming_direct_cb->isRequestAllowed()) {
        agent_communication::AIStreamEvent cb_event;
        response_adapter_->buildStreamEvent(
            "Circuit breaker is OPEN — agent " + agent_url + " is unavailable for streaming",
            request.context_id(), "error", &cb_event);
        callback(cb_event);
        return;
    }

    try {
        a2a::MessageSendParams params = request_adapter_->convertToA2A(request);
        std::string context_id = params.context_id().value_or("");

        a2a::A2AClient client(agent_url);
        client.set_timeout(config_.request_timeout_seconds);

        // [Batch 1] Inject trace headers into direct A2A HTTP streaming call
        auto* trace = agent_rpc::common::TraceContext::current();
        if (trace) {
            trace->startSpan("agent_call_streaming_direct", "a2a_adapter");
            client.add_header("x-trace-id", trace->traceId());
            client.add_header("x-delegation-depth", std::to_string(trace->depth()));
        }

        // [Batch 3] Inject autonomy-level header for streaming direct call
        injectAutonomyHeader(request, "direct_agent", &client);

        client.send_message_streaming(params,
            [this, &callback, &context_id](const std::string& event_line) {
                if (event_line.empty() || event_line == "\n" || event_line == "\r\n") {
                    return;
                }

                std::string event_data = event_line;
                while (!event_data.empty() &&
                       (event_data.back() == '\n' || event_data.back() == '\r')) {
                    event_data.pop_back();
                }

                const std::string data_prefix = "data: ";
                if (event_data.find(data_prefix) == 0) {
                    event_data = event_data.substr(data_prefix.length());
                }

                if (event_data.empty()) return;

                json j;
                try {
                    j = json::parse(event_data);
                } catch (const json::exception&) {
                    return;
                }

                try {
                    if (j.contains("error")) {
                        agent_communication::AIStreamEvent event;
                        std::string error_msg = j["error"].value("message", "Unknown error");
                        response_adapter_->buildStreamEvent(
                            error_msg, context_id, "error", &event);
                        callback(event);
                        return;
                    }

                    if (j.contains("result")) {
                        auto& result = j["result"];
                        std::string type = result.value("type", "");

                        if (type == "chunk") {
                            std::string content = result.value("content", "");
                            agent_communication::AIStreamEvent event;
                            response_adapter_->buildStreamEvent(
                                content, context_id, "partial", &event);
                            callback(event);
                        } else if (type == "stream_start") {
                            agent_communication::AIStreamEvent event;
                            response_adapter_->buildStreamEvent(
                                "", context_id, "status", &event);
                            event.set_task_state("processing");
                            callback(event);
                        } else if (type == "stream_end") {
                            // Completion handled by outer scope
                        } else if (type == "intent") {
                            agent_communication::AIStreamEvent event;
                            std::string intent = result.value("intent", "");
                            response_adapter_->buildStreamEvent(
                                "Intent: " + intent, context_id, "status", &event);
                            callback(event);
                        } else if (type == "status") {
                            // A2A 标准状态事件
                            if (result.contains("status")) {
                                auto& status_obj = result["status"];
                                std::string state = status_obj.value("state", "");
                                if (state == "working") {
                                    agent_communication::AIStreamEvent event;
                                    response_adapter_->buildStreamEvent(
                                        "", context_id, "status", &event);
                                    event.set_task_state("processing");
                                    callback(event);
                                } else if (state == "completed") {
                                    // 提取完成消息中的文本内容
                                    if (status_obj.contains("message")) {
                                        auto& message = status_obj["message"];
                                        if (message.contains("parts")) {
                                            std::string content;
                                            for (auto& part : message["parts"]) {
                                                if (part.value("type", "") == "text") {
                                                    content += part.value("text", "");
                                                }
                                            }
                                            if (!content.empty()) {
                                                agent_communication::AIStreamEvent event;
                                                response_adapter_->buildStreamEvent(
                                                    content, context_id, "partial", &event);
                                                callback(event);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } catch (const std::exception&) {
                    // Skip malformed events
                }
            });

        // [Batch 1] End direct streaming trace span
        if (trace) {
            trace->endSpan();
        }

        // Record streaming direct success to circuit breaker
        streaming_direct_cb->recordSuccess();

        agent_communication::AIStreamEvent complete_event;
        response_adapter_->buildStreamEvent(
            "", context_id, "complete", &complete_event);
        callback(complete_event);

    } catch (const std::exception& e) {
        // Record streaming direct failure to circuit breaker
        streaming_direct_cb->recordFailure();

        agent_communication::AIStreamEvent error_event;
        response_adapter_->buildStreamEvent(
            e.what(), request.context_id(), "error", &error_event);
        callback(error_event);
    }
}

} // namespace a2a_adapter
} // namespace agent_rpc
