-- Query replay log from legacy sql/006.
CREATE TABLE IF NOT EXISTS query_log (
    trace_id UUID PRIMARY KEY,
    user_id VARCHAR(128),
    query_text TEXT,
    route_decision JSONB,
    execution_plan JSONB,
    agent_calls JSONB,
    token_usage_summary JSONB,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_query_log_user_created
    ON query_log(user_id, created_at);
CREATE INDEX IF NOT EXISTS idx_query_log_created
    ON query_log(created_at);
