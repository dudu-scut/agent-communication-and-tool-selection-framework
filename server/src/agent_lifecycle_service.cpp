#include "agent_rpc/server/agent_lifecycle_service.h"
#include "agent_rpc/server/auth_interceptor.h"
#include "agent_rpc/common/agent_runtime_repository.h"
#include "agent_rpc/common/query_domain_repository.h"
#include "agent_rpc/common/logger.h"
#include <json/json.h>

#include <pqxx/pqxx>

#include <cstdint>
#include <random>
#include <string>

namespace agent_rpc { namespace server {
namespace {

// Locally generated primary keys keep the service free of database sequences
// while staying collision-safe (same scheme as the durable-domain repository).
std::string generateRowId(const char* prefix) {
    static thread_local std::mt19937_64 generator{std::random_device{}()};
    constexpr char kHex[] = "0123456789abcdef";
    std::string suffix;
    suffix.reserve(32);
    for (int round = 0; round < 2; ++round) {
        std::uint64_t value = generator();
        for (int index = 0; index < 16; ++index) {
            suffix.push_back(kHex[value & 0xf]);
            value >>= 4;
        }
    }
    return std::string{prefix} + "-" + suffix;
}

}  // namespace

AgentLifecycleServiceImpl::AgentLifecycleServiceImpl(common::RedisClient* redis) : redis_(redis) {}

void AgentLifecycleServiceImpl::setAgentRuntimeRepository(
    common::AgentRuntimeRepository* repository) {
    runtime_repository_ = repository;
}

void AgentLifecycleServiceImpl::setQueryDomainRepository(
    common::QueryDomainRepository* repository) {
    query_repository_ = repository;
}

grpc::Status AgentLifecycleServiceImpl::SubmitFeedback(
    grpc::ServerContext* ctx, const agent_communication::SubmitFeedbackRequest* request,
    agent_communication::SubmitFeedbackResponse* response) {
    if (!AuthInterceptor::isAuthenticated())
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "Authentication required");

    // The feedback owner is always the authenticated user; any client-supplied
    // identity is ignored (owner only ever comes from the auth context).
    const std::string owner = AuthInterceptor::currentUserId();

    if (request->rating() < 1 || request->rating() > 5) {
        response->mutable_status()->set_code(1);
        response->mutable_status()->set_message("rating must be between 1 and 5");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "rating must be between 1 and 5");
    }

    if (runtime_repository_ == nullptr || query_repository_ == nullptr) {
        response->mutable_status()->set_code(1);
        response->mutable_status()->set_message("Feedback persistence not available");
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                            "Feedback persistence not available");
    }

    // Feedback must reference a trace that belongs to the caller. Traces are
    // stored as "trace-<request_id>" but clients may send the bare id, so try
    // both spellings. A missing or foreign trace is NOT_FOUND — never leak
    // another owner's trace existence via a different error.
    std::optional<common::TraceRecord> trace =
        query_repository_->getTraceById(owner, request->trace_id());
    if (!trace.has_value()) {
        trace = query_repository_->getTraceById(owner, "trace-" + request->trace_id());
    }
    if (!trace.has_value()) {
        response->mutable_status()->set_code(1);
        response->mutable_status()->set_message("Trace not found");
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Trace not found for current user");
    }

    common::RuntimeFeedbackRecord feedback;
    feedback.owner_id = owner;
    feedback.query_log_id = trace->query_log_id;
    feedback.trace_id = trace->id;
    feedback.agent_id = request->agent_id();
    feedback.skill_name = request->skill_name();
    feedback.rating = request->rating();
    feedback.comment = request->comment();

    // Persist with a unique_violation safety net: on the astronomically rare
    // primary-key collision, retry once with a fresh id. Any database error
    // is mapped to INTERNAL here — exceptions must never escape the handler.
    bool inserted = false;
    for (int attempt = 0; attempt < 2 && !inserted; ++attempt) {
        feedback.id = generateRowId("feedback");
        try {
            inserted = runtime_repository_->insertFeedback(feedback);
        } catch (const pqxx::unique_violation& error) {
            LOG_WARN(std::string{"SubmitFeedback: primary-key collision, retrying with a new id: "} +
                     error.what());
        } catch (const std::exception& error) {
            LOG_ERROR(std::string{"SubmitFeedback: feedback insert failed: "} + error.what());
            response->mutable_status()->set_code(1);
            response->mutable_status()->set_message("Failed to persist feedback");
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to persist feedback");
        }
    }
    if (!inserted) {
        response->mutable_status()->set_code(1);
        response->mutable_status()->set_message("Failed to persist feedback");
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to persist feedback");
    }

    // Recompute the owner-scoped route quality row immediately so routing
    // picks up the new rating without waiting for the periodic aggregator.
    // Aggregation failure is non-fatal: the raw feedback row is already
    // durable and the aggregator will catch up.
    if (!runtime_repository_->aggregateRouteQuality(owner, request->agent_id(),
                                                    request->skill_name())) {
        LOG_WARN("SubmitFeedback: route quality aggregation failed for owner=" + owner +
                 " agent=" + request->agent_id() + " skill=" + request->skill_name());
    }

    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("OK");
    LOG_INFO("SubmitFeedback: owner=" + owner + " agent=" + request->agent_id() +
             " skill=" + request->skill_name() + " rating=" + std::to_string(request->rating()));
    return grpc::Status::OK;
}

grpc::Status AgentLifecycleServiceImpl::GetAgentCompare(
    grpc::ServerContext* ctx, const agent_communication::GetAgentCompareRequest* /*request*/,
    agent_communication::GetAgentCompareResponse* response) {
    if (!AuthInterceptor::isAuthenticated())
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "Authentication required");
    // Placeholder: returns empty comparison until agent metrics aggregation is wired up
    response->mutable_status()->set_code(0);
    response->mutable_status()->set_message("Not yet implemented — agent metrics aggregation pending");
    return grpc::Status::OK;
}

grpc::Status AgentLifecycleServiceImpl::SetAutonomyLevel(
    grpc::ServerContext* ctx, const agent_communication::SetAutonomyLevelRequest* request,
    agent_communication::SetAutonomyLevelResponse* response) {
    if (!AuthInterceptor::isAuthenticated())
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "Authentication required");
    // Store autonomy level in Redis
    if (redis_) {
        std::string key = "autonomy:" + request->user_id() + ":" + request->agent_id();
        redis_->set(key, std::to_string(request->level()));
        redis_->expire(key, 86400 * 365); // 1 year TTL
        response->mutable_status()->set_code(0);
        response->mutable_status()->set_message("OK");
    } else {
        response->mutable_status()->set_code(1);
        response->mutable_status()->set_message("Redis not available");
    }
    LOG_INFO("SetAutonomyLevel: user=" + request->user_id() + " agent=" + request->agent_id() +
             " level=" + std::to_string(request->level()));
    return grpc::Status::OK;
}

grpc::Status AgentLifecycleServiceImpl::UndoAction(
    grpc::ServerContext* ctx, const agent_communication::UndoActionRequest* /*request*/,
    agent_communication::UndoActionResponse* response) {
    if (!AuthInterceptor::isAuthenticated())
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "Authentication required");
    // Placeholder: undo requires action history persistence which is not yet implemented
    response->mutable_status()->set_code(1);
    response->mutable_status()->set_message("Undo not yet implemented — action history persistence pending");
    return grpc::Status::OK;
}

}} // namespaces
