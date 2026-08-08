#include "agent_rpc/common/query_domain_repository.h"

#include <pqxx/except>
#include <pqxx/version>

#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <utility>

// Compatibility shim: the prebuilt libpqxx in the dependency prefix was
// compiled without <source_location> support, so it only exports the
// single-argument exception constructors. The public headers detect
// source_location support for the current compiler and declare two-argument
// overloads; provide the missing definitions once here so error paths
// (e.g. conversion failures surfaced from cold sections) link cleanly.
#if pqxx_have_source_location
namespace pqxx {
conversion_error::conversion_error(const std::string& message, std::source_location loc)
    : std::domain_error(message), location(loc) {}
conversion_overrun::conversion_overrun(const std::string& message, std::source_location loc)
    : conversion_error(message, loc) {}
}  // namespace pqxx
#endif

namespace agent_rpc::common {
namespace {

template <typename... Arguments>
pqxx::result execParams(pqxx::transaction_base& transaction, const std::string& query,
                        Arguments&&... arguments) {
#if PQXX_VERSION_MAJOR >= 8
    return transaction.exec(query, pqxx::params{std::forward<Arguments>(arguments)...});
#else  // libpqxx 6.x/7.x
    return transaction.exec_params(query, std::forward<Arguments>(arguments)...);
#endif
}

template <typename Row>
ConversationRecord conversationFromRow(const Row& row) {
    return ConversationRecord{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .title = row["title"].template as<std::string>(),
        .memory_summary = row["memory_summary"].template as<std::string>(),
        .created_at = row["created_at"].template as<std::string>(),
        .updated_at = row["updated_at"].template as<std::string>(),
    };
}

template <typename Row>
MessageRecord messageFromRow(const Row& row) {
    return MessageRecord{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .conversation_id = row["conversation_id"].template as<std::string>(),
        .role = row["role"].template as<std::string>(),
        .content = row["content"].template as<std::string>(),
        .sequence_no = row["sequence_no"].template as<std::int64_t>(),
        .created_at = row["created_at"].template as<std::string>(),
        .updated_at = row["updated_at"].template as<std::string>(),
    };
}

template <typename Row>
QueryLogRecord queryLogFromRow(const Row& row) {
    return QueryLogRecord{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .conversation_id = row["conversation_id"].template as<std::string>(),
        .request_text = row["request_text"].template as<std::string>(),
        .route_decision = row["route_decision"].template as<std::string>(),
        .execution_plan = row["execution_plan"].template as<std::string>(),
        .response_text = row["response_text"].template as<std::string>(),
        .model = row["model"].template as<std::string>(),
        .status = row["status"].template as<std::string>(),
        .created_at = row["created_at"].template as<std::string>(),
        .updated_at = row["updated_at"].template as<std::string>(),
    };
}

template <typename Row>
TraceRecord traceFromRow(const Row& row) {
    return TraceRecord{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .query_log_id = row["query_log_id"].template as<std::string>(),
        .trace_payload = row["trace_payload"].template as<std::string>(),
        .status = row["status"].template as<std::string>(),
        .created_at = row["created_at"].template as<std::string>(),
        .updated_at = row["updated_at"].template as<std::string>(),
    };
}

template <typename Row>
TokenUsageLedgerRecord tokenUsageFromRow(const Row& row) {
    return TokenUsageLedgerRecord{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .query_log_id = row["query_log_id"].template as<std::string>(),
        .model = row["model"].template as<std::string>(),
        .prompt_tokens = row["prompt_tokens"].template as<std::int64_t>(),
        .completion_tokens = row["completion_tokens"].template as<std::int64_t>(),
        .estimated = row["estimated"].template as<bool>(),
        .cost_usd = row["cost_usd"].template as<std::string>(),
        .created_at = row["created_at"].template as<std::string>(),
        .updated_at = row["updated_at"].template as<std::string>(),
    };
}

template <typename Row>
RouteQualityRecord routeQualityFromRow(const Row& row) {
    return RouteQualityRecord{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .agent_id = row["agent_id"].template as<std::string>(),
        .sample_count = row["sample_count"].template as<std::int64_t>(),
        .average_rating = row["average_rating"].template as<std::string>(),
        .routing_weight = row["routing_weight"].template as<std::string>(),
        .created_at = row["created_at"].template as<std::string>(),
        .updated_at = row["updated_at"].template as<std::string>(),
    };
}

// Locally generated primary keys keep the repository free of database
// sequences while staying collision-safe for message identifiers.
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

QueryDomainRepository::QueryDomainRepository(PostgresStore& store) : store_(store) {}

bool isPostgresError(const std::exception& error) {
    // pqxx::failure is the common base of every libpqxx 7.x exception type.
    // PostgresUnavailable (thrown by PostgresStore on broken connections,
    // pool exhaustion, etc.) derives from std::runtime_error instead, so it
    // must be matched separately to keep the UNAVAILABLE mapping intact.
    return dynamic_cast<const pqxx::failure*>(&error) != nullptr ||
           dynamic_cast<const PostgresUnavailable*>(&error) != nullptr;
}

bool QueryDomainRepository::createConversation(const ConversationRecord& conversation) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "INSERT INTO conversations "
            "(id, owner_id, title, memory_summary, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, "
            "COALESCE(NULLIF($5, '')::timestamptz, NOW()), "
            "COALESCE(NULLIF($6, '')::timestamptz, NOW())) "
            "RETURNING id",
            conversation.id, conversation.owner_id, conversation.title, conversation.memory_summary,
            conversation.created_at, conversation.updated_at);
        inserted = !result.empty();
    });
    return inserted;
}

