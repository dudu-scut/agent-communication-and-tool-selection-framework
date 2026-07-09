/**
 * @file context_compressor.cpp
 * @brief Implementation of ContextCompressor
 *
 * Batch 3 — Task 2: Context Compression
 */

#include "agent_rpc/orchestrator/context_compressor.h"
#include <cctype>
#include <sstream>
#include <algorithm>
#include <curl/curl.h>
#include <cstdlib>

namespace agent_rpc {
namespace orchestrator {

ContextCompressor::ContextCompressor() = default;

// -----------------------------------------------------------------------
// Token estimation
// -----------------------------------------------------------------------

int ContextCompressor::estimateTokens(
    const std::vector<ConversationTurn>& history) {
    size_t total_chars = 0;
    for (const auto& turn : history) {
        total_chars += turn.content.size();
    }
    if (total_chars == 0) return 0;

    // Heuristic: detect whether content is predominantly CJK
    size_t cjk_count = 0;
    size_t sample = std::min(total_chars, size_t{1024});
    for (size_t i = 0; i < sample && i < total_chars; ++i) {
        // Very rough CJK range check — counts characters in U+4E00..U+9FFF
        // and U+3000..U+303F ranges.  Only samples because full scan is O(n).
        // Practically, the first few hundred characters are representative.
    }

    // Actually do a proper scan over all history but limit to 10k chars
    size_t scan_limit = std::min(total_chars, size_t{10000});
    for (size_t i = 0; i < scan_limit; ++i) {
        // We'll iterate per-character through the combined content.
        // Rather than concatenating, just check the first 10k chars
        // by iterating turns.
    }
    // Simpler approach: count CJK in concatenated content (capped).
    std::string combined;
    combined.reserve(scan_limit);
    for (const auto& turn : history) {
        if (combined.size() >= scan_limit) break;
        size_t room = scan_limit - combined.size();
        combined.append(turn.content.substr(0, room));
    }
    for (unsigned char c : combined) {
        // CJK Unified Ideographs: U+4E00..U+9FFF
        if ((c >= 0xE4 && c <= 0xE9) ||  // leading bytes in UTF-8 for CJK
            c >= 0xE4) {
            // Count multi-byte sequences — imperfect but good enough heuristic
        }
    }

    // Re-count properly using the wide character ranges
    cjk_count = 0;
    for (size_t i = 0; i < combined.size(); ) {
        unsigned char c = combined[i];
        if (c < 0x80) {
            // ASCII: 1 byte
            ++i;
        } else if (c < 0xC0) {
            // Continuation byte — skip
            ++i;
        } else if (c < 0xE0) {
            // 2-byte UTF-8 (mostly Latin supplement, etc.)
            i += 2;
        } else if (c < 0xF0) {
            // 3-byte UTF-8 — this range covers CJK
            // Check if it falls in U+4E00..U+9FFF
            if (c >= 0xE4 && c <= 0xE9 && i + 2 < combined.size()) {
                ++cjk_count;
            }
            i += 3;
        } else {
            i += 4;
        }
    }

    // Ratio: CJK-heavy content uses ~1.5 chars/token, English ~3.5
    double ratio = static_cast<double>(cjk_count) / std::max(combined.size(), size_t{1});
    double avg_chars_per_token = 3.5 * (1.0 - ratio) + 1.5 * ratio;

    return static_cast<int>(total_chars / avg_chars_per_token + 0.5);
}

// -----------------------------------------------------------------------
// LLM summary generation
// -----------------------------------------------------------------------

static size_t writeCallback(void* contents, size_t size, size_t nmemb,
                             std::string* output) {
    size_t total = size * nmemb;
    output->append(static_cast<char*>(contents), total);
    return total;
}

std::string ContextCompressor::generateSummary(
    const std::vector<ConversationTurn>& older_turns,
    const std::string& existing_summary) {

    // Build a prompt for the LLM
    std::ostringstream prompt;
    prompt << "You are a conversation summarizer. "
           << "Condense the following conversation turns into a concise summary "
           << "(2-3 sentences) that preserves key information, decisions, and facts.\n\n";

    if (!existing_summary.empty()) {
        prompt << "Previous summary:\n" << existing_summary << "\n\n";
    }

    prompt << "New turns to summarize:\n";
    for (const auto& turn : older_turns) {
        prompt << turn.role << ": " << turn.content << "\n";
    }

    prompt << "\nSummary:";

    // ---- Call LLM API ----
    const char* api_key = std::getenv("LLM_API_KEY");
    if (!api_key) {
        // Fallback: concatenate into a brief summary
        std::string fallback;
        if (!existing_summary.empty()) {
            fallback = existing_summary + " ";
        }
        size_t total = 0;
        for (const auto& turn : older_turns) {
            size_t take = std::min(turn.content.size(), size_t{200});
            fallback += turn.content.substr(0, take) + " ";
            total += take;
            if (total > 800) break;
        }
        if (fallback.size() > 1000) fallback.resize(1000);
        return fallback;
    }

    const char* api_url = std::getenv("LLM_API_URL");
    if (!api_url) api_url = "https://api.deepseek.com/v1/chat/completions";
    const char* model  = std::getenv("LLM_MODEL");
    if (!model) model  = "deepseek-v4-pro";

    std::string json_body =
        R"({"model":")" + std::string(model) +
        R"(","messages":[
            {"role":"system","content":"You are a concise summarizer. Output ONLY the summary, no preamble."},
            {"role":"user","content":")" + prompt.str() + R"("}],
        "max_tokens":512,"temperature":0.3})";

    CURL* curl = curl_easy_init();
    if (!curl) {
        return "(summary unavailable)";
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth_header = "Authorization: Bearer ";
    auth_header += api_key;
    headers = curl_slist_append(headers, auth_header.c_str());

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // dev only

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return "(LLM summary unavailable: " + std::string(curl_easy_strerror(res)) + ")";
    }

    // Parse JSON response to extract content
    // Simple extraction of "content" field
    auto content_pos = response_body.find("\"content\"");
    if (content_pos == std::string::npos) {
        return "(summary parse error)";
    }
    auto start = response_body.find('"', content_pos + 9);
    if (start == std::string::npos) return "(summary parse error)";
    ++start;
    auto end = response_body.find('"', start);
    if (end == std::string::npos) return "(summary parse error)";

    return response_body.substr(start, end - start);
}

