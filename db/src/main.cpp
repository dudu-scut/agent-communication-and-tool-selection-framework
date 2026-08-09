#include <filesystem>
#include <iostream>
#include <string>

#include "agent_rpc/common/postgres_store.h"
#include "agent_rpc/db/migration_runner.h"

int main(int argc, char** argv) {
    try {
        if (argc != 3 || std::string{argv[1]} != "--migrations") {
            std::cerr << "usage: db_migrate --migrations <directory>" << std::endl;
            return 2;
        }

        const auto config = agent_rpc::common::PostgresConfig::fromEnvironment();
        agent_rpc::common::PostgresStore store{config};
        agent_rpc::db::MigrationRunner runner{store};
        runner.migrate(std::filesystem::path{argv[2]});
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "migration failed: " << error.what() << std::endl;
        return 1;
    }
}