std::optional<ConversationRecord> QueryDomainRepository::getConversationById(
    const std::string& owner_id, const std::string& conversation_id) {
    std::optional<ConversationRecord> conversation;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, title, memory_summary, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM conversations WHERE owner_id = $1 AND id = $2",
            owner_id, conversation_id);
        if (!result.empty()) {
            conversation = conversationFromRow(result.front());
        }
    });
    return conversation;
}

std::vector<ConversationRecord> QueryDomainRepository::listConversations(const std::string& owner_id) {
    std::vector<ConversationRecord> conversations;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, title, memory_summary, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM conversations WHERE owner_id = $1 ORDER BY created_at, id",
            owner_id);
        conversations.reserve(result.size());
        for (const auto& row : result) {
            conversations.push_back(conversationFromRow(row));
        }
    });
    return conversations;
}

bool QueryDomainRepository::ensureConversation(const std::string& owner_id,
                                                 const std::string& conversation_id,
                                                 const std::string& title) {
    bool ensured = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto existing = execParams(
            transaction, "SELECT owner_id FROM conversations WHERE id = $1", conversation_id);
        if (!existing.empty()) {
            ensured = existing.front()["owner_id"].template as<std::string>() == owner_id;
            return;
        }
        // The INSERT runs inside a subtransaction: when a concurrent creator
        // wins the race, the unique_violation aborts only the subtransaction
        // (never the outer pqxx::work), so the ownership re-check below stays
        // legal. Re-running statements on an aborted transaction would throw
        // sql_error (SQLSTATE 25P02), so no exception may escape into it.
        bool inserted = false;
        try {
            pqxx::subtransaction insert_guard(transaction);
            const auto result = execParams(
                insert_guard,
                "INSERT INTO conversations "
                "(id, owner_id, title, memory_summary, created_at, updated_at) "
                "VALUES ($1, $2, $3, '', NOW(), NOW()) RETURNING id",
                conversation_id, owner_id, title);
            inserted = !result.empty();
            insert_guard.commit();
        } catch (const pqxx::unique_violation&) {
            // A concurrent insert won the race; fall through and accept it
            // only when the owner matches.
        }
        if (inserted) {
            ensured = true;
            return;
        }
        const auto recheck = execParams(
            transaction, "SELECT owner_id FROM conversations WHERE id = $1", conversation_id);
        ensured = !recheck.empty() &&
                  recheck.front()["owner_id"].template as<std::string>() == owner_id;
    });
    return ensured;
}

