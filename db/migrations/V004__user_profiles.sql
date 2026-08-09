-- User profile storage from legacy sql/004.
CREATE TABLE IF NOT EXISTS user_profiles (
    user_id VARCHAR(128) PRIMARY KEY,
    identity JSONB NOT NULL DEFAULT '{}'::jsonb,
    preferences JSONB NOT NULL DEFAULT '[]'::jsonb,
    context_snapshot JSONB NOT NULL DEFAULT '{}'::jsonb,
    privacy_level VARCHAR(16) NOT NULL DEFAULT 'full',
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
