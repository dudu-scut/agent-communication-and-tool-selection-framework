-- Durable domain records for conversations, execution history and controlled workflows.
-- Cross-record relationships are represented by IDs so each service can evolve independently.

CREATE TABLE IF NOT EXISTS conversations (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    title TEXT NOT NULL DEFAULT '',
    memory_summary TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_conversations_owner_created
    ON conversations(owner_id, created_at);

CREATE TABLE IF NOT EXISTS conversation_messages (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    conversation_id TEXT NOT NULL,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    sequence_no BIGINT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_conversation_messages_owner_conversation_sequence
    ON conversation_messages(owner_id, conversation_id, sequence_no);
CREATE INDEX IF NOT EXISTS idx_conversation_messages_conversation_sequence
    ON conversation_messages(conversation_id, sequence_no);

CREATE TABLE IF NOT EXISTS query_logs (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    conversation_id TEXT NOT NULL,
    request_text TEXT NOT NULL DEFAULT '',
    route_decision JSONB NOT NULL DEFAULT '{}'::jsonb,
    execution_plan JSONB NOT NULL DEFAULT '{}'::jsonb,
    response_text TEXT NOT NULL DEFAULT '',
    model TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'pending',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_query_logs_owner_created
    ON query_logs(owner_id, created_at);
CREATE INDEX IF NOT EXISTS idx_query_logs_conversation_created
    ON query_logs(conversation_id, created_at);

CREATE TABLE IF NOT EXISTS traces (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    query_log_id TEXT NOT NULL,
    trace_payload JSONB NOT NULL DEFAULT '{}'::jsonb,
    status TEXT NOT NULL DEFAULT 'pending',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_traces_query_log
    ON traces(query_log_id, created_at);
CREATE INDEX IF NOT EXISTS idx_traces_owner_created
    ON traces(owner_id, created_at);

CREATE TABLE IF NOT EXISTS token_usage_ledger (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    query_log_id TEXT NOT NULL,
    model TEXT NOT NULL,
    prompt_tokens BIGINT NOT NULL DEFAULT 0,
    completion_tokens BIGINT NOT NULL DEFAULT 0,
    estimated BOOLEAN NOT NULL DEFAULT FALSE,
    cost_usd NUMERIC NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_token_usage_ledger_query_log
    ON token_usage_ledger(query_log_id, created_at);
CREATE INDEX IF NOT EXISTS idx_token_usage_ledger_owner_created
    ON token_usage_ledger(owner_id, created_at);

CREATE TABLE IF NOT EXISTS feedback (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    query_log_id TEXT NOT NULL,
    agent_id TEXT NOT NULL,
    rating SMALLINT NOT NULL CHECK (rating BETWEEN 1 AND 5),
    comment TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_feedback_query_log
    ON feedback(query_log_id, created_at);
CREATE INDEX IF NOT EXISTS idx_feedback_agent_created
    ON feedback(agent_id, created_at);
CREATE INDEX IF NOT EXISTS idx_feedback_owner_created
    ON feedback(owner_id, created_at);

CREATE TABLE IF NOT EXISTS agent_route_quality (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    agent_id TEXT NOT NULL,
    sample_count BIGINT NOT NULL DEFAULT 0,
    average_rating NUMERIC NOT NULL DEFAULT 0,
    routing_weight NUMERIC NOT NULL DEFAULT 1,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT agent_route_quality_owner_agent_unique UNIQUE (owner_id, agent_id)
);

CREATE INDEX IF NOT EXISTS idx_agent_route_quality_agent
    ON agent_route_quality(agent_id, updated_at);

CREATE TABLE IF NOT EXISTS shares (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    conversation_id TEXT NOT NULL,
    token_hash TEXT NOT NULL,
    permission TEXT NOT NULL DEFAULT 'view',
    expires_at TIMESTAMPTZ,
    revoked_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT shares_token_hash_unique UNIQUE (token_hash)
);

CREATE INDEX IF NOT EXISTS idx_shares_token_hash
    ON shares(token_hash);
CREATE INDEX IF NOT EXISTS idx_shares_conversation
    ON shares(conversation_id, created_at);
CREATE INDEX IF NOT EXISTS idx_shares_owner_expiry
    ON shares(owner_id, expires_at);
CREATE INDEX IF NOT EXISTS idx_shares_expiry
    ON shares(expires_at);

CREATE TABLE IF NOT EXISTS workflow_templates (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    name TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    definition JSONB NOT NULL DEFAULT '{}'::jsonb,
    visibility TEXT NOT NULL DEFAULT 'private',
    version INTEGER NOT NULL DEFAULT 1,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT workflow_templates_owner_name_version_unique UNIQUE (owner_id, name, version)
);

CREATE INDEX IF NOT EXISTS idx_workflow_templates_owner_visibility
    ON workflow_templates(owner_id, visibility);

CREATE TABLE IF NOT EXISTS sandbox_runs (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    query_log_id TEXT NOT NULL,
    request_text TEXT NOT NULL DEFAULT '',
    response_text TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'pending',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_sandbox_runs_query_log
    ON sandbox_runs(query_log_id, created_at);
CREATE INDEX IF NOT EXISTS idx_sandbox_runs_owner_status
    ON sandbox_runs(owner_id, status, created_at);

CREATE TABLE IF NOT EXISTS compare_runs (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    query_log_id TEXT NOT NULL,
    request_text TEXT NOT NULL DEFAULT '',
    results JSONB NOT NULL DEFAULT '[]'::jsonb,
    status TEXT NOT NULL DEFAULT 'pending',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_compare_runs_query_log
    ON compare_runs(query_log_id, created_at);
CREATE INDEX IF NOT EXISTS idx_compare_runs_owner_status
    ON compare_runs(owner_id, status, created_at);

CREATE TABLE IF NOT EXISTS interventions (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    query_log_id TEXT NOT NULL,
    state TEXT NOT NULL DEFAULT 'pending',
    original_request TEXT NOT NULL DEFAULT '',
    edited_request TEXT NOT NULL DEFAULT '',
    decision TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_interventions_query_log
    ON interventions(query_log_id, created_at);
CREATE INDEX IF NOT EXISTS idx_interventions_owner_state
    ON interventions(owner_id, state, created_at);

CREATE TABLE IF NOT EXISTS undo_actions (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    resource_type TEXT NOT NULL,
    resource_id TEXT NOT NULL,
    action_payload JSONB NOT NULL DEFAULT '{}'::jsonb,
    version INTEGER NOT NULL DEFAULT 1,
    expires_at TIMESTAMPTZ,
    undone_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_undo_actions_resource
    ON undo_actions(resource_type, resource_id, version);
CREATE INDEX IF NOT EXISTS idx_undo_actions_expiry
    ON undo_actions(owner_id, expires_at);

CREATE TABLE IF NOT EXISTS autonomy_settings (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    agent_id TEXT NOT NULL,
    level SMALLINT NOT NULL DEFAULT 1 CHECK (level BETWEEN 1 AND 4),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT autonomy_settings_owner_agent_unique UNIQUE (owner_id, agent_id)
);

CREATE INDEX IF NOT EXISTS idx_autonomy_settings_owner_agent
    ON autonomy_settings(owner_id, agent_id);
CREATE INDEX IF NOT EXISTS idx_autonomy_settings_agent
    ON autonomy_settings(agent_id, updated_at);

CREATE TABLE IF NOT EXISTS agent_registry (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    agent_id TEXT NOT NULL,
    display_name TEXT NOT NULL DEFAULT '',
    capabilities JSONB NOT NULL DEFAULT '[]'::jsonb,
    last_heartbeat TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    health_status TEXT NOT NULL DEFAULT 'unknown',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT agent_registry_owner_agent_unique UNIQUE (owner_id, agent_id)
);

CREATE INDEX IF NOT EXISTS idx_agent_registry_owner_agent
    ON agent_registry(owner_id, agent_id);
CREATE INDEX IF NOT EXISTS idx_agent_registry_agent_heartbeat
    ON agent_registry(agent_id, last_heartbeat);
CREATE INDEX IF NOT EXISTS idx_agent_registry_health
    ON agent_registry(owner_id, health_status, last_heartbeat);
