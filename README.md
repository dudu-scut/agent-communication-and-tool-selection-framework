# NexusAI — 多 Agent 通信与工具选择框架

<div align="center">

**C++20 · gRPC · A2A Protocol · MCP · Vue 3**

高性能多 Agent 协作框架 — 四层渐进式智能路由 · DAG 任务编排 · 流式全链路可观测

[![C++](https://img.shields.io/badge/C%2B%2B-20-blue)](https://en.cppreference.com/w/cpp/20)
[![gRPC](https://img.shields.io/badge/gRPC-1.51%2B-green)](https://grpc.io/)
[![License](https://img.shields.io/badge/license-MIT-orange)](LICENSE)
[![Tests](https://img.shields.io/badge/tests-17%20suites-brightgreen)](tests/)
[![Lines](https://img.shields.io/badge/C%2B%2B-76%2C000%2B%20lines-informational)]()

</div>

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

```
┌──────────────────────────────────────────────────────┐
│  Browser (:5173)                                      │
│  Vue 3 SPA — 10 views, SSE streaming, Mermaid DAG    │
└──────────┬───────────────────────────────────────────┘
           │ HTTP / SSE
┌──────────▼───────────────────────────────────────────┐
│  Node.js gRPC Proxy (:8081)                           │
│  gRPC-Web ↔ gRPC transcoding, disconnect detection   │
└──────────┬───────────────────────────────────────────┘
           │ gRPC/Protobuf
┌──────────▼───────────────────────────────────────────┐
│  gRPC Server (:50051) — 9 Services, 35 RPCs          │
│  Auth interceptor + Cost middleware                   │
└─────┬──────┬──────┬──────┬──────┬──────┬─────────────┘
      │      │      │      │      │      │
┌─────▼─┐ ┌──▼──┐ ┌──▼──┐ ┌──▼──┐ ┌──▼──┐ ┌──────────┐
│Agent │ │Orch-│ │A2A  │ │MCP  │ │Mem- │ │Background│
│Router│ │estr-│ │Adap-│ │Clie-│ │ory  │ │Scheduler │
│4-tier│ │ator │ │ter  │ │nt   │ │Svc  │ │8 tasks   │
└──┬───┘ └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘ └──────────┘
      │      │      │      │      │
      └──────┴──────┴──────┴──────┘
                    │
┌───────────────────▼──────────────────────────────────┐
│  Redis (:6379) — state, cache, memory, traces, auth  │
└──────────────────────────────────────────────────────┘
                    │
┌───────────────────▼──────────────────────────────────┐
│  Orchestrator (:5000) — Python, LLM task planning     │
│  Mock Agent (:5100) — Python, A2A protocol            │
└──────────────────────────────────────────────────────┘
```

---

## 项目结构

```
agent-communication-and-tool-selection-framework/
│
├── proto/                           # Protocol Buffer 定义（9 个 .proto 文件）
│   ├── ai_query.proto               #   AI 查询服务（Query / QueryStream / GetQueryStatus / GetAgentMetrics）
│   ├── agent_service.proto          #   Agent 通信服务（注册 / 心跳 / 注销 / 发现 / 消息）
│   ├── agent_lifecycle.proto        #   Agent 生命周期（反馈 / 自主性 / 撤销）
│   ├── orchestration.proto          #   编排服务（ExecutePlan / ReplayQuery / ExportConversation）
│   ├── observability.proto          #   可观测性（GetTraceDetail / GetCostReport）
│   ├── sharing.proto                #   会话共享（ShareSession / ObserveSession / 模板）
│   ├── user_experience.proto        #   用户体验（InterventionResponse / SandboxQuery）
│   ├── user.proto                   #   用户服务（Login / Register）
│   └── common.proto                 #   公共消息定义（ServiceInfo / Status / enums）
│
├── server/                          # gRPC 服务端（9 个 Service 实现）
│   ├── include/agent_rpc/server/    #   头文件（rpc_server, ai_query_service, agent_service 等）
│   └── src/                         #   实现文件（main.cpp, 各 service 实现）
│
├── orchestrator/                    # DAG 任务编排与智能路由
│   ├── include/agent_rpc/orchestrator/
│   │   ├── agent_router.h           #   四级路由引擎
│   │   ├── task_planner.h           #   DAG 任务规划器（Kahn 拓扑排序）
│   │   ├── task_executor.h          #   任务执行器（std::async 并行）
│   │   ├── result_aggregator.h      #   结果聚合器
│   │   ├── feedback_aggregator.h    #   反馈聚合器
│   │   ├── context_compressor.h     #   上下文压缩器
│   │   ├── cron_scheduler.h         #   定时调度器
│   │   ├── export_service.h         #   导出服务
│   │   └── replay_service.h         #   回放服务
│   └── src/                         #   实现文件
│
├── common/                          # 公共组件库
│   ├── include/agent_rpc/common/
│   │   ├── redis_client.h           #   Redis 客户端（hiredis 封装）
│   │   ├── memory_service.h         #   三层记忆系统
│   │   ├── circuit_breaker.h        #   三态熔断器
│   │   ├── load_balancer.h          #   负载均衡（6 种策略）
│   │   ├── cost_tracker.h           #   Token 成本追踪
│   │   ├── trace_context.h          #   链路追踪上下文
│   │   ├── background_scheduler.h   #   后台调度器（8 个周期性任务）
│   │   └── ...                      #   logger, metrics, serializer, env_loader 等
│   └── src/                         #   实现文件
│
├── a2a/                             # A2A 协议核心库（HTTP/JSON-RPC 2.0）
│   ├── include/a2a/
│   │   ├── core/                    #   JSON-RPC 核心（方法/错误码/HTTP客户端/类型）
│   │   ├── models/                  #   数据模型（AgentCard/Task/Message/Artifact）
│   │   ├── client/                  #   A2A 客户端 + AgentCard 解析器
│   │   └── server/                  #   Task 管理器 + 内存存储
│   ├── src/                         #   实现文件
│   └── third_party/json.hpp         #   nlohmann/json
│
├── a2a_adapter/                     # gRPC ↔ A2A 协议适配层
│   ├── include/agent_rpc/a2a_adapter/
│   │   ├── a2a_adapter.h            #   适配器主类（同步/异步/流式/直连 4 模式）
│   │   ├── request_adapter.h        #   Protobuf → JSON-RPC 转换
│   │   ├── response_adapter.h       #   JSON-RPC → Protobuf 转换
│   │   └── ...                      #   config, metrics, error_mapper, retry_policy
│   └── src/                         #   实现文件
│
├── mcp/                             # MCP 客户端（STDIO + SSE 传输）
│   ├── include/agent_rpc/mcp/
│   │   ├── mcp_client.h             #   MCP 客户端
│   │   └── rag/                     #   RAG-MCP：Embedding 向量化 + 语义检索
│   └── src/                         #   实现文件
│
├── mcp_server_integrated/           # 集成 MCP 服务器（独立构建，6 个内置插件）
├── registry/                        # Agent 服务注册与发现（内存注册表 + 后台清理）
├── client/                          # gRPC 客户端 CLI + Agent 自动注册 SDK
│
├── frontend/                        # Vue 3 + TypeScript + Vite 前端
│   └── src/
│       ├── views/                   #   10 个视图页面
│       │   ├── ChatView.vue         #     对话界面（流式 SSE + DAG 渲染）
│       │   ├── AgentTopology.vue    #     Agent 拓扑可视化（ECharts 力导向图）
│       │   ├── Dashboard.vue        #     可观测仪表盘（Token 趋势 / 成本分布）
│       │   ├── Monitor.vue          #     系统健康监控（延迟 / 告警）
│       │   ├── AdminView.vue        #     Agent 管理（健康仪表盘 / 预算 / 回放 / 灰度）
│       │   ├── LoginView.vue        #     登录注册
│       │   ├── AgentSandbox.vue     #     Agent 沙箱试用
│       │   ├── CompareView.vue      #     Agent 对比
│       │   ├── ShareView.vue        #     会话分享
│       │   └── TemplateMarket.vue   #     模板市场
│       ├── components/              #   UI 组件（AgentBadge, StreamingText, ActivityPanel 等）
│       ├── services/grpc-client.ts  #   gRPC 客户端封装（Fetch + ReadableStream SSE）
│       ├── stores/                  #   Pinia 状态管理（chat / auth / agents）
│       └── types/proto.ts           #   Protobuf TypeScript 类型定义
│
├── gateway/                         # API 网关
│   ├── proxy/server.mjs             #   Node.js gRPC Proxy（主力网关）
│   ├── envoy.yaml                   #   Envoy gRPC-Web 配置（Docker 部署备选）
│   └── nginx.conf                   #   Nginx 配置
│
├── deploy/                          # 部署配置
│   └── docker-compose.gateway.yaml  #   Docker 网关编排（Nginx + Envoy）
│
├── tests/                           # 测试代码
│   ├── e2e/                         #   E2E 测试脚本
│   │   ├── e2e_full_test.py         #     综合 E2E 测试（Python）
│   │   ├── e2e_full_test.sh         #     E2E 测试启动脚本
│   │   ├── e2e_test.sh              #     E2E 测试脚本
│   │   └── quick_test.sh            #     快速测试脚本
│   ├── test_agent_communication.cpp #   17 个 C++ 测试文件
│   ├── test_a2a_integration.cpp     #     （GTest 集成 + RapidCheck 属性测试）
│   └── ...                          #
│
├── examples/                        # Python Mock Agent 示例
│   ├── echo_agent.py
│   ├── math_agent.py
│   ├── translator_agent.py
│   └── orchestrator_agent.py
│
├── docs/                            # 项目文档（guides / interview / reports / superpowers）
│
├── sql/                             # PostgreSQL Schema（已定义，当前运行时用 Redis）
├── verify/                          # Mock Agent 验证服务
├── CMakeLists.txt                   # 根 CMake 配置（C++20，10 个子模块）
├── agent-integration-guide.md       # Agent 接入指南
├── run.sh                           # 统一运行脚本（build / test / start-all / stop / verify）
├── scripts/                         # 辅助脚本
│   ├── start_backend.sh             #   后端一键启动
│   └── register_agents.sh           #   Mock Agent 注册
├── CLAUDE.md                        # Claude Code 项目指引
├── .env                             # 环境变量（LLM_API_KEY / LLM_MODEL / LLM_API_URL）
└── .env.example                     # 环境变量示例
```

---

## 前端界面

基于 Vue 3 + TypeScript + Vite 构建的 SPA，提供 10 个功能视图，通过 Node.js gRPC Proxy 实现 gRPC → SSE 流式对接。

### 启动后能看到什么

打开 http://localhost:5173 进入一个完整的多 Agent 协作可视化平台：

- **对话页面（首页 `/`）**：自然语言提问，实时流式回答；复杂问题自动展示 DAG 执行计划流程图（Mermaid），子任务状态实时更新；右侧活动流面板显示 Agent 工作步骤。
- **拓扑页面（`/topology`）**：ECharts 力导向图展示所有 Agent 关系网络，节点颜色/大小反映健康状态与负载，支持拖拽交互与详情查看。
- **仪表板（`/dashboard`）**：Token 消耗趋势、Agent 调用排行、成本分布饼图，CountUp 数字滚动动画。
- **监控面板（`/monitor`）**：系统健康度仪表盘、延迟分布、链路追踪信息与告警状态（绿/黄/红三级），支持自动刷新。
- **管理后台（`/admin`）**：Agent 健康仪表盘、预算配置、按 trace_id 检索重放历史查询、灰度部署管理。
- **登录页（`/login`）**：用户注册与登录，Token 持久化，过期自动登出。

> **开发中：** Agent 沙箱（`/sandbox`）、Agent 对比（`/compare`）、会话分享（`/share/:id`）、模板市场（`/templates`）——后端接口尚未完整实现，前端 UI 已搭建。

### 前端技术选型

| 技术 | 用途 | 选型理由 |
|------|------|----------|
| Vue 3 + TypeScript | UI 框架 | Composition API，手写 Proto 类型映射 |
| Vite | 构建工具 | 极速 HMR + ESBuild，开发代理转发 Node.js Proxy |
| Pinia | 状态管理 | 3 个 Store 协作：chat / auth / agents |
| ECharts | 数据可视化 | 拓扑力导向图 + Dashboard 多图表联动 |
| Mermaid.js | DAG 渲染 | ExecutionPlan JSON 实时渲染为流程图 |
| Tailwind CSS | 样式框架 | 原子化 CSS，统一视觉风格 |

### 视图连接状态

| 视图 | 路由 | 后端连接 |
|------|------|----------|
| **ChatView** | `/` | ✅ 已连接（SSE 流式 + DAG + 活动流） |
| **AgentTopology** | `/topology` | ✅ 已连接（ECharts 拓扑图） |
| **Dashboard** | `/dashboard` | ✅ 已连接（Token/成本图表） |
| **Monitor** | `/monitor` | ⚠️ 部分连接（无数据时 fallback） |
| **AdminView** | `/admin` | ⚠️ 部分连接（仅 Agent 列表来自 API） |
| **LoginView** | `/login` | ✅ 已连接 |
| **AgentSandbox** | `/sandbox` | 🚧 开发中 |
| **CompareView** | `/compare` | 🚧 开发中 |
| **ShareView** | `/share/:id` | 🚧 开发中 |
| **TemplateMarket** | `/templates` | 🚧 开发中 |

### 前端核心能力

**gRPC → SSE 流式对接：** 手写 TypeScript 类型定义 + Fetch API + ReadableStream，经 Node.js Proxy (:8081) 完成协议转换。7 种 SSE 事件类型（`partial` / `status` / `complete` / `error` / `plan` / `subtask_start` / `subtask_complete`），支持逐字渲染、DAG 进度更新、活动流推送。Auth Token 通过 header 自动注入，401 自动登出。

**Mermaid DAG 实时渲染：** `plan` 事件接收 ExecutionPlan JSON → Mermaid.js 渲染流程图 → `subtask_start` / `subtask_complete` 事件驱动节点状态更新（pending → running → completed/failed）。

**ECharts Agent 拓扑力导向图：** 节点颜色/大小反映 Agent 健康状态与负载，30s 轮询刷新，活动流事件驱动节点高亮。

### 启动前端

```bash
cd frontend && npm install   # 首次
cd frontend && npm run dev    # Vite :5173
```

---

## 快速开始

### 环境要求

| 要求 | 版本 | 说明 |
|------|------|------|
| 操作系统 | Linux (Ubuntu 20.04+) | C++ 编译运行 |
| CMake | 3.15+ | 构建系统 |
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
|------|------|--------|
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
# 一键启动全部后端（Redis + Mock Agent + gRPC Server + Orchestrator）
./scripts/start_backend.sh

# 启动 Node.js gRPC Proxy（Windows PowerShell，新窗口）
$env:GRPC_TARGET="localhost:50051"
node gateway/proxy/server.mjs

# 启动前端（Windows PowerShell，新窗口）
cd frontend && npm run dev

# 注册 Mock Agent（WSL 中）
./scripts/register_agents.sh
```

访问 **http://localhost:5173** 即可使用。

### 6. 停止

```bash
./run.sh stop
```

### 服务端口

| 端口 | 服务 | 协议 |
|------|------|------|
| 50051 | gRPC Server | gRPC/HTTP2 |
| 5000 | Orchestrator | HTTP/A2A |
| 5100 | Mock Agent | HTTP/A2A |
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
