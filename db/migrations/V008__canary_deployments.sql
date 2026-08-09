-- Canary deployment state from legacy sql/008.
CREATE TABLE IF NOT EXISTS canary_deployments (
    id BIGSERIAL PRIMARY KEY,
    skill_name VARCHAR(128),
    canary_agent_id VARCHAR(128),
    stable_agent_id VARCHAR(128),
    current_weight INT NOT NULL DEFAULT 10
        CHECK (current_weight BETWEEN 0 AND 100),
    stage VARCHAR(16) NOT NULL DEFAULT 'PENDING',
    started_at TIMESTAMPTZ,
    last_evaluation_at TIMESTAMPTZ
);
CREATE INDEX IF NOT EXISTS idx_canary_deployments_skill_stage
    ON canary_deployments(skill_name, stage);
