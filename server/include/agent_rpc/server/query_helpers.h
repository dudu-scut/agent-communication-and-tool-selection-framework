/**
 * @file query_helpers.h
 * @brief Helper utilities for AIQueryService
 *
 * Extracted from ai_query_service.cpp:
 *   - Task status tracking (recordMetrics, updateTaskStatus, cleanupExpiredTasks)
 *   - Agent switch detection with cross-agent summary generation
 *   - UUID generation, memory context building, error sanitization
 */

#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <set>
#include <memory>

namespace agent_communication { class AIQueryRequest; }

namespace agent_rpc {

// Forward declarations
namespace common { class MemoryService; class RedisClient; }

namespace server {

/**
 * @brief Stateless and stateful helpers extracted from AIQueryServiceImpl
 *
 * Stateless methods are static; stateful methods operate on the task-status cache
 * and summary-generation tracking owned by this struct.
 */
struct QueryHelpers {

    // ========================================================================
    // Task Status Tracking (P2-1)
    // ========================================================================

    struct TaskStatus {
        std::string task_id;
        std::string state;       // submitted | working | completed | failed | cancelled
        std::chrono::steady_clock::time_point created_at;
        std::chrono::steady_clock::time_point updated_at;
        std::string agent_id;
        std::string agent_name;
        std::string error_message;
    };

    std::mutex task_status_mutex;
    std::unordered_map<std::string, TaskStatus> task_status_cache;
    std::atomic<uint64_t> status_query_count{0};

    void updateTaskStatus(const std::string& task_id, const std::string& state,
                          const std::string& agent_id = "",
                          const std::string& agent_name = "",
                          const std::string& error_msg = "");

    void cleanupExpiredTasks();

    // ========================================================================
    // Static helpers
    // ========================================================================

    static void recordMetrics(const std::string& method, int64_t duration_ms, bool success);
    static std::string generateRequestId();

    // Sanitize raw CURL errors into user-friendly messages (B-03)
    static std::string sanitizeErrorMessage(const std::string& msg);

    // ========================================================================
    // Agent Switch / Memory helpers
    // ========================================================================

    std::mutex memory_llm_mutex;
    std::set<std::string> summary_in_progress;  // context_ids with ongoing summary generation
    // Note: memory_llm_client_ is not owned here; passed as parameter.

    void handleAgentSwitch(common::MemoryService* memory_service,
                           void* memory_llm_client,  // LLMClient*
                           const std::string& user_id,
                           const std::string& context_id,
                           const std::string& current_agent_id);

    static std::string buildMemoryContext(const agent_communication::AIQueryRequest* request);
};

} // namespace server
} // namespace agent_rpc