bool QueryDomainRepository::appendMessage(const MessageRecord& message) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "INSERT INTO conversation_messages "
            "(id, owner_id, conversation_id, role, content, sequence_no, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, $5, $6, "
            "COALESCE(NULLIF($7, '')::timestamptz, NOW()), "
            "COALESCE(NULLIF($8, '')::timestamptz, NOW())) "
            "ON CONFLICT (owner_id, conversation_id, sequence_no) DO NOTHING "
            "RETURNING id",
            message.id, message.owner_id, message.conversation_id, message.role, message.content,
            message.sequence_no, message.created_at, message.updated_at);
        inserted = !result.empty();
    });
    return inserted;
}

std::optional<MessageRecord> QueryDomainRepository::appendMessageAutoSequence(
    const std::string& owner_id, const std::string& conversation_id, const std::string& role,
    const std::string& content) {
    return appendMessageAutoSequence(generateRowId("msg"), owner_id, conversation_id, role,
                                     content);
}

std::optional<MessageRecord> QueryDomainRepository::appendMessageAutoSequence(
    const std::string& message_id, const std::string& owner_id,
    const std::string& conversation_id, const std::string& role, const std::string& content) {
    std::optional<MessageRecord> stored;
    store_.executeTransaction([&](pqxx::work& transaction) {
        // Lock the owner's conversation row first. Every auto-sequenced
        // append takes this lock, so the next sequence_no is assigned under
        // serialization and concurrent appends cannot collide or overwrite
        // each other. The same lock serializes the idempotency check below.
        const auto conversation = execParams(
            transaction,
            "SELECT id FROM conversations WHERE id = $2 AND owner_id = $1 FOR UPDATE",
            owner_id, conversation_id);
        if (conversation.empty()) {
            return;
        }
        // Idempotency: a message id deterministically derived from the
        // request id makes retry finalizes safe. An existing row is returned
        // as-is and consumes no sequence number (no duplicate history, no
        // sequence gap), while the sequence assignment stays inside this
        // transaction.
        const auto existing = execParams(
            transaction,
            "SELECT id, owner_id, conversation_id, role, content, sequence_no, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM conversation_messages WHERE id = $1 AND owner_id = $2",
            message_id, owner_id);
        if (!existing.empty()) {
            stored = messageFromRow(existing.front());
            return;
        }
        const auto result = execParams(
            transaction,
            "INSERT INTO conversation_messages "
            "(id, owner_id, conversation_id, role, content, sequence_no, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, $5, "
            "(SELECT COALESCE(MAX(sequence_no), 0) + 1 FROM conversation_messages "
            "WHERE owner_id = $2 AND conversation_id = $3), NOW(), NOW()) "
            "RETURNING id, owner_id, conversation_id, role, content, sequence_no, "
            "created_at::text AS created_at, updated_at::text AS updated_at",
            message_id, owner_id, conversation_id, role, content);
        if (!result.empty()) {
            stored = messageFromRow(result.front());
        }
    });
    return stored;
}

std::vector<MessageRecord> QueryDomainRepository::listMessages(const std::string& owner_id,
                                                                const std::string& conversation_id) {
    std::vector<MessageRecord> messages;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, conversation_id, role, content, sequence_no, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM conversation_messages "
            "WHERE owner_id = $1 AND conversation_id = $2 "
            "ORDER BY sequence_no, created_at, id",
            owner_id, conversation_id);
        messages.reserve(result.size());
        for (const auto& row : result) {
            messages.push_back(messageFromRow(row));
        }
    });
    return messages;
}

