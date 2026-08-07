-- Agent feedback and performance tracking from legacy sql/002.
CREATE TABLE IF NOT EXISTS agent_feedback (
    id BIGSERIAL PRIMARY KEY,
    trace_id UUID NOT NULL,
    user_id VARCHAR(128) NOT NULL,
    agent_id VARCHAR(128) NOT NULL,
    skill_name VARCHAR(128) NOT NULL,
    rating SMALLINT NOT NULL CHECK (rating IN (1, 2, 3)),
    comment TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_agent_feedback_agent_skill
    ON agent_feedback(agent_id, skill_name, created_at);
