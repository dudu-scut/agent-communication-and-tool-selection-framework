#pragma once

#include <string>

namespace agent_rpc {
namespace common {

/**
 * @brief ProfileSummarizer — Template-based user profile summarization
 *
 * Batch 4, U2: Compresses identity and preference JSON into a short
 * human-readable summary string for injection into SystemContext.
 *
 * This basic version uses string formatting only (no LLM).  A future
 * iteration may add LLM-based extraction via processPending().
 */
class ProfileSummarizer {
public:
    /**
     * Produce a concise text summary from identity and preferences JSON.
     *
     * Input format expected:
     *   identity_json:     {"role": "...", "industry": "...", ...}
     *   preferences_json:  [{"key": "style", "value": "..."}, ...]
     *
     * @param identity_json     JSON blob describing the user's identity
     * @param preferences_json  JSON array of preference key-value pairs
     * @param max_tokens        Rough character budget (default 300)
     * @return                  Concatenated summary, e.g.
     *     "用户是开发者，偏好极简风格。最近在调试RPC框架。"
     */
    static std::string summarize(const std::string& identity_json,
                                  const std::string& preferences_json,
                                  int max_tokens = 300);

    /**
     * Placeholder for future LLM-based extraction of identity / preferences
     * from conversation history.  Currently a no-op.
     *
     * Intended to be called periodically from BackgroundScheduler.
     */
    static void processPending();
};

}  // namespace common
}  // namespace agent_rpc
