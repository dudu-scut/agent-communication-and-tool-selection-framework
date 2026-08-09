-- V013: runtime facts — owner-scoped agent invocation facts, feedback trace/skill
-- dimensions and per-skill route quality. Append-only; V001–V012 are never edited.
-- All statements are idempotent so tests may re-apply this migration directly.

-- ============================================================================
-- 1. feedback: link each rating back to the trace and the skill it judged.
--    Existing rows keep '' defaults (pre-V013 feedback carried neither).
-- ============================================================================

ALTER TABLE feedback ADD COLUMN IF NOT EXISTS trace_id TEXT NOT NULL DEFAULT '';
ALTER TABLE feedback ADD COLUMN IF NOT EXISTS skill_name TEXT NOT NULL DEFAULT '';

CREATE INDEX IF NOT EXISTS idx_feedback_owner_agent_skill
    ON feedback(owner_id, agent_id, skill_name, created_at);

-- ============================================================================
-- 2. agent_route_quality: quality is now per (owner, agent, skill) so one
--    user's feedback can never shift another user's routing weights.
-- ============================================================================

ALTER TABLE agent_route_quality ADD COLUMN IF NOT EXISTS skill_name TEXT NOT NULL DEFAULT '';

DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'agent_route_quality_owner_agent_unique'
    ) THEN
        ALTER TABLE agent_route_quality DROP CONSTRAINT agent_route_quality_owner_agent_unique;
    END IF;
END
$$;

CREATE UNIQUE INDEX IF NOT EXISTS uq_agent_route_quality_owner_agent_skill
    ON agent_route_quality(owner_id, agent_id, skill_name);

-- ============================================================================
-- 3. agent_invocations: owner/query-log linked facts for every agent call
--    (success rate, latency aggregates are derived from this table — no
--    out-of-band psql/Redis-only metrics anymore).
-- ============================================================================

CREATE TABLE IF NOT EXISTS agent_invocations (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    query_log_id TEXT NOT NULL DEFAULT '',
    agent_id TEXT NOT NULL,
    skill_name TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'pending',
    latency_ms BIGINT NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_agent_invocations_owner_created
    ON agent_invocations(owner_id, created_at);
CREATE INDEX IF NOT EXISTS idx_agent_invocations_agent_created
    ON agent_invocations(agent_id, created_at);
CREATE INDEX IF NOT EXISTS idx_agent_invocations_query_log
    ON agent_invocations(query_log_id, created_at);
