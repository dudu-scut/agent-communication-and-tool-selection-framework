#pragma once

#include <functional>
#include <mutex>
#include <string>

#include <grpcpp/grpcpp.h>

namespace agent_communication {
class ReplayQueryRequest;
class ReplayQueryResponse;
}

namespace agent_rpc {
namespace common {
class QueryDomainRepository;
}

namespace orchestrator {

/**
 * @brief Query Replay Service (PR-D: durable, PostgreSQL-backed)
 *
 * Replays a previous query identified by its trace id. The original trace is
 * always loaded from PostgreSQL scoped by the AUTHENTICATED owner (a
 * cross-owner or unknown trace id maps to NOT_FOUND; there is no existence
 * leak). Two modes are supported, every other value is rejected with
 * INVALID_ARGUMENT:
 *
 *   - "route": re-computes only the routing decision for the original
 *     question and returns the original vs the new route. NO execution
 *     happens and NO new rows are persisted.
 *   - "exact": re-executes the original question through the durable C2
 *     pipeline under a BRAND-NEW request id. A new query_log + trace pair is
 *     persisted; the original records are never modified. The new trace
 *     payload carries a queryable association back to the original trace.
 *
 * Dependencies (PostgreSQL repository, pipeline executor, route provider)
 * are injected once at server startup via configure(); the singleton keeps
 * the existing static call sites working without leaking gRPC server types
 * into library callers.
 */
class ReplayService final {
public:
    // Runs one durable pipeline execution under a fresh request id. Returns
    // true on a terminal success and fills `answer`; on failure fills
    // `error` with a human-readable cause.
    using PipelineExecutor = std::function<bool(const std::string& request_id,
                                                const std::string& context_id,
                                                const std::string& question,
                                                std::string& answer,
                                                std::string& error)>;

    // Returns the route label the current pipeline would pick right now
    // (e.g. "single-agent-a2a" / "multi-agent").
    using RouteProvider = std::function<std::string()>;

    static ReplayService& instance();

    // Server-startup dependency injection. Safe to call once before serving
    // starts; all pointers/callbacks must outlive the server.
    void configure(common::QueryDomainRepository* domain_repository,
                   RouteProvider route_provider,
                   PipelineExecutor executor);

    /**
     * @brief gRPC-level entry point shared by every ReplayQuery call path.
     *
     * The owner id MUST come from the authenticated session
     * (AuthInterceptor::currentUserId()), never from the request body.
     */
    static grpc::Status handleReplayRequest(const std::string& owner_id,
                                            const agent_communication::ReplayQueryRequest* request,
                                            agent_communication::ReplayQueryResponse* response);

private:
    ReplayService() = default;

    mutable std::mutex mutex_;
    common::QueryDomainRepository* domain_repository_ = nullptr;
    RouteProvider route_provider_;
    PipelineExecutor executor_;
};

} // namespace orchestrator
} // namespace agent_rpc
