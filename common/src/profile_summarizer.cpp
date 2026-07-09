#include "agent_rpc/common/profile_summarizer.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

namespace agent_rpc {
namespace common {

using json = nlohmann::json;

std::string ProfileSummarizer::summarize(const std::string& identity_json,
                                          const std::string& preferences_json,
                                          int max_tokens) {
    std::ostringstream oss;

    // --- Identity ---
    if (!identity_json.empty()) {
        try {
            auto id = json::parse(identity_json);
            std::string role = id.value("role", std::string());
            std::string industry = id.value("industry", std::string());
            std::string name = id.value("name", std::string());

            if (!name.empty() && !role.empty()) {
                oss << "用户是" << role << " " << name;
            } else if (!role.empty()) {
                oss << "用户是" << role;
            } else if (!name.empty()) {
                oss << "用户" << name;
            }
            if (!industry.empty()) {
                oss << "，行业" << industry;
            }
            oss << "。";
        } catch (const json::exception&) {
            // Malformed JSON — skip identity
        }
    }

    // --- Preferences ---
    if (!preferences_json.empty()) {
        try {
            auto prefs = json::parse(preferences_json);
            if (prefs.is_array() && !prefs.empty()) {
                // Collect all values into a single preference line
                std::string style;
                std::string tone;
                std::string language;
                for (const auto& p : prefs) {
                    std::string key = p.value("key", "");
                    std::string val = p.value("value", "");
                    if (key == "style")       style = val;
                    else if (key == "tone")    tone = val;
                    else if (key == "language") language = val;
                }
                if (!style.empty() || !tone.empty() || !language.empty()) {
                    oss << "偏好";
                    if (!style.empty())  oss << style << "风格";
                    if (!tone.empty())   oss << "、" << tone << "语气";
                    if (!language.empty()) oss << "、" << language << "语言";
                    oss << "。";
                }
            }
        } catch (const json::exception&) {
            // Malformed JSON — skip preferences
        }
    }

    // --- Context snapshot placeholders ---
    // The context_snapshot field is handled separately via Redis
    // in memory_service.cpp — we add a placeholder here if the
    // summary is otherwise empty.
    if (oss.tellp() == 0) {
        oss << "用户信息摘要不可用。";
    }

    std::string result = oss.str();

    // Rough token-budget enforcement: truncate at max_tokens characters
    // (A reasonable approximation for plain-text CJK + ASCII).
    if (static_cast<int>(result.size()) > max_tokens) {
        result.resize(max_tokens);
        // Re-trim at the last sentence boundary to avoid cutting mid-sentence
        auto last_period = result.rfind("。");
        auto last_dot   = result.rfind('.');
        auto boundary   = std::max(last_period, last_dot);
        if (boundary != std::string::npos && boundary > max_tokens / 2) {
            result.resize(boundary + 1);  // Keep the period
        }
    }

    return result;
}

void ProfileSummarizer::processPending() {
    // Placeholder for future LLM-based extraction.
    // Called periodically from BackgroundScheduler.
    // In a future iteration, this would scan recent conversation
    // history for identity/preference hints and persist them to
    // the user_profiles table or Redis.
}

}  // namespace common
}  // namespace agent_rpc
