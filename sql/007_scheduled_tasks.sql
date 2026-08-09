-- Cron Scheduler + Webhook
-- Run: psql -U <user> -d <dbname> -f sql/007_scheduled_tasks.sql

CREATE TABLE IF NOT EXISTS scheduled_tasks (
    id BIGSERIAL PRIMARY KEY,
    name VARCHAR(128),
    cron_expr VARCHAR(64),
    query_template TEXT,
    agent_id VARCHAR(128),
    context_id VARCHAR(128),
    enabled BOOLEAN DEFAULT TRUE,
    last_run_at TIMESTAMPTZ,
    next_run_at TIMESTAMPTZ,
    created_by VARCHAR(128)
);

CREATE TABLE IF NOT EXISTS task_results (
    id BIGSERIAL PRIMARY KEY,
    task_id BIGINT,
    trace_id UUID,
    result_text TEXT,
    status VARCHAR(16),
    completed_at TIMESTAMPTZ DEFAULT NOW()
);

-- Verify
SELECT 'scheduled_tasks: ' || COUNT(*)::TEXT FROM scheduled_tasks;
SELECT 'task_results: ' || COUNT(*)::TEXT FROM task_results;
