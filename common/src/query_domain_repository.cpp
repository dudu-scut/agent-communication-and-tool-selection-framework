#include "agent_rpc/common/query_domain_repository.h"

#include <pqxx/version>

#include <cstdint>
#include <string>
#include <utility>

namespace agent_rpc::common {
namespace {

template <typename... Arguments>
pqxx::result execParams(pqxx::work& transaction, const std::string& query, Arguments&&... arguments) {
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

}  // namespace

QueryDomainRepository::QueryDomainRepository(PostgresStore& store) : store_(store) {}

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

}  // namespace agent_rpc::common
