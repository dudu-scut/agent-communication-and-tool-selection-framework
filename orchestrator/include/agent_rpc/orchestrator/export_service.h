#pragma once

#include <string>

namespace agent_rpc {
namespace orchestrator {

/**
 * @brief Conversation Export Service (Batch 6)
 *
 * Exports conversation history in Markdown or HTML format.
 * The Markdown format includes a conversational transcript with
 * a Mermaid DAG visualization when applicable. The HTML format
 * wraps the Markdown in a styled template with chat bubbles.
 */
class ExportService {
public:
    /**
     * @brief Format conversation history as Markdown.
     *
     * Reads conversation history from the conversation store
     * (memory service / Redis) and formats it as a Markdown document
     * with headers for user and agent messages, timestamps, and
     * a Mermaid diagram for multi-agent DAG execution if applicable.
     *
     * @param context_id The conversation context identifier.
     * @return Markdown-formatted string of the conversation.
     */
    static std::string toMarkdown(const std::string& context_id);

    /**
     * @brief Wrap Markdown content in an HTML page with inline CSS.
     *
     * Converts the Markdown conversation into a standalone HTML document
     * with chat-bubble styling, responsive layout, and print-friendly CSS.
     *
     * @param markdown The Markdown content (from toMarkdown()).
     * @return Complete HTML document as string.
     */
    static std::string toHTML(const std::string& markdown);
};

} // namespace orchestrator
} // namespace agent_rpc