bool QueryDomainRepository::createQueryLog(const QueryLogRecord& query_log) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        try {
            const auto result = execParams(
                transaction,
                "INSERT INTO query_logs "
                "(id, owner_id, conversation_id, request_text, route_decision, execution_plan, "
                "response_text, model, status, created_at, updated_at) "
                "VALUES ($1, $2, $3, $4, COALESCE(NULLIF($5, '')::jsonb, '{}'::jsonb), "
                "COALESCE(NULLIF($6, '')::jsonb, '{}'::jsonb), $7, $8, $9, "
                "COALESCE(NULLIF($10, '')::timestamptz, NOW()), "
                "COALESCE(NULLIF($11, '')::timestamptz, NOW())) "
                "RETURNING id",
                query_log.id, query_log.owner_id, query_log.conversation_id, query_log.request_text,
                query_log.route_decision, query_log.execution_plan, query_log.response_text,
                query_log.model, query_log.status, query_log.created_at, query_log.updated_at);
            inserted = !result.empty();
        } catch (const pqxx::unique_violation&) {
            // Duplicate id (same-request retry or cross-owner reuse): never
            // upsert; report the conflict so callers can verify ownership.
            inserted = false;
        }
    });
    return inserted;
}

std::optional<QueryLogRecord> QueryDomainRepository::getQueryLogById(
    const std::string& owner_id, const std::string& query_log_id) {
    std::optional<QueryLogRecord> query_log;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, conversation_id, request_text, "
            "route_decision::text AS route_decision, execution_plan::text AS execution_plan, "
            "response_text, model, status, created_at::text AS created_at, "
            "updated_at::text AS updated_at "
            "FROM query_logs WHERE owner_id = $1 AND id = $2",
            owner_id, query_log_id);
        if (!result.empty()) {
            query_log = queryLogFromRow(result.front());
        }
    });
    return query_log;
}

bool QueryDomainRepository::updateQueryLog(const QueryLogRecord& query_log) {
    bool updated = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "UPDATE query_logs SET response_text = $3, model = $4, status = $5, "
            "updated_at = NOW() WHERE owner_id = $1 AND id = $2 RETURNING id",
            query_log.owner_id, query_log.id, query_log.response_text, query_log.model,
            query_log.status);
        updated = !result.empty();
    });
    return updated;
}

bool QueryDomainRepository::createTrace(const TraceRecord& trace) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        try {
            const auto result = execParams(
                transaction,
                "INSERT INTO traces "
                "(id, owner_id, query_log_id, trace_payload, status, created_at, updated_at) "
                "VALUES ($1, $2, $3, COALESCE(NULLIF($4, '')::jsonb, '{}'::jsonb), $5, "
                "COALESCE(NULLIF($6, '')::timestamptz, NOW()), "
                "COALESCE(NULLIF($7, '')::timestamptz, NOW())) "
                "RETURNING id",
                trace.id, trace.owner_id, trace.query_log_id, trace.trace_payload, trace.status,
                trace.created_at, trace.updated_at);
            inserted = !result.empty();
        } catch (const pqxx::unique_violation&) {
            // Duplicate id (same-request retry or cross-owner reuse): never
            // upsert; report the conflict so callers can verify ownership.
            inserted = false;
        }
    });
    return inserted;
}

std::optional<TraceRecord> QueryDomainRepository::getTraceById(const std::string& owner_id,
                                                                const std::string& trace_id) {
    std::optional<TraceRecord> trace;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, query_log_id, trace_payload::text AS trace_payload, status, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM traces WHERE owner_id = $1 AND id = $2",
            owner_id, trace_id);
        if (!result.empty()) {
            trace = traceFromRow(result.front());
        }
    });
    return trace;
}

bool QueryDomainRepository::updateTrace(const TraceRecord& trace) {
    bool updated = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "UPDATE traces SET "
            "trace_payload = COALESCE(NULLIF($3, '')::jsonb, trace_payload), "
            "status = $4, updated_at = NOW() "
            "WHERE owner_id = $1 AND id = $2 RETURNING id",
            trace.owner_id, trace.id, trace.trace_payload, trace.status);
        updated = !result.empty();
    });
    return updated;
}

