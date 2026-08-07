#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "agent_rpc/common/postgres_store.h"

namespace agent_rpc::db {

class MigrationRunner {
public:
    explicit MigrationRunner(common::PostgresStore& store);

    // Applies VNNN__name.sql files in deterministic version order. Applied
    // checksums are immutable and a PostgreSQL advisory lock serializes runs.
    void migrate(const std::filesystem::path& directory);

private:
    struct Migration {
        std::string version;
        std::filesystem::path path;
        std::string checksum;
    };

    static std::vector<Migration> discover(const std::filesystem::path& directory);
    static std::string readFile(const std::filesystem::path& path);
    static std::string checksum(const std::string& contents);

    common::PostgresStore& store_;
};

}  // namespace agent_rpc::db
