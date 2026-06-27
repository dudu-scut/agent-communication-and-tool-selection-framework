# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test Commands

### Linux

**Prerequisites (Ubuntu):** cmake, build-essential, pkg-config, libgrpc++-dev, libprotobuf-dev, protobuf-compiler, protobuf-compiler-grpc, libcurl4-openssl-dev, libjsoncpp-dev, uuid-dev, libgtest-dev, libhiredis-dev, nlohmann-json3-dev, librapidcheck-dev.

```bash
# Configure and build (from repo root)
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# Run all tests
cd build && ctest --output-on-failure

# Run a single test suite
./build/tests/test_mcp_integration
./build/tests/test_rag_mcp_properties
```

### Windows (PowerShell)

**Prerequisites:** [MSYS2](https://www.msys2.org/) (installed at `C:\msys64`).

```powershell
# 一键环境检测
.\run.ps1 setup

# 安装依赖 + 编译 + 启动
.\run.ps1 build          # 编译项目 (MinGW-w64)
.\run.ps1 orchestrator   # 启动多智能体系统 (自动启动 Redis)
.\run.ps1 grpc           # 启动 gRPC AI Server
.\run.ps1 client         # 交互式客户端
.\run.ps1 stop           # 停止所有服务
.\run.ps1 redis          # 管理 Redis
```

First-time setup (if MSYS2 not installed):
```powershell
winget install MSYS2.MSYS2                          # 安装 MSYS2
C:\msys64\usr\bin\bash.exe -lc 'pacman -S --noconfirm --disable-download-timeout mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-make mingw-w64-x86_64-grpc mingw-w64-x86_64-protobuf mingw-w64-x86_64-curl mingw-w64-x86_64-jsoncpp mingw-w64-x86_64-hiredis mingw-w64-x86_64-nlohmann-json mingw-w64-x86_64-gtest mingw-w64-x86_64-pkgconf mingw-w64-x86_64-redis git'
.\run.ps1 build                                      # 编译项目
```

## Architecture Overview

This is a **C++17 multi-agent communication framework** built on gRPC and the A2A (Agent-to-Agent) protocol. It enables AI agents to collaborate, call MCP tools, and use RAG-based intelligent tool selection.

### Module Dependency Graph (bottom-up)

```
proto/          → generated gRPC/Protobuf code for common, agent_service, ai_query
common/         → logger, circuit_breaker, load_balancer (shared utilities)
registry/       → service registry for agent discovery (in-memory)
a2a/            → pure A2A protocol library (JSON-RPC over HTTP, agent cards, task management)
a2a_adapter/    → bridge between gRPC protobuf messages and A2A JSON-RPC protocol
orchestrator/   → AgentRouter — selects agents by skill-match/round-robin/least-load
mcp/            → MCP client (STDIO + SSE transport), tool management, RAG-MCP (embedding + vector search)
server/         → gRPC server: AIQueryService, HealthService — links to A2AAdapter
client/         → gRPC client with interactive shell (commands: /stream, /context, /status, /quit)
examples/       → ai_orchestrator (multi-agent demo), grpc_ai_demo, rag_mcp_example
tests/          → Google Test + RapidCheck property-based tests
mcp_server_integrated/ → standalone MCP server with calculator, weather, etc. plugins
```

### Data Flow for a User Query

```
User input "1+7"
  → rpc_client (gRPC) → rpc_server (gRPC 0.0.0.0:50051)
  → AIQueryService → A2AAdapter → A2A Client (HTTP JSON-RPC)
  → Orchestrator (port 5000): intent recognition via LLM API → routes to Math Agent
  → Math Agent (port 5001): invokes MCP calculator tool → returns result
  → Response flows back through the same chain
```

### Key Abstractions

- **A2AAdapter** ([a2a_adapter.h](a2a_adapter/include/agent_rpc/a2a_adapter/a2a_adapter.h)): The central bridge. Converts gRPC protobuf requests/responses to A2A JSON-RPC and back. Handles sync, async, and streaming query modes.
- **AgentRouter** ([agent_router.h](orchestrator/include/agent_rpc/orchestrator/agent_router.h)): Multi-strategy agent selection (skill-match, round-robin, random, least-load) with health tracking. Thread-safe.
- **MCPClient** (mcp/src/mcp_client.cpp): Connects to MCP servers via STDIO (subprocess) or SSE (HTTP/SSE remote). Lists tools, calls tools.
- **RAG-MCP** (mcp/src/rag/): Embedding-based tool retrieval. Vectorizes tool descriptions via Embedding API, indexes them for cosine-similarity search, returns only top-K relevant tools to the LLM instead of all tools.

### External API Dependencies

| Env Variable | Service | Purpose |
|---|---|---|
| `LLM_API_KEY` | LLM API (OpenAI-compatible) | AI model for intent recognition, dialogue, and RAG-MCP tool embedding |

Default API endpoint: `https://api.deepseek.com` (OpenAI-compatible). Default model: `deepseek-v4-pro`.

### C++ Standard Versions

Most modules use **C++17** (set in root CMakeLists.txt). The `mcp/` module (RAG-MCP) uses **C++20** (set in its own CMakeLists.txt).

## Project Conventions

- All public headers live under `include/agent_rpc/<module>/`.
- Namespaces: `agent_rpc::a2a_adapter`, `agent_rpc::orchestrator`, etc. Generated proto code uses `agent_communication`.
- Testing uses Google Test for unit/integration tests and RapidCheck for property-based tests. Property tests are named `test_*_properties.cpp`.
- Console output uses a standardized banner format with `===` separators and a rectangular left border for server startup messages.