bool QueryDomainRepository::appendTokenUsageLedger(const TokenUsageLedgerRecord& usage) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        try {
            const auto result = execParams(
                transaction,
                "INSERT INTO token_usage_ledger "
                "(id, owner_id, query_log_id, model, prompt_tokens, completion_tokens, estimated, "
                "cost_usd, created_at, updated_at) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7, COALESCE(NULLIF($8, '')::numeric, 0::numeric), "
                "COALESCE(NULLIF($9, '')::timestamptz, NOW()), "
                "COALESCE(NULLIF($10, '')::timestamptz, NOW())) "
                "RETURNING id",
                usage.id, usage.owner_id, usage.query_log_id, usage.model, usage.prompt_tokens,
                usage.completion_tokens, usage.estimated, usage.cost_usd, usage.created_at,
                usage.updated_at);
            inserted = !result.empty();
        } catch (const pqxx::unique_violation&) {
            // Duplicate id (same request retried): the existing estimate stays
            // authoritative. Report the conflict instead of throwing so
            // finalize paths can call this unconditionally.
            inserted = false;
        }
    });
    return inserted;
}

std::vector<TokenUsageLedgerRecord> QueryDomainRepository::listTokenUsageLedgerByOwner(
    const std::string& owner_id) {
    std::vector<TokenUsageLedgerRecord> usage;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, query_log_id, model, prompt_tokens, completion_tokens, estimated, "
            "cost_usd::text AS cost_usd, created_at::text AS created_at, "
            "updated_at::text AS updated_at "
            "FROM token_usage_ledger WHERE owner_id = $1 ORDER BY created_at, id",
            owner_id);
        usage.reserve(result.size());
        for (const auto& row : result) {
            usage.push_back(tokenUsageFromRow(row));
        }
    });
    return usage;
}

bool QueryDomainRepository::createFeedback(const FeedbackRecord& feedback) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "INSERT INTO feedback "
            "(id, owner_id, query_log_id, agent_id, rating, comment, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, $5, $6, "
            "COALESCE(NULLIF($7, '')::timestamptz, NOW()), "
            "COALESCE(NULLIF($8, '')::timestamptz, NOW())) "
            "RETURNING id",
            feedback.id, feedback.owner_id, feedback.query_log_id, feedback.agent_id,
            feedback.rating, feedback.comment, feedback.created_at, feedback.updated_at);
        inserted = !result.empty();
    });
    return inserted;
}

std::optional<RouteQualityRecord> QueryDomainRepository::getRouteQuality(
    const std::string& owner_id, const std::string& agent_id) {
    std::optional<RouteQualityRecord> quality;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, agent_id, sample_count, "
            "average_rating::text AS average_rating, routing_weight::text AS routing_weight, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM agent_route_quality WHERE owner_id = $1 AND agent_id = $2",
            owner_id, agent_id);
        if (!result.empty()) {
            quality = routeQualityFromRow(result.front());
        }
    });
    return quality;
}

bool QueryDomainRepository::upsertRouteQuality(const RouteQualityRecord& quality) {
    bool written = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "INSERT INTO agent_route_quality "
            "(id, owner_id, agent_id, sample_count, average_rating, routing_weight, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, COALESCE(NULLIF($5, '')::numeric, 0::numeric), "
            "COALESCE(NULLIF($6, '')::numeric, 1::numeric), "
            "COALESCE(NULLIF($7, '')::timestamptz, NOW()), "
            "COALESCE(NULLIF($8, '')::timestamptz, NOW())) "
            "ON CONFLICT (owner_id, agent_id) DO UPDATE SET "
            "sample_count = EXCLUDED.sample_count, "
            "average_rating = EXCLUDED.average_rating, "
            "routing_weight = EXCLUDED.routing_weight, "
            "updated_at = EXCLUDED.updated_at "
            "RETURNING id",
            quality.id, quality.owner_id, quality.agent_id, quality.sample_count,
            quality.average_rating, quality.routing_weight, quality.created_at, quality.updated_at);
        written = !result.empty();
    });
    return written;
}

// ============================================================================
// [PR-E] workflow control: sandbox / compare / intervention / undo / autonomy
// ============================================================================

