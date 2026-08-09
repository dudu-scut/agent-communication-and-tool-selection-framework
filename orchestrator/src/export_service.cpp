#include "agent_rpc/orchestrator/export_service.h"

#include "agent_rpc/common/logger.h"
#include "agent_rpc/common/query_domain_repository.h"

#include "orchestration.pb.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

namespace agent_rpc {
namespace orchestrator {

namespace {

// HTML-escapes every byte that could turn message text into markup, so
// exported content can never execute as HTML/script (& before everything
// else; the entity table keeps &lt; &gt; &quot; &#39; intact).
std::string htmlEscape(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default:  escaped += c; break;
        }
    }
    return escaped;
}

std::string currentUtcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time, &utc);
    std::ostringstream formatted;
    formatted << std::put_time(&utc, "%Y-%m-%d %H:%M:%S") << " UTC";
    return formatted.str();
}

// Renders one message as a Markdown blockquote section; every content line
// is quoted so multi-line messages keep their structure.
void appendMessageSection(std::ostringstream& md, const common::MessageRecord& message) {
    const bool is_user = message.role == "user";
    md << "### " << (is_user ? "用户" : "Agent") << "\n\n";
    std::istringstream lines(message.content);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        md << "> " << line << "\n";
    }
    md << "\n";
}

std::string buildMarkdown(const std::string& context_id,
                          const std::vector<common::MessageRecord>& messages) {
    std::ostringstream md;
    md << "# NexusAI 对话记录\n\n";
    md << "- **会话 ID**: " << context_id << "\n";
    md << "- **导出时间**: " << currentUtcTimestamp() << "\n";
    md << "- **消息数**: " << messages.size() << "\n\n";
    md << "---\n\n";
    md << "## 对话记录\n\n";
    if (messages.empty()) {
        md << "> 该会话暂无消息。\n\n";
    } else {
        for (const auto& message : messages) {
            appendMessageSection(md, message);
        }
    }
    md << "---\n\n";
    md << "*导出自 NexusAI 多智能体协作平台*\n";
    return md.str();
}

// Styled HTML page shell; message content is injected separately (escaped).
void writeHtmlShellStart(std::ostringstream& html) {
    html << "<!DOCTYPE html>\n"
         << "<html lang=\"zh-CN\">\n"
         << "<head>\n"
         << "<meta charset=\"UTF-8\">\n"
         << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
         << "<title>NexusAI 对话记录导出</title>\n"
         << "<style>\n"
         << "* { box-sizing: border-box; margin: 0; padding: 0; }\n"
         << "body {\n"
         << "  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;\n"
         << "  max-width: 800px; margin: 0 auto; padding: 20px;\n"
         << "  background: #f5f5f5; color: #333;\n"
         << "}\n"
         << ".container {\n"
         << "  background: #fff; border-radius: 12px;\n"
         << "  box-shadow: 0 2px 8px rgba(0,0,0,0.1); overflow: hidden;\n"
         << "}\n"
         << ".header {\n"
         << "  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n"
         << "  color: #fff; padding: 24px; text-align: center;\n"
         << "}\n"
         << ".header h1 { font-size: 1.5em; margin-bottom: 8px; }\n"
         << ".header p { opacity: 0.9; font-size: 0.9em; }\n"
         << ".content { padding: 24px; }\n"
         << ".message { margin-bottom: 16px; display: flex; flex-direction: column; }\n"
         << ".message.user { align-items: flex-end; }\n"
         << ".message.agent { align-items: flex-start; }\n"
         << ".bubble {\n"
         << "  max-width: 75%; padding: 12px 16px; border-radius: 18px;\n"
         << "  line-height: 1.5; font-size: 0.95em; white-space: pre-wrap;\n"
         << "}\n"
         << ".message.user .bubble {\n"
         << "  background: #667eea; color: #fff;\n"
         << "  border-bottom-right-radius: 4px;\n"
         << "}\n"
         << ".message.agent .bubble {\n"
         << "  background: #e8e8e8; color: #333;\n"
         << "  border-bottom-left-radius: 4px;\n"
         << "}\n"
         << ".footer {\n"
         << "  text-align: center; padding: 16px;\n"
         << "  color: #999; font-size: 0.8em; border-top: 1px solid #eee;\n"
         << "}\n"
         << "@media print {\n"
         << "  body { background: #fff; }\n"
         << "  .container { box-shadow: none; }\n"
         << "}\n"
         << "</style>\n"
         << "</head>\n"
         << "<body>\n"
         << "<div class=\"container\">\n"
         << "<div class=\"header\">\n"
         << "<h1>NexusAI 对话记录</h1>\n"
         << "<p>多智能体协作平台</p>\n"
         << "</div>\n"
         << "<div class=\"content\">\n";
}

