#include "agent_rpc/mcp/mcp_client.h"
#include "agent_rpc/common/logger.h"
#include <json/json.h>
#include <algorithm>

namespace agent_rpc {
namespace mcp {

MCPToolManager::MCPToolManager(std::shared_ptr<IMCPClient> mcp_client)
    : mcp_client_(mcp_client)
    , alive_flag_(std::make_shared<std::atomic<bool>>(true)) {
}

MCPToolManager::~MCPToolManager() {
    shutdown();
}

bool MCPToolManager::initialize() {
    if (initialized_) {
        LOG_WARN("MCP tool manager already initialized");
        return true;
    }
    
    if (!mcp_client_ || !mcp_client_->isConnected()) {
        LOG_ERROR("MCP client not connected");
        return false;
    }
    
    // Refresh the available tool list
    refreshTools();
    
    initialized_ = true;
    LOG_INFO("MCP tool manager initialized with " + std::to_string(available_tools_.size()) + " tools");
    return true;
}

void MCPToolManager::shutdown() {
    if (!initialized_) {
        return;
    }

    // Signal all pending async calls to stop (prevents use-after-free)
    alive_flag_->store(false);

    std::lock_guard<std::mutex> lock(tools_mutex_);
    available_tools_.clear();
    tool_map_.clear();
    initialized_ = false;

    LOG_INFO("MCP tool manager shutdown");
}

std::vector<MCPTool> MCPToolManager::getAvailableTools() const {
    std::lock_guard<std::mutex> lock(tools_mutex_);
    return available_tools_;
}

bool MCPToolManager::isToolAvailable(const std::string& tool_name) const {
    std::lock_guard<std::mutex> lock(tools_mutex_);
    return tool_map_.find(tool_name) != tool_map_.end();
}

MCPResponse MCPToolManager::executeTool(const std::string& tool_name, const std::string& arguments) {
    if (!initialized_) {
        MCPResponse error_response;
        error_response.is_error = true;
        error_response.error = "Tool manager not initialized";
        return error_response;
    }
    
    if (!isToolAvailable(tool_name)) {
        MCPResponse error_response;
        error_response.is_error = true;
        error_response.error = "Tool not available: " + tool_name;
        return error_response;
    }
    
    LOG_INFO("Executing MCP tool: " + tool_name);
    return mcp_client_->callTool(tool_name, arguments);
}

void MCPToolManager::executeToolAsync(const std::string& tool_name, 
                                     const std::string& arguments,
                                     std::function<void(const MCPResponse&)> callback) {
    if (!initialized_) {
        MCPResponse error_response;
        error_response.is_error = true;
        error_response.error = "Tool manager not initialized";
        callback(error_response);
        return;
    }
    
    if (!isToolAvailable(tool_name)) {
        MCPResponse error_response;
        error_response.is_error = true;
        error_response.error = "Tool not available: " + tool_name;
        callback(error_response);
        return;
    }
    
    // Execute tool call in background thread with alive flag to prevent use-after-free
    auto alive = alive_flag_;  // shared_ptr copy keeps flag alive
    std::thread([this, alive, tool_name, arguments, callback]() {
        if (!alive->load()) {
            return;  // Object was shut down before thread started
        }
        MCPResponse response = mcp_client_->callTool(tool_name, arguments);
        if (alive->load()) {
            callback(response);
        }
    }).detach();
}

bool MCPToolManager::validateToolArguments(const std::string& tool_name, const std::string& arguments) const {
    std::lock_guard<std::mutex> lock(tools_mutex_);
    
    auto it = tool_map_.find(tool_name);
    if (it == tool_map_.end()) {
        return false;
    }
    
    const MCPTool& tool = it->second;
    
    try {
        // Parse the tool's parameter schema
        Json::Value schema;
        Json::Reader reader;
        if (!reader.parse(tool.input_schema, schema)) {
            LOG_WARN("Failed to parse tool schema for: " + tool_name);
            return false;
        }
        
        // Parse the provided arguments
        Json::Value args;
        if (!reader.parse(arguments, args)) {
            LOG_WARN("Invalid JSON arguments for tool: " + tool_name);
            return false;
        }
        
        // Basic validation: check required fields
        if (schema.isMember("required")) {
            const Json::Value& required = schema["required"];
            for (const auto& field : required) {
                std::string field_name = field.asString();
                if (!args.isMember(field_name)) {
                    LOG_WARN("Missing required field '" + field_name + "' for tool: " + tool_name);
                    return false;
                }
            }
        }
        
        // Basic validation: check field types
        if (schema.isMember("properties")) {
            const Json::Value& properties = schema["properties"];
            for (const auto& prop_name : properties.getMemberNames()) {
                if (args.isMember(prop_name)) {
                    const Json::Value& prop_schema = properties[prop_name];
                    const Json::Value& prop_value = args[prop_name];
                    
                    if (prop_schema.isMember("type")) {
                        std::string expected_type = prop_schema["type"].asString();
                        std::string actual_type;
                        
                        if (prop_value.isString()) {
                            actual_type = "string";
                        } else if (prop_value.isInt() || prop_value.isUInt()) {
                            actual_type = "integer";
                        } else if (prop_value.isDouble()) {
                            actual_type = "number";
                        } else if (prop_value.isBool()) {
                            actual_type = "boolean";
                        } else if (prop_value.isArray()) {
                            actual_type = "array";
                        } else if (prop_value.isObject()) {
                            actual_type = "object";
                        }
                        
                        if (actual_type != expected_type) {
                            LOG_WARN("Type mismatch for field '" + prop_name + "' in tool " + tool_name + 
                                   ": expected " + expected_type + ", got " + actual_type);
                            return false;
                        }
                    }
                }
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error validating tool arguments for " + tool_name + ": " + e.what());
        return false;
    }
}

void MCPToolManager::refreshTools() {
    if (!mcp_client_ || !mcp_client_->isConnected()) {
        LOG_ERROR("MCP client not connected, cannot refresh tools");
        return;
    }
    
    std::vector<MCPTool> tools = mcp_client_->listTools();
    
    std::lock_guard<std::mutex> lock(tools_mutex_);
    available_tools_ = tools;
    tool_map_.clear();
    
    for (const auto& tool : tools) {
        tool_map_[tool.name] = tool;
    }
    
    LOG_INFO("Refreshed " + std::to_string(tools.size()) + " MCP tools");
}

void MCPToolManager::processNotification(const std::string& plugin_name, const std::string& notification) {
    LOG_INFO("Received notification from plugin " + plugin_name + ": " + notification);
    
    // Refresh tools if the notification indicates the tool list changed
    if (notification.find("tools_changed") != std::string::npos ||
        notification.find("tools_updated") != std::string::npos) {
        refreshTools();
    }
}

MCPServiceIntegrator::MCPServiceIntegrator() {}

MCPServiceIntegrator::~MCPServiceIntegrator() {
    shutdown();
}

bool MCPServiceIntegrator::initialize(const std::string& mcp_server_path, 
                                     const std::vector<std::string>& mcp_args) {
    if (initialized_) {
        LOG_WARN("MCP service integrator already initialized");
        return true;
    }
    
    mcp_server_path_ = mcp_server_path;
    mcp_args_ = mcp_args;
    
    // Create the MCP client
    mcp_client_ = std::make_shared<MCPClient>();
    
    // Set the notification callback
    mcp_client_->setNotificationCallback([this](const std::string& plugin_name, const std::string& notification) {
        if (tool_manager_) {
            tool_manager_->processNotification(plugin_name, notification);
        }
    });
    
    // Connect to the MCP server
    if (!mcp_client_->connect(mcp_server_path_, mcp_args_)) {
        LOG_ERROR("Failed to connect to MCP server: " + mcp_server_path_);
        return false;
    }
    
    // Create the tool manager
    tool_manager_ = std::make_shared<MCPToolManager>(mcp_client_);
    
    // Initialize the tool manager
    if (!tool_manager_->initialize()) {
        LOG_ERROR("Failed to initialize MCP tool manager");
        mcp_client_->disconnect();
        return false;
    }
    
    initialized_ = true;
    LOG_INFO("MCP service integrator initialized successfully");
    return true;
}

void MCPServiceIntegrator::shutdown() {
    if (!initialized_) {
        return;
    }
    
    if (tool_manager_) {
        tool_manager_->shutdown();
        tool_manager_.reset();
    }
    
    if (mcp_client_) {
        mcp_client_->disconnect();
        mcp_client_.reset();
    }
    
    initialized_ = false;
    LOG_INFO("MCP service integrator shutdown");
}

bool MCPServiceIntegrator::isServiceAvailable() const {
    return initialized_ && mcp_client_ && mcp_client_->isConnected();
}

std::vector<std::string> MCPServiceIntegrator::getAvailableServices() const {
    std::vector<std::string> services;
    
    if (!isServiceAvailable()) {
        return services;
    }
    
    if (tool_manager_) {
        auto tools = tool_manager_->getAvailableTools();
        for (const auto& tool : tools) {
            services.push_back("tool:" + tool.name);
        }
    }
    
    if (mcp_client_) {
        auto prompts = mcp_client_->listPrompts();
        for (const auto& prompt : prompts) {
            services.push_back("prompt:" + prompt.name);
        }
        
        auto resources = mcp_client_->listResources();
        for (const auto& resource : resources) {
            services.push_back("resource:" + resource.name);
        }
    }
    
    return services;
}

std::shared_ptr<MCPToolManager> MCPServiceIntegrator::getToolManager() const {
    return tool_manager_;
}

void MCPServiceIntegrator::setMCPServerPath(const std::string& path) {
    mcp_server_path_ = path;
}

void MCPServiceIntegrator::setMCPServerArgs(const std::vector<std::string>& args) {
    mcp_args_ = args;
}

void MCPServiceIntegrator::setLogLevel(common::LogLevel level) {
    (void)level;  // Avoid unused parameter warning
}

} // namespace mcp
} // namespace agent_rpc


