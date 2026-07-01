#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * @brief Agent 注册信息
 */
struct AgentRegistration {
    std::string id;              // Agent 唯一 ID
    std::string name;            // Agent 名称
    std::string address;         // Agent 地址 (http://host:port)
    std::vector<std::string> tags;  // Agent 标签 (如 "math", "translation")
    std::chrono::system_clock::time_point last_heartbeat;  // 最后心跳时间
    json agent_card;             // Agent Card (A2A 协议标准)
    std::vector<std::string> skills;  // Extracted skill names from agent_card
    
    // 序列化
    json to_json() const {
        json j = {
            {"id", id},
            {"name", name},
            {"address", address},
            {"tags", tags},
            {"last_heartbeat", std::chrono::system_clock::to_time_t(last_heartbeat)}
        };
        if (!agent_card.empty()) {
            j["agent_card"] = agent_card;
        }
        if (!skills.empty()) {
            j["skills"] = skills;
        }
        return j;
    }
    
    // 反序列化
    static AgentRegistration from_json(const json& j) {
        AgentRegistration reg;
        reg.id = j.at("id").get<std::string>();
        reg.name = j.at("name").get<std::string>();
        reg.address = j.at("address").get<std::string>();
        reg.tags = j.at("tags").get<std::vector<std::string>>();
        reg.last_heartbeat = std::chrono::system_clock::now();
        if (j.contains("agent_card")) {
            reg.agent_card = j["agent_card"];
        }
        if (j.contains("skills")) {
            reg.skills = j["skills"].get<std::vector<std::string>>();
        }
        return reg;
    }
    
    /**
     * @brief Extract skill names from agent_card JSON
     * 
     * Parses the agent_card.skills array and populates the skills vector.
     * Falls back to tags if agent_card has no skills defined.
     */
    void extract_skills_from_agent_card() {
        skills.clear();
        
        if (!agent_card.empty() && agent_card.contains("skills") && agent_card["skills"].is_array()) {
            for (const auto& skill_json : agent_card["skills"]) {
                if (skill_json.contains("name") && skill_json["name"].is_string()) {
                    std::string skill_name = skill_json["name"].get<std::string>();
                    if (!skill_name.empty()) {
                        skills.push_back(skill_name);
                    }
                }
            }
        }
        
        // Fallback: use tags as skills if no AgentCard skills found
        if (skills.empty() && !tags.empty()) {
            skills = tags;
        }
    }
};

/**
 * @brief Agent 注册中心
 * 
 * In-memory registry with both tag-based and skill-based indexing.
 * Skills are extracted from AgentCard during registration.
 */
class AgentRegistry {
public:
    explicit AgentRegistry(int heartbeat_timeout_sec = 30, int cleanup_interval_sec = 60)
        : heartbeat_timeout_(heartbeat_timeout_sec)
        , cleanup_interval_(cleanup_interval_sec) {}
    
    // 注册 Agent
    bool register_agent(const AgentRegistration& registration) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Clean stale index entries if agent re-registers with different tags/skills
        auto existing = agents_.find(registration.id);
        if (existing != agents_.end()) {
            for (const auto& tag : existing->second.tags) {
                tags_index_[tag].erase(registration.id);
            }
            for (const auto& skill : existing->second.skills) {
                skills_index_[skill].erase(registration.id);
            }
        }
        
        auto& reg = agents_[registration.id];
        reg = registration;
        reg.last_heartbeat = std::chrono::system_clock::now();
        
        // Extract skills from AgentCard
        reg.extract_skills_from_agent_card();
        
        // 按标签索引
        for (const auto& tag : reg.tags) {
            tags_index_[tag].insert(registration.id);
        }
        
        // 按技能索引
        for (const auto& skill : reg.skills) {
            skills_index_[skill].insert(registration.id);
        }
        
        return true;
    }
    
    // 注销 Agent
    bool deregister_agent(const std::string& agent_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = agents_.find(agent_id);
        if (it == agents_.end()) {
            return false;
        }
        
        // 从标签索引中移除
        for (const auto& tag : it->second.tags) {
            tags_index_[tag].erase(agent_id);
        }
        
        // 从技能索引中移除
        for (const auto& skill : it->second.skills) {
            skills_index_[skill].erase(agent_id);
        }
        
        agents_.erase(it);
        return true;
    }
    
    // 心跳
    bool heartbeat(const std::string& agent_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = agents_.find(agent_id);
        if (it == agents_.end()) {
            return false;
        }
        
        it->second.last_heartbeat = std::chrono::system_clock::now();
        return true;
    }
    
    // 根据标签查找 Agent
    std::vector<AgentRegistration> find_agents_by_tag(const std::string& tag) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<AgentRegistration> result;
        
        auto tag_it = tags_index_.find(tag);
        if (tag_it == tags_index_.end()) {
            return result;
        }
        
        for (const auto& agent_id : tag_it->second) {
            auto agent_it = agents_.find(agent_id);
            if (agent_it != agents_.end()) {
                result.push_back(agent_it->second);
            }
        }
        
        return result;
    }
    
    // 根据技能查找 Agent
    std::vector<AgentRegistration> find_agents_by_skill(const std::string& skill) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<AgentRegistration> result;
        
        auto skill_it = skills_index_.find(skill);
        if (skill_it == skills_index_.end()) {
            return result;
        }
        
        for (const auto& agent_id : skill_it->second) {
            auto agent_it = agents_.find(agent_id);
            if (agent_it != agents_.end()) {
                result.push_back(agent_it->second);
            }
        }
        
        return result;
    }
    
    // 获取所有 Agent
    std::vector<AgentRegistration> get_all_agents() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<AgentRegistration> result;
        for (const auto& pair : agents_) {
            result.push_back(pair.second);
        }
        return result;
    }
    
    /**
     * @brief Get all unique skill names across all registered agents
     * 
     * Returns a map of skill_name → list of agent IDs that have that skill.
     * Used by AgentRouter to build dynamic intent classification prompts.
     */
    std::map<std::string, std::vector<std::string>> get_all_skills() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::map<std::string, std::vector<std::string>> result;
        for (const auto& [skill, agent_ids] : skills_index_) {
            result[skill] = std::vector<std::string>(agent_ids.begin(), agent_ids.end());
        }
        return result;
    }
    
    // 健康检查，移除超时的 Agent
    void check_health() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        std::vector<std::string> to_remove;
        
        for (const auto& pair : agents_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - pair.second.last_heartbeat).count();
            
            if (elapsed > heartbeat_timeout_) {
                to_remove.push_back(pair.first);
            }
        }
        
        // 移除超时的 Agent
        for (const auto& agent_id : to_remove) {
            auto it = agents_.find(agent_id);
            if (it != agents_.end()) {
                // 从标签索引中移除
                for (const auto& tag : it->second.tags) {
                    tags_index_[tag].erase(agent_id);
                }
                // 从技能索引中移除
                for (const auto& skill : it->second.skills) {
                    skills_index_[skill].erase(agent_id);
                }
                agents_.erase(it);
            }
        }
    }
    
private:
    std::mutex mutex_;
    std::map<std::string, AgentRegistration> agents_;  // agent_id -> registration
    std::map<std::string, std::set<std::string>> tags_index_;  // tag -> agent_ids
    std::map<std::string, std::set<std::string>> skills_index_;  // skill -> agent_ids
    int heartbeat_timeout_;  // 心跳超时时间（秒）
    int cleanup_interval_;   // 清理间隔（秒）
};
