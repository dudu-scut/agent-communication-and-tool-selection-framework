-- Agent autonomy settings from legacy sql/003.
CREATE TABLE IF NOT EXISTS agent_autonomy_settings (
    id BIGSERIAL PRIMARY KEY,
    user_id VARCHAR(128) NOT NULL,
    agent_id VARCHAR(128) NOT NULL,
    autonomy_level SMALLINT NOT NULL DEFAULT 1
        CHECK (autonomy_level BETWEEN 1 AND 4),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE(user_id, agent_id)
);
CREATE INDEX IF NOT EXISTS idx_agent_autonomy_agent
    ON agent_autonomy_settings(agent_id, updated_at);
