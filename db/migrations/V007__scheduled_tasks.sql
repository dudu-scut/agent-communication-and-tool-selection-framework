-- Cron scheduler and webhook results from legacy sql/007.
CREATE TABLE IF NOT EXISTS scheduled_tasks (
    id BIGSERIAL PRIMARY KEY,
    name VARCHAR(128),
    cron_expr VARCHAR(64),
    query_template TEXT,
    agent_id VARCHAR(128),
    context_id VARCHAR(128),
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    last_run_at TIMESTAMPTZ,
    next_run_at TIMESTAMPTZ,
    created_by VARCHAR(128)
);

CREATE TABLE IF NOT EXISTS task_results (
    id BIGSERIAL PRIMARY KEY,
    task_id BIGINT REFERENCES scheduled_tasks(id) ON DELETE CASCADE,
    trace_id UUID,
    result_text TEXT,
    status VARCHAR(16),
    completed_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_scheduled_tasks_next_run
    ON scheduled_tasks(next_run_at) WHERE enabled;
CREATE INDEX IF NOT EXISTS idx_task_results_task_completed
    ON task_results(task_id, completed_at);
