# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test Commands

### Linux (primary)

**Prerequisites (Ubuntu 20.04+):** cmake, build-essential, pkg-config, libgrpc++-dev, libprotobuf-dev, protobuf-compiler, protobuf-compiler-grpc, libcurl4-openssl-dev, libjsoncpp-dev, uuid-dev, libgtest-dev, libhiredis-dev, nlohmann-json3-dev, librapidcheck-dev, redis-server.

```bash
# 一键编译 + 测试
./run.sh build          # 编译项目 (cmake + make)
./run.sh test           # 运行全部测试
./run.sh setup          # 检测开发环境
./run.sh start          # 启动 gRPC 服务端 (后台运行，自动启动 Redis)
./run.sh stop           # 停止所有服务

# 手动编译 (from repo root)
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# Run all tests
cd build && ctest --output-on-failure

# Run a single test suite
./build/tests/test_mcp_integration
./build/tests/test_rag_mcp_properties
./build/tests/test_agent_router_properties
./build/tests/test_task_manager_properties
./build/tests/test_redis_services
./build/tests/test_a2a_integration
./build/tests/test_ai_query_integration
./build/tests/test_adapter_properties
./build/tests/test_rpc_framework
./build/tests/test_agent_communication
./build/tests/test_proto_roundtrip
./build/tests/test_serialization
./build/tests/test_service_registry
```

### Windows → WSL2 (C++ backend)

All C++ compilation, testing, and server startup MUST run inside WSL. This machine has **Ubuntu (WSL2)** with all deps pre-installed (cmake 3.28, g++ 13.3, grpc++, protobuf, jsoncpp, hiredis, redis-server). Project path inside WSL:

```bash
/mnt/c/Users/31677/Desktop/NexusAI/agent-communication-and-tool-selection-framework/
```

**One-liner from Windows terminal** (no need to enter WSL manually):

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Users/31677/Desktop/NexusAI/agent-communication-and-tool-selection-framework && ./run.sh build"
```

Replace `build` with `test`, `start`, `stop`, `setup`, etc. All `./run.sh` commands work identically.

Frontend (`npm`) and Docker (`docker compose`) run natively on Windows — no WSL needed.

### Frontend (cross-platform)

```bash
cd frontend
npm install            # install dependencies
npm run dev            # Vite dev server with HMR (proxies gRPC-Web to localhost:8081)
npm run build          # production build (vue-tsc typecheck + vite build)
npm run preview        # preview production build
```

### Gateway (Docker, cross-platform)

```bash
./run.sh gateway       # starts Nginx + Envoy + Redis via docker compose
docker compose -f docker-compose.gateway.yaml up -d   # equivalent manual command
docker compose -f docker-compose.gateway.yaml down    # stop gateway
```

## Architecture Overview

This is a **C++17 multi-agent communication framework** built on gRPC and the A2A (Agent-to-Agent) protocol. It enables AI agents to collaborate, call MCP tools, and use RAG-based intelligent tool selection. A **Vue 3 + TypeScript frontend** and **Docker-based API gateway** (Nginx + Envoy) provide browser access.

### Module Dependency Graph (bottom-up)

```text
proto/          → generated gRPC/Protobuf code for common, agent_service, ai_query, user
common/         → logger, circuit_breaker, load_balancer, redis_client, memory_service, env_loader (shared utilities)
registry/       → service registry for agent discovery (in-memory)
a2a/            → pure A2A protocol library (JSON-RPC over HTTP, agent cards, task management)
a2a_adapter/    → bridge between gRPC protobuf messages and A2A JSON-RPC protocol
orchestrator/   → AgentRouter (4-level routing) + TaskPlanner + TaskExecutor + ResultAggregator
mcp/            → MCP client (STDIO + SSE transport), tool management, RAG-MCP (embedding + vector search)
ai_interface/   → AI service proxy wrapping MCP tool integrator (exists but commented out in root CMakeLists.txt)
server/         → gRPC server: AIQueryService, HealthService, AuthInterceptor — links to A2AAdapter
client/         → gRPC client with interactive shell (commands: /stream, /context, /status, /quit)
examples/       → ai_orchestrator (multi-agent demo), grpc_ai_demo, rag_mcp_example
tests/          → Google Test + RapidCheck property-based tests (13 test suites)
mcp_server_integrated/ → standalone MCP server with calculator, weather, sleep, code-review, bacio-quote plugins
frontend/       → Vue 3 + TypeScript + Vite SPA (ChatView, AdminView, LoginView)
gateway/        → Nginx + Envoy config + Node.js gRPC proxy server
```

### Data Flow Paths

**Browser client path (gRPC-Web):**

```text
Browser (Vue 3 SPA)
  → Nginx :8080 (rate limiting, API key auth, CORS)
    → Envoy :8081 (gRPC-Web ↔ gRPC/2 protocol conversion)
      → RPC Server :50051
        → A2AAdapter → Orchestrator :5000 → Agents
```

**Direct gRPC client path:**

```text
rpc_client (CLI)
  → Nginx :8082 (gRPC passthrough) → RPC Server :50051
  OR
  → RPC Server :50051 directly
    → A2AAdapter → A2A Client (HTTP JSON-RPC)
      → Orchestrator :5000 → intent recognition via LLM API → routes to Agent
        → Agent invokes MCP tools → returns result
```

**Vite dev server path (no Nginx):**

```text
Browser (Vite HMR :5173)
  → Vite proxy → Envoy :8081 → RPC Server :50051
