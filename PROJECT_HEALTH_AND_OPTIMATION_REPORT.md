# PROJECT HEALTH AND OPTIMATION REPORT

> 审计日期：2026-08-04
> 审计范围：仓库内一方源码、Proto、前端、网关、构建/部署配置、启动/验证脚本与现存项目文档
> 审计方式：全仓静态扫描、文档—代码—协议交叉核对，以及不修改源码/配置的语法、构建和测试验证
> 总体结论：**高风险 / 尚不具备按 README 描述直接部署和完整验收的条件**

## 1. 执行摘要

项目不是“空壳”：核心 C++/gRPC 服务、Vue 前端、Node JSON→gRPC 代理、Redis 记忆与部分多 Agent 编排已经形成较完整的代码骨架；本次前端构建和代理契约测试也通过。但当前主用户路径与文档承诺之间仍存在明显断层：

- 前端实际使用 `QueryStream`，而启用 orchestrator 后该路径提前返回，绕过预算、记忆、成本、熔断和 trace 落库等关键治理逻辑。
- Docker 提供的是 gRPC-Web Envoy 链路，前端发送的是 JSON；同时 Compose 挂载路径错误、后端地址硬编码，容器化网关不能按文档直接工作。
- 回放、导出、分享、模板、沙箱、对比、撤销、干预等 RPC/页面中存在大量占位实现，其中若干仍返回成功，容易让验收脚本和调用方误判功能已完成。
- Token/成本生产调用均记录为 0；反馈写入 Redis，而路由聚合读取 PostgreSQL；SQL 数据库没有被部署/初始化，验证脚本还会在缺少 PostgreSQL 时用恒成功 mock 掩盖问题。
- 存在跨用户数据访问、未鉴权 Agent 注册/注销、TLS 无法从主程序启用、关闭证书校验、弱密码派生和硬编码 API key 等安全风险。
- `run.sh` 中的 `((var++))` 与 `set -e` 组合会让 stop/verify 过早退出；另有机器绝对路径脚本和无法被停止的后台代理。

### 1.1 问题数量

| 严重程度 | 数量 | 判定含义 |
|---|---:|---|
| 高 | 13 | 阻断主要功能、部署或验收，或可导致越权、成本失控、虚假成功 |
| 中 | 15 | 明显影响可用性、可维护性、配置一致性或诊断能力 |
| 低 | 6 | 工程卫生、性能或文档精度问题，短期不直接阻断主链路 |
| **合计** | **34** | 不含第三方/生成代码中的 TODO |

### 1.2 建议的发布判断

| 场景 | 判断 | 原因 |
|---|---|---|
| 本地前端静态构建 | 可用 | `npm run build` 通过 |
| Node 代理的协议契约 | 基础可用 | 5 项契约测试通过，但错误映射和流式鉴权仍有缺口 |
| C++ 服务可编译性 | 未确认 | 当前审计环境无 CMake/可用 WSL，不能代替真实编译结果 |
| Docker 一键启动 | 不可用 | Compose 挂载路径、协议链路和后端地址冲突 |
| README 所述完整产品 | 不可验收 | 多项功能为占位或前后端未打通 |
| 生产环境 | 不建议 | 鉴权、TLS、证书校验、成本治理和数据隔离存在高风险问题 |

## 2. 审计与验证结果

### 2.1 仓库概况

- 扫描到约 351 个非 `node_modules`/非构建产物文件；重点审阅一方 C++、TypeScript/Vue、JavaScript、Python、Shell、Proto、SQL、CMake、Docker、Nginx/Envoy 和 Markdown。
- Proto 共注册 9 个 service、35 个 RPC；Node 代理通过 descriptor 动态注册全部 service，并有对应契约测试。
- `tests/CMakeLists.txt` 实际定义 22 个 CTest，而 README/CLAUDE 多处仍写 17 套。
- 第三方目录、生成 Proto 和 `node_modules` 中的 TODO/异常模式不计入项目未完成项，避免把上游代码误报为本项目缺陷。

### 2.2 实际执行的非破坏性验证

| 检查 | 结果 | 说明 |
|---|---|---|
| `node --check gateway/proxy/server.mjs` | 通过 | Node 代理语法有效 |
| `node --test gateway/proxy/test/frontend-contract.test.mjs` | 通过 | 5/5：Observability 暴露、AIQuery/metrics unwrap、流超时、全部 Proto service、全部 streaming RPC 分类 |
| `npm run build`（`frontend`） | 通过但有警告 | Vue/TypeScript 生产构建成功；主 chunk 662.65 kB，超过 Vite 500 kB 提示阈值 |
| Python AST 解析 | 通过 | 18/18 个一方 Python 文件 |
| `bash -n` | 通过 | 19/19 个 Shell 文件；仅表示语法正确，不代表运行逻辑正确 |
| JSON 解析 | 通过 | 9 个非 tsconfig JSON 文件 |
| YAML 解析 | 通过 | 2/2 个 YAML 文件 |
| C++ configure/build/CTest | **未执行** | 当前 Windows PATH 无 CMake，且无可用 WSL 发行版；仓库实际有 22 个 CTest |
| `docker compose config` / 容器 E2E | **未执行** | 当前环境无 Docker；Compose 的错误挂载路径已用文件存在性检查确认 |

> 注意：构建前端会更新被 Git 忽略的 `frontend/dist`，未修改任何受版本控制的源码或配置。

## 3. 高严重度问题

### H-01｜配置问题 / 集成 Bug：Docker 网关链路不能服务当前前端协议

**证据**

- `deploy/docker-compose.gateway.yaml:33`、`:51` 使用 `./gateway/...`；Compose 文件位于 `deploy/`，实际会解析成不存在的 `deploy/gateway/envoy.yaml` 和 `deploy/gateway/nginx.conf`。
- `gateway/envoy.yaml:54-57` 只启用 `grpc_web`，没有 JSON transcoder；`frontend/src/services/grpc-client.ts:43-72` 明确发送 `application/json`。
- `gateway/envoy.yaml:76` 将 gRPC 后端硬编码为 `172.24.214.34`，与 `deploy/docker-compose.gateway.yaml:36-37` 提供的 `host.docker.internal` 不一致。
- `deploy/docker-compose.gateway.yaml:35`、`gateway/proxy/server.mjs:18` 都占用宿主机 8081；两种代理不能并存。
- 文档仍宣称 Vite → Envoy → gRPC：`CLAUDE.md:19`。

**影响**：按文档运行 Docker 网关时，容器可能因挂载失败无法启动；即便启动，当前 JSON 前端也无法通过仅支持 gRPC-Web 的 Envoy 调用后端。

