#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include "agent_rpc/common/types.h"
#include "agent_rpc/common/logger.h"

namespace agent_rpc {
namespace mcp {

// MCP transport types
enum class MCPTransportType {
    STDIO,      // Standard I/O (local process)
    SSE         // Server-Sent Events (HTTP remote)
};

// MCP tool definition
struct MCPTool {
    std::string name;
    std::string description;
    std::string input_schema;  // JSON schema
};

// MCP prompt definition
struct MCPPrompt {
    std::string name;
    std::string description;
    std::string arguments;  // JSON arguments
};

// MCP resource definition
struct MCPResource {
    std::string name;
    std::string description;
    std::string uri;
    std::string mime_type;
};

// MCP request/response
struct MCPRequest {
    std::string method;
    std::string params;
    std::string id;
};

struct MCPResponse {
    std::string id;
    std::string result;
    std::string error;
    bool is_error = false;
};

// MCP connection configuration
struct MCPConnectionConfig {
    MCPTransportType transport = MCPTransportType::STDIO;
    
    // STDIO mode configuration
    std::string server_path;                    // MCP server executable path
    std::vector<std::string> server_args;       // Startup arguments
    
    // SSE mode configuration
    std::string sse_url;                        // SSE server URL (e.g. http://localhost:8080/mcp)
    std::string api_key;                        // API key (optional)
    int connect_timeout_ms = 5000;              // Connection timeout (ms)
    int request_timeout_ms = 30000;             // Request timeout (ms)
    bool verify_ssl = true;                     // Whether to verify the SSL certificate
};

// MCP client interface
class IMCPClient {
public:
    virtual ~IMCPClient() = default;
    
    // Connection management
    virtual bool connect(const std::string& server_path, const std::vector<std::string>& args = {}) = 0;
    virtual bool connect(const MCPConnectionConfig& config) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual MCPTransportType getTransportType() const = 0;
    
    // Tools
    virtual std::vector<MCPTool> listTools() = 0;
    virtual MCPResponse callTool(const std::string& tool_name, const std::string& arguments) = 0;
    
    // Prompts
    virtual std::vector<MCPPrompt> listPrompts() = 0;
    virtual MCPResponse getPrompt(const std::string& prompt_name, const std::string& arguments) = 0;
    
    // Resources
    virtual std::vector<MCPResource> listResources() = 0;
    virtual MCPResponse readResource(const std::string& uri) = 0;
    
    // Notification callback
    virtual void setNotificationCallback(std::function<void(const std::string&, const std::string&)> callback) = 0;
};

// MCP client implementation (supports STDIO and SSE transports)
class MCPClient : public IMCPClient {
public:
    MCPClient();
    ~MCPClient();
    
    // Connection management
    bool connect(const std::string& server_path, const std::vector<std::string>& args = {}) override;
    bool connect(const MCPConnectionConfig& config) override;
    void disconnect() override;
    bool isConnected() const override;
    MCPTransportType getTransportType() const override;
    
    // Tools
    std::vector<MCPTool> listTools() override;
    MCPResponse callTool(const std::string& tool_name, const std::string& arguments) override;
    
    // Prompts
    std::vector<MCPPrompt> listPrompts() override;
    MCPResponse getPrompt(const std::string& prompt_name, const std::string& arguments) override;
    
    // Resources
    std::vector<MCPResource> listResources() override;
    MCPResponse readResource(const std::string& uri) override;
    
    // Notification callback
    void setNotificationCallback(std::function<void(const std::string&, const std::string&)> callback) override;

private:
    // Internal communication (common)
    bool sendRequest(const MCPRequest& request);
    MCPResponse receiveResponse();
    void processNotifications();
    
    // STDIO mode
    bool startMCPServer();
    void stopMCPServer();
    bool sendRequestStdio(const MCPRequest& request);
    void processNotificationsStdio();
    
    // SSE mode
    bool connectSSE();
    void disconnectSSE();
    bool sendRequestSSE(const MCPRequest& request);
    MCPResponse receiveResponseSSE();
    void processNotificationsSSE();
    static size_t sseWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t sseHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata);
    
    // Message handling
    std::string buildJSONRPCRequest(const MCPRequest& request);
    MCPResponse parseJSONRPCResponse(const std::string& response);
    
    // Configuration
    MCPConnectionConfig config_;
    MCPTransportType transport_type_{MCPTransportType::STDIO};
    
    // Common state
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    
    // STDIO mode state
    std::string server_path_;
    std::vector<std::string> server_args_;
    int server_pid_{-1};
    int stdin_pipe_{-1};
    int stdout_pipe_{-1};
    
    // SSE mode state
    void* curl_handle_{nullptr};        // CURL handle
    void* curl_multi_{nullptr};         // CURL multi handle for SSE
    std::string sse_session_id_;        // SSE session ID
    std::string sse_response_buffer_;   // SSE response buffer
    mutable std::mutex sse_buffer_mutex_; // Protects concurrent access to sse_response_buffer_
    std::thread sse_event_thread_;      // SSE event listener thread
    
    // Message queue
    std::queue<MCPResponse> response_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Notification callback
    std::function<void(const std::string&, const std::string&)> notification_callback_;
    std::thread notification_thread_;
    
    // Logging
    std::shared_ptr<common::Logger> logger_;
};

// MCP tool manager
class MCPToolManager {
public:
    MCPToolManager(std::shared_ptr<IMCPClient> mcp_client);
    ~MCPToolManager();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Tool management
    std::vector<MCPTool> getAvailableTools() const;
    bool isToolAvailable(const std::string& tool_name) const;
    
    // Tool invocation
    MCPResponse executeTool(const std::string& tool_name, const std::string& arguments);
    
    // Asynchronous tool invocation
    void executeToolAsync(const std::string& tool_name, 
                         const std::string& arguments,
                         std::function<void(const MCPResponse&)> callback);
    
    // Tool validation
    bool validateToolArguments(const std::string& tool_name, const std::string& arguments) const;

    // Internal methods (used by MCPServiceIntegrator)
    void refreshTools();
    void processNotification(const std::string& plugin_name, const std::string& notification);

private:
    std::shared_ptr<IMCPClient> mcp_client_;
    std::vector<MCPTool> available_tools_;
    std::map<std::string, MCPTool> tool_map_;
    mutable std::mutex tools_mutex_;
    std::atomic<bool> initialized_{false};
    std::shared_ptr<std::atomic<bool>> alive_flag_;  // Prevents use-after-free in async calls
};

// MCP service integrator
class MCPServiceIntegrator {
public:
    MCPServiceIntegrator();
    ~MCPServiceIntegrator();
    
    // Initialization
    bool initialize(const std::string& mcp_server_path, 
                   const std::vector<std::string>& mcp_args = {});
    void shutdown();
    
    // Service management
    bool isServiceAvailable() const;
    std::vector<std::string> getAvailableServices() const;
    
    // Tool service
    std::shared_ptr<MCPToolManager> getToolManager() const;
    
    // Configuration management
    void setMCPServerPath(const std::string& path);
    void setMCPServerArgs(const std::vector<std::string>& args);
    void setLogLevel(common::LogLevel level);

private:
    std::shared_ptr<MCPClient> mcp_client_;
    std::shared_ptr<MCPToolManager> tool_manager_;
    std::string mcp_server_path_;
    std::vector<std::string> mcp_args_;
    std::atomic<bool> initialized_{false};
    std::shared_ptr<common::Logger> logger_;
};

} // namespace mcp
} // namespace agent_rpc