namespace {

template <typename Row>
SandboxRunRecord sandboxRunFromRow(const Row& row) {
    return SandboxRunRecord{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .query_log_id = row["query_log_id"].template as<std::string>(),
        .request_text = row["request_text"].template as<std::string>(),
        .response_text = row["response_text"].template as<std::string>(),
        .status = row["status"].template as<std::string>(),
        .created_at = row["created_at"].template as<std::string>(),
        .updated_at = row["updated_at"].template as<std::string>(),
    };
}

template <typename Row>
CompareRunRecord compareRunFromRow(const Row& row) {
    return CompareRunRecord{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .query_log_id = row["query_log_id"].template as<std::string>(),
        .request_text = row["request_text"].template as<std::string>(),
        .results = row["results"].template as<std::string>(),
        .status = row["status"].template as<std::string>(),
        .created_at = row["created_at"].template as<std::string>(),
        .updated_at = row["updated_at"].template as<std::string>(),
    };
}

template <typename Row>
InterventionRecord interventionFromRow(const Row& row) {
    return InterventionRecord{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .query_log_id = row["query_log_id"].template as<std::string>(),
        .state = row["state"].template as<std::string>(),
        .original_request = row["original_request"].template as<std::string>(),
        .edited_request = row["edited_request"].template as<std::string>(),
        .decision = row["decision"].template as<std::string>(),
        .created_at = row["created_at"].template as<std::string>(),
        .updated_at = row["updated_at"].template as<std::string>(),
    };
}

template <typename Row>
UndoActionRecord undoActionFromRow(const Row& row) {
    UndoActionRecord action{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .resource_type = row["resource_type"].template as<std::string>(),
        .resource_id = row["resource_id"].template as<std::string>(),
        .action_payload = row["action_payload"].template as<std::string>(),
        // version/expired are read as text and converted here on purpose:
        // the prebuilt libpqxx in the dependency prefix does not export the
        // source_location-flavored conversion_overrun constructor that
        // as<bool>/as<int> instantiations can reference.
        .version = [&row] {
            const auto text = row["version"].template as<std::string>();
            return text.empty() ? 1 : std::atoi(text.c_str());
        }(),
    };
    if (!row["expires_at"].is_null()) {
        action.expires_at = row["expires_at"].template as<std::string>();
    }
    if (!row["undone_at"].is_null()) {
        action.undone_at = row["undone_at"].template as<std::string>();
    }
    const auto expired_text = row["expired"].template as<std::string>();
    action.expired = expired_text == "t" || expired_text == "true";
    action.created_at = row["created_at"].template as<std::string>();
    action.updated_at = row["updated_at"].template as<std::string>();
    return action;
}

template <typename Row>
AutonomySettingRecord autonomySettingFromRow(const Row& row) {
    return AutonomySettingRecord{
        .id = row["id"].template as<std::string>(),
        .owner_id = row["owner_id"].template as<std::string>(),
        .agent_id = row["agent_id"].template as<std::string>(),
        .level = row["level"].template as<int>(),
        .created_at = row["created_at"].template as<std::string>(),
        .updated_at = row["updated_at"].template as<std::string>(),
    };
}

}  // namespace

bool QueryDomainRepository::createSandboxRun(const SandboxRunRecord& run) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "INSERT INTO sandbox_runs "
            "(id, owner_id, query_log_id, request_text, response_text, status, "
            "created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, $5, $6, NOW(), NOW()) RETURNING id",
            run.id, run.owner_id, run.query_log_id, run.request_text, run.response_text,
            run.status);
        inserted = !result.empty();
    });
    return inserted;
}

bool QueryDomainRepository::updateSandboxRun(const SandboxRunRecord& run) {
    bool updated = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "UPDATE sandbox_runs SET status = $3, response_text = $4, updated_at = NOW() "
            "WHERE id = $1 AND owner_id = $2 RETURNING id",
            run.id, run.owner_id, run.status, run.response_text);
        updated = !result.empty();
    });
    return updated;
}

