/**
 * @file query_helpers.cpp
 * @brief Implementation of QueryHelpers (extracted from ai_query_service.cpp)
 */

#include "agent_rpc/server/query_helpers.h"
#include "agent_rpc/common/metrics.h"
#include "agent_rpc/common/logger.h"
#include "agent_rpc/common/memory_service.h"
#include <a2a/llm_client.hpp>
#include "ai_query.pb.h"

#include <future>

#ifdef _WIN32
#include <objbase.h>
#include <rpc.h>
#pragma comment(lib, "rpcrt4.lib")
#else
#include <uuid/uuid.h>
#endif

namespace agent_rpc {
namespace server {

// ============================================================================
// Task Status Tracking
// ============================================================================

void QueryHelpers::updateTaskStatus(
    const std::string& task_id,
    const std::string& state,
    const std::string& agent_id,
    const std::string& agent_name,
    const std::string& error_msg) {

    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(task_status_mutex);

    auto it = task_status_cache.find(task_id);
    if (it != task_status_cache.end()) {
        it->second.state = state;
        it->second.updated_at = now;
        if (!agent_id.empty()) it->second.agent_id = agent_id;
        if (!agent_name.empty()) it->second.agent_name = agent_name;
        if (!error_msg.empty()) it->second.error_message = error_msg;
    } else {
        task_status_cache[task_id] = TaskStatus{
            task_id, state, now, now, agent_id, agent_name, error_msg
        };
    }

    // Periodic cleanup every 100 status updates
    uint64_t count = status_query_count.fetch_add(1);
    if (count % 100 == 0 && count > 0) {
        auto cutoff = now - std::chrono::minutes(5);
        for (auto entry = task_status_cache.begin();
             entry != task_status_cache.end(); ) {
            if (entry->second.updated_at < cutoff) {
                entry = task_status_cache.erase(entry);
            } else {
                ++entry;
            }
        }
    }
}

void QueryHelpers::cleanupExpiredTasks() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::minutes(5);

    std::lock_guard<std::mutex> lock(task_status_mutex);
    for (auto it = task_status_cache.begin(); it != task_status_cache.end(); ) {
        if (it->second.updated_at < cutoff) {
            it = task_status_cache.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Static helpers
// ============================================================================

void QueryHelpers::recordMetrics(
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

std::string QueryHelpers::generateRequestId() {
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

std::string QueryHelpers::sanitizeErrorMessage(const std::string& msg) {
    if (msg.find("CURL error") != std::string::npos) {
        if (msg.find("Couldn't connect") != std::string::npos ||
            msg.find("couldn't connect") != std::string::npos) {
            return "Agent service is currently unreachable. Please verify the agent is running and try again later.";
        }
        if (msg.find("timeout") != std::string::npos ||
            msg.find("Timeout") != std::string::npos) {
            return "Agent service did not respond in time. Please try again later.";
        }
        if (msg.find("URL using bad") != std::string::npos ||
            msg.find("missing URL") != std::string::npos) {
            return "Invalid agent endpoint configuration. Please contact the administrator.";
        }
        return "Failed to connect to agent service. Please try again later.";
    }
    return msg;
}

// ============================================================================
// Agent Switch / Memory helpers
// ============================================================================

void QueryHelpers::handleAgentSwitch(
    common::MemoryService* memory_service,
    void* memory_llm_client,
    const std::string& user_id,
    const std::string& context_id,
    const std::string& current_agent_id) {

    if (user_id.empty() || context_id.empty() || current_agent_id.empty()) return;

    std::string last_agent = memory_service->getLastAgent(context_id);
    if (last_agent.empty() || last_agent == current_agent_id) {
        return;
    }

    // Agent switched — generate summary asynchronously
    LOG_INFO("Agent switch detected: " + last_agent + " → " + current_agent_id +
             " (context: " + context_id + ")");

    std::string old_history = memory_service->getConversationHistory(
        context_id, last_agent, 20);

    LLMClient* llm = static_cast<LLMClient*>(memory_llm_client);

    if (!old_history.empty() && llm) {
        {
            std::lock_guard<std::mutex> lock(memory_llm_mutex);
            if (!summary_in_progress.insert(context_id).second) {
                return;  // Another thread already generating for this context
            }
        }

        (void)std::async(std::launch::async,
            [this, llm, context_id, old_history, memory_service]() {
                std::lock_guard<std::mutex> lock(memory_llm_mutex);
                try {
                    std::string summary = llm->chat(
                        "你是一个对话摘要助手。请用2-3句话简洁总结以下用户与助手的对话要点，"
                        "保留关键信息和上下文，以便下一个助手能够无缝接续对话。直接输出摘要，不要加前缀。",
                        old_history);
                    memory_service->setCrossAgentSummary(context_id, summary);
                    LOG_INFO("Cross-agent summary generated for context: " + context_id);
                } catch (const std::exception& e) {
                    LOG_WARN("Failed to generate cross-agent summary: " + std::string(e.what()));
                }
                summary_in_progress.erase(context_id);
            });
    }
}

std::string QueryHelpers::buildMemoryContext(
    const agent_communication::AIQueryRequest* request) {
    std::string memory_ctx;
    if (request->has_system_context()) {
        const auto& sys_ctx = request->system_context();
        if (!sys_ctx.user_memory().empty()) {
            memory_ctx += "[User Context]\n" + sys_ctx.user_memory() + "\n";
        }
        if (!sys_ctx.cross_agent_summary().empty()) {
            memory_ctx += "[Prior Context]\n" + sys_ctx.cross_agent_summary() + "\n";
        }
    }
    return memory_ctx;
}

} // namespace server
} // namespace agent_rpc
