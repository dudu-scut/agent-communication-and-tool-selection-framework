#pragma once

#include "agent_rpc/common/postgres_store.h"

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <vector>

namespace agent_rpc::common {

// Classifies runtime errors raised by the persistence layer without exposing
// libpqxx types to consumers (server targets do not include pqxx headers):
// true when the exception originated from PostgreSQL I/O.
bool isPostgresError(const std::exception& error);

struct ConversationRecord {
    std::string id;
    std::string owner_id;
    std::string title;
    std::string memory_summary;
    std::string created_at;
    std::string updated_at;
};

struct MessageRecord {
    std::string id;
    std::string owner_id;
    std::string conversation_id;
    std::string role;
    std::string content;
    std::int64_t sequence_no = 0;
    std::string created_at;
    std::string updated_at;
};

struct QueryLogRecord {
    std::string id;
    std::string owner_id;
    std::string conversation_id;
    std::string request_text;
    std::string route_decision;
    std::string execution_plan;
    std::string response_text;
    std::string model;
    std::string status;
    std::string created_at;
    std::string updated_at;
};

struct TraceRecord {
    std::string id;
    std::string owner_id;
    std::string query_log_id;
    std::string trace_payload;
    std::string status;
    std::string created_at;
    std::string updated_at;
};

struct TokenUsageLedgerRecord {
    std::string id;
    std::string owner_id;
    std::string query_log_id;
    std::string model;
    std::int64_t prompt_tokens = 0;
    std::int64_t completion_tokens = 0;
    bool estimated = false;
    std::string cost_usd;
    std::string created_at;
    std::string updated_at;
};

struct FeedbackRecord {
    std::string id;
    std::string owner_id;
    std::string query_log_id;
    std::string agent_id;
    int rating = 0;
    std::string comment;
    std::string created_at;
    std::string updated_at;
};

struct RouteQualityRecord {
    std::string id;
    std::string owner_id;
    std::string agent_id;
    std::int64_t sample_count = 0;
    std::string average_rating;
    std::string routing_weight;
    std::string created_at;
    std::string updated_at;
};

// [PR-E] Workflow-control records mirroring the V011 durable-domain tables.
struct SandboxRunRecord {
    std::string id;
    std::string owner_id;
    std::string query_log_id;
    std::string request_text;
    std::string response_text;
    std::string status;
    std::string created_at;
    std::string updated_at;
};

struct CompareRunRecord {
    std::string id;
    std::string owner_id;
    std::string query_log_id;
    std::string request_text;
    std::string results;  // JSONB text: per-agent result array
    std::string status;
    std::string created_at;
    std::string updated_at;
};

struct InterventionRecord {
    std::string id;
    std::string owner_id;
    // For pending interventions created by the autonomy gate this carries the
    // target agent id (the real query log id only exists once the deferred
    // execution runs); after execution it still references the gate target.
    std::string query_log_id;
    std::string state;  // pending | PROCEED | MODIFY | SKIP | ABORT
    std::string original_request;
    std::string edited_request;
    std::string decision;
    std::string created_at;
    std::string updated_at;
};

struct UndoActionRecord {
    std::string id;
    std::string owner_id;
    std::string resource_type;
    std::string resource_id;
    std::string action_payload;  // JSONB text
    int version = 1;
    std::string expires_at;
    std::string undone_at;
    bool expired = false;  // computed server-side (expires_at <= NOW())
    std::string created_at;
    std::string updated_at;
};

struct AutonomySettingRecord {
    std::string id;
    std::string owner_id;
    std::string agent_id;
    int level = 1;
    std::string created_at;
    std::string updated_at;
};

// [PR-E] Outcome of the owner-scoped intervention CAS transition.
enum class InterventionResolveOutcome {
    kResolved,       // pending -> decision happened exactly once
    kNotFound,       // unknown id or foreign owner (no existence leak)
    kAlreadyResolved // the record exists for the owner but is not pending
};

// Names mirror the durable-domain tables while keeping all persistence behind
// one synchronous, tenant-scoped PostgreSQL repository.
class QueryDomainRepository final {
public:
    explicit QueryDomainRepository(PostgresStore& store);

    bool createConversation(const ConversationRecord& conversation);
    std::optional<ConversationRecord> getConversationById(const std::string& owner_id,
                                                            const std::string& conversation_id);
    std::vector<ConversationRecord> listConversations(const std::string& owner_id);

    // Confirms that a conversation exists for the owner, creating it on first
    // use. Idempotent for the same owner; returns false when the conversation
    // id is already owned by another user (or on database failure).
    bool ensureConversation(const std::string& owner_id, const std::string& conversation_id,
                            const std::string& title);

    // Returns false only when the owner/conversation/sequence tuple already
    // exists. Other PostgreSQL failures propagate to the caller.
    bool appendMessage(const MessageRecord& message);

    // Appends a message while assigning the next conversation sequence inside
    // a single transaction that first locks the owner's conversation row. The
    // sequence is never computed by callers (no MAX()+1 outside the
    // transaction). Returns the stored message, or std::nullopt when the
    // conversation does not belong to the owner.
    std::optional<MessageRecord> appendMessageAutoSequence(const std::string& owner_id,
                                                             const std::string& conversation_id,
                                                             const std::string& role,
                                                             const std::string& content);

    // Idempotent variant with a caller-chosen message id (deterministically
    // derived from the request id). When a row with that id already exists
    // the append is skipped WITHOUT consuming a sequence number, so retries
    // never duplicate history nor leave sequence gaps; the existing row is
    // returned. Unknown conversations or foreign owners still return
    // std::nullopt.
    std::optional<MessageRecord> appendMessageAutoSequence(const std::string& message_id,
                                                             const std::string& owner_id,
                                                             const std::string& conversation_id,
                                                             const std::string& role,
                                                             const std::string& content);

