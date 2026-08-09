# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

> 详尽的项目介绍、架构设计、技术亮点和求职竞争力分析见 [docs/NexusAI-project-introduction.md](docs/NexusAI-project-introduction.md)。
## PR1 platform baseline

- Work in WSL2 Ubuntu on the Linux filesystem; run `./scripts/bootstrap-wsl.sh` before the first build.
- On Ubuntu 26.04, bootstrap replaces only the stock libpqxx major<8 with the pinned, SHA-256-verified 8.0.1 user-prefix build; Ubuntu 24.04 keeps its system package. `run.sh build` discovers the controlled prefix without a manual export, and ordinary CMake configuration does not download dependencies.
- The supported browser protocol is JSON: `Browser/Vite -> Node JSON proxy :8081 -> RPC server :50051`. Vite proxies only the Node proxy.
- `docker compose up --build` from the repository root starts PostgreSQL, Redis, the RPC server, the Node proxy, and the Nginx frontend at `http://127.0.0.1:8080`. The RPC server applies `db/migrations` itself at startup (`NEXUSAI_MIGRATIONS_DIR`); there is no separate `migrate` service.
- Compose services use DNS names. The proxy must use `GRPC_TARGET=rpc-server:50051` in containers. Nginx forwards browser JSON and SSE to `proxy:8081`; no TLS certificates or frontend secrets are required.
- The Node JSON-to-gRPC proxy is the only supported browser gateway.
- MCP/RAG is optional and off by default (`-DENABLE_MCP=ON` is required to build it).


## Quick Commands

```bash
# 一键操作（WSL 内或通过 wsl -d Ubuntu -- bash -c "..."）
./run.sh build          # 编译 (cmake + make)
./run.sh test           # 运行全部 37 套测试
./run.sh start-all      # 一键启动全部后端 (Redis + Mock Agent + Proxy + Orchestrator + gRPC)
./run.sh stop           # 停止全部服务
./run.sh verify         # E2E 验证 (全部 8 批 32 个场景)
./run.sh setup          # 环境检测

# 前端 (Windows 原生)
cd frontend && npm ci && npm run dev   # Vite :5173 → Node proxy :8081 → gRPC :50051

# 单测
cd build && ctest --output-on-failure
./build/tests/test_agent_router_properties  # 路由属性测试
./build/tests/test_a2a_integration          # A2A 协议集成测试
# ...（共 37 套，cd build && ctest -N 可列出）
```

**环境：** C++ 编译/测试/服务启动必须在 WSL2 (Ubuntu) 中运行。前端和 Docker 在 Windows 原生运行。

## Architecture (condensed)

```text
proto/          → 9 proto files, 9 gRPC Services, 35 RPCs
common/         → Logger, CircuitBreaker, LoadBalancer(6), RedisClient, MemoryService, BackgroundScheduler
registry/       → ServiceRegistry (agent discovery, health)
a2a/            → Pure A2A protocol library (C++ A2AClient, JSON-RPC, AgentCard)
a2a_adapter/    → Protobuf ↔ A2A JSON-RPC bridge (sync/async/streaming/direct)
orchestrator/   → AgentRouter (Embedding→LLM→Keyword→Fallback) + TaskPlanner + TaskExecutor + ResultAggregator
mcp/            → Optional MCPClient (disabled unless ENABLE_MCP=ON)
server/         → gRPC Server :50051 (9 services, AuthInterceptor, CostInterceptor)
client/         → Interactive gRPC CLI
frontend/       → Vue 3 + TS + Vite SPA (10 views: Chat, Topology, Dashboard, Monitor, Admin, Sandbox, Compare, Share, Templates, Login)
gateway/        → Node JSON-to-gRPC Proxy (:8081); frontend Nginx is the container entrypoint
tests/          → 37 suites (GTest + RapidCheck property-based) + tests/e2e release E2E
```

**Data flow:** `Browser → Nginx :8080 → Node JSON Proxy :8081 → gRPC Server :50051 → A2AAdapter → Orchestrator :5000 → Agents`

## Key Abstractions

- **AgentRouter**: 4-tier routing — Embedding(80% queries, <100ms) → LLM → Keyword IDF → Fallback. Thread-safe, feedback-driven weighted selection.
- **A2AAdapter**: Central bridge converting gRPC requests to A2A JSON-RPC, back to Protobuf. 4 modes: sync, async, streaming, direct.
- **TaskPlanner/TaskExecutor**: LLM decomposes complex queries into DAG → Kahn topological sort → `std::async` parallel in same layer.
- **RAG-MCP**: Embedding vectors tool descriptions → cosine similarity Top-K → only relevant tools sent to LLM.
- **MemoryService**: 3-tier memory (conversation history per agent, long-term user memory, cross-agent summaries) → SystemContext injected per request.
- **CircuitBreaker**: CLOSED→OPEN→HALF_OPEN state machine, per-agent failure tracking.
- **BackgroundScheduler**: Coordinator + Worker Pool for periodic tasks (span batch flush, feedback aggregation, metrics aggregation, profile extraction, health evaluation, cache cleanup). Cron/Canary tasks were removed per the local target boundary.

