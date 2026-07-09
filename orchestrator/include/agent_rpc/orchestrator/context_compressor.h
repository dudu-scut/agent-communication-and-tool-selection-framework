#pragma once
#include <string>
#include <vector>
#include <memory>

namespace agent_rpc {
namespace orchestrator {

struct ConversationTurn {
    std::string role;
    std::string content;
};

/**
 * @brief Compresses long conversation histories to fit within context windows.
 *
 * When the estimated token count exceeds 70 % of max_context_tokens,
 * older turns are summarized via LLM while the most recent N turns
 * are preserved verbatim.
 */
class ContextCompressor {
public:
    explicit ContextCompressor();

    /**
     * @brief Compress history if it exceeds the budget.
     *
     * @param history         Full conversation turns (oldest first).
     * @param max_context_tokens  Upper bound on total tokens.
     * @param keep_recent_turns  Number of most recent turns to keep raw.
     * @return Possibly compressed conversation (summary + recent raw turns).
     */
    std::vector<ConversationTurn> compressIfNeeded(
        const std::vector<ConversationTurn>& history,
        int max_context_tokens,
        int keep_recent_turns = 10);

    /// Current running summary string (accumulated across compressions).
    std::string getSummary() const { return summary_; }

private:
    /// Rough token estimation: char_count / avg_chars_per_token.
    int estimateTokens(const std::vector<ConversationTurn>& history);

    /// Call LLM to generate/update a summary string.
    std::string generateSummary(
        const std::vector<ConversationTurn>& older_turns,
        const std::string& existing_summary);

    std::string summary_;
    int summary_cursor_ = 0; ///< Index into original history already summarized.
};

} // namespace orchestrator
} // namespace agent_rpc