**初步建议**：明确唯一浏览器协议方案。若保留 Node JSON 代理，应在 Compose 中部署它并让 Nginx/Vite 指向该服务；若改用 gRPC-Web，应使用生成的 gRPC-Web 客户端并移除 JSON fetch。修正挂载为相对 Compose 文件真实位置的路径，并用环境变量/template 生成 Envoy cluster 地址。

### H-02｜Bug / 治理缺失：主流式查询绕过预算、记忆、成本、熔断与 trace 持久化

**证据**

- 前端主聊天只调用流式接口：`frontend/src/stores/chat.ts:62-66`。
- orchestrator 启用时，`QueryStream` 在 `server/src/ai_query_service.cpp:388-391` 直接返回。
- 被跳过的逻辑位于其后：熔断 `server/src/ai_query_service.cpp:394-399`、记忆写入 `:450-459`、成本记录 `:461-468`、trace 持久化 `:479-509`。
- unary 路径有预算检查 `server/src/ai_query_service.cpp:188-205`，流式 orchestrator 路径没有等价检查。

**影响**：真实主路径可能不受预算限制、不形成可恢复记忆、不记录成本与完整 trace，也绕过 A2A 熔断器；README 的治理和可观测性承诺在主路径上不成立。

**初步建议**：将预算、治理、记忆、成本、trace 和状态更新抽成统一 pipeline/finalizer，让 unary、普通 streaming、orchestrator streaming 共用；为成功、异常、取消三条路径增加集成测试。

### H-03｜Bug：orchestrator 流不响应取消，并忽略下游写失败

**证据**

- `server/src/multi_agent_handler.cpp:227-245` 明确丢弃 `ServerContext`，且首次 `Write` 不检查结果。
- 后续写入同样未处理失败：`server/src/multi_agent_handler.cpp:261-263`、`:300-306`、`:334-362`、`:368-380`。
- Node 代理在浏览器断开时会 cancel：`gateway/proxy/server.mjs:211-216`，但后端 handler 不读取取消状态。

**影响**：用户停止生成或浏览器断开后，规划和 Agent 调用仍可能继续，造成资源泄漏、额外 LLM 成本和幽灵任务；客户端写失败也会被记录成“completed”。

**初步建议**：在规划前、每个子任务前后和 progress callback 中检查 `context->IsCancelled()`；任何 `Write == false` 立即取消执行并返回 `CANCELLED`；把取消信号传播到 TaskExecutor/A2A/LLM 层。

### H-04｜前后端契约 Bug：complete、trace 和 activity 流事件未真正打通

**证据**

- 后端在 complete 事件字段中写 `trace_summary`：`server/src/multi_agent_handler.cpp:300-306`、`:374-380`，Proto 字段为 `proto/ai_query.proto:88`。
- 前端类型缺少该字段，却虚构 `trace_summary` 事件类型和 `trace_id`：`frontend/src/types/proto.ts:75-83`；store 只在 `case 'trace_summary'` 中解析 `event.content`：`frontend/src/stores/chat.ts:156-163`。
- Proto 有 `activity_json` 字段 `proto/ai_query.proto:89`，但一方后端没有 `set_activity_json`；前端却等待独立 `activity_json` event_type：`frontend/src/stores/chat.ts:147-154`。
- 后端已经发 complete，代理在流结束时又合成一个 complete：`gateway/proxy/server.mjs:190-194`；前端每次 complete 都追加完成活动：`frontend/src/stores/chat.ts:166-175`。
- Chat 页面渲染本地 activity 数组 `frontend/src/views/ChatView.vue:157`、`:190`，而流事件写入 Pinia store 的另一个数组 `frontend/src/stores/chat.ts:23-31`。

**影响**：trace 摘要不可见、实时活动流不显示或只显示本地 UI 动作、查询完成事件重复；监控页也难以从聊天消息取得 trace ID。

**初步建议**：以 Proto 为唯一契约源生成 TS 类型；complete 只由后端或代理一处发出；统一 `event_type` 与 payload 字段模型；ChatView 直接使用 `chatStore.activityEntries`，并增加从 gRPC event 到 SSE 到 Pinia 到 UI 的端到端契约测试。

### H-05｜功能 Bug：Token/成本始终接近零，预算计数还有未定义值风险

**证据**

- 规划调用明确以 0 token 记录：`orchestrator/src/task_planner.cpp:59-68`。
- unary 和 streaming 生产记录同样传入 `0, 0, "unknown"`：`server/src/ai_query_service.cpp:287-290`、`:465-468`。
- 成本日键只有 90,000 秒 TTL：`common/src/cost_tracker.cpp:105-108`，但 Dashboard 请求 30 天趋势：`frontend/src/views/Dashboard.vue:98-107`。
- 预算中间件不检查 `incrby` 成功与否，却读取未初始化 total：`server/src/budget_middleware.cpp:116-130`、`:134-147`、`:151-165`、`:169-184`。
- README 宣称实时 Token/成本与超额拒绝：`README.md:37`。

**影响**：Dashboard 的 Token/成本数据失真，历史趋势一天后消失；Redis 故障时预算判断可能基于未定义值，出现误拒绝或绕过。

**初步建议**：让 LLM client 返回 provider usage，按模型价格表记录真实 token；成本聚合保留至少报表窗口并设置明确保留策略；所有 Redis 操作检查返回值，失败时按配置 fail-closed/fail-open，并使用初始化值和事务/Lua 保证多级预算原子性。

### H-06｜未完成任务 / API 语义 Bug：回放与导出为占位实现却返回成功

**证据**

- replay 明确 TODO 并返回 placeholder：`orchestrator/src/replay_service.cpp:14-26`、`:38-47`。
- RPC 无视 `ReplayResult.success`，固定返回成功：`server/src/orchestration_service_impl.cpp:193-207`。
- 导出生成固定 Mermaid 和假对话：`orchestrator/src/export_service.cpp:28-47`，RPC 仍返回 `status.code=0`：`server/src/orchestration_service_impl.cpp:241-264`。

**影响**：调用方和测试会把假结果当成已完成的真实回放/导出，属于危险的“绿灯假成功”。

**初步建议**：完成 trace/query_log 与对话存储后再开放 RPC；完成前返回 `UNIMPLEMENTED` 或非零业务状态，并在前端隐藏入口/标记 preview。

### H-07｜未完成任务：分享、模板、沙箱、对比、撤销和干预均未形成闭环

**证据**

