# NexusAI — Multi-Agent Communication and Tool Selection Framework

基于 C++ 和 gRPC 的高性能多 Agent 协作框架。支持智能路由、A2A 协议通信、MCP 工具调用、用户认证和记忆系统。

## 核心能力

| 能力 | 说明 |
|------|------|
| gRPC 通信 | 高性能 RPC 服务层，支持同步和流式查询 |
| A2A 协议 | 基于 HTTP/JSON-RPC 2.0 的 Agent-to-Agent 通信 |
| 智能路由 | 四级路由引擎：Embedding → LLM 意图分类 → 关键词 IDF → 兜底 |
| MCP 工具调用 | Model Context Protocol，支持 STDIO 和 SSE 两种传输 |
| 任务编排 | Orchestrator 协调多 Agent，支持任务规划、并行执行和结果聚合 |
| 用户认证 | 注册/登录/Token 校验，gRPC 拦截器统一鉴权 |
| 记忆系统 | 三层记忆：对话历史 → 用户长期记忆 → 跨 Agent 摘要，Redis 持久化 |
| 服务注册 | Agent 通过 gRPC 注册/心跳/注销，路由引擎自动发现 |

## 技术栈

- **语言**: C++17
- **RPC**: gRPC + Protocol Buffers
- **HTTP 客户端**: libcurl
- **JSON**: nlohmann/json
- **持久化**: Redis（认证、记忆、任务状态）
- **测试**: Google Test + RapidCheck
- **前端**: Vue 3 + TypeScript + Vite
- **网关**: Nginx + Envoy（gRPC-Web 协议转换）
- **AI 模型**: 兼容 OpenAI API 的大语言模型

---

## 系统架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                          用户层                                      │
│  ┌──────────────┐    ┌──────────────────────┐                       │
│  │  RPC Client   │    │  Vue 3 前端 (浏览器)  │                       │
│  │  (CLI 工具)   │    │  gRPC-Web            │                       │
│  └──────┬───────┘    └──────────┬───────────┘                       │
└─────────┼───────────────────────┼───────────────────────────────────┘
          │ gRPC                  │ gRPC-Web (HTTP/1.1)
          ▼                       ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       API 网关层                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Nginx (:8080 gRPC-Web / :8082 gRPC 直连)                    │   │
│  │  限流 + API Key 认证 + CORS + 负载均衡                        │   │
│  └──────────────────────────────┬──────────────────────────────┘   │
│                                  │                                  │
│  ┌──────────────┐               │                                  │
│  │ Envoy (:8081)│ ← gRPC-Web ↔ gRPC 协议转换                     │
│  └──────────────┘               │                                  │
└──────────────────────────────────┼──────────────────────────────────┘
                                   │ gRPC/2
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        RPC 服务层                                    │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  RPC Server (:50051)                                         │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐   │   │
│  │  │AIQueryService│  │ A2A Adapter  │  │ AuthInterceptor  │   │   │
│  │  └──────┬───────┘  └──────┬───────┘  └──────────────────┘   │   │
│  └─────────┼──────────────────┼─────────────────────────────────┘   │
└────────────┼──────────────────┼─────────────────────────────────────┘
             │ A2A/HTTP          │
             ▼                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        Agent 协调层                                  │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Orchestrator (:5000)                                        │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐   │   │
│  │  │ 意图识别      │  │ Agent 路由    │  │ 任务规划 + 执行   │   │   │
│  │  │ (LLM API)    │  │ (四级路由)    │  │ (并行/串行)      │   │   │
│  │  └──────────────┘  └──────┬───────┘  └──────────────────┘   │   │
│  └──────────────────────────┼───────────────────────────────────┘   │
│                             │ A2A/HTTP                               │
│            ┌────────────────┼────────────────┐                      │
│            ▼                ▼                ▼                      │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐                │
│  │ Math Agent   │ │ Code Agent   │ │  自定义 Agent │                │
│  │  (:5001)     │ │  (可扩展)     │ │  (gRPC 注册)  │                │
│  └──────────────┘ └──────────────┘ └──────────────┘                │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Registry Server (:8500) — Agent 注册中心                     │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### 数据流向

```
1. 用户发送 "计算 1+7"
2. RPC Client → gRPC → RPC Server (:50051)
3. AuthInterceptor 校验 Token
4. AIQueryService → A2A Adapter → Orchestrator (:5000)
5. AgentRouter 四级路由匹配 → Math Agent (:5001)
6. Math Agent 调用 MCP 工具执行计算
7. 结果经 Orchestrator 聚合后原路返回
```

---

## 环境要求

| 要求 | 版本 |
|------|------|
| 操作系统 | Linux（Ubuntu 20.04+）/ WSL |
| CMake | 3.15+ |
| C++ 编译器 | GCC 9+（C++17） |
| Redis | 6.0+ |
| gRPC | 1.51.1+ |
| Docker | 20+（网关部署可选） |
| Node.js | 18+（前端开发可选） |

