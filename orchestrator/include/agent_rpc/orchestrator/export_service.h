#pragma once

#include <string>

#include <grpcpp/grpcpp.h>

namespace agent_communication {
class ExportConversationRequest;
class ExportConversationResponse;
}

namespace agent_rpc {
namespace common {
class QueryDomainRepository;
}

namespace orchestrator {

/**
 * @brief Conversation Export Service (durable, PostgreSQL-backed)
 *
 * Exports a conversation transcript stored in PostgreSQL
 * (conversation_messages) as Markdown or standalone HTML. The lookup is
 * scoped by the AUTHENTICATED owner: a conversation that does not exist or
 * belongs to another user maps to NOT_FOUND.
 *
 * The HTML rendering escapes every message byte (& < > " '), so message
 * text can never execute as HTML/script in the exported document.
 */
class ExportService final {
public:
    static ExportService& instance();

    // Server-startup dependency injection; the repository must outlive the
    // server.
    void configure(common::QueryDomainRepository* domain_repository);

    /**
     * @brief gRPC-level entry point shared by every ExportConversation call
     *        path. `owner_id` MUST come from the authenticated session.
     */
    static grpc::Status handleExportRequest(const std::string& owner_id,
                                            const agent_communication::ExportConversationRequest* request,
                                            agent_communication::ExportConversationResponse* response);

    /**
     * @brief Wrap a Markdown transcript in a standalone HTML page. All text
     *        content is HTML-escaped; raw markup never reaches the output.
     */
    static std::string toHTML(const std::string& markdown);

private:
    ExportService() = default;

    common::QueryDomainRepository* domain_repository_ = nullptr;
};

} // namespace orchestrator
} // namespace agent_rpc
