#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <utility>

#ifndef NEXUSAI_DURABLE_DOMAIN_MIGRATION_SOURCE
#error "NEXUSAI_DURABLE_DOMAIN_MIGRATION_SOURCE must point at V011__durable_domain.sql"
#endif

namespace {

std::string readFile(const char* path) {
    std::ifstream source{path, std::ios::binary};
    EXPECT_TRUE(source.is_open()) << "cannot read " << path;
    return {std::istreambuf_iterator<char>{source}, std::istreambuf_iterator<char>{}};
}

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

std::string tableDefinition(const std::string& source, const std::string& table_name) {
    const std::string marker = "CREATE TABLE IF NOT EXISTS " + table_name;
    const std::size_t start = source.find(marker);
    EXPECT_NE(start, std::string::npos) << "missing table " << table_name;
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t end = source.find(");", start);
    EXPECT_NE(end, std::string::npos) << "unterminated table " << table_name;
    if (end == std::string::npos) {
        return source.substr(start);
    }
    return source.substr(start, end - start);
}

void expectContains(const std::string& table, const std::string& fragment,
                    const std::string& table_name) {
    EXPECT_NE(table.find(fragment), std::string::npos)
        << table_name << " is missing " << fragment;
}

TEST(DurableDomainSchemaContractTest, DefinesAllTablesWithOwnerAndAuditColumnsWithoutForeignKeys) {
    const std::string migration = uppercase(readFile(NEXUSAI_DURABLE_DOMAIN_MIGRATION_SOURCE));
    ASSERT_FALSE(migration.empty());
    EXPECT_EQ(migration.find("FOREIGN KEY"), std::string::npos);
    EXPECT_EQ(migration.find(" REFERENCES "), std::string::npos);

    for (const std::string table_name : {
              "CONVERSATIONS", "CONVERSATION_MESSAGES", "QUERY_LOGS", "TRACES", "TOKEN_USAGE_LEDGER",
             "FEEDBACK", "AGENT_ROUTE_QUALITY", "SHARES", "WORKFLOW_TEMPLATES", "SANDBOX_RUNS",
             "COMPARE_RUNS", "INTERVENTIONS", "UNDO_ACTIONS", "AUTONOMY_SETTINGS", "AGENT_REGISTRY"}) {
        const std::string table = tableDefinition(migration, table_name);
        ASSERT_FALSE(table.empty()) << table_name;
        expectContains(table, "ID TEXT PRIMARY KEY", table_name);
        expectContains(table, "OWNER_ID TEXT NOT NULL", table_name);
        expectContains(table, "CREATED_AT TIMESTAMPTZ NOT NULL DEFAULT NOW()", table_name);
        expectContains(table, "UPDATED_AT TIMESTAMPTZ NOT NULL DEFAULT NOW()", table_name);
    }
}

TEST(DurableDomainSchemaContractTest, DefinesRequiredColumnsAndSafeJsonDefaults) {
    const std::string migration = uppercase(readFile(NEXUSAI_DURABLE_DOMAIN_MIGRATION_SOURCE));
    ASSERT_FALSE(migration.empty());

    for (const auto& required : {
             std::pair{"CONVERSATIONS", "TITLE TEXT"},
             std::pair{"CONVERSATIONS", "MEMORY_SUMMARY TEXT"},
             std::pair{"CONVERSATION_MESSAGES", "CONVERSATION_ID TEXT NOT NULL"},
             std::pair{"CONVERSATION_MESSAGES", "ROLE TEXT NOT NULL"},
             std::pair{"CONVERSATION_MESSAGES", "CONTENT TEXT NOT NULL"},
             std::pair{"CONVERSATION_MESSAGES", "SEQUENCE_NO BIGINT NOT NULL"},
             std::pair{"QUERY_LOGS", "CONVERSATION_ID TEXT"},
             std::pair{"QUERY_LOGS", "REQUEST_TEXT TEXT"},
             std::pair{"QUERY_LOGS", "ROUTE_DECISION JSONB"},
             std::pair{"QUERY_LOGS", "EXECUTION_PLAN JSONB"},
             std::pair{"QUERY_LOGS", "RESPONSE_TEXT TEXT"},
             std::pair{"QUERY_LOGS", "MODEL TEXT"},
             std::pair{"QUERY_LOGS", "STATUS TEXT"},
             std::pair{"TRACES", "QUERY_LOG_ID TEXT NOT NULL"},
             std::pair{"TRACES", "TRACE_PAYLOAD JSONB"},
             std::pair{"TRACES", "STATUS TEXT"},
             std::pair{"TOKEN_USAGE_LEDGER", "QUERY_LOG_ID TEXT NOT NULL"},
             std::pair{"TOKEN_USAGE_LEDGER", "MODEL TEXT NOT NULL"},
             std::pair{"TOKEN_USAGE_LEDGER", "PROMPT_TOKENS BIGINT"},
             std::pair{"TOKEN_USAGE_LEDGER", "COMPLETION_TOKENS BIGINT"},
             std::pair{"TOKEN_USAGE_LEDGER", "ESTIMATED BOOLEAN"},
             std::pair{"TOKEN_USAGE_LEDGER", "COST_USD NUMERIC"},
             std::pair{"FEEDBACK", "QUERY_LOG_ID TEXT NOT NULL"},
             std::pair{"FEEDBACK", "AGENT_ID TEXT NOT NULL"},
             std::pair{"FEEDBACK", "RATING SMALLINT"},
             std::pair{"FEEDBACK", "COMMENT TEXT"},
             std::pair{"AGENT_ROUTE_QUALITY", "AGENT_ID TEXT NOT NULL"},
             std::pair{"AGENT_ROUTE_QUALITY", "SAMPLE_COUNT BIGINT"},
             std::pair{"AGENT_ROUTE_QUALITY", "AVERAGE_RATING NUMERIC"},
             std::pair{"AGENT_ROUTE_QUALITY", "ROUTING_WEIGHT NUMERIC"},
             std::pair{"SHARES", "CONVERSATION_ID TEXT NOT NULL"},
             std::pair{"SHARES", "TOKEN_HASH TEXT NOT NULL"},
             std::pair{"SHARES", "PERMISSION TEXT NOT NULL"},
             std::pair{"SHARES", "EXPIRES_AT TIMESTAMPTZ"},
             std::pair{"SHARES", "REVOKED_AT TIMESTAMPTZ"},
             std::pair{"WORKFLOW_TEMPLATES", "NAME TEXT NOT NULL"},
             std::pair{"WORKFLOW_TEMPLATES", "DESCRIPTION TEXT"},
             std::pair{"WORKFLOW_TEMPLATES", "DEFINITION JSONB"},
             std::pair{"WORKFLOW_TEMPLATES", "VISIBILITY TEXT"},
             std::pair{"WORKFLOW_TEMPLATES", "VERSION INTEGER"},
             std::pair{"SANDBOX_RUNS", "QUERY_LOG_ID TEXT NOT NULL"},
             std::pair{"SANDBOX_RUNS", "REQUEST_TEXT TEXT"},
             std::pair{"SANDBOX_RUNS", "RESPONSE_TEXT TEXT"},
             std::pair{"SANDBOX_RUNS", "STATUS TEXT"},
             std::pair{"COMPARE_RUNS", "QUERY_LOG_ID TEXT NOT NULL"},
             std::pair{"COMPARE_RUNS", "REQUEST_TEXT TEXT"},
             std::pair{"COMPARE_RUNS", "RESULTS JSONB"},
             std::pair{"COMPARE_RUNS", "STATUS TEXT"},
             std::pair{"INTERVENTIONS", "QUERY_LOG_ID TEXT NOT NULL"},
             std::pair{"INTERVENTIONS", "STATE TEXT NOT NULL"},
             std::pair{"INTERVENTIONS", "ORIGINAL_REQUEST TEXT"},
             std::pair{"INTERVENTIONS", "EDITED_REQUEST TEXT"},
             std::pair{"INTERVENTIONS", "DECISION TEXT"},
             std::pair{"UNDO_ACTIONS", "RESOURCE_TYPE TEXT NOT NULL"},
             std::pair{"UNDO_ACTIONS", "RESOURCE_ID TEXT NOT NULL"},
             std::pair{"UNDO_ACTIONS", "ACTION_PAYLOAD JSONB"},
             std::pair{"UNDO_ACTIONS", "VERSION INTEGER"},
             std::pair{"UNDO_ACTIONS", "EXPIRES_AT TIMESTAMPTZ"},
             std::pair{"UNDO_ACTIONS", "UNDONE_AT TIMESTAMPTZ"},
             std::pair{"AUTONOMY_SETTINGS", "AGENT_ID TEXT NOT NULL"},
             std::pair{"AUTONOMY_SETTINGS", "LEVEL SMALLINT"},
             std::pair{"AGENT_REGISTRY", "AGENT_ID TEXT NOT NULL"},
             std::pair{"AGENT_REGISTRY", "DISPLAY_NAME TEXT NOT NULL"},
             std::pair{"AGENT_REGISTRY", "CAPABILITIES JSONB"},
             std::pair{"AGENT_REGISTRY", "LAST_HEARTBEAT TIMESTAMPTZ"},
             std::pair{"AGENT_REGISTRY", "HEALTH_STATUS TEXT"}}) {
        const std::string table = tableDefinition(migration, required.first);
        ASSERT_FALSE(table.empty()) << required.first;
        expectContains(table, required.second, required.first);
    }

    for (const auto& json_default : {
             std::pair{"QUERY_LOGS", "ROUTE_DECISION JSONB NOT NULL DEFAULT '{}'::JSONB"},
             std::pair{"QUERY_LOGS", "EXECUTION_PLAN JSONB NOT NULL DEFAULT '{}'::JSONB"},
             std::pair{"TRACES", "TRACE_PAYLOAD JSONB NOT NULL DEFAULT '{}'::JSONB"},
             std::pair{"WORKFLOW_TEMPLATES", "DEFINITION JSONB NOT NULL DEFAULT '{}'::JSONB"},
             std::pair{"COMPARE_RUNS", "RESULTS JSONB NOT NULL DEFAULT '[]'::JSONB"},
             std::pair{"UNDO_ACTIONS", "ACTION_PAYLOAD JSONB NOT NULL DEFAULT '{}'::JSONB"},
             std::pair{"AGENT_REGISTRY", "CAPABILITIES JSONB NOT NULL DEFAULT '[]'::JSONB"}}) {
        const std::string table = tableDefinition(migration, json_default.first);
        ASSERT_FALSE(table.empty()) << json_default.first;
        expectContains(table, json_default.second, json_default.first);
    }
}

TEST(DurableDomainSchemaContractTest, DefinesDomainConstraintsAndLookupIndexes) {
    const std::string migration = uppercase(readFile(NEXUSAI_DURABLE_DOMAIN_MIGRATION_SOURCE));
    ASSERT_FALSE(migration.empty());

    EXPECT_NE(migration.find("CREATE UNIQUE INDEX IF NOT EXISTS UQ_CONVERSATION_MESSAGES_OWNER_CONVERSATION_SEQUENCE"),
              std::string::npos);
    EXPECT_NE(migration.find("ON CONVERSATION_MESSAGES(OWNER_ID, CONVERSATION_ID, SEQUENCE_NO)"),
              std::string::npos);
    EXPECT_NE(migration.find("UNIQUE (TOKEN_HASH)"), std::string::npos);
    EXPECT_NE(migration.find("UNIQUE (OWNER_ID, AGENT_ID)"), std::string::npos);
    EXPECT_NE(migration.find("UNIQUE (OWNER_ID, NAME, VERSION)"), std::string::npos);
    EXPECT_NE(migration.find("RATING SMALLINT NOT NULL CHECK (RATING BETWEEN 1 AND 5)"),
              std::string::npos);
    EXPECT_NE(migration.find("LEVEL SMALLINT NOT NULL DEFAULT 1 CHECK (LEVEL BETWEEN 1 AND 4)"),
              std::string::npos);

    for (const std::string index_name : {
             "IDX_CONVERSATIONS_OWNER_CREATED", "IDX_CONVERSATION_MESSAGES_CONVERSATION_SEQUENCE",
             "IDX_QUERY_LOGS_CONVERSATION_CREATED", "IDX_QUERY_LOGS_OWNER_CREATED",
             "IDX_TRACES_QUERY_LOG", "IDX_TOKEN_USAGE_LEDGER_QUERY_LOG", "IDX_FEEDBACK_QUERY_LOG",
             "IDX_SHARES_TOKEN_HASH", "IDX_SHARES_OWNER_EXPIRY", "IDX_SHARES_EXPIRY",
             "IDX_SANDBOX_RUNS_QUERY_LOG", "IDX_COMPARE_RUNS_QUERY_LOG", "IDX_INTERVENTIONS_QUERY_LOG",
             "IDX_UNDO_ACTIONS_RESOURCE", "IDX_UNDO_ACTIONS_EXPIRY", "IDX_AUTONOMY_SETTINGS_OWNER_AGENT",
             "IDX_AGENT_REGISTRY_OWNER_AGENT"}) {
        EXPECT_NE(migration.find(index_name), std::string::npos) << index_name;
    }
}

TEST(DurableDomainSchemaContractTest, SharesPersistOnlyTokenHashes) {
    const std::string migration = uppercase(readFile(NEXUSAI_DURABLE_DOMAIN_MIGRATION_SOURCE));
    const std::string shares = tableDefinition(migration, "SHARES");
    ASSERT_FALSE(shares.empty());
    EXPECT_NE(shares.find("TOKEN_HASH TEXT NOT NULL"), std::string::npos);
    EXPECT_EQ(std::regex_search(shares, std::regex(R"(\bTOKEN\s+(TEXT|VARCHAR|CHAR|BYTEA)\b)")), false);
}

}  // namespace