- 分享/模板只生成 UUID 或拼接 context，无持久化：`server/src/sharing_service.cpp:32-67`；Observe 直接返回“未实现”：`:18-29`。
- SandboxQuery 忽略请求并返回随机字符串：`server/src/user_experience_service.cpp:21-36`；Intervention 仅写日志并成功：`:9-18`。
- Compare 和 Undo 为 placeholder：`server/src/agent_lifecycle_service.cpp:41-49`、`:73-81`。
- 前端页面直接展示开发中：`frontend/src/views/ShareView.vue:16-21`、`frontend/src/views/TemplateMarket.vue:13-20`、`frontend/src/views/AgentSandbox.vue:45-51`、`frontend/src/views/CompareView.vue:30-44`。
- 这些入口仍出现在主导航：`frontend/src/components/layout/SideNav.vue:35-38`。

**影响**：产品导航暴露大量不可用功能；部分后端用成功码伪装实现，用户操作没有实际效果。

**初步建议**：按用户价值优先完成“分享只读链接”和“对比调用”两个最短闭环；其余使用 feature flag 隐藏。所有未实现 RPC 返回 `UNIMPLEMENTED`，不要返回随机/固定成功数据。

### H-08｜数据链路 Bug：反馈和 Agent 指标的写入端与读取端分裂

**证据**

- `SubmitFeedback` 仅写 Redis：`server/src/agent_lifecycle_service.cpp:18-33`。
- 路由反馈聚合通过外部 `psql` 读取 `agent_feedback`：`orchestrator/src/feedback_aggregator.cpp:50-61`、`:80-101`；性能指标读取 `agent_calls`：`:148-166`。
- 仓库一方生产代码没有向这两个表插入数据；Compose 也只有 Redis/Envoy/Nginx：`deploy/docker-compose.gateway.yaml:9-67`。
- PostgreSQL 不存在时，验证脚本安装恒成功 mock：`verify/scripts/helpers.sh:21-27`；mock 对 count 查询总返回正数：`verify/scripts/mock_psql.py:17-28`。

**影响**：用户反馈无法影响路由，质量权重/成功率不能由真实调用更新；验证报告可能错误显示数据库断言通过。

**初步建议**：选择唯一事实源。若使用 PostgreSQL，加入服务、migration、生产写入和真实测试 fixture；若使用 Redis，修改 aggregator 直接读 Redis。禁止缺失依赖时伪造成功，测试应 skip 或 fail 并给出明确原因。

### H-09｜安全问题：多个 RPC 存在 IDOR/跨用户访问与无角色后台

**证据**

- `GetCostReport` 只检查“已登录”，随后直接信任请求 user_id：`server/src/observability_service.cpp:179-188`。
- `GetTraceDetail` 只检查“已登录”，随后按任意 trace_id 查询：`server/src/observability_service.cpp:90-101`。
- `ExecutePlan` 直接用请求中的 user_id 加载记忆：`server/src/orchestration_service_impl.cpp:91-103`。
- `SetAutonomyLevel` 按请求 user_id/agent_id 写 Redis，无所有权和 level 范围校验：`server/src/agent_lifecycle_service.cpp:52-69`。
- `/admin` 仅要求登录，没有 admin role：`frontend/src/router/index.ts:26-29`、`:74-88`。

**影响**：普通登录用户可能读取其他用户成本/trace/记忆，或修改他人的自治级别；后台能力没有权限边界。

**初步建议**：从 AuthInterceptor 的认证上下文取得当前 user_id，默认忽略客户端传入身份；trace/context 持久化 owner 并逐次授权；增加 role/permission claims 和服务端授权拦截器，前端路由守卫仅作为 UX 辅助。

### H-10｜安全问题：Agent 注册/注销/心跳完全免鉴权

**证据**

- whitelist 包含 RegisterAgent、UnregisterAgent、Heartbeat：`server/src/auth_interceptor.cpp:61-66`。
- 注册者可提交 host/port/skills 并写入路由：`server/src/agent_service.cpp:271-318`、`:324-362`。
- 注销按调用方给定 agent_id 直接删除：`server/src/agent_service.cpp:374-400`。

**影响**：未认证调用方可注册恶意端点污染路由、注销合法 Agent 或伪造心跳，造成 SSRF 风险、请求劫持或拒绝服务。

**初步建议**：为 Agent 使用独立 mTLS/service token 身份；注册时校验允许网段、服务声明和唯一身份；注销/心跳必须证明与 Agent owner/credential 匹配。

### H-11｜安全问题：认证、TLS 和外部 HTTPS 实现不满足生产要求

**证据**

- 自定义“PBKDF2-style”实际是手写迭代 SHA-256，而不是 PBKDF2：`server/src/auth_service.cpp:261-291`；比较使用普通字符串比较：`:329-335`。
- 存储值中的 iteration 直接 `std::stoi`，没有异常保护/上限：`server/src/auth_service.cpp:294-321`。
- 登录写 token 和 TTL 不检查 Redis 返回值，却直接返回成功：`server/src/auth_service.cpp:161-181`。
- ServerConfig 虽有 SSL 字段 `common/include/agent_rpc/common/types.h:31-33`，主程序只解析端口/orchestrator/registry 等：`server/src/main.cpp:153-169`；未配置时固定 insecure：`server/src/rpc_server.cpp:294-300`、`:395-421`。
- ProfileSummarizer 关闭 TLS 证书校验：`common/src/profile_summarizer.cpp:231-239`。
- Nginx 配置内硬编码生产风格 API key：`gateway/nginx.conf:25-30`。

**影响**：密码离线破解防护弱于文档所称 PBKDF2，损坏的 Redis hash 可触发异常/CPU 放大，token 可能未正确持久化或不失效；网络凭据、LLM API key 和用户内容可能遭中间人攻击。

**初步建议**：使用 OpenSSL `PKCS5_PBKDF2_HMAC`、Argon2id 或 scrypt，并做 constant-time compare 与参数上限；检查 Redis 事务结果；给主程序增加 TLS/mTLS 参数并在非开发环境禁止 insecure；恢复证书校验；密钥从 secret/env 注入并轮换现有示例值。

### H-12｜未完成任务 / 文档不实：MCP 编译了库但未接入运行中的 RPC Server

**证据**

- 根构建加入 MCP 库：`CMakeLists.txt:57-58`，库目标为 `mcp/CMakeLists.txt:17-18`。
- `rpc_server` 和 `agent_rpc_server` 链接列表均没有 `agent_rpc_mcp`：`server/CMakeLists.txt:28-44`、`:47-62`。
- Server header 和实现明确标为预留：`server/include/agent_rpc/server/rpc_server.h:106`、`server/src/rpc_server.cpp:82`。
- 根 CMake 也没有加入 README 所列的 `mcp_server_integrated`；README 却称 MCP 完整集成：`README.md:33`、`:150-156`。