// -----------------------------------------------------------------------
// Compression entry-point
// -----------------------------------------------------------------------

std::vector<ConversationTurn> ContextCompressor::compressIfNeeded(
    const std::vector<ConversationTurn>& history,
    int max_context_tokens,
    int keep_recent_turns) {

    if (history.empty() || max_context_tokens <= 0) {
        return history;
    }

    int estimated = estimateTokens(history);
    int threshold = static_cast<int>(max_context_tokens * 0.7);

    if (estimated <= threshold) {
        return history;  // no compression needed
    }

    // Determine which turns to summarize (everything before the last N)
    if (keep_recent_turns <= 0) keep_recent_turns = 1;
    if (keep_recent_turns >= static_cast<int>(history.size())) {
        // Not enough turns to separate — just return history as-is
        return history;
    }

    size_t split = (history.size() > static_cast<size_t>(keep_recent_turns))
                       ? history.size() - static_cast<size_t>(keep_recent_turns)
                       : 0;

    std::vector<ConversationTurn> older(history.begin(),
                                         history.begin() + split);
    std::vector<ConversationTurn> recent(history.begin() + split,
                                          history.end());

    // Generate / update summary from turns after the cursor
    if (!older.empty()) {
        summary_ = generateSummary(older, summary_);
        summary_cursor_ += static_cast<int>(older.size());
    }

    // Build result: summary turn + recent raw turns
    std::vector<ConversationTurn> result;
    result.reserve(1 + recent.size());
    result.push_back({"system", "[Compressed summary: " + summary_ + "]"});
    result.insert(result.end(), recent.begin(), recent.end());

    return result;
}

} // namespace orchestrator
} // namespace agent_rpc
