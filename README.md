<h1 align="center">NexusAI — 多 Agent 通信与工具选择框架</h1>

<div align="center">

**C++20 · gRPC · A2A Protocol · MCP · Vue 3**

高性能多 Agent 协作框架 — 四层渐进式智能路由 · DAG 任务编排 · 流式全链路可观测

[![C++](https://img.shields.io/badge/C%2B%2B-20-blue)](https://en.cppreference.com/w/cpp/20)
[![gRPC](https://img.shields.io/badge/gRPC-1.51%2B-green)](https://grpc.io/)
[![License](https://img.shields.io/badge/license-MIT-orange)](LICENSE)
[![Tests](https://img.shields.io/badge/tests-17%20suites-brightgreen)](tests/)
[![Lines](https://img.shields.io/badge/C%2B%2B-76%2C000%2B%20lines-informational)]()

</div>

## Supported platform path (PR1)

Run the project from a WSL2 Ubuntu distribution and keep the checkout on the Linux filesystem (for example `~/src/nexusai`), not under `/mnt/c`. Bootstrap the documented WSL2 toolchain with:

```bash
./scripts/bootstrap-wsl.sh
```

Ubuntu 26.04's stock `libpqxx` 7.10 package is rejected because of its
process-exit double-free. The bootstrap detects that distribution and only
then fetches the pinned libpqxx 8.0.1 source archive, verifies SHA-256
`24f878a1b4249035e4b6c07d49351506bf99f88df584d36bf198d58ebf293823`, and
installs it under the user-controlled
`${XDG_DATA_HOME:-$HOME/.local/share}/nexusai/libpqxx-8.0.1` prefix. A marker
and `pkg-config` version check make the install idempotent; a failed or
mismatched archive stops bootstrap, and no apt-owned files are overwritten.
`run.sh build` discovers that prefix automatically and passes it to CMake.
Ubuntu 24.04 keeps using its system libpqxx package (including 6.x), and
ordinary CMake configuration never downloads dependencies.

The browser path is JSON only: `Browser/Vite -> Node JSON proxy (:8081) -> gRPC RPC server (:50051)`. Vite forwards only browser RPC paths to the local Node proxy.

For the complete containerized stack, use the root Compose file:

```bash
docker compose up --build
```

It starts PostgreSQL, Redis, SQL migrations, the RPC server, the Node proxy, and an Nginx-served frontend at <http://127.0.0.1:8080>. Services communicate over Compose DNS (`proxy -> rpc-server:50051`); no host IP addresses are required. The browser entrypoint is local-only HTTP, so Compose does not require TLS certificates or frontend secrets. PostgreSQL data persists in the ignored `./.nexusai-data/postgres` bind mount.

MCP/RAG is optional and disabled by default. Enable it deliberately when configuring CMake with `-DENABLE_MCP=ON`.

---

## 这是什么？

一个让多个 AI Agent 协同工作的 C++ 基础设施框架。用户用自然语言提问，后端自动将复杂问题拆解为子任务 DAG 并行执行，通过四层智能路由调度合适的 Agent，经 A2A 协议实现 Agent 间通信，全程流式推送进度到前端可视化。15,000+ 行核心 C++，10 个 CMake 模块，9 个 gRPC Service，35 个 RPC 端点。

## 核心能力

**DAG 任务编排引擎：** LLM 分析请求自动生成带依赖关系的子任务 DAG，Kahn 拓扑排序分层后 `std::async` 并行执行同层无依赖子任务，前置结果自动注入下游上下文。全局超时防止无限等待，委派深度限制 5 层防止 Agent 递归失控，可配置重试策略自动处理瞬态错误。ResultAggregator 聚合多 Agent 输出为统一响应，前端 Mermaid 实时渲染 DAG 状态流转。

**四级智能路由与熔断容错：** Embedding 向量语义匹配 → LLM 意图识别 → IDF 加权关键词匹配 → 兜底路由，四级递进式降级确保各种条件下精准路由。质量系数驱动路由加权，三态熔断器（Closed / Open / HalfOpen）自动隔离故障 Agent，6 种负载均衡策略含 FNV-1a 一致性哈希，支持运行时热切换。

**gRPC 流式查询全链路：** gRPC Server Streaming → Node.js Proxy SSE 转码 → 前端实时渲染，端到端流式体验涵盖增量内容推送、DAG 节点状态更新、Agent 拓扑实时高亮、链路追踪摘要展示。Proxy 层自动检测客户端断连并取消后端流，释放服务端资源。

**三层记忆系统：** Redis 作为运行时主存储，承载对话历史（Tier 1，按 Agent 粒度隔离，LTRIM 保留最近 50 条）、用户长期记忆（Tier 2，Agent 通过 memory_hints 上报）、跨 Agent 摘要（LLM 生成，保障 Agent 切换时上下文连贯）三层架构。查询前自动整合 user_id + user_memory + conversation_history + cross_agent_summary 注入 AI 查询。PostgreSQL schema 已定义（`sql/` 目录含 9 个迁移脚本），当前运行时全部使用 Redis。

**A2A 协议与 MCP 工具调用：** 完整实现 Agent-to-Agent 协议（HTTP/JSON-RPC 2.0），支持 `message/send` 同步调用与 `message/stream` SSE 流式调用，Task 状态机覆盖 Submitted → Running → Completed / Failed / Canceled 全生命周期。MCP 工具调用支持 STDIO（本地进程管道）和 SSE（远程 HTTP）两种传输，集成 MCP Server 提供 6 个内置插件。

**Agent 注册发现与消息系统：** gRPC RegisterAgent / Heartbeat / UnregisterAgent 全生命周期管理，Agent 注册时自动建立标签/技能倒排索引并同步至 AgentRouter 路由表。后台清理线程每 30s 扫描心跳超时 Agent，自动下线并同步移除路由索引与消息队列。FindAgents 支持按标签、技能、关键词多维发现与分页查询。Agent 间消息系统基于线程安全队列，支持单播、批量流式发送与广播三种模式。ServiceRegistry 维护 EMA 平滑延迟与环形缓冲区成功率，为负载均衡提供实时质量信号。

**全链路可观测性：** `thread_local` TraceContext + 嵌套 Span 跨线程自动传播，Span 数据持久化 Redis 保留 7 天。Token 成本追踪按用户/日期聚合，预算中间件预扣费机制超额自动拒绝。前端 Dashboard 展示调用链路瀑布图、Token 趋势与成本分布。

**认证与安全：** PBKDF2 风格密码哈希（32 字节安全盐 + 10,000 次 SHA-256 迭代），UUID Token + Redis TTL 24 小时过期管理，gRPC 拦截器全接口鉴权（白名单 RPC 免认证）。预算控制与自主性级别控制，高消耗自动触发人工确认。

---

## 架构一览

```text
                        ┌─────────────────────────┐
                        │   Browser  (:5173)       │
                        │   Vue 3 SPA · SSE · DAG  │
                        └────────────┬────────────┘
                                     │  HTTP / SSE
                        ┌────────────▼────────────┐
                        │   Node.js Proxy (:8081)  │
                        │   HTTP JSON ↔ gRPC        │
                        └────────────┬────────────┘
                                     │  gRPC / Protobuf
                        ┌────────────▼────────────┐
                        │   gRPC Server (:50051)   │
                        │   9 Services · 35 RPCs   │
                        │   Auth + Cost Interceptor│
                        └──────┬───────┬──────────┘
                               │       │
              ┌────────────────┼───────┼────────────────┐
              │                │       │                │
    ┌─────────▼───┐  ┌────────▼──┐ ┌──▼──────────┐    │
    │ AgentRouter │  │Orchestrator│ │ A2A Adapter  │    │
    │  4-tier     │  │ DAG Engine │ │ Proto↔JSON   │    │
    └─────────┬───┘  └────────┬──┘ └──┬──────────┘    │
              │               │       │                │
    ┌─────────▼───┐  ┌────────▼──┐ ┌──▼──────────┐    │
    │ MCP Client  │  │Memory Svc │ │Background   │    │
    │ STDIO + SSE │  │ 3-tier    │ │Scheduler    │    │
    └─────────┬───┘  └────────┬──┘ └──┬──────────┘    │
              │               │       │                │
              └───────────────┼───────┘                │
                              │                        │
                 ┌────────────▼────────────┐           │
                 │   Redis  (:6379)        │◄──────────┘
                 │   cache · memory · auth │
                 └────────────┬────────────┘
                              │
                 ┌────────────▼────────────┐
                 │   External Agents        │
                 │   A2A HTTP · JSON-RPC    │
                 │   Orchestrator (:5000)   │
                 │   Demo Agent  (:5100)    │
                 └─────────────────────────┘
```

---

## 项目结构

```text
agent-communication-and-tool-selection-framework/
│
├── proto/                           # Protocol Buffer 定义（9 个 .proto）
│   ├── ai_query.proto               #   AI 查询 — Query / QueryStream / Status / Metrics
│   ├── agent_service.proto          #   Agent 通信 — 注册 / 心跳 / 注销 / 发现 / 消息
│   ├── agent_lifecycle.proto        #   Agent 生命周期 — 反馈 / 自主性 / 撤销
│   ├── orchestration.proto          #   编排 — ExecutePlan / Replay / Export
│   ├── observability.proto          #   可观测性 — Trace / CostReport
│   ├── sharing.proto                #   会话共享 — Share / Observe / Templates
│   ├── user_experience.proto        #   用户体验 — Intervention / Sandbox
│   ├── user.proto                   #   用户服务 — Login / Register
│   └── common.proto                 #   公共消息 — ServiceInfo / Status / Enums
│
├── server/                          # gRPC 服务端 — 9 个 Service 实现
├── orchestrator/                    # DAG 任务编排与智能路由
│   ├── include/agent_rpc/orchestrator/
│   │   ├── agent_router.h           #   四级路由引擎
│   │   ├── task_planner.h           #   DAG 规划器（Kahn 拓扑排序）
│   │   ├── task_executor.h          #   并行执行器（std::async）
│   │   ├── result_aggregator.h      #   结果聚合器
│   │   ├── feedback_aggregator.h    #   反馈聚合器
│   │   ├── context_compressor.h     #   上下文压缩器
│   │   ├── cron_scheduler.h         #   定时调度器
│   │   ├── export_service.h         #   导出服务
│   │   └── replay_service.h         #   回放服务
│   └── src/
│
├── common/                          # 公共组件库
│   ├── include/agent_rpc/common/
│   │   ├── redis_client.h           #   Redis 客户端
│   │   ├── memory_service.h         #   三层记忆系统
│   │   ├── circuit_breaker.h        #   三态熔断器
│   │   ├── load_balancer.h          #   负载均衡（6 种策略）
│   │   ├── cost_tracker.h           #   Token 成本追踪
│   │   ├── trace_context.h          #   链路追踪上下文
│   │   ├── background_scheduler.h   #   后台调度器（8 个周期任务）
│   │   └── ...                      #   logger, metrics, serializer 等
│   └── src/
│
├── a2a/                             # A2A 协议核心库（HTTP/JSON-RPC 2.0）
│   ├── include/a2a/
│   │   ├── core/                    #   JSON-RPC 核心
│   │   ├── models/                  #   数据模型（AgentCard / Task / Message）
│   │   ├── client/                  #   A2A 客户端
│   │   └── server/                  #   Task 管理器
│   ├── src/
│   └── third_party/json.hpp         #   nlohmann/json
│
├── a2a_adapter/                     # gRPC ↔ A2A 协议适配层
│   ├── include/agent_rpc/a2a_adapter/
│   │   ├── a2a_adapter.h            #   适配器（同步 / 异步 / 流式 / 直连）
│   │   ├── request_adapter.h        #   Protobuf → JSON-RPC
│   │   ├── response_adapter.h       #   JSON-RPC → Protobuf
│   │   └── ...                      #   config, metrics, retry_policy 等
│   └── src/
│
├── mcp/                             # MCP 客户端（STDIO + SSE）
│   ├── include/agent_rpc/mcp/
│   │   ├── mcp_client.h             #   MCP 客户端
│   │   └── rag/                     #   RAG-MCP：Embedding + 语义检索
│   └── src/
│
├── mcp_server_integrated/           # 集成 MCP Server（独立构建，6 个插件）
├── registry/                        # Agent 注册与发现
├── client/                          # gRPC CLI + Agent 注册 SDK
│
├── frontend/                        # Vue 3 + TypeScript + Vite
│   └── src/
│       ├── views/                   #   10 个视图
│       │   ├── ChatView.vue         #     对话界面（SSE 流式 + DAG）
│       │   ├── AgentTopology.vue    #     Agent 拓扑（ECharts 力导向图）
│       │   ├── Dashboard.vue        #     可观测仪表盘
│       │   ├── Monitor.vue          #     系统健康监控
│       │   ├── AdminView.vue        #     Agent 管理
│       │   ├── LoginView.vue        #     登录注册
│       │   ├── AgentSandbox.vue     #     Agent 沙箱
│       │   ├── CompareView.vue      #     Agent 对比
│       │   ├── ShareView.vue        #     会话分享
│       │   └── TemplateMarket.vue   #     模板市场
│       ├── components/              #   UI 组件
│       ├── services/grpc-client.ts  #   gRPC 客户端（Fetch + ReadableStream）
│       ├── stores/                  #   Pinia 状态管理
│       └── types/proto.ts           #   TypeScript 类型定义
│
├── gateway/                         # API 网关
│   ├── proxy/server.mjs             #   Node.js gRPC Proxy（主力）
│   ├── proxy/Dockerfile              #   Node JSON proxy image
│   └── nginx.conf                   #   Nginx 配置
│
├── deploy/                          # 部署编排
│   └── ../docker-compose.yml         #   Root container stack
│
├── tests/                           # 测试（17 套，GTest + RapidCheck）
│   ├── e2e/                         #   E2E 测试脚本
│   └── test_*.cpp                   #   C++ 测试文件
│
├── examples/                        # Agent 接入示例（Python）
│   ├── echo_agent.py
│   ├── math_agent.py
│   ├── translator_agent.py
│   └── orchestrator_agent.py
│
├── docs/                            # 项目文档
├── sql/                             # PostgreSQL Schema（已定义，运行时用 Redis）
├── verify/                          # Agent 验证服务
├── CMakeLists.txt                   # 根 CMake（C++20，10 个子模块）
├── agent-integration-guide.md       # Agent 接入指南
├── run.sh                           # 统一运行脚本
├── scripts/                         # 辅助脚本
│   ├── start_backend.sh             #   后端一键启动
│   └── register_agents.sh           #   Agent 注册
├── CLAUDE.md                        # Claude Code 项目指引
├── .env                             # 环境变量
└── .env.example                     # 环境变量示例
```

---

## 前端界面

基于 Vue 3 + TypeScript + Vite 构建的 SPA，提供 10 个功能视图，通过 Node.js gRPC Proxy 实现 gRPC → SSE 流式对接。

### 启动后能看到什么

打开 <http://localhost:5173> 进入一个完整的多 Agent 协作可视化平台：

- **对话页面（首页 `/`）**：自然语言提问，实时流式回答；复杂问题自动展示 DAG 执行计划流程图（Mermaid），子任务状态实时更新；右侧活动流面板显示 Agent 工作步骤。
- **拓扑页面（`/topology`）**：ECharts 力导向图展示所有 Agent 关系网络，节点颜色/大小反映健康状态与负载，支持拖拽交互与详情查看。
- **仪表板（`/dashboard`）**：Token 消耗趋势、Agent 调用排行、成本分布饼图，CountUp 数字滚动动画。
- **监控面板（`/monitor`）**：系统健康度仪表盘、延迟分布、链路追踪信息与告警状态（绿/黄/红三级），支持自动刷新。
- **管理后台（`/admin`）**：Agent 健康仪表盘、预算配置、按 trace_id 检索重放历史查询、灰度部署管理。
- **登录页（`/login`）**：用户注册与登录，Token 持久化，过期自动登出。

> **持续迭代中：** Agent 沙箱（`/sandbox`）、Agent 对比（`/compare`）、会话分享（`/share/:id`）、模板市场（`/templates`）——前端 UI 已就绪，后端接口逐步接入中。

### 前端技术选型

| 技术 | 用途 | 选型理由 |
| ------ | ------ | -------- |
| Vue 3 + TypeScript | UI 框架 | Composition API，手写 Proto 类型映射 |
| Vite | 构建工具 | 极速 HMR + ESBuild，开发代理转发 Node.js Proxy |
| Pinia | 状态管理 | 3 个 Store 协作：chat / auth / agents |
| ECharts | 数据可视化 | 拓扑力导向图 + Dashboard 多图表联动 |
| Mermaid.js | DAG 渲染 | ExecutionPlan JSON 实时渲染为流程图 |
| Tailwind CSS | 样式框架 | 原子化 CSS，统一视觉风格 |

### 视图连接状态

| 视图 | 路由 | 后端连接 |
| ------ | ------ | -------- |
| **ChatView** | `/` | ✅ 已连接（SSE 流式 + DAG + 活动流） |
| **AgentTopology** | `/topology` | ✅ 已连接（ECharts 拓扑图） |
| **Dashboard** | `/dashboard` | ✅ 已连接（Token/成本图表） |
| **Monitor** | `/monitor` | ⚠️ 部分连接（无数据时 fallback） |
| **AdminView** | `/admin` | ⚠️ 部分连接（Agent 列表来自 API） |
| **LoginView** | `/login` | ✅ 已连接 |
| **AgentSandbox** | `/sandbox` | 🚧 后端接入中 |
| **CompareView** | `/compare` | 🚧 后端接入中 |
| **ShareView** | `/share/:id` | 🚧 后端接入中 |
| **TemplateMarket** | `/templates` | 🚧 后端接入中 |

### 前端核心能力

**gRPC → SSE 流式对接：** 手写 TypeScript 类型定义 + Fetch API + ReadableStream，经 Node.js Proxy (:8081) 完成协议转换。7 种 SSE 事件类型（`partial` / `status` / `complete` / `error` / `plan` / `subtask_start` / `subtask_complete`），支持逐字渲染、DAG 进度更新、活动流推送。Auth Token 通过 header 自动注入，401 自动登出。

**Mermaid DAG 实时渲染：** `plan` 事件接收 ExecutionPlan JSON → Mermaid.js 渲染流程图 → `subtask_start` / `subtask_complete` 事件驱动节点状态更新（pending → running → completed/failed）。

**ECharts Agent 拓扑力导向图：** 节点颜色/大小反映 Agent 健康状态与负载，30s 轮询刷新，活动流事件驱动节点高亮。

### 启动前端

```bash
cd frontend && npm ci   # 首次
cd frontend && npm run dev    # Vite :5173
```

---

## 快速开始

### 环境要求

| 要求 | 版本 | 说明 |
| ------ | ------ | ------ |
| 操作系统 | Linux (Ubuntu 20.04+) | C++ 编译运行 |
| CMake | 3.20+ | 构建系统 |
| GCC | 10+（C++20） | 编译器 |
| gRPC | 1.51.1+ | RPC 框架 |
| Redis | 6.0+ | 缓存/存储 |
| Node.js | 18+ | 前端 + gRPC Proxy |

### 1. 安装依赖

```bash
sudo apt-get update
sudo apt-get install -y \
    cmake build-essential pkg-config \
    libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc \
    libcurl4-openssl-dev libjsoncpp-dev uuid-dev \
    libgtest-dev libhiredis-dev redis-server \
    nlohmann-json3-dev
```

### 2. 配置 API Key

项目根目录 `.env` 文件：

| 变量 | 用途 | 默认值 |
| ------ | ------ | -------- |
| `LLM_API_KEY` | LLM API 密钥（必填） | — |
| `LLM_MODEL` | 模型名称 | `deepseek-v4-pro` |
| `LLM_API_URL` | API 端点 | `https://api.deepseek.com` |

### 3. 编译

```bash
./run.sh build
```

### 4. 运行测试

```bash
./run.sh test        # 运行全部 17 套测试
cd build && ctest --output-on-failure   # 或单独运行
```

### 5. 启动服务

```bash
# 一键启动全部后端（Redis + Agent + Proxy + Orchestrator + gRPC Server）
./run.sh start-all

# 启动 Node.js gRPC Proxy（Windows PowerShell，新窗口）
$env:GRPC_TARGET="localhost:50051"
node gateway/proxy/server.mjs

# 启动前端（Windows PowerShell，新窗口）
cd frontend && npm run dev

# 注册 Agent（WSL 中）
./scripts/register_agents.sh
```

访问 **http://localhost:5173** 即可使用。

### 6. 停止

```bash
./run.sh stop
```

### 服务端口

| 端口 | 服务 | 协议 |
| ------ | ------ | ------ |
| 50051 | gRPC Server | gRPC/HTTP2 |
| 5000 | Orchestrator | HTTP/A2A |
| 5100 | Agent Endpoint | HTTP/A2A |
| 8080 | Nginx browser entrypoint | HTTP |
| 8081 | Node.js gRPC Proxy | HTTP |
| 6379 | Redis | TCP |
| 5173 | Vite Dev Server | HTTP |

---

## Agent 接入

要将外部 Agent 接入框架，参考 [agent-integration-guide.md](agent-integration-guide.md)。核心步骤：

1. **实现 A2A AgentCard** — 声明 Agent 的 skill、端点、版本等元数据
2. **实现 A2A 接口** — 支持 `message/send`（同步）或 `message/stream`（SSE 流式）
3. **启动并注册** — 运行 Agent 服务后通过 `scripts/register_agents.sh` 注册到 gRPC Server

示例 Agent 见 [examples/](examples/) 目录。

---

## 许可证

MIT License

### PostgreSQL migrations (PR2.1)

The compiled `db_migrate` binary applies the canonical `db/migrations/VNNN__name.sql`
set in order and records checksums in `schema_migrations`. Compose runs it as the
`migrate` service before `rpc-server`; the legacy `sql/` files remain reference
inputs and are never executed by Compose.

On WSL2, keep the checkout on the Linux filesystem (not `/mnt/c`) and run
`./scripts/bootstrap-wsl.sh` before configuring CMake. Ubuntu 26.04 receives
the verified user-prefix libpqxx 8.0.1 workaround described above; Ubuntu
24.04 continues to use its system `libpqxx-dev`. The `.env.example` password
is local-only.