### 安装依赖

```bash
sudo apt-get update
sudo apt-get install -y \
    cmake build-essential pkg-config \
    libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc \
    libcurl4-openssl-dev libjsoncpp-dev uuid-dev \
    libgtest-dev libhiredis-dev redis-server \
    nlohmann-json3-dev
```

### API Key

| 变量 | 用途 |
|------|------|
| `LLM_API_KEY` | LLM 对话、意图识别、Embedding 向量化（兼容 OpenAI API） |
| `LLM_MODEL` | 模型名称（可选，默认 deepseek-v4-pro） |
| `LLM_API_URL` | API 地址（可选，默认 OpenAI 兼容端点） |

---

## 编译与运行

项目使用 `run.sh` 统一管理编译、测试和运行。

### 编译

```bash
./run.sh build
```

### 运行测试

```bash
./run.sh test
```

### 启动服务

```bash
# 启动 gRPC Server（自动启动 Redis）
./run.sh start

# 启动 API 网关（需 Docker）
./run.sh gateway

# 启动前端开发服务器
./run.sh frontend-dev
```

### 停止服务

```bash
./run.sh stop
```

### 完整命令列表

```
./run.sh build          编译项目
./run.sh test           运行全部测试
./run.sh clean          清理构建目录
./run.sh start          启动 gRPC 服务端（后台运行，自动启动 Redis）
./run.sh redis          启动 Redis 服务
./run.sh gateway        启动 API 网关：Nginx + Envoy（需 Docker）
./run.sh frontend-dev   启动前端开发服务器
./run.sh frontend-build 构建前端生产包
./run.sh stop           停止所有服务（本地进程 + Docker 容器）
./run.sh setup          检测开发环境
```

---

## 端口说明

| 端口 | 服务 | 说明 |
|------|------|------|
| 50051 | RPC Server | gRPC 主入口 |
| 5000 | Orchestrator | Agent 协调器 |
| 5001 | Math Agent | 数学计算 Agent |
| 8500 | Registry | Agent 注册中心 |
| 8080 | Nginx | 浏览器 gRPC-Web 入口 |
| 8082 | Nginx | 后端 gRPC 直连入口 |
| 8081 | Envoy | gRPC-Web 协议转换 |
| 6379 | Redis | 认证 + 记忆 + 任务状态 |

---

## 核心模块

### 智能路由（AgentRouter）

四级路由策略，逐级降级：

1. **Embedding 匹配**：用户请求与所有 Agent 的 skills 做向量相似度对比，高置信度直选
2. **LLM 意图分类**：调用大模型判断用户意图属于哪个 skill
3. **关键词 IDF 匹配**：提取请求关键词，与 skill 名称做 IDF 加权匹配
4. **兜底**：以上均未命中时，选择健康状态的通用 Agent

路由在规划阶段预绑定 Agent URL（`resolveAgents()`），执行时直连，避免重复路由。

### 任务编排（Orchestrator）

- **TaskPlanner**：将复杂请求拆解为多个子任务，预绑定目标 Agent
- **TaskExecutor**：支持串行依赖执行和并行独立执行
- **ResultAggregator**：聚合子任务结果，支持简单拼接和 LLM 综合两种策略

### 记忆系统（MemoryService）

三层记忆，Redis 持久化：

| 层级 | 粒度 | 存储 | 说明 |
|------|------|------|------|
| Tier 1 | (context_id, agent_id) | Redis List | 对话历史，按 Agent 分片，LTRIM 保留最近 50 条 |
| Tier 2 | user_id | Redis Hash | 用户长期记忆，Agent 通过 memory_hints 上报，平台统一写入 |
| 跨 Agent | context_id | Redis String | Agent 切换时由 LLM 生成的跨 Agent 摘要 |

请求到达时，MemoryService 自动构建 SystemContext（user_id + user_memory + conversation_history + cross_agent_summary）注入到 AI 查询中。

### 用户认证（AuthService + AuthInterceptor）

- 注册/登录返回 UUID Token，Redis 存储，24 小时 TTL 自动过期
- gRPC 拦截器统一校验 Bearer Token，白名单 RPC 免认证
- 前端 LoginView + 路由守卫 + Token 自动注入

### MCP 工具调用

支持 STDIO（本地进程管道）和 SSE（远程 HTTP）两种传输方式：

| 传输 | 场景 | 说明 |
|------|------|------|
| STDIO | 本地部署 | MCP Server 作为子进程，通过管道通信 |
| SSE | 分布式 | 通过 HTTP/SSE 连接远程 MCP Server |

### A2A 协议

完整的 Agent-to-Agent 协议实现（HTTP/JSON-RPC 2.0）：

- `message/send`：同步调用
- `message/stream`：SSE 流式调用
- Task 状态机：Submitted → Running → Completed / Failed / Canceled
- 支持 AgentCard 元数据和服务发现

