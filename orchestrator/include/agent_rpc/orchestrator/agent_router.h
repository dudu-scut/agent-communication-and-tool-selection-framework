/**
 * @file agent_router.h
 * @brief Agent Router - selects appropriate agent for requests
 * 
 * Task 8.3: 实现AgentRouter路由器
 * Requirements: 2.3, 3.3, 3.4
 */

#pragma once

#include "agent_info.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declarations for MCP RAG types (P3 embedding routing)
namespace agent_rpc { namespace mcp { namespace rag {
    class EmbeddingService;
    class VectorIndex;
    class EmbeddingCache;
}}}

// Forward declaration for P1-1 LLM-based intent classification
class LLMClient;

namespace agent_rpc {
namespace orchestrator {

/**
 * @brief Configuration for embedding-based skill routing (P3)
 */
struct EmbeddingRouterConfig {
    bool enabled = false;
    float high_threshold = 0.85f;   // similarity > this → direct route
    float low_threshold = 0.50f;    // similarity < this → no match
    std::string api_key;            // embedding API key (falls back to LLM_API_KEY env)
    std::string model = "deepseek-v4-pro";
    int dimension = 1024;
    std::string api_url = "https://api.deepseek.com/v1/embeddings";
};

/**
 * @brief Result of hybrid skill analysis (P3)
 */
struct SkillMatchResult {
    std::string skill_name;
    float confidence = 0.0f;
    enum Source { KEYWORD, EMBEDDING, NONE } source = NONE;
};

/**
 * @brief Agent Router - routes requests to appropriate agents
 * 
 * Features:
 * - Multiple routing strategies (round-robin, random, skill-match, least-load)
 * - Health status tracking
 * - Skill-based filtering
 * - Thread-safe operations
 */
class AgentRouter {
public:
    AgentRouter();
    ~AgentRouter();
    
    // Disable copy
    AgentRouter(const AgentRouter&) = delete;
    AgentRouter& operator=(const AgentRouter&) = delete;
    
    /**
     * @brief Initialize router with strategy
     * @param strategy Routing strategy to use
     * @return true if initialization successful
     */
    bool initialize(RoutingStrategy strategy = RoutingStrategy::SKILL_MATCH);
    
    /**
     * @brief Shutdown router
     */
    void shutdown();
    
    // === Agent Selection ===
    
    /**
     * @brief Select an agent for a request
     * @param question The question/request content (used for skill analysis)
     * @param required_skills Optional list of required skills
     * @return Selected agent info, or nullopt if no suitable agent found
     * 
     * Property 4: Agent Selection Determinism
     */
    std::optional<AgentInfo> selectAgent(
        const std::string& question,
        const std::vector<std::string>& required_skills = {});

    /**
     * @brief Callback type for agent invocation (P1-2 generic dispatch)
     *
     * Parameters: (agent_url, prompt)
     * Returns: agent response text, or empty string on failure.
     */
    using AgentDispatchFn = std::function<std::string(
        const std::string& agent_url, const std::string& prompt)>;

    /**
     * @brief Result of a dispatch() call
     */
    struct DispatchResult {
        std::string response;          // Agent response text
        std::string agent_id;          // Selected agent ID (empty if routing failed)
        std::string agent_name;        // Selected agent name
        std::string matched_skill;     // Skill that was matched
        int64_t latency_ms = 0;        // Total dispatch latency
        bool success = false;          // Whether the call succeeded
    };

    /**
     * @brief Generic dispatch: route + call agent (P1-2)
     *
     * Combines selectAgent() with an injectable call function.
     * Eliminates if/else dispatch — any registered agent is reachable
     * through the same code path.
     *
     * @param question User input text
     * @param call_fn  Injectable agent call function
     * @param required_skills Optional skill filter
     * @return DispatchResult with response and routing metadata
     */
    DispatchResult dispatch(
        const std::string& question,
        const AgentDispatchFn& call_fn,
        const std::vector<std::string>& required_skills = {});

    /**
     * @brief Find agents by skill
     * @param skill Skill to search for
     * @return Vector of agents with the skill
     */
    std::vector<AgentInfo> findAgentsBySkill(const std::string& skill);
    
