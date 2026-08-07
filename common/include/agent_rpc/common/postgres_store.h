#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <pqxx/pqxx>

namespace agent_rpc::common {

class PostgresUnavailable : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct PostgresConfig {
    static constexpr int kDefaultPoolSize = 10;
    static constexpr int kMaxPoolSize = 10;

    std::string host;
    int port = 5432;
    std::string database;
    std::string user;
    std::string password;
    int pool_size = kDefaultPoolSize;

    using EnvironmentLookup = std::function<std::string(const std::string&)>;

    // Reads only NEXUSAI_POSTGRES_HOST/PORT/DATABASE/USER/PASSWORD/POOL_SIZE. The
    // callback overload keeps configuration tests independent of process env.
    static PostgresConfig fromEnvironment();
    static PostgresConfig fromEnvironment(const EnvironmentLookup& lookup);
};

namespace detail {

// Coordinates exclusive leases without coupling tests to live PostgreSQL
// connections. Each lease owns one slot until release() is called.
class PostgresPoolLeaseCoordinator final {
public:
    explicit PostgresPoolLeaseCoordinator(int pool_size);

    PostgresPoolLeaseCoordinator(const PostgresPoolLeaseCoordinator&) = delete;
    PostgresPoolLeaseCoordinator& operator=(const PostgresPoolLeaseCoordinator&) = delete;

    std::size_t acquire();
    void release(std::size_t index) noexcept;
    std::size_t size() const noexcept;

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<bool> leased_;
};

}  // namespace detail

// A local synchronous PostgreSQL connection pool. Each transaction leases one
// connection and returns it when the transaction completes.
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
    std::unique_ptr<pqxx::connection> createConnection() const;
    pqxx::connection& ensureConnection(std::size_t index);
    std::string connectionString() const;

    PostgresConfig config_;
    std::vector<std::unique_ptr<pqxx::connection>> connections_;
    detail::PostgresPoolLeaseCoordinator lease_coordinator_;
};

}  // namespace agent_rpc::common
