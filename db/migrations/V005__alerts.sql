-- Agent health dashboard alerts from legacy sql/005.
CREATE TABLE IF NOT EXISTS alerts (
    id BIGSERIAL PRIMARY KEY,
    agent_id VARCHAR(128),
    alert_type VARCHAR(32),
    severity VARCHAR(16),
    message TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    acknowledged BOOLEAN NOT NULL DEFAULT FALSE
);
CREATE INDEX IF NOT EXISTS idx_alerts_agent_created
    ON alerts(agent_id, created_at);
CREATE INDEX IF NOT EXISTS idx_alerts_severity
    ON alerts(severity, created_at);