**影响**：实际 `rpc_server` 不会使用 MCP/RAG 工具链，核心产品卖点与运行态不符；相关单元测试只能证明孤立库行为。

**初步建议**：明确 MCP 运行架构，链接并初始化 MCP client/integration，提供失败降级和 E2E；若暂不启用，应在文档和 UI 中标为实验模块。

### H-13｜脚本 Bug：`run.sh` 的 stop/verify 会因 `set -e` 提前退出

**证据**

- 脚本启用 `set -euo pipefail`：`run.sh:18`。
- 第一次 `((stopped++))` 在旧值为 0 时返回 shell 状态 1：`run.sh:315`，会让 stop 在处理首个进程后退出。
- `((total_pass++))` / `((total_fail++))` 同样会在首批结果后退出：`run.sh:488`、`:491`。
- 另一个启动脚本含机器专属绝对路径：`scripts/start_backend.sh:2`。

**影响**：停止命令可能遗留进程，验证命令可能只跑第一批却中断；跨机器启动脚本直接失败。

**初步建议**：改用 `stopped=$((stopped + 1))` 等不会触发 `errexit` 的赋值；给脚本关键分支增加 Shell 集成测试；绝对路径用脚本自身目录解析。

## 4. 中严重度问题

### M-01｜配置问题：`.env.example` 与真实读取项严重漂移

**证据**

- 示例声明但代码未读取：`.env.example:22-23`、`:27`、`:34-40`、`:43`。
- 代码读取但示例未声明：客户端 `client/src/main.cpp:106-112`，PostgreSQL `orchestrator/src/feedback_aggregator.cpp:22`，前端 `frontend/src/services/grpc-client.ts:25`，预算 `server/src/budget_middleware.cpp:93-97`，MCP/RAG `mcp/src/mcp_agent_integration.cpp:545-614`。
- LLM 默认值不一致：`.env.example:13-18` 使用 flash/pro 混合，而 `README.md:304-305`、`CLAUDE.md:64-65` 描述另一个默认值/URL。

**影响**：新环境照示例配置仍可能连接错误服务、使用意外模型或默默采用硬编码默认值。

**初步建议**：建立单一 typed config schema，启动时打印脱敏后的有效配置并校验未知/缺失项；从 schema 自动生成 `.env.example` 和文档表格。

### M-02｜构建配置问题：根 CMake 最低版本和依赖策略不一致

**证据**

- 根项目声称 CMake 3.15：`CMakeLists.txt:1`，子模块要求 3.20：`mcp/CMakeLists.txt:1`。
- 根构建无条件启用测试并要求 GTest：`CMakeLists.txt:6-15`，无条件加入 tests：`:61`；tests 又要求 RapidCheck：`tests/CMakeLists.txt:61`。
- 全局 flags 无条件 `-O2`，Debug 只追加 `-g -DDEBUG`：`CMakeLists.txt:27-31`。

**影响**：3.15–3.19 用户配置必失败；生产只构建服务也必须安装测试依赖；Debug 构建仍优化，影响调试。

**初步建议**：根最低版本提升到全局实际要求；使用 `include(CTest)`/`BUILD_TESTING`；依赖使用 imported targets；用 per-config `target_compile_options`，避免全局 `CMAKE_CXX_FLAGS`。

### M-03｜启动脚本问题：setup 检查不覆盖实际构建/验收依赖

**证据**

- `run.sh setup` 只检查基础工具和 grpc++/protobuf/jsoncpp/hiredis：`run.sh:341-372`。
- 实际 CMake 还要求 CURL、OpenSSL、GTest：`CMakeLists.txt:15`、`:35-39`，tests 要 RapidCheck：`tests/CMakeLists.txt:61`。
- E2E/辅助链路还依赖 Node/npm、Python、grpcurl、psql，但 setup 未统一检查；Node/npm 仅在特定前端命令局部检查：`run.sh:404-444`。

**影响**：setup 可能显示环境正常，随后 build/test/verify 才失败，增加首次使用成本。

**初步建议**：按 `build`、`frontend`、`e2e`、`docker` profile 输出依赖矩阵；提供 `doctor` 命令和明确的可选/必选状态。

### M-04｜脚本/进程管理问题：start-all 启动的 Node 代理无法可靠管理

**证据**

- 代理通过无 PID 文件的后台 subshell 启动：`run.sh:603-611`。
- stop 只遍历 `pids/*.pid`：`run.sh:303-318`。
- start-all 不检查 proxy `node_modules`，也不等待 HTTP health，就直接打印成功：`run.sh:603-630`。

**影响**：干净 clone 可能代理启动失败却显示成功；正常停止会遗留 8081 进程并导致下次启动端口冲突。

**初步建议**：与 rpc_server 一样记录 PID/log，启动后做 `/health` 或端口探测；缺依赖时明确失败或执行受控安装；退出/失败时统一 cleanup。

### M-05｜测试质量问题：Python E2E 把超时、任意 code 和占位 status 当成功

**证据**

- 无鉴权测试只要输出包含任意 `code` 就通过：`tests/e2e/e2e_full_test.py:47-64`。
- streaming timeout 被计为 pass：`tests/e2e/e2e_full_test.py:145-162`。
- 多个 RPC 只检查存在 `status`，包括 Compare/Undo/Replay/Export 等：`tests/e2e/e2e_full_test.py:124-141`、`:188-218`。
- replay batch 只核对回显 trace/request 字符串：`verify/scripts/verify-batch5.sh:74-77`。

**影响**：未实现、挂死或错误状态也能产生绿色验收结果，削弱测试作为发布门禁的意义。

**初步建议**：断言进程退出码、gRPC code、业务 `status.code`、schema 和真实副作用；超时必须 fail；对未实现功能标记 expected-fail/skip，不得当 pass。

### M-06｜前端未完成任务：Admin 控件只改本地状态，未调用后端

**证据**

- Admin 仅导入 agents store/types：`frontend/src/views/AdminView.vue:266-269`。
- 预算初始值固定为 0：`frontend/src/views/AdminView.vue:316-320`。
- replay 返回“开发中”本地对象：`frontend/src/views/AdminView.vue:349-359`。
- Cron、触发、删除、Canary 都只改本地数组：`frontend/src/views/AdminView.vue:361-405`。

