#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>

#ifndef NEXUSAI_AUTH_MIGRATION_SOURCE
#error "NEXUSAI_AUTH_MIGRATION_SOURCE must point at V010__local_auth.sql"
#endif

#ifndef NEXUSAI_AUTH_REPOSITORY_HEADER
#error "NEXUSAI_AUTH_REPOSITORY_HEADER must point at auth_repository.h"
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

TEST(AuthRepositoryContractTest, MigrationHasOwnerAndAuditColumnsWithoutForeignKeys) {
    const std::string migration = uppercase(readFile(NEXUSAI_AUTH_MIGRATION_SOURCE));
    ASSERT_FALSE(migration.empty());
    EXPECT_EQ(migration.find("FOREIGN KEY"), std::string::npos);
    EXPECT_EQ(migration.find(" REFERENCES "), std::string::npos);

    for (const std::string table_name : {"USERS", "AUTH_SESSIONS"}) {
        const std::string table = tableDefinition(migration, table_name);
        ASSERT_FALSE(table.empty());
        EXPECT_NE(table.find("OWNER_ID"), std::string::npos) << table_name;
        EXPECT_NE(table.find("CREATED_AT"), std::string::npos) << table_name;
        EXPECT_NE(table.find("UPDATED_AT"), std::string::npos) << table_name;
    }
}

TEST(AuthRepositoryContractTest, RepositoryExposesPostgresBackedAuthOperations) {
    const std::string header = readFile(NEXUSAI_AUTH_REPOSITORY_HEADER);
    ASSERT_FALSE(header.empty());
    EXPECT_NE(header.find("PostgresStore"), std::string::npos);
    for (const std::string operation : {"createUser", "findUserByUsername", "createSession",
                                        "findActiveSessionByTokenHash", "revokeSession"}) {
        EXPECT_NE(header.find(operation), std::string::npos) << operation;
    }
}

}  // namespace