std::optional<SandboxRunRecord> QueryDomainRepository::getSandboxRunById(
    const std::string& owner_id, const std::string& run_id) {
    std::optional<SandboxRunRecord> run;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, query_log_id, request_text, response_text, status, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM sandbox_runs WHERE id = $2 AND owner_id = $1",
            owner_id, run_id);
        if (!result.empty()) {
            run = sandboxRunFromRow(result.front());
        }
    });
    return run;
}

bool QueryDomainRepository::createCompareRun(const CompareRunRecord& run) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "INSERT INTO compare_runs "
            "(id, owner_id, query_log_id, request_text, results, status, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, COALESCE(NULLIF($5, '')::jsonb, '[]'::jsonb), $6, NOW(), NOW()) "
            "RETURNING id",
            run.id, run.owner_id, run.query_log_id, run.request_text, run.results, run.status);
        inserted = !result.empty();
    });
    return inserted;
}

bool QueryDomainRepository::updateCompareRun(const CompareRunRecord& run) {
    bool updated = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "UPDATE compare_runs SET results = COALESCE(NULLIF($3, '')::jsonb, '[]'::jsonb), "
            "status = $4, updated_at = NOW() "
            "WHERE id = $1 AND owner_id = $2 RETURNING id",
            run.id, run.owner_id, run.results, run.status);
        updated = !result.empty();
    });
    return updated;
}

std::optional<CompareRunRecord> QueryDomainRepository::getCompareRunById(
    const std::string& owner_id, const std::string& run_id) {
    std::optional<CompareRunRecord> run;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, query_log_id, request_text, results::text AS results, status, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM compare_runs WHERE id = $2 AND owner_id = $1",
            owner_id, run_id);
        if (!result.empty()) {
            run = compareRunFromRow(result.front());
        }
    });
    return run;
}

std::vector<CompareRunRecord> QueryDomainRepository::listCompareRunsByOwner(
    const std::string& owner_id) {
    std::vector<CompareRunRecord> runs;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, query_log_id, request_text, results::text AS results, status, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM compare_runs WHERE owner_id = $1 "
            "ORDER BY created_at DESC, id DESC LIMIT 50",
            owner_id);
        runs.reserve(result.size());
        for (const auto& row : result) {
            runs.push_back(compareRunFromRow(row));
        }
    });
    return runs;
}

bool QueryDomainRepository::createIntervention(const InterventionRecord& intervention) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "INSERT INTO interventions "
            "(id, owner_id, query_log_id, state, original_request, edited_request, decision, "
            "created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, NOW(), NOW()) RETURNING id",
            intervention.id, intervention.owner_id, intervention.query_log_id,
            intervention.state, intervention.original_request, intervention.edited_request,
            intervention.decision);
        inserted = !result.empty();
    });
    return inserted;
}

std::optional<InterventionRecord> QueryDomainRepository::getInterventionById(
    const std::string& owner_id, const std::string& intervention_id) {
    std::optional<InterventionRecord> intervention;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, query_log_id, state, original_request, edited_request, "
            "decision, created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM interventions WHERE id = $2 AND owner_id = $1",
            owner_id, intervention_id);
        if (!result.empty()) {
            intervention = interventionFromRow(result.front());
        }
    });
    return intervention;
}

InterventionResolveOutcome QueryDomainRepository::resolveIntervention(
    const std::string& owner_id, const std::string& intervention_id,
    const std::string& decision, const std::string& edited_request) {
    InterventionResolveOutcome outcome = InterventionResolveOutcome::kNotFound;
    store_.executeTransaction([&](pqxx::work& transaction) {
        // Single CAS statement: only a pending record owned by the caller can
        // transition, so concurrent resolvers serialize on the affected-row
        // count. edited_request is written only when a non-empty text arrives.
        const auto transition = execParams(
            transaction,
            "UPDATE interventions SET state = $3, decision = $3, "
            "edited_request = CASE WHEN $4 <> '' THEN $4 ELSE edited_request END, "
            "updated_at = NOW() "
            "WHERE id = $1 AND owner_id = $2 AND state = 'pending'",
            intervention_id, owner_id, decision, edited_request);
        if (transition.affected_rows() > 0) {
            outcome = InterventionResolveOutcome::kResolved;
            return;
        }
        const auto existing = execParams(
            transaction, "SELECT id FROM interventions WHERE id = $2 AND owner_id = $1",
            owner_id, intervention_id);
        outcome = existing.empty() ? InterventionResolveOutcome::kNotFound
                                   : InterventionResolveOutcome::kAlreadyResolved;
    });
    return outcome;
}

