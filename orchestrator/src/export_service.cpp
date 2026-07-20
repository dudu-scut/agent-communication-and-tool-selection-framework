#include "agent_rpc/orchestrator/export_service.h"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>
#include <iomanip>

namespace agent_rpc {
namespace orchestrator {

std::string ExportService::toMarkdown(const std::string& context_id) {
    std::ostringstream md;

    // Header
    md << "# NexusAI 对话记录\n\n";
    md << "- **会话 ID**: " << context_id << "\n";
    md << "- **导出时间**: ";

    // Current timestamp
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&t, &tm_buf);
    md << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "\n\n";

    // Mermaid DAG placeholder
    md << "```mermaid\n";
    md << "graph TD\n";
    md << "    User((User)) -->|query| System\n";
    md << "    System -->|route| Agent1[Agent]\n";
    md << "    Agent1 -->|response| User\n";
    md << "```\n\n";

    md << "---\n\n";
    md << "## 对话记录\n\n";

    // Conversation messages would be read from memory service.
    // For now, emit a skeleton that the caller populates.
    md << "> 注意：对话内容将在未来版本中完整加载。\n\n";

    // Placeholder message format
    md << "### 用户\n\n";
    md << "> 请输入您的问题...\n\n";
    md << "### Agent\n\n";
    md << "> 暂无回复\n\n";

    md << "---\n\n";
    md << "*导出自 NexusAI 多智能体协作平台*\n";

    return md.str();
}

std::string ExportService::toHTML(const std::string& markdown) {
    std::ostringstream html;

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
         << "  line-height: 1.5; font-size: 0.95em;\n"
         << "}\n"
         << ".message.user .bubble {\n"
         << "  background: #667eea; color: #fff;\n"
         << "  border-bottom-right-radius: 4px;\n"
         << "}\n"
         << ".message.agent .bubble {\n"
         << "  background: #e8e8e8; color: #333;\n"
         << "  border-bottom-left-radius: 4px;\n"
         << "}\n"
         << ".timestamp {\n"
         << "  font-size: 0.75em; color: #999; margin: 4px 8px;\n"
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

    // Simple Markdown-to-HTML conversion: preserve paragraphs
    // HTML-escapes content to prevent XSS injection from message bodies
    auto htmlEscape = [](const std::string& s) -> std::string {
        std::string escaped;
        escaped.reserve(s.size());
        for (char c : s) {
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
    };

    std::istringstream stream(markdown);
    std::string line;
    while (std::getline(stream, line)) {
        // Skip the outer markdown header/metadata — rendered by the HTML template
        if (line.rfind("# ", 0) == 0 || line.rfind("---", 0) == 0 ||
            line.rfind("```", 0) == 0 || line.rfind("*", 0) == 0) {
            continue;
        }
        // Format user messages
        if (line.rfind("### 用户", 0) == 0) {
            html << "<div class=\"message user\"><div class=\"bubble\">";
            continue;
        }
        // Format agent messages
        if (line.rfind("### Agent", 0) == 0) {
            html << "</div></div><div class=\"message agent\"><div class=\"bubble\">";
            continue;
        }
        // Skip blockquote markers
        if (line.rfind("> ", 0) == 0) {
            html << htmlEscape(line.substr(2));
            continue;
        }
        // Skip list items
        if (line.rfind("- ", 0) == 0) {
            continue;
        }
        // Empty line closes current bubble
        if (line.empty()) {
            html << "</div></div>";
            continue;
        }
        html << htmlEscape(line);
    }

    html << "</div>\n"
         << "<div class=\"footer\">\n"
         << "导出自 NexusAI 多智能体协作平台\n"
         << "</div>\n"
         << "</div>\n"
         << "</body>\n"
         << "</html>\n";

    return html.str();
}

} // namespace orchestrator
} // namespace agent_rpc
