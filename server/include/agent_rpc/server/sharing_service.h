#pragma once
#include "sharing.grpc.pb.h"
#include "sharing.pb.h"

namespace agent_rpc {
namespace common {
class PostgresStore;
class QueryDomainRepository;
}

namespace server {

/**
 * @brief Session sharing & workflow templates (durable, PG-backed).
 *
 * Shares are read-only ("view") bearer links. The raw high-entropy token is
 * returned exactly once at creation; PostgreSQL stores only its SHA-256
 * hash. Public reads (ReadSharedConversation) are anonymous, read-only and
 * sanitized, and enforce TTL/revocation. Templates are owner-scoped; using
 * one creates a real conversation + initial message for the CURRENT owner.
 */
class SharingServiceImpl final : public agent_communication::SharingService::Service {
public:
    SharingServiceImpl() = default;
    ~SharingServiceImpl() override = default;

    // Server-startup dependency injection (PostgreSQL is the source of
    // truth; both must outlive the server).
    void setStore(common::PostgresStore* store) { store_ = store; }
    void setQueryDomainRepository(common::QueryDomainRepository* repository) {
        domain_repository_ = repository;
    }

    grpc::Status ShareSession(
        grpc::ServerContext* context,
        const agent_communication::ShareSessionRequest* request,
        agent_communication::ShareSessionResponse* response) override;

    grpc::Status ObserveSession(
        grpc::ServerContext* context,
        const agent_communication::ObserveSessionRequest* request,
        grpc::ServerWriter<agent_communication::AIStreamEvent>* writer) override;

    grpc::Status SaveTemplate(
        grpc::ServerContext* context,
        const agent_communication::SaveTemplateRequest* request,
        agent_communication::SaveTemplateResponse* response) override;

    grpc::Status UseTemplate(
        grpc::ServerContext* context,
        const agent_communication::UseTemplateRequest* request,
        agent_communication::UseTemplateResponse* response) override;

    // Restricted public read by raw token (no auth; sanitized).
    grpc::Status ReadSharedConversation(
        grpc::ServerContext* context,
        const agent_communication::ReadSharedConversationRequest* request,
        agent_communication::ReadSharedConversationResponse* response) override;

    // Owner-side share management.
    grpc::Status ListShares(
        grpc::ServerContext* context,
        const agent_communication::ListSharesRequest* request,
        agent_communication::ListSharesResponse* response) override;

    grpc::Status RevokeShare(
        grpc::ServerContext* context,
        const agent_communication::RevokeShareRequest* request,
        agent_communication::RevokeShareResponse* response) override;

    // Template listing / detail.
    grpc::Status ListTemplates(
        grpc::ServerContext* context,
        const agent_communication::ListTemplatesRequest* request,
        agent_communication::ListTemplatesResponse* response) override;

    grpc::Status GetTemplate(
        grpc::ServerContext* context,
        const agent_communication::GetTemplateRequest* request,
        agent_communication::GetTemplateResponse* response) override;

private:
    common::PostgresStore* store_ = nullptr;
    common::QueryDomainRepository* domain_repository_ = nullptr;
};

}} // namespaces