bool QueryDomainRepository::restoreInterventionToPending(const std::string& owner_id,
                                                         const std::string& intervention_id) {
    bool restored = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "UPDATE interventions SET state = 'pending', decision = '', updated_at = NOW() "
            "WHERE id = $2 AND owner_id = $1 AND state <> 'pending' RETURNING id",
            owner_id, intervention_id);
        restored = !result.empty();
    });
    return restored;
}

bool QueryDomainRepository::createUndoAction(const UndoActionRecord& action) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "INSERT INTO undo_actions "
            "(id, owner_id, resource_type, resource_id, action_payload, version, "
            "expires_at, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, COALESCE(NULLIF($5, '')::jsonb, '{}'::jsonb), $6, "
            "NOW() + INTERVAL '24 hours', NOW(), NOW()) RETURNING id",
            action.id, action.owner_id, action.resource_type, action.resource_id,
            action.action_payload, std::to_string(action.version));
        inserted = !result.empty();
    });
    return inserted;
}

std::optional<UndoActionRecord> QueryDomainRepository::getUndoActionById(
    const std::string& owner_id, const std::string& action_id) {
    std::optional<UndoActionRecord> action;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, resource_type, resource_id, action_payload::text AS action_payload, "
            "version::text AS version, expires_at::text AS expires_at, undone_at::text AS undone_at, "
            "(expires_at IS NOT NULL AND expires_at <= NOW())::text AS expired, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM undo_actions WHERE id = $2 AND owner_id = $1",
            owner_id, action_id);
        if (!result.empty()) {
            action = undoActionFromRow(result.front());
        }
    });
    return action;
}

bool QueryDomainRepository::markUndoActionUndone(const std::string& owner_id,
                                                 const std::string& action_id) {
    bool marked = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        // CAS: only the first caller flips undone_at; every retry observes
        // undone_at IS NOT NULL and gets zero rows back.
        const auto result = execParams(
            transaction,
            "UPDATE undo_actions SET undone_at = NOW(), updated_at = NOW() "
            "WHERE id = $2 AND owner_id = $1 AND undone_at IS NULL RETURNING id",
            owner_id, action_id);
        marked = !result.empty();
    });
    return marked;
}

bool QueryDomainRepository::upsertAutonomySetting(const std::string& owner_id,
                                                  const std::string& agent_id, int level) {
    bool written = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        // Upsert via conflict-update (never a conflict-skip): a repeated set
        // must refresh the same row in place, and the level CHECK constraint
        // (1..4) rejects anything out of range at the database level too.
        const auto result = execParams(
            transaction,
            "INSERT INTO autonomy_settings (id, owner_id, agent_id, level, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, NOW(), NOW()) "
            "ON CONFLICT (owner_id, agent_id) DO UPDATE SET "
            "level = EXCLUDED.level, updated_at = NOW() RETURNING id",
            generateRowId("autonomy"), owner_id, agent_id, level);
        written = !result.empty();
    });
    return written;
}

std::optional<AutonomySettingRecord> QueryDomainRepository::getAutonomySetting(
    const std::string& owner_id, const std::string& agent_id) {
    std::optional<AutonomySettingRecord> setting;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, agent_id, level, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM autonomy_settings WHERE owner_id = $1 AND agent_id = $2",
            owner_id, agent_id);
        if (!result.empty()) {
            setting = autonomySettingFromRow(result.front());
        }
    });
    return setting;
}

}  // namespace agent_rpc::common
