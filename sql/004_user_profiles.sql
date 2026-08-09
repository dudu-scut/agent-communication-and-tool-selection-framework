-- User Profile Storage
-- Unified Memory + Activity Feed + DAG Composition

CREATE TABLE IF NOT EXISTS user_profiles (
    user_id VARCHAR(128) PRIMARY KEY,
    identity JSONB NOT NULL DEFAULT '{}',
    preferences JSONB NOT NULL DEFAULT '[]',
    context_snapshot JSONB NOT NULL DEFAULT '{}',
    privacy_level VARCHAR(16) DEFAULT 'full',
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
