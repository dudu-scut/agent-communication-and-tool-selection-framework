#pragma once

#include "ai_query.pb.h"

#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent_rpc {
namespace common {

/**
 * @brief 记忆服务：管理多层记忆系统
 *
 * Tier 1 — 对话历史：按 (context_id, agent_id) 分片存储，防止Agent切换时脏窗口
 * Tier 2 — 用户长期记忆：按 user_id 存储键值对，Agent通过 memory_hints 上报，平台写入
 * 跨Agent摘要：Agent切换时生成的上下文摘要
 *
 * 当前为内存实现，数据结构兼容后续Redis迁移。
 */
class MemoryService {
public:
    MemoryService();
    ~MemoryService() = default;

    // ========================================================================
    // Tier 1: 对话历史 (per context_id + agent_id)
    // ========================================================================

    struct Message {
        std::string role;     // "user" | "agent"
        std::string content;
        int64_t timestamp;
    };

    /** 追加一条消息到对话历史 */
    void appendMessage(const std::string& context_id,
                       const std::string& agent_id,
                       const std::string& role,
                       const std::string& content);

    /** 获取指定 (context_id, agent_id) 的对话历史，格式化为文本 */
    std::string getConversationHistory(const std::string& context_id,
                                       const std::string& agent_id,
                                       int max_messages = 10) const;

    /** 获取指定 context_id 下最后一次使用的 agent_id */
    std::string getLastAgent(const std::string& context_id) const;

    /** 记录当前 agent 为该 context_id 的最后活跃 agent */
    void setLastAgent(const std::string& context_id, const std::string& agent_id);

    // ========================================================================
    // Tier 2: 用户长期记忆 (per user_id)
    // ========================================================================

    /** 设置用户记忆的单个键值对 */
    void setUserMemory(const std::string& user_id,
                       const std::string& key,
                       const std::string& value);

    /** 获取用户所有长期记忆，格式化为文本 */
    std::string getUserMemory(const std::string& user_id) const;

    /** 从 Agent 上报的 memory_hints 批量更新用户记忆 (方式二) */
    void updateUserMemoryFromHints(
        const std::string& user_id,
        const std::map<std::string, std::string>& hints);

    // ========================================================================
    // 跨Agent摘要
    // ========================================================================

    /** 设置跨Agent切换摘要 */
    void setCrossAgentSummary(const std::string& context_id,
                               const std::string& summary);

    /** 获取跨Agent切换摘要 */
    std::string getCrossAgentSummary(const std::string& context_id) const;

    // ========================================================================
    // 构建 SystemContext (供 AIQueryService 注入)
    // ========================================================================

    /** 构建完整的 SystemContext 用于注入到 AIQueryRequest */
    agent_communication::SystemContext buildSystemContext(
        const std::string& user_id,
        const std::string& context_id,
        const std::string& agent_id,
        int max_history = 10) const;

private:
    using ConversationKey = std::pair<std::string, std::string>; // (context_id, agent_id)

    struct ConversationKeyHash {
        size_t operator()(const ConversationKey& k) const {
            auto h1 = std::hash<std::string>{}(k.first);
            auto h2 = std::hash<std::string>{}(k.second);
            return h1 ^ (h2 << 1);
        }
    };

    static constexpr int kMaxHistoryPerAgent = 50;

    mutable std::mutex mutex_;

    // Tier 1: (context_id, agent_id) → 对话消息队列
    std::unordered_map<ConversationKey, std::deque<Message>, ConversationKeyHash>
        conversations_;

    // context_id → last active agent_id
    std::unordered_map<std::string, std::string> last_agents_;

    // Tier 2: user_id → { key → value } 长期记忆
    std::unordered_map<std::string, std::map<std::string, std::string>>
        user_memories_;

    // context_id → cross-agent summary
    std::unordered_map<std::string, std::string> cross_agent_summaries_;

    static std::string formatHistory(const std::deque<Message>& messages,
                                      int max_messages);
};

}  // namespace common
}  // namespace agent_rpc
