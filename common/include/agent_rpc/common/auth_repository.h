#pragma once

#include "agent_rpc/common/postgres_store.h"

#include <optional>
#include <string>

namespace agent_rpc::common {

struct UserRecord {
    std::string id;
    std::string owner_id;
    std::string username;
    std::string display_name;
    std::string password_scrypt;
    std::string role;
    std::string created_at;
    std::string updated_at;
};

struct AuthSessionRecord {
    std::string id;
    std::string owner_id;
    std::string token_hash;
    std::string expires_at;
    std::optional<std::string> revoked_at;
    std::string created_at;
    std::string updated_at;
};

class AuthRepository final {
public:
    explicit AuthRepository(PostgresStore& store);

    bool createUser(const UserRecord& user);
    std::optional<UserRecord> findUserByUsername(const std::string& username);
    bool createSession(const AuthSessionRecord& session);
    std::optional<AuthSessionRecord> findActiveSessionByTokenHash(const std::string& token_hash);
    bool revokeSession(const std::string& session_id);

private:
    PostgresStore& store_;
};

}  // namespace agent_rpc::common
