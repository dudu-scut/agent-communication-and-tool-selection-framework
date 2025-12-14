/**
 * @file a2a_adapter.cpp
 * @brief Implementation of main A2A adapter
 * 
 * Requirements: 8.1, 8.2, 8.5
 */

#include "agent_rpc/a2a_adapter/a2a_adapter.h"
#include "ai_query.pb.h"
#include <a2a/core/exception.hpp>
#include <chrono>

namespace agent_rpc {
namespace a2a_adapter {

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
    
    try {
        // Convert RPC request to A2A format
        a2a::MessageSendParams params = request_adapter_->convertToA2A(request);
        
        // Send message via A2A client
        a2a::A2AResponse a2a_response = a2a_client_->send_message(params);
        
        // Convert A2A response to RPC format
        response_adapter_->convertFromA2A(a2a_response, request.request_id(), response);
        
        // Calculate processing time
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        response->set_processing_time_ms(duration.count());
        
        // Success if we got any valid response (Task or Message)
        return true;
        
    } catch (const a2a::A2AException& e) {
        // Handle A2A specific errors
        auto* status = response->mutable_status();
        status->set_code(static_cast<int>(e.error_code()));
        std::string error_msg = e.what();
        if (error_msg.empty()) {
            error_msg = "A2A request failed (Orchestrator may not be running at " + 
                       config_.orchestrator_url + ")";
        }
        status->set_message(error_msg);
        return false;
    } catch (const std::exception& e) {
        // Handle general errors
        auto* status = response->mutable_status();
        status->set_code(-1);
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
    
    try {
        // Convert RPC request to A2A format
        a2a::MessageSendParams params = request_adapter_->convertToA2A(request);
        std::string context_id = params.context_id().value_or("");
        
        // Use streaming API
        a2a_client_->send_message_streaming(params, 
            [this, &callback, &context_id](const std::string& event_data) {
                agent_communication::AIStreamEvent event;
                response_adapter_->buildStreamEvent(
                    event_data, context_id, "partial", &event);
                callback(event);
            });
        
        // Send completion event
        agent_communication::AIStreamEvent complete_event;
        response_adapter_->buildStreamEvent(
            "", context_id, "complete", &complete_event);
        callback(complete_event);
        
    } catch (const std::exception& e) {
        // Send error event
        agent_communication::AIStreamEvent error_event;
        response_adapter_->buildStreamEvent(
            e.what(), request.context_id(), "error", &error_event);
        callback(error_event);
    }
}

bool A2AAdapter::isAvailable() const {
    return initialized_;
}

} // namespace a2a_adapter
} // namespace agent_rpc