---

## 前端

Vue 3 + TypeScript + Vite 单页应用，通过 gRPC-Web 与后端通信。

```bash
# 开发模式
./run.sh frontend-dev

# 生产构建
./run.sh frontend-build
```

技术选型：Vue 3 Composition API、Pinia 状态管理、vue-router、手写 TypeScript 类型定义对应 proto 消息。

页面结构：`/` 聊天页、`/admin` 管理页、`/login` 登录页。开发时 Vite 代理将 gRPC-Web 请求转发到 Envoy（:8081），无需 Nginx。

---

## API 网关

三层代理架构：

```
浏览器 (gRPC-Web / HTTP/1.1)
    → Nginx :8080 (限流 + API Key + CORS + proxy_pass)
        → Envoy :8081 (gRPC-Web ↔ gRPC/2 协议转换)
            → NexusAI :50051 (gRPC/2)
```

后端服务间调用走直连路径：Nginx :8082 → grpc_pass → NexusAI :50051。

启动网关：

```bash
./run.sh gateway
# 等价于 docker compose -f docker-compose.gateway.yaml up -d
```

网关包含三个容器：Redis（持久化）、Envoy（协议转换）、Nginx（反向代理）。

---

## Agent 接入

其他 Agent 通过 gRPC 注册接入平台，路由引擎自动发现并匹配请求。接入流程：

1. 实现 A2A HTTP 接口（`message/send` 和 `message/stream`）
2. 提供 AgentCard 元数据（skills 描述决定路由命中率）
3. 调用 `RegisterAgent` RPC 注册
4. 维持心跳（建议 15 秒间隔）
5. 下线时调用 `UnregisterAgent` 注销

详细接入指南和 Python 完整示例见 [Agent 接入指南](docs/agent-integration-guide.md)。

C++ Agent 可直接使用项目提供的 `AgentAutoRegistrar` SDK，封装了注册 → 心跳 → 注销的完整生命周期。

---

## 项目结构

```
agent-communication-and-tool-selection-framework/
├── proto/                   # Protocol Buffer 定义
│   ├── ai_query.proto       #   AI 查询服务
│   ├── agent_service.proto  #   Agent 通信服务（注册/心跳/注销）
│   ├── common.proto         #   公共消息（ServiceInfo/Status）
│   └── user.proto           #   用户服务（注册/登录）
├── server/                  # RPC 服务端
│   └── src/
│       ├── main.cpp         #   服务入口
│       ├── ai_query_service.cpp  # AI 查询实现
│       ├── auth_service.cpp      # 认证服务
│       └── auth_interceptor.cpp  # gRPC 鉴权拦截器
├── client/                  # RPC 客户端
│   └── src/
│       ├── agent_auto_registrar.cpp  # Agent 注册 SDK
│       └── ...
├── orchestrator/            # Agent 编排
│   └── src/
│       ├── agent_router.cpp       # 四级路由引擎
│       ├── task_planner.cpp       # 任务规划
│       ├── task_executor.cpp      # 任务执行
│       └── result_aggregator.cpp  # 结果聚合
├── a2a/                     # A2A 协议库（HTTP/JSON-RPC 2.0）
├── a2a_adapter/             # gRPC ↔ A2A 适配层
├── ai_interface/            # LLM API 调用封装
├── common/                  # 公共组件
│   └── src/
│       ├── memory_service.cpp  # 三层记忆系统
│       ├── redis_client.cpp    # Redis 客户端（hiredis 封装）
│       ├── auth_service.cpp    # 认证服务（共享）
│       └── logger.cpp          # 日志模块
├── mcp/                     # MCP 客户端模块
├── mcp_server_integrated/   # MCP Server 及插件
├── registry/                # Agent 注册中心
├── frontend/                # Vue 3 前端
│   └── src/
│       ├── views/           #   页面组件
│       ├── components/      #   UI 组件
│       ├── services/        #   gRPC 客户端封装
│       ├── stores/          #   Pinia 状态管理
│       └── types/           #   TypeScript 类型定义
├── gateway/                 # API 网关配置
│   ├── nginx.conf           #   Nginx 主配置
│   └── envoy.yaml           #   Envoy gRPC-Web 配置
├── tests/                   # 测试代码
├── docs/                    # 文档
│   ├── architecture.md      #   架构设计
│   ├── a2a-protocol.md      #   A2A 协议
│   ├── deployment.md        #   部署指南
│   ├── mcp-plugin-development.md  # MCP 插件开发
│   └── NexusAI/
│       ├── development-plan.md     # 开发计划
│       └── optimization-log.md     # 优化记录
├── docker-compose.gateway.yaml    # 网关容器编排
├── run.sh                         # 统一运行脚本
└── README.md                      # 本文档
```

---

## 许可证

MIT License
