-- PostgreSQL-backed token budgets.  IDs remain application-owned so this
-- migration deliberately has no cross-table foreign keys.

CREATE TABLE IF NOT EXISTS budget_reservations (
    request_id TEXT NOT NULL,
    owner_id TEXT NOT NULL CHECK (length(owner_id) > 0),
    context_id TEXT NOT NULL CHECK (length(context_id) > 0),
    estimated_tokens BIGINT NOT NULL CHECK (estimated_tokens >= 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT budget_reservations_owner_request_pk PRIMARY KEY (owner_id, request_id),
    CONSTRAINT budget_reservations_request_id_nonempty CHECK (length(request_id) > 0)
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_budget_reservations_owner_request
    ON budget_reservations(owner_id, request_id);
CREATE INDEX IF NOT EXISTS idx_budget_reservations_owner_context_created
    ON budget_reservations(owner_id, context_id, created_at);

CREATE TABLE IF NOT EXISTS budget_counters (
    bucket_type TEXT NOT NULL CHECK (bucket_type IN ('global', 'user_daily', 'user_monthly', 'session')),
    owner_id TEXT NOT NULL CHECK (length(owner_id) > 0),
    session_id TEXT NOT NULL CHECK (length(session_id) > 0),
    bucket_start DATE NOT NULL,
    used_tokens BIGINT NOT NULL DEFAULT 0 CHECK (used_tokens >= 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT budget_counters_bucket_pk PRIMARY KEY (bucket_type, owner_id, session_id, bucket_start)
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_budget_counters_bucket
    ON budget_counters(bucket_type, owner_id, session_id, bucket_start);
CREATE INDEX IF NOT EXISTS idx_budget_counters_owner_session
    ON budget_counters(owner_id, session_id, bucket_type, bucket_start);

CREATE TABLE IF NOT EXISTS budget_policies (
    owner_id TEXT NOT NULL CHECK (length(owner_id) > 0),
    global_limit BIGINT NOT NULL DEFAULT 0 CHECK (global_limit >= 0),
    user_daily_limit BIGINT NOT NULL DEFAULT 0 CHECK (user_daily_limit >= 0),
    user_monthly_limit BIGINT NOT NULL DEFAULT 0 CHECK (user_monthly_limit >= 0),
    session_limit BIGINT NOT NULL DEFAULT 0 CHECK (session_limit >= 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT budget_policies_owner_pk PRIMARY KEY (owner_id)
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_budget_policies_owner
    ON budget_policies(owner_id);
CREATE INDEX IF NOT EXISTS idx_budget_policies_owner_updated
    ON budget_policies(owner_id, updated_at);
