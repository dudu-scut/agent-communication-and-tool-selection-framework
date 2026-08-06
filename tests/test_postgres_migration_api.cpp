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

}  // namespace
