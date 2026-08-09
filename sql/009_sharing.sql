-- Session Sharing
-- Run: psql -U <user> -d <dbname> -f sql/009_sharing.sql

CREATE TABLE IF NOT EXISTS shared_sessions (
    id BIGSERIAL PRIMARY KEY,
    share_id VARCHAR(64) UNIQUE NOT NULL,
    context_id VARCHAR(128) NOT NULL,
    mode VARCHAR(16) DEFAULT 'view',    -- "view" or "interact"
    shared_by VARCHAR(128),
    expiry_days INT DEFAULT 7,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    expires_at TIMESTAMPTZ
);

CREATE TABLE IF NOT EXISTS session_comments (
    id BIGSERIAL PRIMARY KEY,
    share_id VARCHAR(64) NOT NULL REFERENCES shared_sessions(share_id),
    user_id VARCHAR(128),
    comment_text TEXT,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS session_templates (
    id BIGSERIAL PRIMARY KEY,
    template_id VARCHAR(64) UNIQUE NOT NULL,
    name VARCHAR(128) NOT NULL,
    description TEXT,
    dag_json TEXT,
    created_by VARCHAR(128),
    usage_count INT DEFAULT 0,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_shared_sessions_share_id
    ON shared_sessions(share_id);
CREATE INDEX IF NOT EXISTS idx_shared_sessions_context
    ON shared_sessions(context_id);
CREATE INDEX IF NOT EXISTS idx_session_templates_name
    ON session_templates(name);

-- Verify
SELECT 'shared_sessions: ' || COUNT(*)::TEXT FROM shared_sessions;
SELECT 'session_comments: ' || COUNT(*)::TEXT FROM session_comments;
SELECT 'session_templates: ' || COUNT(*)::TEXT FROM session_templates;
