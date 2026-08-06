#include "agent_rpc/common/postgres_store.h"

#include <charconv>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace agent_rpc::common {
namespace {

std::string Required(const PostgresConfig::EnvironmentLookup& lookup, const std::string& name) {
    const std::string value = lookup(name);
    if (value.empty()) {
        throw std::invalid_argument(name + " must be set");
    }
    return value;
}

int ParsePort(const std::string& value) {
    if (value.empty()) {
        throw std::invalid_argument("NEXUSAI_POSTGRES_PORT must be between 1 and 65535");
    }
    for (const unsigned char character : value) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument("NEXUSAI_POSTGRES_PORT must be a decimal number");
        }
    }

    int port = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), port);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || port < 1 || port > 65535) {
        throw std::invalid_argument("NEXUSAI_POSTGRES_PORT must be between 1 and 65535");
    }
    return port;
}

void RejectNul(const std::string& value, const std::string& name) {
    if (value.find('\0') != std::string::npos) {
        throw std::invalid_argument(name + " must not contain NUL");
    }
}

std::string EscapeConnectionValue(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '\'') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

}  // namespace

PostgresConfig PostgresConfig::fromEnvironment() {
    return fromEnvironment([](const std::string& name) {
        const char* value = std::getenv(name.c_str());
        return value == nullptr ? std::string{} : std::string{value};
    });
}

PostgresConfig PostgresConfig::fromEnvironment(const EnvironmentLookup& lookup) {
    PostgresConfig config{
        .host = Required(lookup, "NEXUSAI_POSTGRES_HOST"),
        .database = Required(lookup, "NEXUSAI_POSTGRES_DATABASE"),
        .user = Required(lookup, "NEXUSAI_POSTGRES_USER"),
        .password = Required(lookup, "NEXUSAI_POSTGRES_PASSWORD"),
    };

    RejectNul(config.host, "NEXUSAI_POSTGRES_HOST");
    RejectNul(config.database, "NEXUSAI_POSTGRES_DATABASE");
    RejectNul(config.user, "NEXUSAI_POSTGRES_USER");
    RejectNul(config.password, "NEXUSAI_POSTGRES_PASSWORD");

    const std::string port = lookup("NEXUSAI_POSTGRES_PORT");
    config.port = port.empty() ? 5432 : ParsePort(port);
    return config;
}

PostgresStore::PostgresStore(PostgresConfig config) : config_(std::move(config)) {}

PostgresStore::~PostgresStore() = default;

void PostgresStore::executeTransaction(const std::function<void(pqxx::work&)>& operation) {
    if (!operation) {
        throw std::invalid_argument("PostgresStore transaction callback must not be empty");
    }

    std::scoped_lock lock(mutex_);
    try {
        ensureOpen();
        pqxx::work transaction{*connection_};
        operation(transaction);
        transaction.commit();
    } catch (const pqxx::broken_connection& error) {
        connection_.reset();
        throw PostgresUnavailable(std::string{"PostgreSQL is unavailable: "} + error.what());
    }
}

bool PostgresStore::healthCheck() noexcept {
    try {
        executeTransaction([](pqxx::work& transaction) { transaction.exec("SELECT 1"); });
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void PostgresStore::ensureOpen() {
    if (connection_ && connection_->is_open()) {
        return;
    }

    try {
        connection_ = std::make_unique<pqxx::connection>(connectionString());
        if (!connection_->is_open()) {
            connection_.reset();
            throw PostgresUnavailable("PostgreSQL is unavailable: connection did not open");
        }
    } catch (const PostgresUnavailable&) {
        throw;
    } catch (const std::exception& error) {
        connection_.reset();
        throw PostgresUnavailable(std::string{"PostgreSQL is unavailable: "} + error.what());
    }
}

std::string PostgresStore::connectionString() const {
    std::ostringstream stream;
    stream << "host='" << EscapeConnectionValue(config_.host) << "' "
           << "port='" << config_.port << "' "
           << "dbname='" << EscapeConnectionValue(config_.database) << "' "
           << "user='" << EscapeConnectionValue(config_.user) << "' "
           << "password='" << EscapeConnectionValue(config_.password) << "'";
    return stream.str();
}

}  // namespace agent_rpc::common