```

### Key Abstractions

- **A2AAdapter** ([a2a_adapter.h](a2a_adapter/include/agent_rpc/a2a_adapter/a2a_adapter.h)): The central bridge. Converts gRPC protobuf requests/responses to A2A JSON-RPC and back. Handles sync, async, and streaming query modes.
- **AgentRouter** ([agent_router.h](orchestrator/include/agent_rpc/orchestrator/agent_router.h)): Four-level routing engine. ① Embedding vector similarity → ② LLM intent classification → ③ Keyword IDF matching → ④ Fallback to healthy generic agent. Routes are pre-bound during planning; execution hits the agent directly. Thread-safe with health tracking.
- **TaskPlanner / TaskExecutor / ResultAggregator** (orchestrator/src/): Decompose complex requests into sub-tasks, execute them serially (dependencies) or in parallel (independent), then aggregate results (simple concatenation or LLM synthesis).
- **MCPClient** (mcp/src/mcp_client.cpp): Connects to MCP servers via STDIO (subprocess pipe) or SSE (HTTP/SSE remote). Lists tools, calls tools.
- **RAG-MCP** (mcp/src/rag/): Embedding-based tool retrieval. Vectorizes tool descriptions via Embedding API, indexes them for cosine-similarity search, returns only top-K relevant tools to the LLM instead of all tools.
- **MemoryService** (common/src/memory_service.cpp): Three-tier memory persisted in Redis. Tier 1: conversation history per (context_id, agent_id) in Redis Lists (LTRIM to 50). Tier 2: user long-term memory in Redis Hashes, written via memory_hints from agents. Cross-agent: LLM-generated summaries stored in Redis Strings. Builds SystemContext injected into every AI query.
- **AuthService + AuthInterceptor** (server/src/): Registration/login returns UUID tokens (Redis, 24h TTL). gRPC interceptor validates Bearer tokens on every call; whitelist for public RPCs. Frontend: LoginView + Pinia auth store + router guard + automatic token injection.

### External API Dependencies

| Env Variable | Service | Purpose |
| --- | --- | --- |
| `LLM_API_KEY` | LLM API (OpenAI-compatible) | AI model for intent recognition, dialogue, task planning, and RAG-MCP tool embedding |
| `LLM_MODEL` | — | Model name override (default: `deepseek-v4-pro`) |
| `LLM_API_URL` | — | API endpoint override (default: `https://api.deepseek.com`) |

All three can be set in a `.env` file at the project root (loaded automatically by both `run.sh` and the C++ server via `env_loader`).

### Service Port Map

| Port | Service | Protocol |
| --- | --- | --- |
| 50051 | RPC Server | gRPC/2 |
| 5000 | Orchestrator | HTTP/A2A |
| 5001 | Math Agent (demo) | HTTP/A2A |
| 8500 | Registry | gRPC |
| 8080 | Nginx (browser entry) | HTTP/1.1 gRPC-Web |
| 8081 | Envoy (protocol conversion) | HTTP/1.1 → gRPC/2 |
| 8082 | Nginx (backend entry) | gRPC/2 passthrough |
| 6379 | Redis | TCP |

### C++ Standard Versions

Most modules use **C++17** (set in root CMakeLists.txt). The `mcp/` module (RAG-MCP) uses **C++20** (set in its own CMakeLists.txt).

### Frontend Architecture

Vue 3 Composition API + TypeScript + Vite. Three views: `/` ChatView (main chat with streaming), `/admin` AdminView (agent management), `/login` LoginView.

State management via Pinia stores: `chat.ts` (messages, streaming), `auth.ts` (token, user), `agents.ts` (agent registry).

gRPC-Web communication via hand-written TypeScript types ([proto.ts](frontend/src/types/proto.ts)) matching the proto definitions, with a fetch-based client ([grpc-client.ts](frontend/src/services/grpc-client.ts)) that serializes requests as JSON and posts to the Envoy proxy. Auth token is automatically attached to every request.

### Gateway Architecture

Three Docker containers defined in [docker-compose.gateway.yaml](docker-compose.gateway.yaml):

1. **Redis** (`redis:7-alpine`): Persistent storage for auth tokens, user data, conversation memory
2. **Envoy** (`envoyproxy/envoy:v1.31-latest`): gRPC-Web ↔ gRPC/2 protocol translation (replaced the unmaintained `improbable/grpcwebproxy`)
3. **Nginx** (`nginx:1.27-alpine`): Reverse proxy with rate limiting, API key authentication, CORS headers, and load balancing. Dual-port: `:8080` for browser gRPC-Web traffic, `:8082` for direct gRPC service-to-service calls.

An additional Node.js gRPC proxy server lives in [gateway/proxy/](gateway/proxy/) (`server.mjs`) for alternative gRPC-Web translation.

## Project Conventions

- All public headers live under `include/agent_rpc/<module>/`.
- Namespaces: `agent_rpc::a2a_adapter`, `agent_rpc::orchestrator`, `agent_rpc::ai`, `agent_rpc::mcp`, `agent_rpc::common`, etc. Generated proto code uses `agent_communication`.
- Testing uses Google Test for unit/integration tests and RapidCheck for property-based tests. Property tests are named `test_*_properties.cpp`.
- Console output uses a standardized banner format with `===` separators and a rectangular left border for server startup messages.
- Redis-dependent tests (auth, memory, agent communication) will fail if Redis is not running on `localhost:6379` — the `run.sh test` command warns about this.
- LLM-dependent functionality (intent recognition, task planning, RAG-MCP embedding) requires `LLM_API_KEY` to be set; the system degrades gracefully without it.
- The `ai_interface/` module exists on disk but is commented out of the root [CMakeLists.txt](CMakeLists.txt#L59) — it wraps MCP tool integration behind an AI service proxy and may be re-enabled in the future.
