#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

#ifndef RPC_SERVER_MAIN_SOURCE_PATH
#error "RPC_SERVER_MAIN_SOURCE_PATH must be provided by tests/CMakeLists.txt"
#endif

#ifndef RPC_SERVER_CMAKE_PATH
#error "RPC_SERVER_CMAKE_PATH must be provided by tests/CMakeLists.txt"
#endif

#ifndef RPC_COMPOSE_PATH
#error "RPC_COMPOSE_PATH must be provided by tests/CMakeLists.txt"
#endif

namespace {

std::string readFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void expectOrdered(const std::string& source, const std::string& earlier,
                   const std::string& later) {
    const std::size_t earlier_position = source.find(earlier);
    const std::size_t later_position = source.find(later);
    ASSERT_NE(earlier_position, std::string::npos) << "missing: " << earlier;
    ASSERT_NE(later_position, std::string::npos) << "missing: " << later;
    EXPECT_LT(earlier_position, later_position)
        << earlier << " must occur before " << later;
}

}  // namespace

TEST(RpcServerMigrationsTest, RunsMigrationsBeforeServerInitialization) {
    const std::string source = readFile(RPC_SERVER_MAIN_SOURCE_PATH);
    ASSERT_FALSE(source.empty()) << "Unable to read " << RPC_SERVER_MAIN_SOURCE_PATH;

    EXPECT_NE(source.find("PostgresConfig::fromEnvironment"), std::string::npos);
    EXPECT_NE(source.find("PostgresStore"), std::string::npos);
    EXPECT_NE(source.find("MigrationRunner"), std::string::npos);
    expectOrdered(source, ".migrate(", "server.initialize");
}

TEST(RpcServerMigrationsTest, MigrationFailureReturnsBeforeInitialization) {
    const std::string source = readFile(RPC_SERVER_MAIN_SOURCE_PATH);
    ASSERT_FALSE(source.empty()) << "Unable to read " << RPC_SERVER_MAIN_SOURCE_PATH;

    const std::size_t migration = source.find(".migrate(");
    const std::size_t initialize = source.find("server.initialize");
    ASSERT_NE(migration, std::string::npos);
    ASSERT_NE(initialize, std::string::npos);
    ASSERT_LT(migration, initialize);

    const std::string startup = source.substr(migration, initialize - migration);
    EXPECT_NE(startup.find("catch (const std::exception"), std::string::npos);
    EXPECT_NE(startup.find("return 1;"), std::string::npos);
}

TEST(RpcServerMigrationsTest, UsesEnvironmentOverrideAndDeterministicCompileDefault) {
    const std::string source = readFile(RPC_SERVER_MAIN_SOURCE_PATH);
    const std::string cmake = readFile(RPC_SERVER_CMAKE_PATH);
    ASSERT_FALSE(source.empty()) << "Unable to read " << RPC_SERVER_MAIN_SOURCE_PATH;
    ASSERT_FALSE(cmake.empty()) << "Unable to read " << RPC_SERVER_CMAKE_PATH;

    EXPECT_NE(source.find("NEXUSAI_MIGRATIONS_DIR"), std::string::npos);
    EXPECT_NE(source.find("NEXUSAI_MIGRATIONS_DEFAULT_DIR"), std::string::npos);
    EXPECT_NE(cmake.find("NEXUSAI_MIGRATIONS_DEFAULT_DIR"), std::string::npos);
    EXPECT_NE(cmake.find("db/migrations"), std::string::npos);
    EXPECT_NE(cmake.find("agent_rpc_db"), std::string::npos);
}

TEST(RpcServerMigrationsTest, ComposeRunsMigrationsInRpcServerStartup) {
    const std::string compose = readFile(RPC_COMPOSE_PATH);
    ASSERT_FALSE(compose.empty()) << "Unable to read " << RPC_COMPOSE_PATH;

    EXPECT_EQ(compose.find("\n  migrate:"), std::string::npos);
    const std::size_t rpc_server = compose.find("\n  rpc-server:");
    ASSERT_NE(rpc_server, std::string::npos);
    const std::size_t next_service = compose.find("\n  proxy:", rpc_server);
    ASSERT_NE(next_service, std::string::npos);
    const std::string rpc_block = compose.substr(rpc_server, next_service - rpc_server);

    EXPECT_NE(rpc_block.find("NEXUSAI_MIGRATIONS_DIR: /usr/local/share/nexusai/migrations"),
              std::string::npos);
    EXPECT_EQ(rpc_block.find("migrate:"), std::string::npos);
    EXPECT_NE(rpc_block.find("postgres:"), std::string::npos);
    EXPECT_NE(rpc_block.find("redis:"), std::string::npos);
    EXPECT_NE(rpc_block.find("condition: service_healthy"), std::string::npos);
}
