/**
 * @file agent_router.cpp
 * @brief Agent Router implementation
 * 
 * Task 8.3: 实现AgentRouter路由器
 */

#include "agent_rpc/orchestrator/agent_router.h"
#include <agent_rpc/mcp/rag/embedding_service.h>
#include <agent_rpc/mcp/rag/vector_index.h>
#include <agent_rpc/mcp/rag/embedding_cache.h>
#include <a2a/llm_client.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <sstream>

namespace {

// Common English stopwords to filter out when extracting keywords from descriptions
const std::unordered_set<std::string> STOPWORDS = {
    "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
    "have", "has", "had", "do", "does", "did", "will", "would", "shall",
    "should", "may", "might", "must", "can", "could", "to", "of", "in",
    "for", "on", "with", "at", "by", "from", "as", "into", "through",
    "and", "but", "or", "nor", "not", "so", "yet", "both", "either",
    "neither", "each", "every", "all", "any", "few", "more", "most",
    "other", "some", "such", "no", "only", "own", "same", "than", "too",
    "very", "just", "because", "if", "when", "while", "that", "this",
    "it", "its", "they", "them", "their", "we", "our", "you", "your",
    "he", "she", "his", "her", "i", "me", "my", "about", "which", "who",
    "whom", "what", "how", "where", "there", "here", "up", "out", "then"
};

// Decode one UTF-8 code point starting at position pos, return code point and advance pos
uint32_t decodeUtf8(const std::string& s, size_t& pos) {
    unsigned char c = static_cast<unsigned char>(s[pos]);
    uint32_t cp = 0;
    if (c < 0x80) {
        cp = c; pos += 1;
    } else if ((c & 0xE0) == 0xC0 && pos + 1 < s.size()) {
        cp = (c & 0x1F) << 6;
        cp |= (static_cast<unsigned char>(s[pos + 1]) & 0x3F);
        pos += 2;
    } else if ((c & 0xF0) == 0xE0 && pos + 2 < s.size()) {
        cp = (c & 0x0F) << 12;
        cp |= (static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 6;
        cp |= (static_cast<unsigned char>(s[pos + 2]) & 0x3F);
        pos += 3;
    } else if ((c & 0xF8) == 0xF0 && pos + 3 < s.size()) {
        cp = (c & 0x07) << 18;
        cp |= (static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 12;
        cp |= (static_cast<unsigned char>(s[pos + 2]) & 0x3F) << 6;
        cp |= (static_cast<unsigned char>(s[pos + 3]) & 0x3F);
        pos += 4;
    } else {
        pos += 1; // skip malformed byte
    }
    return cp;
}

// Encode a code point back to UTF-8
std::string encodeUtf8(uint32_t cp) {
    std::string result;
    if (cp < 0x80) {
        result += static_cast<char>(cp);
    } else if (cp < 0x800) {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        result += static_cast<char>(0xF0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return result;
}

bool isCJK(uint32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK Unified Ideographs
           (cp >= 0x3400 && cp <= 0x4DBF);      // CJK Extension A
}

// P1-3: Word-boundary keyword matching.
// ASCII keywords require whole-word match (bounded by non-alnum or string edge).
// CJK keywords (bigrams) use substring match since Chinese has no word boundaries.
bool matchKeyword(const std::string& text, const std::string& keyword) {
    if (keyword.empty()) return false;

    size_t pos = 0;
    while ((pos = text.find(keyword, pos)) != std::string::npos) {
        // Check CJK: if the first byte is a UTF-8 multi-byte lead, treat as CJK keyword
        unsigned char first = static_cast<unsigned char>(keyword[0]);
        if (first >= 0x80) {
            return true;  // CJK keyword — substring match is sufficient
        }

        // ASCII keyword — require word boundaries
        bool left_ok = (pos == 0) || !std::isalnum(static_cast<unsigned char>(text[pos - 1]));
        size_t end_pos = pos + keyword.size();
        bool right_ok = (end_pos >= text.size()) || !std::isalnum(static_cast<unsigned char>(text[end_pos]));

        if (left_ok && right_ok) {
            return true;
        }

        pos++;
    }
    return false;
}

} // anonymous namespace

namespace agent_rpc {
namespace orchestrator {

AgentRouter::AgentRouter() 
    : random_generator_(std::random_device{}()) {}

AgentRouter::~AgentRouter() {
    shutdown();
}

bool AgentRouter::initialize(RoutingStrategy strategy) {
    if (initialized_) {
        return true;
    }
    
    strategy_ = strategy;
    round_robin_index_ = 0;
    initialized_ = true;
    return true;
}

void AgentRouter::shutdown() {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(agents_mutex_);
    agents_.clear();
    initialized_ = false;
}

std::optional<AgentInfo> AgentRouter::selectAgent(
    const std::string& question,
    const std::vector<std::string>& required_skills) {

    // Phase 1: Determine skills to match (intent analysis).
    // Restructured pipeline: Embedding → LLM → Keyword → Fallback
    //   - Embedding first (cheapest, ~10ms vector search)
    //   - LLM only when embedding uncertain (saves tokens)
    //   - Keyword as local fallback (zero cost)
    //   - Any healthy agent as last resort
    std::vector<std::string> skills_to_match = required_skills;
    bool used_fallback = false;

    if (skills_to_match.empty() && strategy_ == RoutingStrategy::SKILL_MATCH) {
        // Tier 0: Embedding — high confidence only (≥ high_threshold)
        if (isEmbeddingEnabled()) {
            std::string emb_skill = analyzeRequiredSkillEmbedding(question);
            if (!emb_skill.empty()) {
                skills_to_match.push_back(emb_skill);
            }
        }

        // Tier 1: LLM — only when embedding uncertain/unavailable (saves tokens)
        if (skills_to_match.empty() && llm_client_) {
            std::string llm_skill = analyzeIntentWithLLM(question);
            if (!llm_skill.empty()) {
                skills_to_match.push_back(llm_skill);
            }
        }

        // Tier 2: Keyword IDF matching — pure local, zero cost
        if (skills_to_match.empty()) {
            std::lock_guard<std::mutex> lock(agents_mutex_);
            std::string detected_skill = analyzeRequiredSkill(question);
            if (!detected_skill.empty()) {
                skills_to_match.push_back(detected_skill);
            }
        }

        // Tier 3: Fallback — mark for Phase 2 to pick any healthy agent
        if (skills_to_match.empty()) {
            used_fallback = true;
        }
    }

    // Phase 2: Filter and select from candidate agents
    std::lock_guard<std::mutex> lock(agents_mutex_);

    if (agents_.empty()) {
        return std::nullopt;
    }

    // Build candidate list
    std::vector<AgentInfo> candidates;

    if (used_fallback || skills_to_match.empty()) {
        // Fallback or no skill requirements: use all healthy agents
        for (const auto& [id, agent] : agents_) {
            if (agent.is_healthy) {
                candidates.push_back(agent);
            }
        }
    } else {
        // Filter agents by skill requirements
        for (const auto& [id, agent] : agents_) {
            if (!agent.is_healthy) continue;
            if (!agent.hasAnySkill(skills_to_match)) continue;
            candidates.push_back(agent);
        }

        // If no candidates found with required skills, fall back to all healthy agents
        if (candidates.empty()) {
            for (const auto& [id, agent] : agents_) {
                if (agent.is_healthy) {
                    candidates.push_back(agent);
                }
            }
        }
    }
    
    if (candidates.empty()) {
        return std::nullopt;
    }
    
    return selectByStrategy(candidates);
}

AgentRouter::DispatchResult AgentRouter::dispatch(
    const std::string& question,
    const AgentDispatchFn& call_fn,
    const std::vector<std::string>& required_skills) {

    auto start = std::chrono::steady_clock::now();
    DispatchResult result;

    // Route: select the best agent
    auto agent = selectAgent(question, required_skills);
    if (!agent.has_value()) {
        result.response = "No available agent to handle this request.";
        return result;
    }

    result.agent_id = agent->id;
    result.agent_name = agent->name;

    // Determine matched skill (first skill of selected agent, or first required_skill)
    if (!required_skills.empty()) {
        result.matched_skill = required_skills.front();
    } else if (!agent->skills.empty()) {
        result.matched_skill = agent->skills.front();
    }

    // Call: invoke agent via the injected callback
    try {
        result.response = call_fn(agent->url, question);
        result.success = !result.response.empty();
    } catch (const std::exception& e) {
        result.response = std::string("Agent call failed: ") + e.what();
    }

    auto end = std::chrono::steady_clock::now();
    result.latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    return result;
}

std::vector<AgentInfo> AgentRouter::findAgentsBySkill(const std::string& skill) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    std::vector<AgentInfo> result;
    for (const auto& [id, agent] : agents_) {
        if (agent.hasSkill(skill)) {
            result.push_back(agent);
        }
    }
    return result;
}

std::vector<AgentInfo> AgentRouter::findAgentsByTags(const std::vector<std::string>& tags) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    std::vector<AgentInfo> result;
    for (const auto& [id, agent] : agents_) {
        bool has_all_tags = true;
        for (const auto& tag : tags) {
            if (!agent.hasTag(tag)) {
                has_all_tags = false;
                break;
            }
        }
        if (has_all_tags) {
            result.push_back(agent);
        }
    }
    return result;
}

std::vector<AgentInfo> AgentRouter::findHealthyAgentsWithSkills(
    const std::vector<std::string>& required_skills) {
    
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    std::vector<AgentInfo> result;
    for (const auto& [id, agent] : agents_) {
        if (agent.is_healthy && agent.hasAnySkill(required_skills)) {
            result.push_back(agent);
        }
    }
    return result;
}

void AgentRouter::updateAgentList(const std::vector<AgentInfo>& agents) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    agents_.clear();
    for (const auto& agent : agents) {
        agents_[agent.id] = agent;
    }
    rebuildSkillKeywordIndex();
}

void AgentRouter::addAgent(const AgentInfo& agent) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    agents_[agent.id] = agent;
    rebuildSkillKeywordIndex();
}

bool AgentRouter::removeAgent(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    bool removed = agents_.erase(agent_id) > 0;
    if (removed) {
        rebuildSkillKeywordIndex();
    }
    return removed;
}

std::optional<AgentInfo> AgentRouter::getAgent(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    auto it = agents_.find(agent_id);
    if (it != agents_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<AgentInfo> AgentRouter::getAllAgents() {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    std::vector<AgentInfo> result;
    result.reserve(agents_.size());
    for (const auto& [id, agent] : agents_) {
        result.push_back(agent);
    }
    return result;
}

std::vector<AgentInfo> AgentRouter::getHealthyAgents() {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    std::vector<AgentInfo> result;
    for (const auto& [id, agent] : agents_) {
        if (agent.is_healthy) {
            result.push_back(agent);
        }
    }
    return result;
}

void AgentRouter::markAgentUnhealthy(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    auto it = agents_.find(agent_id);
    if (it != agents_.end()) {
        it->second.is_healthy = false;
    }
}

void AgentRouter::markAgentHealthy(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    auto it = agents_.find(agent_id);
    if (it != agents_.end()) {
        it->second.is_healthy = true;
        it->second.last_heartbeat = std::chrono::steady_clock::now();
    }
}

bool AgentRouter::isAgentHealthy(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    auto it = agents_.find(agent_id);
    if (it != agents_.end()) {
        return it->second.is_healthy;
    }
    return false;
}

void AgentRouter::updateHeartbeat(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    auto it = agents_.find(agent_id);
    if (it != agents_.end()) {
        it->second.last_heartbeat = std::chrono::steady_clock::now();
    }
}

void AgentRouter::updateAgentLoad(const std::string& agent_id, int load) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    auto it = agents_.find(agent_id);
    if (it != agents_.end()) {
        it->second.current_load = load;
    }
}

void AgentRouter::setStrategy(RoutingStrategy strategy) {
    strategy_ = strategy;
}

RoutingStrategy AgentRouter::getStrategy() const {
    return strategy_;
}

size_t AgentRouter::getAgentCount() const {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    return agents_.size();
}

size_t AgentRouter::getHealthyAgentCount() const {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    size_t count = 0;
    for (const auto& [id, agent] : agents_) {
        if (agent.is_healthy) {
            count++;
        }
    }
    return count;
}

std::string AgentRouter::analyzeRequiredSkill(const std::string& question) {
    // Lowercase the question for case-insensitive matching
    std::string lower_question;
    lower_question.reserve(question.size());
    for (char c : question) {
        lower_question += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Aggregate IDF-weighted scores across all matched keywords per skill.
    // Unique keywords (low IDF denominator) contribute more; shared keywords
    // contribute less — naturally downweighting generic terms.
    std::unordered_map<std::string, double> skill_scores;

    for (const auto& [keyword, entries] : skill_keywords_) {
        if (matchKeyword(lower_question, keyword)) {
            for (const auto& entry : entries) {
                skill_scores[entry.skill_name] += entry.weight;
            }
        }
    }

    // Pick the skill with the highest aggregated score
    std::string best_skill;
    double best_score = 0.0;
    for (const auto& [skill, score] : skill_scores) {
        if (score > best_score) {
            best_score = score;
            best_skill = skill;
        }
    }

    return best_skill;  // Empty string if no skill detected
}

void AgentRouter::rebuildSkillKeywordIndex() {
    // Must be called while agents_mutex_ is held.
    //
    // Two-pass construction:
    //   Pass 1 — collect keyword → set<skill_name> into a temporary map
    //   Pass 2 — compute IDF weight (1.0 / skill count) and build inverted index
    //
    // This ensures shared keywords (e.g. "write" appearing in both code-generation
    // and article-writing) get a lower weight, while unique keywords retain full
    // signal strength.

    skill_keywords_.clear();

    // Pass 1: collect keyword → set<skill>
    std::unordered_map<std::string, std::unordered_set<std::string>> keyword_to_skills;

    auto add_keyword = [&](const std::string& keyword, const std::string& skill) {
        if (!keyword.empty()) {
            keyword_to_skills[keyword].insert(skill);
        }
    };

    for (const auto& [id, agent] : agents_) {
        if (!agent.is_healthy) continue;

        for (size_t i = 0; i < agent.skills.size(); ++i) {
            const std::string& skill = agent.skills[i];

            // 1) Add the skill name itself as a keyword (lowercased)
            std::string lower_skill;
            for (char c : skill) {
                lower_skill += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            add_keyword(lower_skill, skill);

            // 2) Split skill name by - and _ as additional keywords
            std::istringstream name_stream(skill);
            std::string token;
            while (std::getline(name_stream, token, '-')) {
                std::istringstream sub_stream(token);
                std::string sub_token;
                while (std::getline(sub_stream, sub_token, '_')) {
                    std::string lower_token;
                    for (char c : sub_token) {
                        lower_token += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    add_keyword(lower_token, skill);
                }
            }

            // 3) Extract keywords from skill description
            auto desc_it = agent.skill_descriptions.find(skill);
            if (desc_it == agent.skill_descriptions.end() || desc_it->second.empty()) {
                continue;
            }

            const std::string& desc = desc_it->second;

            // Extract English words (space-delimited, filtered by stopwords)
            std::istringstream word_stream(desc);
            std::string word;
            while (word_stream >> word) {
                // Strip trailing punctuation
                while (!word.empty() && (word.back() == ',' || word.back() == '.' ||
                       word.back() == ';' || word.back() == ':' || word.back() == ')' ||
                       word.back() == '(')) {
                    word.pop_back();
                }
                std::string lower_word;
                for (char c : word) {
                    lower_word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (lower_word.size() >= 2 && STOPWORDS.find(lower_word) == STOPWORDS.end()) {
                    add_keyword(lower_word, skill);
                }
            }

            // Extract Chinese character bigrams for CJK text matching
            size_t pos = 0;
            std::vector<uint32_t> codepoints;
            while (pos < desc.size()) {
                codepoints.push_back(decodeUtf8(desc, pos));
            }
            for (size_t j = 0; j + 1 < codepoints.size(); ++j) {
                if (isCJK(codepoints[j]) && isCJK(codepoints[j + 1])) {
                    std::string bigram = encodeUtf8(codepoints[j]) + encodeUtf8(codepoints[j + 1]);
                    add_keyword(bigram, skill);
                }
            }
        }
    }

    // Pass 2: build inverted index with IDF weights
    for (const auto& [keyword, skills] : keyword_to_skills) {
        double weight = 1.0 / static_cast<double>(skills.size());
        std::vector<KeywordEntry> entries;
        entries.reserve(skills.size());
        for (const auto& skill : skills) {
            entries.push_back({skill, weight});
        }
        skill_keywords_[keyword] = std::move(entries);
    }

    // Rebuild embedding index when agents change (if embedding routing is enabled)
    if (isEmbeddingEnabled()) {
        buildSkillEmbeddingIndex();
    }
}

AgentInfo AgentRouter::selectByStrategy(const std::vector<AgentInfo>& candidates) {
    if (candidates.size() == 1) {
        return candidates[0];
    }
    
    switch (strategy_) {
        case RoutingStrategy::ROUND_ROBIN:
            return selectRoundRobin(candidates);
        case RoutingStrategy::RANDOM:
            return selectRandom(candidates);
        case RoutingStrategy::LEAST_LOAD:
            return selectLeastLoad(candidates);
        case RoutingStrategy::SKILL_MATCH:
        default:
            // For skill match, candidates are already filtered, use round-robin
            return selectRoundRobin(candidates);
    }
}

AgentInfo AgentRouter::selectRoundRobin(const std::vector<AgentInfo>& candidates) {
    size_t index = round_robin_index_.fetch_add(1) % candidates.size();
    return candidates[index];
}

AgentInfo AgentRouter::selectRandom(const std::vector<AgentInfo>& candidates) {
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(random_generator_)];
}

AgentInfo AgentRouter::selectLeastLoad(const std::vector<AgentInfo>& candidates) {
    auto min_it = std::min_element(candidates.begin(), candidates.end(),
        [](const AgentInfo& a, const AgentInfo& b) {
            return a.current_load < b.current_load;
        });
    return *min_it;
}

// === Dynamic Intent Classification (P0-1 / P1-1) ===

std::string AgentRouter::buildDynamicIntentPrompt(const std::string& user_text) const {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    
    // Collect all unique skills with descriptions from healthy agents
    std::unordered_map<std::string, std::string> all_skills;
    
    for (const auto& [id, agent] : agents_) {
        if (!agent.is_healthy) continue;
        
        for (const auto& skill : agent.skills) {
            // Only add if not already present (first occurrence wins)
            if (all_skills.find(skill) == all_skills.end()) {
                auto desc_it = agent.skill_descriptions.find(skill);
                if (desc_it != agent.skill_descriptions.end() && !desc_it->second.empty()) {
                    all_skills[skill] = desc_it->second;
                } else {
                    all_skills[skill] = "";
                }
            }
        }
    }
    
    // If no skills registered, fall back to a minimal prompt
    if (all_skills.empty()) {
        return "判断以下用户输入的意图类型，只回答类型名称。\n"
               "用户输入: " + user_text;
    }
    
    // Build the dynamic prompt
    std::ostringstream prompt;
    prompt << "判断以下用户输入最匹配哪个技能，只回答技能名称之一：\n";
    
    for (const auto& [skill, description] : all_skills) {
        prompt << "- " << skill;
        if (!description.empty()) {
            prompt << ": " << description;
        }
        prompt << "\n";
    }
    
    prompt << "- none: 以上都不匹配\n\n";
    prompt << "用户输入: " << user_text;
    
    return prompt.str();
}

std::unordered_map<std::string, std::string> AgentRouter::getAllSkillDescriptions() const {
    std::lock_guard<std::mutex> lock(agents_mutex_);

    std::unordered_map<std::string, std::string> result;

    for (const auto& [id, agent] : agents_) {
        if (!agent.is_healthy) continue;

        for (const auto& skill : agent.skills) {
            if (result.find(skill) == result.end()) {
                auto desc_it = agent.skill_descriptions.find(skill);
                if (desc_it != agent.skill_descriptions.end()) {
                    result[skill] = desc_it->second;
                } else {
                    result[skill] = "";
                }
            }
        }
    }

    return result;
}

void AgentRouter::setLLMClient(std::unique_ptr<LLMClient> client) {
    llm_client_ = std::move(client);
}

std::string AgentRouter::analyzeIntentWithLLM(const std::string& question) {
    if (!llm_client_) return {};

    // Build dynamic prompt from registered agents (no agents_mutex_ needed here —
    // buildDynamicIntentPrompt() acquires it internally)
    std::string prompt = buildDynamicIntentPrompt(question);
    if (prompt.empty()) return {};

    // Call LLM for intent classification
    std::string response;
    try {
        response = llm_client_->chat(
            "你是一个意图分类器。只回答列出的技能名称之一，不要解释。",
            prompt);
    } catch (...) {
        return {};
    }

    // Trim whitespace and lowercase
    while (!response.empty() && std::isspace(static_cast<unsigned char>(response.front()))) {
        response.erase(response.begin());
    }
    while (!response.empty() && std::isspace(static_cast<unsigned char>(response.back()))) {
        response.pop_back();
    }
    std::string lower_response;
    for (char c : response) {
        lower_response += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // "none" means no match
    if (lower_response == "none" || lower_response.empty()) return {};

    // Exact match against registered skill names (case-insensitive)
    std::lock_guard<std::mutex> lock(agents_mutex_);
    for (const auto& [id, agent] : agents_) {
        if (!agent.is_healthy) continue;
        for (const auto& skill : agent.skills) {
            std::string lower_skill;
            for (char c : skill) {
                lower_skill += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (lower_response == lower_skill) {
                return skill;
            }
        }
    }

    return {};  // LLM returned a skill that's not registered
}

// === Embedding-based Routing (P3) ===

bool AgentRouter::enableEmbedding(const EmbeddingRouterConfig& config) {
    std::lock_guard<std::mutex> lock(embedding_mutex_);

    embedding_config_ = config;

    if (!config.enabled) {
        embedding_service_.reset();
        skill_index_.reset();
        embedding_cache_.reset();
        return true;
    }

    try {
        agent_rpc::mcp::rag::EmbeddingConfig emb_config;
        emb_config.api_key = config.api_key;
        emb_config.model = config.model;
        emb_config.dimension = config.dimension;
        emb_config.api_url = config.api_url;

        embedding_service_ = std::make_unique<agent_rpc::mcp::rag::EmbeddingService>(emb_config);
        skill_index_ = std::make_unique<agent_rpc::mcp::rag::VectorIndex>();
        skill_index_->setVersion(config.model);

        agent_rpc::mcp::rag::CacheConfig cache_config;
        cache_config.max_size = 500;
        cache_config.ttl_seconds = 3600;
        embedding_cache_ = std::make_unique<agent_rpc::mcp::rag::EmbeddingCache>(cache_config);

    } catch (const std::exception&) {
        embedding_service_.reset();
        skill_index_.reset();
        embedding_cache_.reset();
        embedding_config_.enabled = false;
        return false;
    }

    // Validate thresholds
    if (config.high_threshold <= config.low_threshold) {
        embedding_service_.reset();
        skill_index_.reset();
        embedding_cache_.reset();
        embedding_config_.enabled = false;
        return false;
    }

    // Build initial embedding index.
    // Must hold agents_mutex_ to protect agents_ iteration inside
    // buildSkillEmbeddingIndex(). Lock order: agents_mutex_ → embedding_mutex_.
    try {
        std::lock_guard<std::mutex> agents_lock(agents_mutex_);
        buildSkillEmbeddingIndex();
    } catch (const std::exception&) {
        embedding_service_.reset();
        skill_index_.reset();
        embedding_cache_.reset();
        embedding_config_.enabled = false;
        return false;
    }

    return true;
}

bool AgentRouter::isEmbeddingEnabled() const {
    return embedding_config_.enabled && embedding_service_ != nullptr;
}

void AgentRouter::buildSkillEmbeddingIndex() {
    // Locking: acquires embedding_mutex_ internally.
    // Callers must hold agents_mutex_ (for iterating agents_).
    // Lock order: agents_mutex_ → embedding_mutex_ (consistent with rest of codebase).
    std::lock_guard<std::mutex> lock(embedding_mutex_);
    if (!embedding_service_ || !skill_index_) return;

    skill_index_->clear();

    for (const auto& [id, agent] : agents_) {
        if (!agent.is_healthy) continue;

        for (const auto& skill : agent.skills) {
            // Build embedding text: "skill_name: description"
            std::string text = skill;
            auto desc_it = agent.skill_descriptions.find(skill);
            if (desc_it != agent.skill_descriptions.end() && !desc_it->second.empty()) {
                text += ": " + desc_it->second;
            }

            // Check cache first
            std::vector<float> embedding;
            if (embedding_cache_) {
                auto cached = embedding_cache_->get(text);
                if (cached.has_value()) {
                    embedding = cached.value();
                }
            }

            if (embedding.empty()) {
                try {
                    embedding = embedding_service_->embed(text);
                    if (embedding_cache_ && !embedding.empty()) {
                        embedding_cache_->put(text, embedding);
                    }
                } catch (const std::exception&) {
                    // Skip this skill if embedding fails
                    continue;
                }
            }

            // Store in vector index (reuse IndexedTool with skill data)
            agent_rpc::mcp::rag::IndexedTool tool;
            tool.name = skill;
            tool.description = (desc_it != agent.skill_descriptions.end()) ? desc_it->second : "";
            tool.embedding = std::move(embedding);
            skill_index_->addTool(tool);
        }
    }
}

SkillMatchResult AgentRouter::analyzeRequiredSkillHybrid(const std::string& question) {
    // Three-tier routing pipeline:
    //   1. Embedding high confidence → direct route
    //   2. Fuzzy zone → return with confidence, caller decides
    //   3. Low confidence → fall back to keyword IDF matching

    SkillMatchResult result;

    // Tier 1 & 2: Try embedding match if enabled
    if (isEmbeddingEnabled()) {
        embedding_query_count_.fetch_add(1);

        std::lock_guard<std::mutex> lock(embedding_mutex_);

        try {
            // Embed the question
            std::vector<float> query_embedding;
            if (embedding_cache_) {
                auto cached = embedding_cache_->get(question);
                if (cached.has_value()) {
                    query_embedding = cached.value();
                }
            }
            if (query_embedding.empty()) {
                query_embedding = embedding_service_->embed(question);
                if (embedding_cache_ && !query_embedding.empty()) {
                    embedding_cache_->put(question, query_embedding);
                }
            }

            // Search skill index
            auto search_results = skill_index_->search(query_embedding, 1, 0.0f);

            if (!search_results.empty()) {
                const auto& best = search_results[0];

                if (best.similarity >= embedding_config_.high_threshold) {
                    // High confidence: direct route
                    result.skill_name = best.tool.name;
                    result.confidence = best.similarity;
                    result.source = SkillMatchResult::EMBEDDING;
                    embedding_hit_count_.fetch_add(1);
                    return result;
                }

                if (best.similarity >= embedding_config_.low_threshold) {
                    // Fuzzy zone: return result, let caller decide
                    result.skill_name = best.tool.name;
                    result.confidence = best.similarity;
                    result.source = SkillMatchResult::EMBEDDING;
                    embedding_hit_count_.fetch_add(1);
                    return result;
                }
            }
        } catch (const std::exception&) {
            // Embedding failed, fall through to keyword matching
        }
    }

    // Tier 3: Fall back to keyword IDF matching
    std::lock_guard<std::mutex> lock(agents_mutex_);
    std::string keyword_skill = analyzeRequiredSkill(question);

    if (!keyword_skill.empty()) {
        result.skill_name = keyword_skill;
        result.confidence = 0.6f;  // keyword match gets moderate confidence
        result.source = SkillMatchResult::KEYWORD;
    }

    return result;
}

std::string AgentRouter::analyzeRequiredSkillEmbedding(const std::string& question) {
    if (!isEmbeddingEnabled()) return {};

    embedding_query_count_.fetch_add(1);

    std::lock_guard<std::mutex> lock(embedding_mutex_);

    try {
        // Embed the question (check cache first)
        std::vector<float> query_embedding;
        if (embedding_cache_) {
            auto cached = embedding_cache_->get(question);
            if (cached.has_value()) {
                query_embedding = cached.value();
            }
        }

        if (query_embedding.empty()) {
            query_embedding = embedding_service_->embed(question);
            if (embedding_cache_ && !query_embedding.empty()) {
                embedding_cache_->put(question, query_embedding);
            }
        }

        // Search skill index
        auto search_results = skill_index_->search(query_embedding, 1, 0.0f);

        if (!search_results.empty()) {
            const auto& best = search_results[0];
            if (best.similarity >= embedding_config_.high_threshold) {
                embedding_hit_count_.fetch_add(1);
                return best.tool.name;
            }
        }
    } catch (const std::exception&) {
        // Embedding failed, caller falls through to next tier
    }

    return {};
}

} // namespace orchestrator
} // namespace agent_rpc
