#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

#include <pqxx/version>

#ifndef NEXUSAI_MIGRATION_RUNNER_SOURCE
#error "NEXUSAI_MIGRATION_RUNNER_SOURCE must point at migration_runner.cpp"
#endif

namespace {

std::string readMigrationRunnerSource() {
    std::ifstream source{NEXUSAI_MIGRATION_RUNNER_SOURCE, std::ios::binary};
    EXPECT_TRUE(source.is_open()) << "cannot read " << NEXUSAI_MIGRATION_RUNNER_SOURCE;
    return {std::istreambuf_iterator<char>{source}, std::istreambuf_iterator<char>{}};
}

TEST(PostgresMigrationApiTest, KeepsExplicitVersionSpecificParameterizedBranches) {
    const std::string source = readMigrationRunnerSource();
    const std::string version_guard = "#if PQXX_VERSION_MAJOR >= 8";
    const std::string legacy_guard = "#else  // libpqxx 6.x/7.x";

    const std::size_t guard = source.find(version_guard);
    ASSERT_NE(guard, std::string::npos);
    const std::size_t legacy = source.find(legacy_guard, guard + version_guard.size());
    ASSERT_NE(legacy, std::string::npos);
    const std::size_t end = source.find("#endif", legacy + legacy_guard.size());
    ASSERT_NE(end, std::string::npos);

    const std::string v8_branch = source.substr(guard, legacy - guard);
    const std::string legacy_branch = source.substr(legacy, end - legacy);
    EXPECT_NE(v8_branch.find("transaction.exec("), std::string::npos);
    EXPECT_NE(v8_branch.find("pqxx::params{"), std::string::npos);
    EXPECT_EQ(v8_branch.find("exec_params"), std::string::npos);
    EXPECT_NE(legacy_branch.find("transaction.exec_params("), std::string::npos);

    EXPECT_NE(source.find("<pqxx/version>"), std::string::npos);
    EXPECT_NE(source.find("WHERE version = $1"), std::string::npos);
    EXPECT_NE(source.find("VALUES ($1, $2, $3)"), std::string::npos);
}

TEST(PostgresMigrationApiTest, ExposesSupportedVersionMacroAtCompileTime) {
    static_assert(PQXX_VERSION_MAJOR >= 6, "libpqxx 6.x or newer is required");

#if PQXX_VERSION_MAJOR >= 8
    SUCCEED() << "compiled against the libpqxx 8+ exec(query, params) path";
#else
    SUCCEED() << "compiled against the libpqxx 6.x/7.x exec_params path";
#endif
}

TEST(PostgresMigrationApiTest, UsesTransactionScopedAdvisoryLockForSchemaAndEachMigration) {
    const std::string source = readMigrationRunnerSource();

    EXPECT_EQ(source.find("pg_advisory_lock"), std::string::npos);
    EXPECT_EQ(source.find("pg_advisory_unlock"), std::string::npos);

    constexpr char xact_lock[] = "transaction.exec(\"SELECT pg_advisory_xact_lock(739214640)\")";
    const std::size_t first_lock = source.find(xact_lock);
    ASSERT_NE(first_lock, std::string::npos);
    EXPECT_EQ(source.find(xact_lock, first_lock + sizeof(xact_lock) - 1), std::string::npos);

    const std::size_t create_transaction =
        source.find("store_.executeTransaction([](pqxx::work& transaction) {");
    ASSERT_NE(create_transaction, std::string::npos);
    const std::size_t create_table = source.find("CREATE TABLE IF NOT EXISTS schema_migrations", create_transaction);
    ASSERT_NE(create_table, std::string::npos);
    EXPECT_LT(first_lock, create_table);
    EXPECT_GT(first_lock, create_transaction);

    const std::size_t migration_transaction =
        source.find("store_.executeTransaction([&](pqxx::work& transaction) {");
    ASSERT_NE(migration_transaction, std::string::npos);
    const std::size_t second_lock = source.find(xact_lock, first_lock + sizeof(xact_lock) - 1);
    ASSERT_NE(second_lock, std::string::npos);
    const std::size_t checksum_query =
        source.find("SELECT checksum FROM schema_migrations WHERE version = $1", migration_transaction);
    ASSERT_NE(checksum_query, std::string::npos);
    EXPECT_GT(second_lock, migration_transaction);
    EXPECT_LT(second_lock, checksum_query);
}

}  // namespace