**影响**：用户看到完整管理 UI，但操作不影响后端；刷新后状态丢失。

**初步建议**：先定义 Admin API/RPC、权限和持久化，再逐项接入；完成前禁用入口并明确 demo，不应让按钮表现为成功。

### M-07｜前端 Bug：Agent 选择和反馈只停留在本地 UI

**证据**

- selector 默认永不显示，candidates 未填充：`frontend/src/views/ChatView.vue:184-187`。
- 选择只写本地 ID：`frontend/src/views/ChatView.vue:222-225`；发送仍只传 text：`:204-214`。
- feedback 只更新 message 字段：`frontend/src/stores/chat.ts:188-193`，未调用已有 `SubmitFeedback` RPC。

**影响**：用户以为可指定 Agent/提交反馈，实际路由和反馈学习系统均不受影响。

**初步建议**：为 sendQuestion 增加显式 agent preference；从 FindAgents 填充候选；反馈按钮调用 SubmitFeedback，并处理 loading/retry/idempotency。

### M-08｜可用性 Bug：Agent 健康状态与监控熔断状态由前端臆测

**证据**

- store 将 GetAgents 返回的所有 Agent 强制标记 healthy：`frontend/src/stores/agents.ts:66-75`。
- Monitor 按 success_rate 阈值推断 CLOSED/HALF_OPEN/OPEN，而非读取真实 circuit breaker：`frontend/src/views/Monitor.vue:270-295`。
- 监控 trace 只从当前浏览器聊天消息中取最近 10 个 ID：`frontend/src/views/Monitor.vue:199-218`。

**影响**：离线 Agent 可能显示健康，熔断面板可能与服务端真实状态不同；监控不是系统级视图，仅覆盖本地会话。

**初步建议**：在 Proto 中返回 heartbeat/health/deployment/circuit 状态；Observability 提供 trace 列表和时间窗口查询；UI 明确区分“推断值”与“服务端值”。

### M-09｜代理/前端错误处理 Bug：流式 401 不登出，gRPC 错误大量映射为 HTTP 500

**证据**

- unary 401 会回调 logout：`frontend/src/services/grpc-client.ts:65-69`。
- QueryStream 的非 2xx 只抛通用错误，没有触发 `_onUnauthorized`：`frontend/src/services/grpc-client.ts:138-141`、`:185-197`。
- Node 代理只映射 UNAUTHENTICATED/NOT_FOUND/ALREADY_EXISTS，其余都为 500：`gateway/proxy/server.mjs:315-321`。

**影响**：token 过期时主聊天只显示流错误，页面仍保持假登录；参数错误、限额、权限错误都表现为“服务器故障”。

**初步建议**：统一 unary/stream fetch wrapper；对 401 调用 logout；完整映射 INVALID_ARGUMENT、PERMISSION_DENIED、RESOURCE_EXHAUSTED、UNAVAILABLE、DEADLINE_EXCEEDED 等，并向前端传递结构化错误码。

### M-10｜契约问题：手写 TypeScript 类型已经与 Proto 漂移

**证据**

- Proto Artifact 为 `bytes data` + metadata：`proto/ai_query.proto:50-55`；TS 却定义 `content: string` 且无 metadata：`frontend/src/types/proto.ts:69-73`。
- AIStreamEvent 字段/事件模型漂移见 `frontend/src/types/proto.ts:75-83` 与 `proto/ai_query.proto:72-90`。
- TS `FindAgentsRequest.required_skills` 并不存在于对应 Proto：`frontend/src/types/proto.ts:99-105`。

**影响**：二进制 artifact、trace/activity 字段和搜索请求可能在运行期静默丢失或解析错误，编译器无法保护真实 wire contract。

**初步建议**：从 Proto 自动生成 Web 类型/client；若必须手写，新增 descriptor 驱动的字段级 contract test，而不只检查 service/method 是否存在。

### M-11｜未完成任务：底层框架、异步任务、Redis task store 和 etcd 仍为 stub/fallback

**证据**

- 通用 RPC server 启动仍 TODO：`common/src/rpc_framework.cpp:27-29`。
- A2A async 实际同步：`a2a_adapter/src/a2a_adapter.cpp:252-253`。
- Redis task store 未实现，回退内存：`a2a_adapter/src/task_manager_wrapper.cpp:101-102`。
- Etcd registry 明确未实现：`registry/src/service_registry.cpp:466-475`。
- 双向实时通信仅 echo，不路由：`server/src/agent_service.cpp:487-501`。

**影响**：多实例、进程重启、真正异步和实时通信场景与接口命名/文档预期不符。

**初步建议**：对外能力表中标注 supported/experimental；优先完成持久 task store 与实时路由，或删除/禁用未使用的抽象以减少误解。

### M-12｜性能 Bug：所谓异步摘要会在 future 析构时阻塞，并持锁执行网络调用

**证据**

- 丢弃 `std::async` 返回的 future：`server/src/query_helpers.cpp:176-190`。
- lambda 在 `memory_llm_mutex` 锁内调用 LLM：`server/src/query_helpers.cpp:178-188`。

**影响**：临时 future 的析构可能等待任务结束，使请求路径同步阻塞；全局 mutex 还会串行化其他上下文的 LLM 摘要。

**初步建议**：使用受管理线程池/后台队列保存 future 生命周期；锁只保护集合增删，不覆盖网络调用；增加超时和 shutdown drain。

### M-13｜配置/安全问题：Envoy/Nginx 脚本和健康检查不可配置或语义错误

**证据**

- `gateway/envoy.sh:15-16` 读取 BACKEND_ADDR/PROXY_PORT，但最终只执行固定配置文件：`gateway/envoy.sh:37-43`，没有替换 YAML。
- Nginx `/health` 被 grpc_pass 到并非标准 gRPC method path 的 `/health`：`gateway/nginx.conf:108-112`、`:143-145`。
- Nginx 同时配置 Envoy upstream 和直连 gRPC，增加两套路径：`gateway/nginx.conf:35-45`、`:94-128`。

**影响**：运维设置看似生效实际无效；健康探针可能持续失败；双链路行为不一致。

**初步建议**：启动前生成配置或使用 Envoy bootstrap template；健康检查调用真实 `HealthService/Check` 或单独 HTTP health adapter；选择一个明确的生产 upstream。

### M-14｜脚本/示例不一致：注册脚本把多个 Agent 全部指向同一 mock 端口

**证据**

- `scripts/register_agents.sh:13-29` 将 mock/math/translator/echo 均注册到 5100。
- 示例文档为不同 Agent 声明不同端口：`examples/README.md:18-31`。

