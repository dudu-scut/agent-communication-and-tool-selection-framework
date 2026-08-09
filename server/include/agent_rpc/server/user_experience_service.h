#pragma once

#include "user_experience.grpc.pb.h"
#include "user_experience.pb.h"

#include <functional>
#include <string>

namespace agent_rpc::common {
class QueryDomainRepository;
}  // namespace agent_rpc::common

namespace agent_rpc {
namespace server {

class UserExperienceServiceImpl final : public agent_communication::UserExperienceService::Service {
public:
    // [PR-E] Executor invoked for sandbox executions and deferred
    // intervention executions. It runs the request through the already
    // initialized durable Query pipeline (the only execution entry point —
    // budget/owner/trace/cost/finalize logic is never duplicated here) with
    // the sandbox flag set, so long-term memory writes stay skipped.
    // Returns true on success and fills answer; otherwise fills error.
    using PipelineExecutor =
        std::function<bool(const std::string& request_id, const std::string& context_id,
                           const std::string& question, std::string& answer,
                           std::string& error)>;

    UserExperienceServiceImpl() = default;
    ~UserExperienceServiceImpl() override = default;

    // PostgreSQL is the source of truth for sandbox_runs/interventions/
    // undo_actions/autonomy_settings. May be null in minimal setups (the RPCs
    // then degrade to FAILED_PRECONDITION instead of crashing).
    void setQueryDomainRepository(common::QueryDomainRepository* repository);
    void setExecutor(PipelineExecutor executor);

    grpc::Status InterventionResponse(
        grpc::ServerContext* context,
        const agent_communication::InterventionResponseRequest* request,
        agent_communication::InterventionResponseResponse* response) override;

    grpc::Status SandboxQuery(
        grpc::ServerContext* context,
        const agent_communication::SandboxQueryRequest* request,
        agent_communication::SandboxQueryResponse* response) override;

private:
    common::QueryDomainRepository* query_repository_ = nullptr;
    PipelineExecutor executor_;
};

} // namespace server
} // namespace agent_rpc
