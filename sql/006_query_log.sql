-- Batch 5: Query Replay System — query_log table
-- Run: psql -U <user> -d <dbname> -f sql/006_query_log.sql

CREATE TABLE IF NOT EXISTS query_log (
    trace_id UUID PRIMARY KEY,
    user_id VARCHAR(128),
    query_text TEXT,
    route_decision JSONB,
    execution_plan JSONB,
    agent_calls JSONB,
    token_usage_summary JSONB,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_query_log_user_created
    ON query_log(user_id, created_at);

CREATE INDEX IF NOT EXISTS idx_query_log_created
    ON query_log(created_at);

-- Verify
SELECT 'query_log: ' || COUNT(*)::TEXT FROM query_log;