**影响**：演示时所有能力实际由同一个 mock 服务响应，容易把“注册成功”误当成多 Agent 服务已启动。

**初步建议**：区分 mock fixture 注册与真实 examples 注册；先探测对应端口健康，再注册；脚本名称和输出明确标注 mock。

### M-15｜静默异常问题：多处 catch 空块隐藏数据损坏和降级原因

**证据（代表性且需优先处理）**

- A2A activity 写入/事件解析失败被吞：`a2a_adapter/src/a2a_adapter.cpp:409-411`、`:443-445`、`:795-797`、`:829-831`。
- Cron 解析异常为空：`orchestrator/src/cron_scheduler.cpp:94`、`:144`。
- BackgroundScheduler 注释称“log and swallow”但没有日志：`common/include/agent_rpc/common/background_scheduler.h:137-141`。
- 前端流解析忽略残缺/非法 JSON：`frontend/src/services/grpc-client.ts:165-181`。

**影响**：活动流、定时任务和流式事件出现问题时只能看到缺数据，无法区分格式错误、Redis 故障还是协议漂移。

**初步建议**：对预期降级做限流 warn/metric，对不可恢复数据错误返回结构化错误；前端记录帧序号和截断后的原始片段，避免泄露敏感内容。

## 5. 低严重度问题

### L-01｜性能优化：前端主 chunk 偏大

**证据**：本次 `npm run build` 生成 662.65 kB chunk，Vite 给出超过 500 kB 的警告；依赖集中在 `frontend/package.json:11-22`。

**影响**：首屏下载、解析和低端设备交互时间可能偏高。

**初步建议**：用 bundle visualizer 定位 ECharts/Mermaid/Iconify 等，配置 manualChunks，并确认路由级懒加载没有被聚合回主包。

### L-02｜项目卫生：package-lock 忽略规则与实际跟踪状态冲突

**证据**：`.gitignore:32` 忽略 `package-lock.json`，但 `frontend/package-lock.json` 和 `gateway/proxy/package-lock.json` 已被 Git 跟踪。

**影响**：维护者可能误以为 lockfile 不应提交，造成依赖策略混乱。

**初步建议**：明确应用仓库必须提交 lockfile，删除该 ignore；CI 使用 `npm ci`。

### L-03｜依赖分类：运行依赖中包含 TypeScript 类型包

**证据**：`frontend/package.json:13` 将 `@types/file-saver` 放在 dependencies，而实际 devDependencies 从 `:24` 开始。

**影响**：生产安装携带不必要包，依赖语义不清。

**初步建议**：移动到 devDependencies，并检查其他 `@types/*` 分类。

### L-04｜测试/开发脚本缺口：前端和代理 package scripts 不完整

**证据**：前端只有 dev/build/preview：`frontend/package.json:6-9`；代理只有 start：`gateway/proxy/package.json:7-9`，尽管存在 `gateway/proxy/test/frontend-contract.test.mjs`。

**影响**：开发者不知道标准 lint/typecheck/test 命令，CI 容易遗漏代理测试。

**初步建议**：增加 `typecheck`、`test`、`lint`、`format:check`，根脚本统一编排；代理增加 `npm test`。

### L-05｜版本库卫生：跟踪了运行时 PID 文件

**证据**：`verify/mock-agent/pid.txt:1` 保存具体进程号；`.gitignore` 只覆盖 `*.pid`，没有覆盖 `pid.txt`。

**影响**：陈旧 PID 会误导调试，并产生无意义 diff。

**初步建议**：运行时 PID 统一放入 ignored `pids/`，不要提交实例值。

### L-06｜文档/注释漂移与缺失文档

**证据**

- README/CLAUDE 仍称 17 套测试：`README.md:12`、`:186`、`:316`，`CLAUDE.md:12`、`:44`；实际有 22 个 `add_test`（例如首尾为 `tests/CMakeLists.txt:54`、`:429`）。
- `CLAUDE.md:5`、`:25` 引用不存在的 `docs/NexusAI-project-introduction.md`；`run.sh:511` 引用不存在的 verification checklist。
- README 的 MIT badge/声明指向缺失 LICENSE：`README.md:11`、`:372`。
- `server/src/main.cpp:298` 仍称 ProfileSummarizer 为 no-op，但实现已包含真实 HTTP 调用：`common/src/profile_summarizer.cpp:225-245`。

**影响**：用户无法找到验收文档/许可证，测试规模和模块状态被错误描述。

**初步建议**：恢复或删除失效链接；补 LICENSE；从 CTest 自动生成测试数；将 capability matrix 作为发布检查项。

## 6. 配置文件专项诊断

| 配置域 | 冲突/重复/不一致 | 证据 | 建议 |
|---|---|---|---|
| Docker Compose | 相对挂载路径错误 | `deploy/docker-compose.gateway.yaml:33`、`:51` | 修正路径并在 CI 运行 `docker compose config` + smoke test |
| 网关协议 | JSON Proxy 与 gRPC-Web Envoy 两套互不兼容方案 | `frontend/src/services/grpc-client.ts:53-63`；`gateway/envoy.yaml:54-57` | 选定唯一浏览器协议 |
| 端口 | Envoy 与 Node Proxy 同占 8081 | `deploy/docker-compose.gateway.yaml:35`；`gateway/proxy/server.mjs:18` | 分离 dev/prod profile 或删除一套 |
| 后端地址 | Envoy IP 硬编码，Compose 却配置 host gateway | `gateway/envoy.yaml:76`；`deploy/docker-compose.gateway.yaml:36-37` | template/env 注入 DNS 名称 |
| Nginx | Envoy 和直连 gRPC 双 upstream，健康 path 可疑 | `gateway/nginx.conf:35-45`、`:94-145` | 简化为单 upstream，使用真实健康 RPC |
| CMake | 根最低 3.15，MCP 最低 3.20 | `CMakeLists.txt:1`；`mcp/CMakeLists.txt:1` | 统一最低版本 |
| CMake 测试 | 生产构建强制 GTest/RapidCheck | `CMakeLists.txt:7-15`、`:61`；`tests/CMakeLists.txt:61` | `BUILD_TESTING` 条件化 |
| 环境变量 | 声明项和消费项双向漂移 | `.env.example:22-43`；`server/src/budget_middleware.cpp:93-97`；`mcp/src/mcp_agent_integration.cpp:545-614` | typed config + 自动生成文档 |
| Node 依赖 | lockfile 被跟踪但全局 ignore | `.gitignore:32` | 明确提交 lockfile，CI 使用 `npm ci` |
| 数据库 | SQL/psql 功能无 Compose 服务、无启动 migration | `orchestrator/src/feedback_aggregator.cpp:50-61`；`deploy/docker-compose.gateway.yaml:9-67` | 增加 PostgreSQL/migration 或统一改用 Redis |

