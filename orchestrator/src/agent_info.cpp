/**
 * @file agent_info.cpp
 * @brief AgentInfo implementation - bridge from AgentRegistration to routing layer
 * 
 * P0-3: Parses AgentCard JSON from HTTP Registry registration data
 * to populate AgentInfo.skills and AgentInfo.skill_descriptions for routing.
 */

#include "agent_rpc/orchestrator/agent_info.h"
#include <a2a/examples/agent_registry.hpp>
#include <iostream>

namespace agent_rpc {
namespace orchestrator {

AgentInfo AgentInfo::from_registration(const AgentRegistration& reg) {
    AgentInfo info;
    
    // Basic fields
    info.id = reg.id;
    info.name = reg.name;
    info.url = reg.address;
    info.tags = reg.tags;
    info.is_healthy = true;
    info.last_heartbeat = std::chrono::steady_clock::now();
    
    // Parse AgentCard to extract skills
    if (!reg.agent_card.empty() && reg.agent_card.contains("skills")) {
        const auto& skills_array = reg.agent_card["skills"];
        
        if (skills_array.is_array()) {
            for (const auto& skill_json : skills_array) {
                // Extract skill name (required field)
                std::string skill_name;
                if (skill_json.contains("name") && skill_json["name"].is_string()) {
                    skill_name = skill_json["name"].get<std::string>();
                }
                
                if (skill_name.empty()) {
                    continue;  // Skip skills without a name
                }
                
                // Add to skills list
                info.skills.push_back(skill_name);
                
                // Extract skill description (optional but valuable for dynamic prompts)
                if (skill_json.contains("description") && skill_json["description"].is_string()) {
                    info.skill_descriptions[skill_name] = skill_json["description"].get<std::string>();
                }
            }
        }
    }
    
    // Fallback: if AgentCard has no skills, use tags as skills
    // This ensures backward compatibility with agents that only register with tags
    if (info.skills.empty() && !reg.tags.empty()) {
        info.skills = reg.tags;
    }
    
    // Extract agent-level metadata from AgentCard
    if (!reg.agent_card.empty()) {
        if (reg.agent_card.contains("description") && reg.agent_card["description"].is_string()) {
            info.description = reg.agent_card["description"].get<std::string>();
        }
        if (reg.agent_card.contains("version") && reg.agent_card["version"].is_string()) {
            info.version = reg.agent_card["version"].get<std::string>();
        }
    }
    
    return info;
}

} // namespace orchestrator
} // namespace agent_rpc
