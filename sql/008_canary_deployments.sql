-- Batch 6: Canary Deployment
-- Run: psql -U <user> -d <dbname> -f sql/008_canary_deployments.sql

CREATE TABLE IF NOT EXISTS canary_deployments (
    id BIGSERIAL PRIMARY KEY,
    skill_name VARCHAR(128),
    canary_agent_id VARCHAR(128),
    stable_agent_id VARCHAR(128),
    current_weight INT DEFAULT 10,
    stage VARCHAR(16) DEFAULT 'PENDING',
    started_at TIMESTAMPTZ,
    last_evaluation_at TIMESTAMPTZ
);

-- Verify
SELECT 'canary_deployments: ' || COUNT(*)::TEXT FROM canary_deployments;