## 7. 启动、构建与测试脚本专项诊断

| 项目 | 当前状态 | 主要问题 | 证据 |
|---|---|---|---|
| `run.sh setup` | 不完整 | 漏掉多项真实依赖 | `run.sh:341-372`；`CMakeLists.txt:15`、`:35-39` |
| `run.sh build` | 逻辑冗余 | `set -e` 下 `$?` 的失败分支通常不可达 | `run.sh:18`、`:81-92` |
| `run.sh stop` | 有确定 Bug | 第一次自增可触发退出 | `run.sh:303-318` |
| `run.sh verify` | 有确定 Bug | 首次 pass/fail 自增可触发退出 | `run.sh:471-497` |
| `run.sh start-all` | 生命周期不完整 | Node Proxy 无 PID、依赖/健康检查 | `run.sh:603-630` |
| `scripts/start_backend.sh` | 不可移植 | 写死另一台机器路径 | `scripts/start_backend.sh:2` |
| `scripts/register_agents.sh` | 演示失真 | 多个 Agent 指向同一个 mock | `scripts/register_agents.sh:13-29` |
| CTest | 规模可观但本次未运行 | 无 CMake 环境；文档数量错误 | `tests/CMakeLists.txt:54-429` |
| Python E2E | 断言过松 | timeout/status/任意 code 可通过 | `tests/e2e/e2e_full_test.py:47-64`、`:145-162` |
| Batch verify | 会伪造 PG 成功 | 缺 psql 时安装恒成功函数 | `verify/scripts/helpers.sh:21-27`；`verify/scripts/mock_psql.py:17-28` |

### 推荐的统一命令结构

1. `./run.sh doctor [build|frontend|e2e|docker]`：只诊断依赖，不修改系统。
2. `./run.sh build --with-tests/--without-tests`：显式控制测试依赖。
3. `./run.sh test unit|integration|contract|e2e`：严格退出码，禁止 mock 基础设施伪造通过。
4. `./run.sh start dev|docker`：dev 使用 Node JSON proxy；docker 使用同一种协议实现。
5. `./run.sh stop`：所有进程统一 PID/log/health/cleanup。

## 8. 前后端功能闭环矩阵

| 能力 | 后端 | 前端 | 闭环判断 | 关键证据 |
|---|---|---|---|---|
| 登录/注册 | 已有，但密码/TLS/Redis 错误处理不足 | 已接入 | 部分闭环 | `server/src/auth_service.cpp:161-181`、`:261-335` |
| 主聊天流 | 可编排，但绕过治理且取消无效 | 主路径已接入 | **高风险部分闭环** | `server/src/ai_query_service.cpp:388-391`；`frontend/src/stores/chat.ts:62-66` |
| Agent 列表/发现 | 已实现基础注册和查询 | 多页面调用 | 部分闭环，健康语义失真 | `server/src/agent_service.cpp:271-362`；`frontend/src/stores/agents.ts:66-75` |
| Agent 消息/实时通信 | unary queue 有实现；bidi 仅 echo | 无主要 UI | 未闭环 | `server/src/agent_service.cpp:487-501` |
| 记忆 | unary/普通流有写入 | 间接使用 | orchestrator 主流未闭环 | `server/src/ai_query_service.cpp:450-459` |
| Trace | Redis detail RPC 有实现 | Dashboard/Monitor 尝试读取 | stream contract 断裂 | `server/src/observability_service.cpp:79-160`；`frontend/src/stores/chat.ts:156-163` |
| Token/成本/预算 | 有骨架，usage 为 0，流绕过预算 | Dashboard 已接入 | 数据不可信 | `orchestrator/src/task_planner.cpp:59-68`；`frontend/src/views/Dashboard.vue:72-107` |
| 反馈驱动路由 | 写 Redis，聚合读 PG | feedback 仅本地 | 未闭环 | `server/src/agent_lifecycle_service.cpp:18-33`；`orchestrator/src/feedback_aggregator.cpp:80-101` |
| Replay/Export | placeholder/fixed content | Admin replay 本地占位；Chat 有本地导出 | 未闭环 | `orchestrator/src/replay_service.cpp:14-47`；`orchestrator/src/export_service.cpp:28-47` |
| 分享/模板 | UUID/拼接返回，无存储 | 页面开发中 | 未闭环 | `server/src/sharing_service.cpp:32-67`；`frontend/src/views/ShareView.vue:16-21` |
| Sandbox/Compare | random/empty placeholder | 页面开发中 | 未闭环 | `server/src/user_experience_service.cpp:21-36`；`frontend/src/views/CompareView.vue:30-44` |
| Intervention/Undo/Autonomy | 日志/placeholder/Redis 简单写 | Admin 本地控件 | 未闭环且有越权风险 | `server/src/user_experience_service.cpp:9-18`；`server/src/agent_lifecycle_service.cpp:52-81` |
| MCP/RAG | 独立库存在，Server 未链接 | 无明确工具 UI | 未闭环 | `server/CMakeLists.txt:28-62`；`server/include/agent_rpc/server/rpc_server.h:106` |
| Docker 部署 | Redis/Envoy/Nginx 配置存在 | 前端协议为 JSON | 不可用 | `deploy/docker-compose.gateway.yaml:9-67`；`frontend/src/services/grpc-client.ts:53-63` |

## 9. TODO、FIXME、空 catch 与诊断日志清单

### 9.1 一方代码中的显式 TODO / 未实现标记

| 文件与行号 | 内容 | 归类 |
|---|---|---|
| `common/src/rpc_framework.cpp:27-29` | 完整 server startup 未实现 | 未完成任务 |
| `a2a_adapter/src/a2a_adapter.cpp:252-253` | true async/thread pool 未实现 | 未完成任务 |
| `a2a_adapter/src/task_manager_wrapper.cpp:101-102` | Redis task store 未实现 | 未完成任务 |
| `orchestrator/src/replay_service.cpp:14-26`、`:38-47` | exact/route replay 未实现 | 未完成任务 |
| `server/CMakeLists.txt:56` | gRPC reflection imported target 待规范化 | 配置债务 |
| `frontend/src/views/AgentTopology.vue:45` | 待复用 agents store | 重复/维护债务 |
| `registry/src/service_registry.cpp:466-475` | etcd registry 未实现 | 未完成任务 |
| `server/include/agent_rpc/server/rpc_server.h:106` | MCP client 预留待实现 | 未完成任务 |
| `server/src/agent_service.cpp:498` | 双向通信暂为 echo | 未完成任务 |
| `server/src/agent_lifecycle_service.cpp:46-48`、`:78-80` | compare/undo placeholder | 未完成任务 |
| `server/src/sharing_service.cpp:22-27` | observe session 未实现 | 未完成任务 |

