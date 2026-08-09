-- Agent Health Dashboard — alerts table
-- Run: psql -U <user> -d <dbname> -f sql/005_alerts.sql

CREATE TABLE IF NOT EXISTS alerts (
    id BIGSERIAL PRIMARY KEY,
    agent_id VARCHAR(128),
    alert_type VARCHAR(32),
    severity VARCHAR(16),
    message TEXT,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    acknowledged BOOLEAN DEFAULT FALSE
);

CREATE INDEX IF NOT EXISTS idx_alerts_agent_created
    ON alerts(agent_id, created_at);

CREATE INDEX IF NOT EXISTS idx_alerts_severity
    ON alerts(severity, created_at);

-- Verify
SELECT 'alerts: ' || COUNT(*)::TEXT FROM alerts;
