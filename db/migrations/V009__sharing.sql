-- Session sharing and templates from legacy sql/009.
CREATE TABLE IF NOT EXISTS shared_sessions (
    id BIGSERIAL PRIMARY KEY,
    share_id VARCHAR(64) UNIQUE NOT NULL,
    context_id VARCHAR(128) NOT NULL,
    mode VARCHAR(16) NOT NULL DEFAULT 'view',
    shared_by VARCHAR(128),
    expiry_days INT NOT NULL DEFAULT 7,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at TIMESTAMPTZ
);

CREATE TABLE IF NOT EXISTS session_comments (
    id BIGSERIAL PRIMARY KEY,
    share_id VARCHAR(64) NOT NULL
        REFERENCES shared_sessions(share_id) ON DELETE CASCADE,
    user_id VARCHAR(128),
    comment_text TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS session_templates (
    id BIGSERIAL PRIMARY KEY,
    template_id VARCHAR(64) UNIQUE NOT NULL,
    name VARCHAR(128) NOT NULL,
    description TEXT,
    dag_json TEXT,
    created_by VARCHAR(128),
    usage_count INT NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_shared_sessions_share_id
    ON shared_sessions(share_id);
CREATE INDEX IF NOT EXISTS idx_shared_sessions_context
    ON shared_sessions(context_id);
CREATE INDEX IF NOT EXISTS idx_session_comments_share_created
    ON session_comments(share_id, created_at);
CREATE INDEX IF NOT EXISTS idx_session_templates_name
    ON session_templates(name);