### 9.2 空 catch / 静默降级

除 M-15 的高价值项外，还发现以下静默 fallback；多数不会立刻导致崩溃，但会削弱问题定位：

- `orchestrator/src/agent_router.cpp:1013-1015`：embedding 失败静默回退。
- `client/src/rpc_client.cpp:687-688`：负载均衡选择异常静默回退。
- `server/src/ai_query_service.cpp:640`、`:647`：metadata 数值解析失败无日志。
- `common/src/profile_summarizer.cpp:54-56`、`:83-85`：JSON/时间解析异常静默跳过。
- `common/src/memory_service.cpp:142-144`：记忆 JSON 解析异常静默跳过。
- `server/src/agent_service.cpp:347-359`：AgentCard 解析失败只退回技能名，无 metric。
- `mcp/src/mcp_client.cpp:741-743`、`mcp/src/mcp_agent_integration.cpp:575-577`、`:602`、`:610`：配置解析异常回退默认值。
- `frontend/src/stores/agents.ts:62-64`、`frontend/src/stores/auth.ts:33-35`、`frontend/src/stores/chat.ts:151-163`：解析失败静默回退/忽略。

### 9.3 `console.error` / `console.warn` 复核结论

- `gateway/proxy/server.mjs:201`、`:317` 的 `console.error` 用于记录真实 gRPC/stream 错误，本身不是 Bug；问题在于上游错误映射不完整（M-09）。
- 前端多处 `console.warn` 是降级路径，如 `frontend/src/views/Monitor.vue:301-308`、`frontend/src/views/Dashboard.vue:140-173`。Dashboard/Monitor 会回空数据而非伪造漂亮数据，这一点是合理的；仍建议增加可见的失败原因、重试和 request ID。
- `frontend/src/views/CompareView.vue:60-66`、`frontend/src/views/AgentSandbox.vue:81-95` 仅在 console 报失败，页面缺少 retry/error state，属于可用性优化项。

## 10. 按优先级的修复路线图

### P0：先让主路径“真实、可控、可验证”

1. 修复 H-02/H-03/H-04：统一 Query/QueryStream pipeline，打通取消、预算、记忆、成本、trace、activity 和 complete 契约。
2. 修复 H-01：确定唯一浏览器网关协议并使 Docker/本地启动一致。
3. 修复 H-09/H-10/H-11：身份从认证上下文注入、Agent 服务鉴权、启用 TLS/证书校验、替换密码派生、移除硬编码 key。
4. 修复 H-13/M-04：保证 start/stop/verify 的退出码、PID、health 和 cleanup 可信。
5. 删除测试中的恒成功 PostgreSQL mock；让未实现功能返回 `UNIMPLEMENTED`。

**P0 验收条件**：真实登录用户通过前端完成一次流式查询；预算被扣减、记忆可读取、trace 可查询、token/cost 非零、断开后后端停止；Docker 和本地 dev 各有一条可重复 E2E；越权测试全部拒绝。

### P1：完成最有用户价值的功能闭环

1. 统一反馈数据源，并让 UI feedback 真正影响路由质量。
2. 完成分享只读链接（owner、权限、TTL）和真实 conversation export。
3. 完成 Agent compare 或从导航隐藏；Agent selector 传入真实 routing preference。
4. Observability 增加系统级 trace 列表、真实 breaker/heartbeat/health 状态。
5. 将 MCP 链接到运行态并增加工具调用 E2E，或明确降级为实验模块。

### P2：工程化与体验优化

1. typed config/schema、生成式 Proto Web client、BUILD_TESTING、统一依赖 doctor。
2. 前端 lint/typecheck/unit/component tests，Node 代理标准 `npm test`。
3. 拆分大 chunk，完善页面 error/empty/retry 状态。
4. 恢复项目介绍/验收文档和 LICENSE，自动同步 CTest 数量/capability matrix。
5. 为可接受的 fallback 增加结构化日志、指标和采样，清理静默 catch 与过时注释。

## 11. 推荐新增的验收门禁

- **契约门禁**：Proto descriptor 与 TS 字段级一致；每种 streaming event 有 producer、proxy、consumer 三段测试。
- **主路径 E2E**：登录 → QueryStream → Agent/A2A → complete → memory/trace/cost/budget 副作用断言。
- **取消 E2E**：浏览器 abort 后，gRPC context、TaskExecutor 和下游 HTTP/LLM 均在限定时间停止。
- **权限测试**：用户 A 不能读用户 B 的 cost/trace/context，普通用户不能访问 admin RPC，匿名者不能注册/注销 Agent。
- **部署 smoke**：`docker compose config`、容器 health、前端 JSON/gRPC-Web 实际请求、无硬编码宿主 IP。
- **真实性规则**：缺失 PostgreSQL/Redis/LLM 等依赖时只允许 fail 或明确 skip，不允许 mock 基础设施返回“存在记录”。
- **未实现规则**：placeholder RPC 必须返回 `UNIMPLEMENTED`，不得 `status.code=0`。

## 12. 审计限制

- 本报告完成了全仓一方代码和配置的静态扫描，并执行了可用的 Node、前端、Python、Shell、JSON、YAML 验证。
- 因当前环境没有 Docker、CMake 和可用 WSL 发行版，无法实际编译 C++、运行 22 个 CTest、启动 Redis/gRPC/Agent 全链路或验证容器网络；这些项目在本报告中均明确标为“未验证”，没有用静态推断冒充运行结果。
- 未提供真实 LLM key、PostgreSQL 和生产证书，因此没有发起真实外部 LLM、成本计费或 TLS 验证。
- 行号基于 2026-08-04 审计时工作区版本；后续修改后应重新生成引用。

---

**最终判断**：项目当前最重要的不是继续增加页面或 RPC 数量，而是把现有主聊天流、网关、认证授权、成本治理和可观测性收敛成一条真实可验收的链路。完成 P0 后，项目才适合把“完整多 Agent 平台”作为可交付能力；在此之前，应把占位页面/RPC 明确标为实验或隐藏，避免功能状态和测试结果继续失真。