    /**
     * @brief Find agents by tags
     * @param tags Tags to search for (agent must have all tags)
     * @return Vector of matching agents
     */
    std::vector<AgentInfo> findAgentsByTags(const std::vector<std::string>& tags);
    
    /**
     * @brief Find healthy agents with required skills
     * @param required_skills Skills to match
     * @return Vector of healthy agents with matching skills
     */
    std::vector<AgentInfo> findHealthyAgentsWithSkills(
        const std::vector<std::string>& required_skills);
    
    // === Agent Management ===
    
    /**
     * @brief Update the list of available agents
     * @param agents New agent list
     */
    void updateAgentList(const std::vector<AgentInfo>& agents);
    
    /**
     * @brief Add a single agent
     * @param agent Agent to add
     */
    void addAgent(const AgentInfo& agent);
    
    /**
     * @brief Remove an agent by ID
     * @param agent_id Agent identifier
     * @return true if agent was removed
     */
    bool removeAgent(const std::string& agent_id);
    
    /**
     * @brief Get agent by ID
     * @param agent_id Agent identifier
     * @return Agent info if found
     */
    std::optional<AgentInfo> getAgent(const std::string& agent_id);
    
    /**
     * @brief Get all registered agents
     * @return Vector of all agents
     */
    std::vector<AgentInfo> getAllAgents();
    
    /**
     * @brief Get all healthy agents
     * @return Vector of healthy agents
     */
    std::vector<AgentInfo> getHealthyAgents();
    
    // === Health Management ===
    
    /**
     * @brief Mark an agent as unhealthy
     * @param agent_id Agent identifier
     * 
     * Property 8: Agent Health State Consistency
     */
    void markAgentUnhealthy(const std::string& agent_id);
    
    /**
     * @brief Mark an agent as healthy
     * @param agent_id Agent identifier
     */
    void markAgentHealthy(const std::string& agent_id);
    
    /**
     * @brief Check if agent is healthy
     * @param agent_id Agent identifier
     * @return true if agent is healthy
     */
    bool isAgentHealthy(const std::string& agent_id);
    
    /**
     * @brief Update agent heartbeat timestamp
     * @param agent_id Agent identifier
     */
    void updateHeartbeat(const std::string& agent_id);
    
    /**
     * @brief Update agent load
     * @param agent_id Agent identifier
     * @param load New load value
     */
    void updateAgentLoad(const std::string& agent_id, int load);
    
    // === Configuration ===
    
    /**
     * @brief Set routing strategy
     * @param strategy New strategy
     */
    void setStrategy(RoutingStrategy strategy);
    
    /**
     * @brief Get current routing strategy
     * @return Current strategy
     */
    RoutingStrategy getStrategy() const;
    
    // === Statistics ===
    
    /**
     * @brief Get total number of agents
     */
    size_t getAgentCount() const;
    
    /**
     * @brief Get number of healthy agents
     */
    size_t getHealthyAgentCount() const;
    
    // === Dynamic Intent Classification (P0-1 / P1-1) ===
    
    /**
     * @brief Build a dynamic intent classification prompt from registered agents
     * 
     * Constructs an LLM prompt that lists all registered skills dynamically.
     * Replaces the hardcoded math/code/general prompt in the orchestrator.
     * 
     * @param user_text The user's input text
     * @return Complete prompt string ready to send to LLM
     */
    std::string buildDynamicIntentPrompt(const std::string& user_text) const;
    
    /**
     * @brief Get all unique skill names with their descriptions
     * 
     * Returns a map of skill_name → description for prompt building.
     */
    std::unordered_map<std::string, std::string> getAllSkillDescriptions() const;

    /**
     * @brief Set LLM client for dynamic intent classification (P1-1)
     *
     * When set, selectAgent() will use LLM-based intent classification
     * with buildDynamicIntentPrompt() before falling back to embedding/keyword routing.
     *
     * @param client LLMClient instance (caller transfers ownership)
     */
    void setLLMClient(std::unique_ptr<LLMClient> client);

    // === Embedding-based Routing (P3) ===

    /**
     * @brief Enable embedding-based skill routing
     * @param config Embedding router configuration
     * @return true if embedding service initialized successfully
     */
    bool enableEmbedding(const EmbeddingRouterConfig& config);

