/**
 * @file tool_validator.h
 * @brief Optional tool validator
 */

#pragma once

#include "tool_retriever.h"
#include "../mcp_agent_integration.h"

#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace agent_rpc {
namespace mcp {
namespace rag {

/**
 * @brief Validation result
 */
struct ValidationResult {
    std::string tool_name;            ///< Tool name
    bool is_valid = false;            ///< Whether valid
    std::string error_message;        ///< Error message
    int64_t duration_ms = 0;          ///< Validation duration (ms)
};

/**
 * @brief Validator configuration
 */
struct ValidatorConfig {
    int timeout_ms = 5000;            ///< Validation timeout (ms)
    bool treat_timeout_as_valid = true; ///< Whether timeout is treated as valid
    int max_test_queries = 3;         ///< Maximum test queries per tool
};

/**
 * @brief Tool validator
 * 
 * Validates whether retrieved tools are compatible with the current task.
 * Validates tools by generating test queries and checking the responses.
 */
class ToolValidator {
public:
    /**
     * @brief Tool call function type
     * @param tool_name Tool name
     * @param arguments Arguments JSON
     * @return Call result
     */
    using ToolCallFunc = std::function<ToolCallResult(
        const std::string& tool_name, 
        const std::string& arguments)>;
    
    explicit ToolValidator(const ValidatorConfig& config);
    ~ToolValidator() = default;
    
    /**
     * @brief Set the tool call function
     */
    void setToolCallFunc(ToolCallFunc func) { tool_call_func_ = func; }
    
    /**
     * @brief Validate a single tool
     * @param tool Tool info
     * @return Validation result
     */
    ValidationResult validate(const RetrievedTool& tool);
    
    /**
     * @brief Validate tools in batch
     * @param tools Tool list
     * @return List of validation results
     */
    std::vector<ValidationResult> validateBatch(const std::vector<RetrievedTool>& tools);
    
    /**
     * @brief Filter out invalid tools
     * @param tools Tool list
     * @return List of valid tools
     */
    std::vector<RetrievedTool> filterInvalid(const std::vector<RetrievedTool>& tools);
    
    /**
     * @brief Get the configuration
     */
    const ValidatorConfig& getConfig() const { return config_; }

private:
    /**
     * @brief Generate test queries
     */
    std::vector<std::string> generateTestQueries(const RetrievedTool& tool);
    
    /**
     * @brief Execute a test query
     */
    bool executeTestQuery(const std::string& tool_name, const std::string& query);
    
    ValidatorConfig config_;
    ToolCallFunc tool_call_func_;
};

} // namespace rag
} // namespace mcp
} // namespace agent_rpc
