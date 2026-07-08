-- Batch 1: Shared infrastructure tables
-- Run: psql -U <user> -d <dbname> -f sql/001_shared_tables.sql

-- Table 1: agent_calls — every Agent HTTP call recorded by a2a_adapter
CREATE TABLE IF NOT EXISTS agent_calls (
    id BIGSERIAL PRIMARY KEY,
    trace_id UUID NOT NULL,
    agent_id VARCHAR(128) NOT NULL,
    skill_name VARCHAR(128),
    success BOOLEAN NOT NULL DEFAULT TRUE,
    latency_ms INT NOT NULL,
    prompt_tokens INT DEFAULT 0,
    completion_tokens INT DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_agent_calls_agent_created
    ON agent_calls(agent_id, created_at);
CREATE INDEX IF NOT EXISTS idx_agent_calls_trace
    ON agent_calls(trace_id);

-- Table 2: token_usage — every LLM API call recorded by CostTracker
CREATE TABLE IF NOT EXISTS token_usage (
    id BIGSERIAL PRIMARY KEY,
    trace_id UUID NOT NULL,
    user_id VARCHAR(128) NOT NULL,
    context_id VARCHAR(128),
    agent_id VARCHAR(128),
    component VARCHAR(32) NOT NULL,
    prompt_tokens INT NOT NULL,
    completion_tokens INT NOT NULL,
    cost_usd NUMERIC(10,6),
    latency_ms BIGINT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_token_usage_user_date
    ON token_usage(user_id, created_at);
CREATE INDEX IF NOT EXISTS idx_token_usage_trace
    ON token_usage(trace_id);

-- Table 3: trace_spans — distributed tracing span storage
CREATE TABLE IF NOT EXISTS trace_spans (
    id BIGSERIAL PRIMARY KEY,
    trace_id UUID NOT NULL,
    span_id UUID NOT NULL,
    parent_span_id UUID,
    component VARCHAR(32) NOT NULL,
    start_time TIMESTAMPTZ,
    end_time TIMESTAMPTZ,
    duration_ms INT,
    status VARCHAR(16) DEFAULT 'ok',
    error_message TEXT,
    metadata JSONB
);
CREATE INDEX IF NOT EXISTS idx_trace_spans_trace
    ON trace_spans(trace_id);

-- Verify
SELECT 'agent_calls: ' || COUNT(*)::TEXT FROM agent_calls;
SELECT 'token_usage: ' || COUNT(*)::TEXT FROM token_usage;
SELECT 'trace_spans: ' || COUNT(*)::TEXT FROM trace_spans;
