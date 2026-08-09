#pragma once

#include "types.h"
#include "serializer.h"
#include "agent_service.pb.h"
#include "common.pb.h"
#include <memory>
#include <string>
#include <vector>

namespace agent_rpc {
namespace common {

// Converts between internal types and protobuf messages
class MessageConverter {
public:
    static agent_communication::common::ServiceInfo toProtobuf(const ServiceEndpoint& endpoint);
    
    static ServiceEndpoint fromProtobuf(const agent_communication::common::ServiceInfo& service_info);
    
    static agent_communication::Message toProtobufMessage(const std::string& content, 
                                                         const std::string& id = "",
                                                         const std::string& type = "text");
    
    static std::string fromProtobufMessage(const agent_communication::Message& message);
    
    static agent_communication::SendMessageRequest createSendMessageRequest(
        const std::string& content,
        const std::string& target_agent,
        int32_t timeout_seconds = 30);
    
    static agent_communication::ReceiveMessageRequest createReceiveMessageRequest(
        const std::string& agent_id,
        int32_t max_messages = 10,
        int32_t timeout_seconds = 30);
    
    static agent_communication::BroadcastMessageRequest createBroadcastMessageRequest(
        const std::string& content,
        const std::vector<std::string>& target_agents = {},
        bool exclude_sender = true);
    
    static agent_communication::RegisterAgentRequest createRegisterAgentRequest(
        const ServiceEndpoint& agent_info,
        int32_t heartbeat_interval = 30);
    
    static agent_communication::UnregisterAgentRequest createUnregisterAgentRequest(
        const std::string& agent_id,
        const std::string& reason = "");
    
    static agent_communication::HeartbeatRequest createHeartbeatRequest(
        const std::string& agent_id,
        const ServiceEndpoint& agent_info);
    
    static agent_communication::GetAgentsRequest createGetAgentsRequest(
        const std::string& filter = "",
        int32_t limit = 100,
        int32_t offset = 0);
    
    static bool parseSendMessageResponse(const agent_communication::SendMessageResponse& response,
                                       std::string& message_id,
                                       int64_t& timestamp);
    
    static bool parseReceiveMessageResponse(const agent_communication::ReceiveMessageResponse& response,
                                          std::vector<std::string>& messages);
    
    static bool parseBroadcastMessageResponse(const agent_communication::BroadcastMessageResponse& response,
                                            int32_t& success_count,
                                            int32_t& failure_count);
    
    static bool parseRegisterAgentResponse(const agent_communication::RegisterAgentResponse& response,
                                         std::string& agent_id,
                                         int64_t& registration_time);
    
    static bool parseUnregisterAgentResponse(const agent_communication::UnregisterAgentResponse& response,
                                           int64_t& unregistration_time);
    
    static bool parseHeartbeatResponse(const agent_communication::HeartbeatResponse& response,
                                     int64_t& server_time);
    
    static bool parseGetAgentsResponse(const agent_communication::GetAgentsResponse& response,
                                     std::vector<ServiceEndpoint>& agents,
                                     int32_t& total_count);
    
    static agent_communication::common::Status createSuccessStatus(const std::string& message = "Success");
    
    static agent_communication::common::Status createErrorStatus(int32_t code, 
                                                               const std::string& message,
                                                               const std::string& details = "");
    
    static bool isStatusSuccess(const agent_communication::common::Status& status);
    
    static std::string getStatusMessage(const agent_communication::common::Status& status);
};

// Builder for complex protobuf messages
class MessageBuilder {
public:
    static agent_communication::Message buildMessage(
        const std::string& content,
        const std::string& id = "",
        const std::string& type = "text",
        const std::map<std::string, std::string>& headers = {},
        const std::string& payload = "");
    
    static agent_communication::common::ServiceInfo buildServiceInfo(
        const std::string& service_name,
        const std::string& version,
        const std::string& host,
        int32_t port,
        const std::vector<std::string>& tags = {},
        const std::map<std::string, std::string>& metadata = {});
    
    static agent_communication::common::Status buildStatus(
        int32_t code,
        const std::string& message,
        const std::string& details = "");
    
    static agent_communication::common::HealthCheckResponse buildHealthCheckResponse(
        bool is_healthy);
    
    static agent_communication::common::LogEntry buildLogEntry(
        agent_communication::common::LogLevel level,
        const std::string& message,
        const std::string& source = "",
        const std::map<std::string, std::string>& fields = {});
};

// Validates protobuf messages
class MessageValidator {
public:
    static bool validateMessage(const agent_communication::Message& message);
    
    static bool validateServiceInfo(const agent_communication::common::ServiceInfo& service_info);
    
    static bool validateStatus(const agent_communication::common::Status& status);
    
    static bool validateSendMessageRequest(const agent_communication::SendMessageRequest& request);
    
    static bool validateReceiveMessageRequest(const agent_communication::ReceiveMessageRequest& request);
    
    static bool validateBroadcastMessageRequest(const agent_communication::BroadcastMessageRequest& request);
    
    static bool validateRegisterAgentRequest(const agent_communication::RegisterAgentRequest& request);
    
    static bool validateUnregisterAgentRequest(const agent_communication::UnregisterAgentRequest& request);
    
    static bool validateHeartbeatRequest(const agent_communication::HeartbeatRequest& request);
    
    static bool validateGetAgentsRequest(const agent_communication::GetAgentsRequest& request);
};

} // namespace common
} // namespace agent_rpc
