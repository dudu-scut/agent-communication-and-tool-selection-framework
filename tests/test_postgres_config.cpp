#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

#include "agent_rpc/common/postgres_store.h"

namespace {

using Values = std::unordered_map<std::string, std::string>;

agent_rpc::common::PostgresConfig LoadConfig(const Values& values) {
    return agent_rpc::common::PostgresConfig::fromEnvironment(
        [&values](const std::string& name) {
            const auto it = values.find(name);
            return it == values.end() ? std::string{} : it->second;
        });
}

Values CompleteConfig() {
    return {
        {"NEXUSAI_POSTGRES_HOST", "db.internal"},
        {"NEXUSAI_POSTGRES_PORT", "5544"},
        {"NEXUSAI_POSTGRES_DATABASE", "nexusai"},
        {"NEXUSAI_POSTGRES_USER", "service"},
        {"NEXUSAI_POSTGRES_PASSWORD", "test-only-secret"},
    };
}

TEST(PostgresConfigTest, LoadsOnlyNamedPostgresEnvironmentVariables) {
    auto values = CompleteConfig();
    values.emplace("PGPASSWORD", "must-not-be-read");
    values.emplace("PG_URL", "must-not-be-read");

    const auto config = LoadConfig(values);

    EXPECT_EQ(config.host, "db.internal");
    EXPECT_EQ(config.port, 5544);
    EXPECT_EQ(config.database, "nexusai");
    EXPECT_EQ(config.user, "service");
    EXPECT_EQ(config.password, "test-only-secret");
}

TEST(PostgresConfigTest, RejectsMissingRequiredValues) {
    for (const std::string name : {"NEXUSAI_POSTGRES_HOST", "NEXUSAI_POSTGRES_DATABASE",
                                   "NEXUSAI_POSTGRES_USER", "NEXUSAI_POSTGRES_PASSWORD"}) {
        auto values = CompleteConfig();
        values.erase(name);
        EXPECT_THROW((void)LoadConfig(values), std::invalid_argument) << name;
    }
}

TEST(PostgresConfigTest, DefaultsPortOnlyWhenItIsAbsent) {
    auto values = CompleteConfig();
    values.erase("NEXUSAI_POSTGRES_PORT");

    EXPECT_EQ(LoadConfig(values).port, 5432);
}

TEST(PostgresConfigTest, DefaultsPoolSizeToTen) {
    auto values = CompleteConfig();
    EXPECT_EQ(LoadConfig(values).pool_size, 10);
}

TEST(PostgresConfigTest, AcceptsPoolSizeOverrideWithinRange) {
    for (const std::string pool_size : {"1", "10"}) {
        auto values = CompleteConfig();
        values["NEXUSAI_POSTGRES_POOL_SIZE"] = pool_size;
        EXPECT_EQ(LoadConfig(values).pool_size, std::stoi(pool_size));
    }
}

TEST(PostgresConfigTest, RejectsInvalidPoolSizes) {
    for (const std::string pool_size : {"0", "11", "-1", "+1", " 2", "2 ", "2x"}) {
        auto values = CompleteConfig();
        values["NEXUSAI_POSTGRES_POOL_SIZE"] = pool_size;
        EXPECT_THROW((void)LoadConfig(values), std::invalid_argument) << pool_size;
    }
}
TEST(PostgresConfigTest, RejectsNonDecimalOrOutOfRangePorts) {
    for (const std::string port : {"0", "65536", "-1", "+5432", " 5432", "5432 ", "54x2"}) {
        auto values = CompleteConfig();
        values["NEXUSAI_POSTGRES_PORT"] = port;
        EXPECT_THROW((void)LoadConfig(values), std::invalid_argument) << port;
    }
}

TEST(PostgresConfigTest, AcceptsPortsAtBothBounds) {
    for (const std::string port : {"1", "65535"}) {
        auto values = CompleteConfig();
        values["NEXUSAI_POSTGRES_PORT"] = port;
        EXPECT_EQ(LoadConfig(values).port, std::stoi(port));
    }
}

}  // namespace
