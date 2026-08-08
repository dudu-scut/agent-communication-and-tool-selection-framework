#pragma once

#include "agent_rpc/common/postgres_store.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agent_rpc::common {

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

}  // namespace agent_rpc::common
