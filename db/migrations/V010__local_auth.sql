-- Durable local authentication records. PostgreSQL is the source of truth;
-- no cache or external identity store is consulted for these facts.
CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL CHECK (owner_id = id),
    username TEXT NOT NULL,
    display_name TEXT NOT NULL DEFAULT '',
    password_scrypt TEXT NOT NULL,
    role TEXT NOT NULL DEFAULT 'USER' CHECK (role IN ('USER', 'ADMIN')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT users_username_unique UNIQUE (username)
);

CREATE TABLE IF NOT EXISTS auth_sessions (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    token_hash TEXT NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT auth_sessions_token_hash_unique UNIQUE (token_hash)
);

CREATE INDEX IF NOT EXISTS idx_users_username
    ON users (username);
CREATE INDEX IF NOT EXISTS idx_auth_sessions_token_hash
    ON auth_sessions (token_hash);
CREATE INDEX IF NOT EXISTS idx_auth_sessions_owner_active
    ON auth_sessions (owner_id, expires_at)
    WHERE revoked_at IS NULL;
CREATE INDEX IF NOT EXISTS idx_auth_sessions_token_active
    ON auth_sessions (token_hash, expires_at)
    WHERE revoked_at IS NULL;