void writeHtmlShellEnd(std::ostringstream& html) {
    html << "</div>\n"
         << "<div class=\"footer\">\n"
         << "导出自 NexusAI 多智能体协作平台\n"
         << "</div>\n"
         << "</div>\n"
         << "</body>\n"
         << "</html>\n";
}

// Structured rendering straight from the durable message records: every
// content byte is escaped exactly once, so hostile payloads can never
// appear as markup (a raw <script> shows up as &lt;script&gt;).
std::string buildHTML(const std::vector<common::MessageRecord>& messages) {
    std::ostringstream html;
    writeHtmlShellStart(html);
    if (messages.empty()) {
        html << "<div class=\"message agent\"><div class=\"bubble\">"
             << htmlEscape("该会话暂无消息。") << "</div></div>\n";
    }
    for (const auto& message : messages) {
        const bool is_user = message.role == "user";
        html << "<div class=\"message " << (is_user ? "user" : "agent")
             << "\"><div class=\"bubble\">"
             << htmlEscape(message.content)
             << "</div></div>\n";
    }
    writeHtmlShellEnd(html);
    return html.str();
}

} // namespace

ExportService& ExportService::instance() {
    static ExportService service;
    return service;
}

void ExportService::configure(common::QueryDomainRepository* domain_repository) {
    domain_repository_ = domain_repository;
}

namespace {

grpc::Status handleExportRequestImpl(
    common::QueryDomainRepository* repository,
    const std::string& owner_id,
    const agent_communication::ExportConversationRequest* request,
    agent_communication::ExportConversationResponse* response) {

    if (owner_id.empty()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                            "Valid authentication token required");
    }
    if (request->context_id().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "context_id is required");
    }

    std::string format = request->format();
    if (format.empty()) {
        format = "markdown";
    }
    if (format != "markdown" && format != "html") {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "format must be 'markdown' or 'html'");
    }

    // Owner-scoped lookup: a missing or foreign conversation is NOT_FOUND.
    const auto conversation =
        repository->getConversationById(owner_id, request->context_id());
    if (!conversation.has_value()) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND,
                            "Conversation not found: " + request->context_id());
    }

    const auto messages = repository->listMessages(owner_id, request->context_id());

    std::string file_content;
    std::string mime_type;
    if (format == "html") {
        file_content = buildHTML(messages);
        mime_type = "text/html; charset=utf-8";
    } else {
        file_content = buildMarkdown(request->context_id(), messages);
        mime_type = "text/markdown; charset=utf-8";
    }

    response->set_file_data(file_content);
    response->set_mime_type(mime_type);
    auto* status = response->mutable_status();
    status->set_code(0);
    status->set_message("OK");

    LOG_INFO("ExportConversation completed: context=" + request->context_id() +
             " format=" + format + " messages=" + std::to_string(messages.size()) +
             " size=" + std::to_string(file_content.size()));
    return grpc::Status::OK;
}

} // namespace

grpc::Status ExportService::handleExportRequest(
    const std::string& owner_id,
    const agent_communication::ExportConversationRequest* request,
    agent_communication::ExportConversationResponse* response) {

    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Invalid request or response");
    }
    common::QueryDomainRepository* repository = instance().domain_repository_;
    if (!repository) {
        return grpc::Status(grpc::StatusCode::INTERNAL,
                            "Export service is not configured");
    }
    // Top-level guard: PostgreSQL faults must surface as UNAVAILABLE (the
    // Query pipeline convention), never escape the gRPC handler.
    try {
        return handleExportRequestImpl(repository, owner_id, request, response);
    } catch (const std::exception& error) {
        const bool persistence_fault = common::isPostgresError(error);
        LOG_ERROR(std::string("ExportConversation failed: ") + error.what());
        // The client-facing status message is a fixed, sanitized
        // text. Internal exception detail stays in the server log only and
        // never rides the response (no cross-tenant information leak).
        return grpc::Status(
            persistence_fault ? grpc::StatusCode::UNAVAILABLE
                              : grpc::StatusCode::INTERNAL,
            persistence_fault ? "Export unavailable: persistence layer error"
                              : "Export failed unexpectedly");
    }
}

std::string ExportService::toHTML(const std::string& markdown) {
    // Generic fallback: wrap arbitrary Markdown text in the same styled,
    // fully escaped shell. The durable export path uses buildHTML() with the
    // structured message records instead.
    std::ostringstream html;
    writeHtmlShellStart(html);
    html << "<div class=\"message agent\"><div class=\"bubble\">"
         << htmlEscape(markdown)
         << "</div></div>\n";
    writeHtmlShellEnd(html);
    return html.str();
}

} // namespace orchestrator
} // namespace agent_rpc
