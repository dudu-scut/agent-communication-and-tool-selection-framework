#pragma once

#include "agent_rpc/common/redis_client.h"
#include "ai_query.pb.h"

#include <string>

namespace agent_rpc {
namespace common {

/**
 * @brief 记忆服务：管理多层记忆系统 (Redis-backed)
 *
 * Tier 1 — 对话历史：按 (context_id, agent_id) 分片存储 (Redis list)
 * Tier 2 — 用户长期记忆：按 user_id 存储 (Redis hash)
 * 跨Agent摘要：Agent切换时生成的上下文摘要 (Redis string)
 */
class MemoryService {
public:
    explicit MemoryService(RedisClient* redis);
    ~MemoryService() = default;

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

    /** 设置用户记忆的单个键值对 */
    void setUserMemory(const std::string& user_id,
                       const std::string& key,
                       const std::string& value);

    /** 获取用户所有长期记忆，格式化为文本 */
    std::string getUserMemory(const std::string& user_id) const;

    /** 从 Agent 上报的 memory_hints 批量更新用户记忆 */
    void updateUserMemoryFromHints(
        const std::string& user_id,
        const std::map<std::string, std::string>& hints);

    /** 设置跨Agent切换摘要 */
    void setCrossAgentSummary(const std::string& context_id,
                               const std::string& summary);

    /** 获取跨Agent切换摘要 */
    std::string getCrossAgentSummary(const std::string& context_id) const;

    /** 构建完整的 SystemContext 用于注入到 AIQueryRequest */
    agent_communication::SystemContext buildSystemContext(
        const std::string& user_id,
        const std::string& context_id,
        const std::string& agent_id,
        int max_history = 10) const;

private:
    // Redis key helpers
    static std::string convKey(const std::string& ctx, const std::string& agent) {
        return "nexusai:conv:" + ctx + ":" + agent;
    }
    static std::string lastAgentKey(const std::string& ctx) {
        return "nexusai:last_agent:" + ctx;
    }
    static std::string memoryKey(const std::string& uid) {
        return "nexusai:memory:" + uid;
    }
    static std::string summaryKey(const std::string& ctx) {
        return "nexusai:summary:" + ctx;
    }

    static constexpr int kMaxHistoryPerAgent = 50;

    RedisClient* redis_;  // not owned

    static std::string formatHistory(const std::vector<std::string>& raw_messages,
                                      int max_messages);
};

}  // namespace common
}  // namespace agent_rpc
