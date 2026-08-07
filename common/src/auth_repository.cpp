#include "agent_rpc/common/auth_repository.h"

#include <pqxx/version>

#include <string>
#include <utility>

namespace agent_rpc::common {
namespace {

template <typename... Arguments>
pqxx::result execParams(pqxx::work& transaction, const std::string& query, Arguments&&... arguments) {
#if PQXX_VERSION_MAJOR >= 8
    return transaction.exec(query, pqxx::params{std::forward<Arguments>(arguments)...});
#else  // libpqxx 6.x/7.x
    return transaction.exec_params(query, std::forward<Arguments>(arguments)...);
#endif
}

UserRecord userFromRow(const pqxx::row& row) {
    return UserRecord{
        .id = row["id"].as<std::string>(),
        .owner_id = row["owner_id"].as<std::string>(),
        .username = row["username"].as<std::string>(),
        .display_name = row["display_name"].as<std::string>(),
        .password_scrypt = row["password_scrypt"].as<std::string>(),
        .role = row["role"].as<std::string>(),
        .created_at = row["created_at"].as<std::string>(),
        .updated_at = row["updated_at"].as<std::string>(),
    };
}

AuthSessionRecord sessionFromRow(const pqxx::row& row) {
    AuthSessionRecord session{
        .id = row["id"].as<std::string>(),
        .owner_id = row["owner_id"].as<std::string>(),
        .token_hash = row["token_hash"].as<std::string>(),
        .expires_at = row["expires_at"].as<std::string>(),
        .revoked_at = std::nullopt,
        .created_at = row["created_at"].as<std::string>(),
        .updated_at = row["updated_at"].as<std::string>(),
    };
    if (!row["revoked_at"].is_null()) {
        session.revoked_at = row["revoked_at"].as<std::string>();
    }
    return session;
}

}  // namespace

AuthRepository::AuthRepository(PostgresStore& store) : store_(store) {}

bool AuthRepository::createUser(const UserRecord& user) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "INSERT INTO users (id, owner_id, username, display_name, password_scrypt, role, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, $5, $6, "
            "COALESCE(NULLIF($7, '')::timestamptz, NOW()), "
            "COALESCE(NULLIF($8, '')::timestamptz, NOW())) "
            "ON CONFLICT DO NOTHING RETURNING id",
            user.id, user.owner_id, user.username, user.display_name, user.password_scrypt, user.role,
            user.created_at, user.updated_at);
        inserted = !result.empty();
    });
    return inserted;
}

std::optional<UserRecord> AuthRepository::findUserByUsername(const std::string& username) {
    std::optional<UserRecord> user;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, username, display_name, password_scrypt, role, "
            "created_at::text AS created_at, updated_at::text AS updated_at "
            "FROM users WHERE username = $1",
            username);
        if (!result.empty()) {
            user = userFromRow(result.front());
        }
    });
    return user;
}

bool AuthRepository::createSession(const AuthSessionRecord& session) {
    bool inserted = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "INSERT INTO auth_sessions "
            "(id, owner_id, token_hash, expires_at, revoked_at, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4::timestamptz, NULLIF($5, '')::timestamptz, "
            "COALESCE(NULLIF($6, '')::timestamptz, NOW()), "
            "COALESCE(NULLIF($7, '')::timestamptz, NOW())) "
            "ON CONFLICT DO NOTHING RETURNING id",
            session.id, session.owner_id, session.token_hash, session.expires_at,
            session.revoked_at.value_or(std::string{}), session.created_at, session.updated_at);
        inserted = !result.empty();
    });
    return inserted;
}

std::optional<AuthSessionRecord> AuthRepository::findActiveSessionByTokenHash(
    const std::string& token_hash) {
    std::optional<AuthSessionRecord> session;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "SELECT id, owner_id, token_hash, expires_at::text AS expires_at, "
            "revoked_at::text AS revoked_at, created_at::text AS created_at, "
            "updated_at::text AS updated_at "
            "FROM auth_sessions "
            "WHERE token_hash = $1 AND revoked_at IS NULL AND expires_at > NOW()",
            token_hash);
        if (!result.empty()) {
            session = sessionFromRow(result.front());
        }
    });
    return session;
}

bool AuthRepository::revokeSession(const std::string& session_id) {
    bool revoked = false;
    store_.executeTransaction([&](pqxx::work& transaction) {
        const auto result = execParams(
            transaction,
            "UPDATE auth_sessions SET revoked_at = NOW(), updated_at = NOW() "
            "WHERE id = $1 AND revoked_at IS NULL RETURNING id",
            session_id);
        revoked = !result.empty();
    });
    return revoked;
}

}  // namespace agent_rpc::common
