#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include <pqxx/pqxx>

namespace agent_rpc::common {

class PostgresUnavailable : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct PostgresConfig {
    std::string host;
    int port = 5432;
    std::string database;
    std::string user;
    std::string password;

    using EnvironmentLookup = std::function<std::string(const std::string&)>;

    // Reads only NEXUSAI_POSTGRES_HOST/PORT/DATABASE/USER/PASSWORD. The
    // callback overload keeps configuration tests independent of process env.
    static PostgresConfig fromEnvironment();
    static PostgresConfig fromEnvironment(const EnvironmentLookup& lookup);
};

// A narrow, serialized PostgreSQL access point. Connection creation and every
// transaction are protected by one mutex so libpqxx is never used concurrently
// through this object.
class PostgresStore {
public:
    explicit PostgresStore(PostgresConfig config);
    ~PostgresStore();

    PostgresStore(const PostgresStore&) = delete;
    PostgresStore& operator=(const PostgresStore&) = delete;
    PostgresStore(PostgresStore&&) = delete;
    PostgresStore& operator=(PostgresStore&&) = delete;

    void executeTransaction(const std::function<void(pqxx::work&)>& operation);
    bool healthCheck() noexcept;

private:
    void ensureOpen();
    std::string connectionString() const;

    PostgresConfig config_;
    std::mutex mutex_;
    std::unique_ptr<pqxx::connection> connection_;
};

}  // namespace agent_rpc::common