## External API Dependencies

| Env Variable | Purpose | Default |
| --- | --- | --- |
| `LLM_API_KEY` | LLM API key (OpenAI-compatible) | required |
| `LLM_MODEL` | Model name | `deepseek-v4-pro` |
| `LLM_API_URL` | API endpoint | `https://api.deepseek.com` |

Load via `.env` file at project root (auto-loaded by `run.sh` and `env_loader`).

## Service Port Map

| Port | Service | Protocol |
| --- | --- | --- |
| 50051 | RPC Server | gRPC/2 |
| 5000 | Orchestrator | HTTP/A2A |
| 5100 | Mock Agent | HTTP/A2A |
| 8080 | Nginx (browser) | HTTP |
| 8081 | Node JSON Proxy | HTTP JSON ↔ gRPC |
| 6379 | Redis | TCP |

## Conventions

- Headers: `include/agent_rpc/<module>/<name>.h`; Sources: `src/<module>/<name>.cpp`
- Namespaces: `agent_rpc::a2a_adapter`, `agent_rpc::orchestrator`, `agent_rpc::mcp`, `agent_rpc::common`. Proto: `agent_communication`.
- C++20 throughout (root CMakeLists.txt `CMAKE_CXX_STANDARD 20`).
- Tests: GTest (integration) + RapidCheck (property-based). Property tests named `test_*_properties.cpp`.
- Redis must be running on `localhost:6379` for auth/memory/agent-communication tests.
- LLM-dependent features (routing tier 2, planning, RAG embedding) degrade gracefully without `LLM_API_KEY`.
- `ai_interface/` module is commented out of root CMakeLists.txt — may be re-enabled in the future.

## PR2.1 PostgreSQL migration foundation

- `db/migrations/VNNN__name.sql` is the canonical migration set (13 files);
  `db_migrate` applies them in order and records checksums in
  `schema_migrations`. The RPC server runs the same migration logic itself at
  startup — Compose has no separate `migrate` service. `sql/` remains legacy
  reference material and is never executed.
- PostgresStore reads only `NEXUSAI_POSTGRES_HOST`, `PORT`, `DATABASE`, `USER`,
  and `PASSWORD`. Run the CMake build on WSL2 Linux filesystems with
  `libpqxx-dev` (or the bootstrap user-prefix libpqxx 8.0.1 on Ubuntu 26.04).
- Compose wires `rpc-server` to the `postgres` service via the
  `NEXUSAI_POSTGRES_*` variables; no `PG_URL` bridge is used.

## Current capability baseline (PR-C2 … PR-G)

- **Durable Query pipeline**: Query/QueryStream persist conversation, query
  log, trace, budget reservation and token ledger in PostgreSQL; owner always
  comes from the auth context (request-body `user_id` is ignored); retries on
  the same `request_id` keep exactly one reservation/ledger; client abort
  finalizes `cancelled`, budget exhaustion finalizes `rejected` +
  RESOURCE_EXHAUSTED.
- **Budgets**: four layers via `NEXUSAI_BUDGET_GLOBAL_TOKENS` (0 = unlimited),
  `..._USER_DAILY_TOKENS` (200000), `..._USER_MONTHLY_TOKENS` (4000000),
  `..._SESSION_TOKENS` (100000); estimated tokens = 64 + question.size()/4.
- **Observability/feedback/lifecycle**: traces, cost reports, feedback,
  agent_route_quality, autonomy, intervention and undo are owner-scoped PG
  facts; `NEXUSAI_ADMIN_USERNAME` grants the ADMIN role for management RPCs.
- **Replay/Export/Share/Template**: real PG-backed closed loops (exact replay
  persists a new trace; shares are token-hashed with TTL + revoke).
- **Sandbox/Compare**: real backend closed loops wired into the frontend.
- **Honest boundaries**: `RealTimeCommunication` returns UNIMPLEMENTED;
  `GetQueryStatus` reads durable PG state (no in-memory cache); etcd registry
  only via explicit `RPC_REGISTRY_ADDRESS=etcd://`; MCP requires
  `-DENABLE_MCP=ON`; Cron/Canary removed. ProfileSummarizer::processPending()
  does real Redis+LLM work but persists profiles to Redis only (not PG).
- **Release E2E**: `python3 tests/e2e/e2e_pr_g_release.py` (WSL) — real
  rpc_server + real Docker PG/Redis, all assertions checked via real psql;
  missing prerequisites produce an explicit SKIP.
