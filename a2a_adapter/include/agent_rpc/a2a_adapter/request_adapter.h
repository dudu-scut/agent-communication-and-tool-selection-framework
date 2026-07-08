/**
 * @file request_adapter.h
 * @brief RPC request to A2A request adapter
 *
 * Requirements: 8.1, 8.2
 */

#pragma once

#include <string>
#include <atomic>
#include <a2a/models/message_send_params.hpp>
#include <a2a/models/agent_message.hpp>

// Forward declaration for protobuf types
namespace agent_communication {
class AIQueryRequest;
}

namespace agent_rpc {
namespace a2a_adapter {

/**
 * @brief Adapts RPC requests to A2A format
 */
class RequestAdapter {
public:
    RequestAdapter() = default;
    ~RequestAdapter() = default;

    /**
     * @brief Convert RPC AIQueryRequest to A2A MessageSendParams
     * @param request The RPC request
     * @return A2A message send parameters
     */
    a2a::MessageSendParams convertToA2A(
        const agent_communication::AIQueryRequest& request);

    /**
     * @brief Generate a unique message ID
     * @return Unique message ID
     */
    std::string generateMessageId();

    /**
     * @brief Generate a unique context ID
     * @return Unique context ID
     */
    std::string generateContextId();

private:
    std::atomic<uint64_t> message_counter_{0};  // Fix #21: atomic for thread safety
    std::atomic<uint64_t> context_counter_{0};  // Fix #21: atomic for thread safety

    a2a::AgentMessage buildAgentMessage(
        const std::string& content,
        const std::string& context_id,
        a2a::MessageRole role);

    std::string extractOrGenerateContextId(
        const agent_communication::AIQueryRequest& request);
};

} // namespace a2a_adapter
} // namespace agent_rpc
