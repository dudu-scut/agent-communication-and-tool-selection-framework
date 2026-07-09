-- Batch 3: Agent Autonomy Spectrum Settings
-- Run: psql -U <user> -d <dbname> -f sql/003_autonomy_settings.sql

-- autonomy_level:
--   1 = Manual  (agent waits for user confirmation before every action)
--   2 = Suggest (agent suggests actions, user approves or rejects)
--   3 = Semi-auto (agent executes low-risk actions autonomously,
--                  asks for confirmation on high-risk actions)
--   4 = Auto    (agent acts fully autonomously, user can intervene)

CREATE TABLE IF NOT EXISTS agent_autonomy_settings (
    id BIGSERIAL PRIMARY KEY,
    user_id VARCHAR(128) NOT NULL,
    agent_id VARCHAR(128) NOT NULL,
    autonomy_level SMALLINT NOT NULL DEFAULT 1
        CHECK (autonomy_level BETWEEN 1 AND 4),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE(user_id, agent_id)
);

-- Verify
SELECT 'agent_autonomy_settings: ' || COUNT(*)::TEXT FROM agent_autonomy_settings;
