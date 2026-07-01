#include "agent_rpc/common/memory_service.h"

#include <algorithm>
#include <sstream>

namespace agent_rpc {
namespace common {

MemoryService::MemoryService() = default;

// ============================================================================
// Tier 1: 对话历史
// ============================================================================

void MemoryService::appendMessage(const std::string& context_id,
                                    const std::string& agent_id,
                                    const std::string& role,
                                    const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto key = std::make_pair(context_id, agent_id);
    auto& conv = conversations_[key];

    Message msg;
    msg.role = role;
    msg.content = content;
    msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();

    conv.push_back(std::move(msg));

    // 限制单个对话的历史长度
    while (static_cast<int>(conv.size()) > kMaxHistoryPerAgent) {
        conv.pop_front();
    }

    // 更新最后活跃Agent
    last_agents_[context_id] = agent_id;
}

std::string MemoryService::getConversationHistory(
    const std::string& context_id,
    const std::string& agent_id,
    int max_messages) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto key = std::make_pair(context_id, agent_id);
    auto it = conversations_.find(key);
    if (it == conversations_.end()) return "";

    return formatHistory(it->second, max_messages);
}

std::string MemoryService::getLastAgent(const std::string& context_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = last_agents_.find(context_id);
    return (it != last_agents_.end()) ? it->second : "";
}

void MemoryService::setLastAgent(const std::string& context_id,
                                   const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_agents_[context_id] = agent_id;
}

// ============================================================================
// Tier 2: 用户长期记忆
// ============================================================================

void MemoryService::setUserMemory(const std::string& user_id,
                                    const std::string& key,
                                    const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    user_memories_[user_id][key] = value;
}

std::string MemoryService::getUserMemory(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = user_memories_.find(user_id);
    if (it == user_memories_.end() || it->second.empty()) return "";

    std::ostringstream oss;
    for (const auto& [key, value] : it->second) {
        oss << "- " << key << ": " << value << "\n";
    }
    return oss.str();
}

void MemoryService::updateUserMemoryFromHints(
    const std::string& user_id,
    const std::map<std::string, std::string>& hints) {
    if (hints.empty() || user_id.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    auto& mem = user_memories_[user_id];
    for (const auto& [key, value] : hints) {
        if (value.empty()) {
            mem.erase(key);  // 空值表示删除
        } else {
            mem[key] = value;
        }
    }
}

// ============================================================================
// 跨Agent摘要
// ============================================================================

void MemoryService::setCrossAgentSummary(const std::string& context_id,
                                            const std::string& summary) {
    std::lock_guard<std::mutex> lock(mutex_);
    cross_agent_summaries_[context_id] = summary;
}

std::string MemoryService::getCrossAgentSummary(
    const std::string& context_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cross_agent_summaries_.find(context_id);
    return (it != cross_agent_summaries_.end()) ? it->second : "";
}

// ============================================================================
// SystemContext 构建
// ============================================================================

agent_communication::SystemContext MemoryService::buildSystemContext(
    const std::string& user_id,
    const std::string& context_id,
    const std::string& agent_id,
    int max_history) const {

    agent_communication::SystemContext ctx;
    ctx.set_user_id(user_id);

    // Tier 2: 用户长期记忆
    ctx.set_user_memory(getUserMemory(user_id));

    // Tier 1: 当前Agent对话历史 (路由前agent_id为空，跳过)
    if (!agent_id.empty()) {
        ctx.set_conversation_history(
            getConversationHistory(context_id, agent_id, max_history));
    }

    // 跨Agent摘要 (仅在切换Agent时有值)
    ctx.set_cross_agent_summary(getCrossAgentSummary(context_id));

    return ctx;
}

// ============================================================================
// 格式化
// ============================================================================

std::string MemoryService::formatHistory(const std::deque<Message>& messages,
                                           int max_messages) {
    if (messages.empty()) return "";

    int start = std::max(0, static_cast<int>(messages.size()) - max_messages);

    std::ostringstream oss;
    for (int i = start; i < static_cast<int>(messages.size()); ++i) {
        const auto& msg = messages[i];
        oss << (msg.role == "user" ? "用户: " : "助手: ") << msg.content << "\n";
    }
    return oss.str();
}

}  // namespace common
}  // namespace agent_rpc