    std::vector<MessageRecord> listMessages(const std::string& owner_id,
                                             const std::string& conversation_id);

    bool createQueryLog(const QueryLogRecord& query_log);
    std::optional<QueryLogRecord> getQueryLogById(const std::string& owner_id,
                                                  const std::string& query_log_id);

    // Owner-scoped terminal update of an existing query log. This is a plain
    // UPDATE and never inserts: it returns false when the row does not exist
    // or belongs to another owner. Only the persisted terminal fields are
    // written (response_text, model, status, updated_at); every other record
    // field is ignored.
    bool updateQueryLog(const QueryLogRecord& query_log);

    bool createTrace(const TraceRecord& trace);
    std::optional<TraceRecord> getTraceById(const std::string& owner_id,
                                            const std::string& trace_id);

    // Owner-scoped terminal update of an existing trace. Same contract as
    // updateQueryLog: missing or cross-owner rows return false, no upsert.
    // Only the persisted terminal fields are written (trace_payload, status,
    // updated_at); every other record field is ignored.
    bool updateTrace(const TraceRecord& trace);

    // Appends one estimate-only ledger entry per id. A duplicate id (same
    // request retried) returns false instead of throwing, so finalize paths
    // can call it unconditionally; other PostgreSQL failures propagate.
    bool appendTokenUsageLedger(const TokenUsageLedgerRecord& usage);
    std::vector<TokenUsageLedgerRecord> listTokenUsageLedgerByOwner(const std::string& owner_id);

    bool createFeedback(const FeedbackRecord& feedback);

    std::optional<RouteQualityRecord> getRouteQuality(const std::string& owner_id,
                                                      const std::string& agent_id);
    bool upsertRouteQuality(const RouteQualityRecord& quality);

    // Short aliases retain the domain wording used by callers without adding
    // another SQL path or changing ownership semantics.
    bool appendTokenUsage(const TokenUsageLedgerRecord& usage) {
        return appendTokenUsageLedger(usage);
    }
    std::vector<TokenUsageLedgerRecord> listTokenUsageByOwner(const std::string& owner_id) {
        return listTokenUsageLedgerByOwner(owner_id);
    }

    // ---- [PR-E] workflow control: sandbox / compare / intervention / undo --

    bool createSandboxRun(const SandboxRunRecord& run);
    // Owner-scoped terminal update (status + response_text). Returns false
    // when the run does not exist or belongs to another owner.
    bool updateSandboxRun(const SandboxRunRecord& run);
    std::optional<SandboxRunRecord> getSandboxRunById(const std::string& owner_id,
                                                      const std::string& run_id);

    bool createCompareRun(const CompareRunRecord& run);
    // Owner-scoped terminal update (results JSONB + status).
    bool updateCompareRun(const CompareRunRecord& run);
    std::optional<CompareRunRecord> getCompareRunById(const std::string& owner_id,
                                                      const std::string& run_id);
    std::vector<CompareRunRecord> listCompareRunsByOwner(const std::string& owner_id);

    bool createIntervention(const InterventionRecord& intervention);
    std::optional<InterventionRecord> getInterventionById(const std::string& owner_id,
                                                          const std::string& intervention_id);
    // Single-statement CAS: pending -> decision for the owner's record. The
    // affected-row count decides success; a follow-up owner-scoped existence
    // check distinguishes NOT_FOUND from ALREADY_EXISTS inside the same
    // transaction.
    InterventionResolveOutcome resolveIntervention(const std::string& owner_id,
                                                   const std::string& intervention_id,
                                                   const std::string& decision,
                                                   const std::string& edited_request);
    // Inverse operation used by undo: any resolved state back to pending.
    // Returns false for unknown/foreign rows or rows already pending.
    bool restoreInterventionToPending(const std::string& owner_id,
                                      const std::string& intervention_id);

    // The expiry window (24 hours) is assigned by SQL at insert time.
    bool createUndoAction(const UndoActionRecord& action);
    std::optional<UndoActionRecord> getUndoActionById(const std::string& owner_id,
                                                      const std::string& action_id);
    // Single-statement CAS: sets undone_at only while it is still NULL.
    // Returns false when another caller already won the CAS.
    bool markUndoActionUndone(const std::string& owner_id, const std::string& action_id);

    // PostgreSQL upsert keyed on (owner_id, agent_id); a repeated set updates
    // the same row in place and never duplicates it.
    bool upsertAutonomySetting(const std::string& owner_id, const std::string& agent_id,
                               int level);
    // std::nullopt means "no setting"; callers apply their own conservative
    // default (the autonomy gate treats it as level 1 = confirmation).
    std::optional<AutonomySettingRecord> getAutonomySetting(const std::string& owner_id,
                                                            const std::string& agent_id);

private:
    PostgresStore& store_;
};

using Conversation = ConversationRecord;
using Message = MessageRecord;
using QueryLog = QueryLogRecord;
using Trace = TraceRecord;
using TokenUsageLedger = TokenUsageLedgerRecord;
using Feedback = FeedbackRecord;
using RouteQuality = RouteQualityRecord;
using SandboxRun = SandboxRunRecord;
using CompareRun = CompareRunRecord;
using Intervention = InterventionRecord;
using UndoAction = UndoActionRecord;
using AutonomySetting = AutonomySettingRecord;

}  // namespace agent_rpc::common