    /**
     * @brief Hybrid skill analysis: embedding (fast path) + keyword (fallback)
     *
     * Three-tier routing pipeline:
     * 1. Embedding high confidence (similarity > high_threshold) → direct route
     * 2. Fuzzy zone (low_threshold ~ high_threshold) → return result, caller decides
     * 3. Low confidence (< low_threshold) → fall back to keyword IDF matching
     *
     * @param question User input text
     * @return SkillMatchResult with skill name, confidence score, and source
     */
    SkillMatchResult analyzeRequiredSkillHybrid(const std::string& question);

    /**
     * @brief High-confidence embedding-only skill matching.
     *
     * Returns a skill only when embedding similarity >= high_threshold.
     * Used as the first tier in the restructured routing pipeline:
     *   Embedding(high) → LLM → Keyword → Fallback
     *
     * @param question User input text
     * @return Skill name if high-confidence match found, empty string otherwise
     */
    std::string analyzeRequiredSkillEmbedding(const std::string& question);

    /**
     * @brief Check if embedding routing is enabled
     */
    bool isEmbeddingEnabled() const;

private:
    /**
     * @brief Analyze question to determine required skill
     * @param question Question content
     * @return Detected skill or empty string
     */
    std::string analyzeRequiredSkill(const std::string& question);
    
    /**
     * @brief Select agent using current strategy
     * @param candidates List of candidate agents
     * @return Selected agent
     */
    AgentInfo selectByStrategy(const std::vector<AgentInfo>& candidates);
    
    /**
     * @brief Select using round-robin strategy
     */
    AgentInfo selectRoundRobin(const std::vector<AgentInfo>& candidates);
    
    /**
     * @brief Select using random strategy
     */
    AgentInfo selectRandom(const std::vector<AgentInfo>& candidates);
    
    /**
     * @brief Select using least-load strategy
     */
    AgentInfo selectLeastLoad(const std::vector<AgentInfo>& candidates);
    
    /**
     * @brief Rebuild the skill keyword index from current agents
     * 
     * Extracts keywords from healthy agents' skill names and descriptions.
     * Must be called while agents_mutex_ is held (e.g. from addAgent/removeAgent).
     */
    void rebuildSkillKeywordIndex();

    /**
     * @brief LLM-based intent classification using buildDynamicIntentPrompt() (P1-1)
     *
     * Calls LLM with a dynamically built prompt, parses the response as a
     * skill name, and does exact matching against registered skills.
     *
     * @param question User input text
     * @return Matched skill name, or empty string on failure
     */
    std::string analyzeIntentWithLLM(const std::string& question);

    /**
     * @brief Build/rebuild the skill embedding index from current agents
     *
     * Embeds each skill's "name + description" text and stores in skill_index_.
     * Called from rebuildSkillKeywordIndex() when embedding is enabled.
     */
    void buildSkillEmbeddingIndex();

    mutable std::mutex agents_mutex_;
    std::unordered_map<std::string, AgentInfo> agents_;
    RoutingStrategy strategy_ = RoutingStrategy::SKILL_MATCH;
    std::atomic<size_t> round_robin_index_{0};
    std::mt19937 random_generator_;
    bool initialized_ = false;

    // Inverted keyword index: keyword → list of (skill, IDF weight) entries.
    // Rebuilt on addAgent() / removeAgent().
    struct KeywordEntry {
        std::string skill_name;
        double weight;  // IDF: 1.0 / number of skills sharing this keyword
    };
    std::unordered_map<std::string, std::vector<KeywordEntry>> skill_keywords_;

    // Embedding-based routing (P3)
    EmbeddingRouterConfig embedding_config_;
    std::unique_ptr<agent_rpc::mcp::rag::EmbeddingService> embedding_service_;
    std::unique_ptr<agent_rpc::mcp::rag::VectorIndex> skill_index_;
    std::unique_ptr<agent_rpc::mcp::rag::EmbeddingCache> embedding_cache_;
    mutable std::mutex embedding_mutex_;
    std::atomic<uint64_t> embedding_query_count_{0};
    std::atomic<uint64_t> embedding_hit_count_{0};

    // LLM-based intent classification (P1-1)
    std::unique_ptr<LLMClient> llm_client_;
};

} // namespace orchestrator
} // namespace agent_rpc
