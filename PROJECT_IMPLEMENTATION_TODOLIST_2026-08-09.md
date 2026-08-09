# NexusAI 未完成部分实施交接 TODO（2026-08-09）

> **给实现 Agent：** 按 PR 顺序执行；每个 PR 先写失败测试、只修改下列“文件所有权”范围、在 WSL 复验后再进入下一项。不要把未实现功能伪装为成功。

**目标：** 把现有的本地 NexusAI 收敛为一条真实可验证的链路：登录用户发起 Query / QueryStream，服务端从认证上下文取得 owner，PostgreSQL 持久化查询、消息、trace、成本和预算，前端显示真实结果。

## 1. 项目背景与当前基线

技术栈：C++20 / gRPC / libpqxx / PostgreSQL 16 / Redis 7 / Vue 3 + Pinia / Node JSON-SSE Proxy / Docker Compose。浏览器唯一支持路径为：

```text
Vue 或 Nginx :8080 → Node JSON/SSE Proxy :8081 → gRPC :50051 → A2A / Orchestrator → Agent
                                                └→ PostgreSQL（业务事实）
                                                └→ Redis（缓存、在线状态、限流、短锁）
```

当前分支为 `codex/handoff-postgres-foundation-20260808`，实际 HEAD 是 `54bb550`；交接文档中记录的 `b2c965b` 已过时。

已经存在且可复用：

- `PostgresStore` 十连接池、启动 migration、PostgreSQL 用户/opaque session/scrypt 认证。
- `V011__durable_domain.sql` 的 conversations、messages、query_logs、traces、token ledger、feedback、分享/模板/受控工作流/agent registry 表。
- `QueryDomainRepository` 的创建和 owner-scoped 查询；`PostgresBudgetRepository` 的四层原子预算保留与 request-id 幂等。
- Node JSON/SSE Proxy、前端登录页和基础聊天 UI。

已复核的红灯：`frontend npm run typecheck` 通过；`gateway/proxy npm test` 为 **30/33 通过**。3 个失败只是测试仍要求已删除的 `migrate` Compose 服务、仍断言 9 条 migration，不是应把迁移架构回滚的理由。

## 2. 全局硬约束

- **owner 不可由请求体决定。** 所有受保护 RPC 使用 `AuthInterceptor::currentUserId()`；忽略 `request.user_id`、`SetAutonomyLevelRequest.user_id`、`GetCostReportRequest.user_id` 等客户端值。
- **PostgreSQL 是持久事实源。** Redis 只能缓存、心跳、限流或短锁；不得成为消息、trace、成本、预算、反馈、路由质量或工作流结果的唯一写入处。
- **只追加 migration。** 已执行的 V001–V012 不编辑；新增 schema 使用 `V013__...sql` 起的新文件。所有 SQL 使用 libpqxx 参数绑定，禁止 `popen("psql ...")`。
- **状态必须真实。** 失败/取消/预算拒绝都更新 query log 与 trace；无数据返回空态或真实 gRPC 错误，禁止 `status.code=0` 的占位回复、随机 UUID 结果或假数据。
- **本地目标边界：** 不实现 TLS/mTLS、动态 RBAC、MCP、etcd、Cron、Canary、灰度和分布式事务。Cron/Canary 必须从 UI、后台调度和文档删除或明确禁用。
- **平台：** C++ 配置、编译、CTest 在 WSL2 Linux 文件系统；前端可在 Windows 运行；Docker Compose 使用服务 DNS，不能写死容器 IP。
- **工作区纪律：** 不删除/提交 `.superpowers/`、`TDD.md`、历史 worktree 或各 worktree 的 `TASK_REPORT.md`；不 push；每项在独立 worktree 完成。

## 3. 实施顺序

### PR-0：先恢复可信基线（小而必须）

**文件：**

- 验证/合入候选：`common/include/agent_rpc/common/query_domain_repository.h`、`common/src/query_domain_repository.cpp`、`tests/test_query_domain_repository_contract.cpp`
- 修改：`gateway/proxy/test/platform-contract.test.mjs`

- [ ] 在专用 WSL clone 验证候选 `70ffaa6`、`11274b8`：

  ```bash
  git fetch origin codex/luna-pr-c-query-domain-updates
  git checkout -B review-query-domain-updates origin/codex/luna-pr-c-query-domain-updates
  cmake --build build-pg-budget-review --target test_query_domain_repository_contract -j2
  cd build-pg-budget-review
  ctest --output-on-failure -R '^QueryDomainRepositoryContractTest$'
  ```

  候选提供 owner-scoped `updateQueryLog` / `updateTrace`，不存在行或跨 owner 必须返回 `false`，绝不能 upsert。通过后才在主分支 `git cherry-pick 70ffaa6 11274b8` 并执行 `git diff --check HEAD~2..HEAD`。

- [ ] 更新 Node 平台契约：Compose 应有 `postgres/redis/rpc-server/proxy/frontend`，不应要求 `migrate` 服务；migration 数量改为当前 12 条，并断言 `server/src/main.cpp` 在监听前迁移。

- [ ] Windows 验收：

  ```powershell
  cd gateway/proxy; npm test
  cd ../../frontend; npm run typecheck
  ```

  目标：Node 33/33、前端 typecheck 均通过。

### PR-C2：Query / QueryStream durable pipeline（最高优先级）

**文件所有权：**

- 修改：`server/include/agent_rpc/server/ai_query_service.h`、`server/src/ai_query_service.cpp`、`server/include/agent_rpc/server/rpc_server.h`、`server/src/rpc_server.cpp`、`server/src/multi_agent_handler.cpp`、`gateway/proxy/server.mjs`
- 修改/新增 repository：`common/include/agent_rpc/common/query_domain_repository.h`、`common/src/query_domain_repository.cpp`
- 测试：新增 `tests/test_durable_query_pipeline.cpp`、`gateway/proxy/test/query-stream-contract.test.mjs`，更新 `tests/CMakeLists.txt`

**依赖注入：** `RpcServer` 拥有唯一的 `PostgresStore`、`QueryDomainRepository`、`PostgresBudgetRepository`；服务仅保存非 owning 引用。建议初始化签名：

```cpp
bool AIQueryServiceImpl::initialize(
    const common::RpcConfig& config,
    const a2a_adapter::A2AConfig& a2a_config,
    common::RedisClient* redis,
    common::PostgresStore& store,
    common::QueryDomainRepository& domain,
    common::PostgresBudgetRepository& budget);
```

`RpcServer::initialize()` 必须先构造三个 PG 对象再初始化 `AIQueryServiceImpl`。初始化失败时不注册 Query 服务，不允许静默降级。

**每次 Query 的固定顺序：**

1. `owner_id = AuthInterceptor::currentUserId()`；为空直接 `UNAUTHENTICATED`。无条件执行 `enriched_request.set_user_id(owner_id)`。
2. 生成不可重复的 `request_id`；若 `context_id` 为空，生成并回填响应。通过 repository 确认/创建同 owner 的 conversation。
3. 创建 `query_logs(status="running")` 与 `traces(status="running")`。route/plan/spans 用 JSON，不能把用户文本拼进 SQL。
4. 调用 `PostgresBudgetRepository::reserve(owner, context, request_id, estimated_tokens, limits)`；拒绝时将两个记录更新为 `rejected` 后返回 `RESOURCE_EXHAUSTED`。删除 Query 路径的 `BudgetMiddleware::checkAndDeduct`。
5. 从 PG messages/memory summary 组装 SystemContext；Redis MemoryService 只可作为加速缓存。执行 A2A 或 multi-agent 分支。
6. 一个统一的 `finalize(status, response, error, spans)` 完成：写 agent 消息、`updateQueryLog`、`updateTrace`、token ledger，并记录完成/失败/取消。无论同步、流式、单 Agent、多 Agent、异常或浏览器断开，都恰好调用一次。

**必补 repository 操作：** 当前 `appendMessage` 由调用者提供 sequence，多个请求可冲突。新增 `ensureConversation(owner, id, title)` 与 `appendMessageAutoSequence(...)`：在一个 transaction 内锁定 owner conversation、分配下一个 sequence、插入消息；context 被另一 owner 占用时拒绝。不要在 service 中 `MAX(sequence)+1`。

**预算与成本：** V012 当前是“估算 token 预留”，没有 actual-token settle API。第一版用稳定估算写 `token_usage_ledger(estimated=true)`，并在日志中标记 estimate；后续拿到 Agent/provider usage 时再追加 V013 的 settle/adjust，不要把微美元 Redis 配置复用为 token 限额。新增明确的 `NEXUSAI_BUDGET_*_TOKENS` 配置和默认值文档。

**流式唯一终态：** `AIQueryServiceImpl` 是唯一 `complete/error` 发射者。下层 A2A 与 `MultiAgentHandler` 只产生事件，由一个 relay 过滤下层 `complete`、记录 partial、检查 `context->IsCancelled()` 和 `writer->Write()`；用 `std::once_flag`/原子标志保护终态。Node Proxy 也要记住 `completeSeen`，仅在 gRPC 未提供终态时补一个 complete。浏览器 `close` 必须 `stream.cancel()`，服务端持久化 `cancelled`。

**C2 必测：**

- 用户 A 在 body 放 user B，所有 PG 行仍为 A；B 不能读 A 的 trace/message。
- 同一 request_id 重试只保留一次预算 reservation 和一次最终 ledger；跨 owner 同 request_id 被拒绝。
- 成功、预算拒绝、Agent 异常、gRPC cancel、SSE close 各有 query log/trace 正确终态。
- stream 的 `complete` 恰好一次；代理、单 Agent、multi-agent 三条路径均测。
- Redis 重启后，conversation、messages、query log、trace、ledger 仍可读取。

### PR-C3：PG 可观测性、反馈与 Agent 生命周期

**文件所有权：**

- 新增：`db/migrations/V013__runtime_facts.sql`、`common/include/agent_rpc/common/agent_runtime_repository.h`、`common/src/agent_runtime_repository.cpp`
- 修改：`server/src/{agent_service,agent_lifecycle_service,observability_service,rpc_server}.cpp` 与 headers、`orchestrator/src/{feedback_aggregator,agent_router}.cpp` 与 headers、相关 proto/Node client/测试。

**实现：**

- V013 新增 owner/query-log 关联的 Agent 调用事实表；为 `feedback` 和 `agent_route_quality` 补齐 `skill_name` 维度（当前 proto 与 Router 是 agent+skill，而 V011 不足）。使用新 repository 聚合，废弃 `FeedbackAggregator::execPsql()` 和 `PG_URL`。
- Agent 注册/注销/心跳写 `agent_registry`，Redis 仅保存 liveness 缓存。`RegisterAgent`、`UnregisterAgent`、`Heartbeat` 当前漏做 handler auth 检查，必须补上；本地方案由静态配置的 ADMIN 管理 Agent。
- 将用户 role 加入登录/ValidateToken/AuthContext，并实现 `requireAdmin()`。Admin UI 与预算策略、Agent 管理、反馈查看均在服务端校验，前端 route guard 只是体验优化。
- `SubmitFeedback` 先验证 trace/query-log 属于当前 owner，再写 `feedback`；聚合写 `agent_route_quality`。Router 选择函数必须拿到 owner+skill 或注入 owner-aware quality provider，不能继续从无 owner 的 Redis key 取唯一事实。
- `GetTraceDetail` / `GetCostReport` 读 PG；请求中的 user_id 必须被当前 owner 覆盖。成本缺 provider usage 时显示 estimated，不伪造精确值。
- 删除 Router 中 Canary/Deprecated 权重行为和 `server/src/main.cpp` 的 Cron/Canary 后台任务。

**验收：** Agent 重启后 registry/metrics 仍在；A 的反馈只改变 A 的路由质量；普通用户访问 admin RPC 为 `PERMISSION_DENIED`；无 `popen`、无 `PG_URL`、无 Redis-only observability 路径。

### PR-D：Replay、Export、Share、Template 的最小真实闭环

**文件：** `orchestrator/src/{replay_service,export_service}.cpp`、`server/src/sharing_service.cpp`、对应 headers/proto、`gateway/proxy/server.mjs`、`frontend/src/views/{ShareView,TemplateMarket,AdminView}.vue`。

- [ ] Replay：按 `trace_id + current owner` 查 trace/query log。`route` 仅重新路由并返回“原 route vs 新 route”；`exact` 用新的 request_id 走 C2 pipeline，并保存新的 trace，绝不覆盖原记录。只接受 `exact|route`。
- [ ] Export：从 PG messages 生成 Markdown/HTML；保留现有 HTML escape，消息文本不得作为 HTML/SQL 执行。不存在/跨 owner context 返回 `NOT_FOUND`。
- [ ] Share：只支持只读 `view`。生成高熵 raw token、仅存 hash、仅首次返回 raw token；检查 conversation owner、TTL/revoke。增加受限的 public `ReadSharedConversation(token)`（只读、脱敏、过期拒绝）以及 owner 的 list/revoke；不要使用写死的 `https://nexusai.local`。
- [ ] Template：保存 JSON definition 前解析校验；新增 list/get；`UseTemplate` 在当前 owner 下创建真实 conversation 与初始消息，不接受跨 owner template。
- [ ] 每个页面实现 loading/error/empty/result；没有后端数据时不展示“开发中”或假成功。

### PR-E：Sandbox、Compare、Intervention、Undo、Autonomy

**文件：** `server/src/{user_experience_service,agent_lifecycle_service}.cpp`、headers/proto、Workflow repository、`frontend/src/views/{AgentSandbox,CompareView}.vue`。

- [ ] Sandbox 复用 C2 pipeline 的 `ExecutionOptions{sandbox=true}`：独立 conversation/run，写 `sandbox_runs`、query/trace/cost，但不写入正常长期记忆；返回真实 response。
- [ ] Compare 新增“最多 3 个健康 agent”的执行 RPC，不把现有 `GetAgentCompare` 空数组当结果；写 `compare_runs.results`，每个 agent 的失败独立可见，整体取消可传播。
- [ ] Intervention 只允许 owner 对 `pending` 记录做一次 CAS 状态迁移：`PROCEED/MODIFY/SKIP/ABORT`；MODIFY 需非空文本。可逆动作同时写 `undo_actions`。
- [ ] Undo 以 action id（对旧 proto 可新增字段）读取 owner 的未过期、未撤销记录，原子标记 `undone_at` 后执行 inverse payload；重复 undo 返回真实冲突/失败。
- [ ] Autonomy 的 level 只接受 1–4，忽略 request user_id，写 `autonomy_settings(owner, agent)`；其值必须实际决定是否创建 intervention。

### PR-F：前端和网关收尾

**文件：** `frontend/src/services/grpc-client.ts`、`frontend/src/types/proto.ts`、Pinia stores、10 个 view、`gateway/proxy/server.mjs` 与 tests。

- [ ] Proxy 将 gRPC `UNAUTHENTICATED/PERMISSION_DENIED/NOT_FOUND/RESOURCE_EXHAUSTED/CANCELLED` 映射成稳定 JSON/SSE 错误；传递 Authorization 和浏览器 abort。
- [ ] Dashboard/Monitor 只读 PG durability RPC；Chat 渲染真实 plan/activity/trace/cost，保留失败原因与重试。
- [ ] 删除 Admin 的 Cron/Canary tabs、本地数组和按钮；保留 Agent 管理、预算、反馈、Replay，并按 role 隐藏入口。
- [ ] `frontend/src/types/proto.ts` 每次 proto 改动同步。无需为了本任务引入重型代码生成器，但添加字段级 contract test，防止手写 TS 漂移。

### PR-G：发布验收与文档

- [ ] 更新 `.env.example`（Postgres、静态 ADMIN、token budget），README、CLAUDE、能力矩阵、测试数量；修正“17 套测试”、过期 migration service 和 `ProfileSummarizer` “no-op”描述。`ProfileSummarizer::processPending()` 当前有真实 Redis+LLM 逻辑，问题是未持久化，不是 no-op。
- [ ] 对仍不在本地目标内的 RPC（etcd registry、Redis task store、A2A true async、bidi echo、MCP）二选一：实现最小闭环，或从前端/文档支持面移除并返回明确 `UNIMPLEMENTED`；不可返回成功空结果。
- [ ] WSL 最终门禁：

  ```bash
  ./scripts/bootstrap-wsl.sh          # 新环境首次执行
  ./run.sh setup
  ./run.sh build
  ./run.sh test
  docker compose config
  docker compose up --build -d
  docker compose ps
  ```

- [ ] 增加真实 E2E：注册/登录 → QueryStream → 一次 complete → 查询 trace/cost/messages；浏览器 abort → `cancelled`；预算拒绝；A/B 越权；反馈改变路由；share TTL/revoke。E2E 只能使用真实 Docker PostgreSQL/Redis，依赖缺失时 fail 或明确 skip，禁止 mock `psql` 伪造通过。

## 4. 交接给下一个 Agent 的执行协议

1. 先完成 PR-0；当前基线不是全绿，不能直接宣称“已有测试全通过”。
2. 每个 PR 使用一个独立 worktree；先限定文件所有权，避免 Query、schema、UI 并行修改同一文件。
3. Worker 完成后，主线程必须检查 `git status`、完整 diff、`git diff --check` 和相关 WSL 测试；只凭文字报告不得合入。
4. Query pipeline 是所有工作流的唯一执行入口。D/E 不得复制预算、owner、trace、cost、取消和最终化逻辑。
5. 最后才更新文档/宣传语；任何页面出现“成功”必须能在 PostgreSQL 查到对应 owner 的持久记录。


## 5. 无脑执行手册（严格按此顺序）

### 5.1 开工前

不要在交接分支直接写代码。每个 PR 一个 worktree；Windows 只用 PowerShell 管理 worktree，C++ 构建和 CTest 必须进入 WSL 的 Linux 文件系统路径执行。

```powershell
git status --short --branch
git worktree add ..\nexusai-c2 -b codex/c2-durable-query origin/codex/handoff-postgres-foundation-20260808
Set-Location ..\nexusai-c2
git status --short
```

- [ ] `git status --short` 只能出现本 PR 文件；`.superpowers/`、`TDD.md` 和其它遗留文件一律不提交。
- [ ] 开始时记录 `git rev-parse --short HEAD`、`git diff --check`、Proxy `npm test`、Frontend `npm run typecheck` 输出。
- [ ] 每写一个测试，先单独运行证明红，再做最小实现直到绿；把输出写入本 worktree 的 `TASK_REPORT.md`（不提交）。
- [ ] Docker/WSL/下游 Agent 缺失时记录 `BLOCKED(environment)`。禁止通过关闭认证、放宽 owner 断言、mock 成功响应来伪绿。

### 5.2 PR-0 执行卡

1. [ ] 只在候选 checkout 跑第 3 节的 CTest。通过条件：`QueryDomainRepositoryContractTest` 通过，跨 owner update 返回 `false`。
2. [ ] 依次 cherry-pick `70ffaa6`、`11274b8`。发生冲突时只保留 `updateQueryLog` / `updateTrace` 的 owner 条件；禁止用 upsert 替代 UPDATE。
3. [ ] 修改 `gateway/proxy/test/platform-contract.test.mjs`：Compose 只断言 `postgres/redis/rpc-server/proxy/frontend`；migration 为 V001–V012；迁移发生在 `server/src/main.cpp` 监听前。
4. [ ] 运行 `git diff --check`、Proxy `npm test`、Frontend `npm run typecheck`。全绿后提交 `test: align platform contract with startup migrations`。

停止条件：候选 CTest 失败就不 cherry-pick；报告失败的 SQL、测试名和 schema 版本，等维护者决定。

### 5.3 PR-C2 执行卡（最高优先级，必须单独审查）

先锁定接口再改调用方。服务必须具备这些最小能力：

```cpp
bool ensureConversation(std::string_view owner_id, std::string_view conversation_id,
                        std::string_view title);
std::optional<Message> appendMessageAutoSequence(
    std::string_view owner_id, std::string_view conversation_id,
    MessageRole role, std::string_view content, const Json& metadata);

bool AIQueryServiceImpl::initialize(
    const common::RpcConfig&, const a2a_adapter::A2AConfig&,
    common::RedisClient*, common::PostgresStore&,
    common::QueryDomainRepository&, common::PostgresBudgetRepository&);
```

1. [ ] 在 `tests/test_durable_query_pipeline.cpp` 写失败测试：body 冒充 B 仍写 A；预算拒绝写 `rejected`；A2A 异常写 `failed`；取消写 `cancelled`。用测试 PostgreSQL，不 mock repository。
2. [ ] 为 `ensureConversation` 写合约测试：首次创建成功、同 owner 重试成功、另一 owner 使用同 context 被拒绝。
3. [ ] 为 `appendMessageAutoSequence` 写连续和并发测试：sequence 为 1、2、3…且不会覆盖消息。实现必须在 transaction 内分配 sequence，禁止 service 使用 `MAX()+1`。
4. [ ] 在 `RpcServer` 持有 `PostgresStore`、`QueryDomainRepository`、`PostgresBudgetRepository`，且严格按 store → repositories → AI service 初始化 → 注册 service 的顺序构造；任一步失败即启动失败。
5. [ ] `Query` 与 `QueryStream` 首段从 `AuthInterceptor::currentUserId()` 取 owner，用它覆写 A2A request。未认证返回 `UNAUTHENTICATED`，不创建数据库行。
6. [ ] 固定顺序：ensure conversation → running query log/trace → reserve budget → 读取上下文 → A2A → finalize。预算拒绝先把两个记录更新为 `rejected`，再 `RESOURCE_EXHAUSTED`；移除 Query 主路径的 Redis `BudgetMiddleware::checkAndDeduct`。
7. [ ] 只保留一个私有 `finalize(status, response, error, spans)`，负责 assistant message、query log、trace、ledger；以 `std::once_flag` 防重复，正常、异常、cancel、stream 全部经过它。
8. [ ] A2A/MultiAgent 的 complete 只消费不转发；顶层是唯一 complete/error 发射者。Proxy 的 `streamCall` 维护 `completeSeen`；浏览器 close 调 `stream.cancel()`。
9. [ ] 验证同一 request_id 仅一条 reservation/ledger，Redis 重启后仍读到 conversation/log/trace/ledger。提交建议：`feat: persist authenticated query pipeline`。

C2 不做：不改 A2A 协议、不引入消息队列、不做 provider token settlement。ledger 可以标 estimated，不能假装精确 usage。

### 5.4 PR-C3 执行卡

1. [ ] 新建 `db/migrations/V013__runtime_facts.sql`，绝不改 V001–V012；补齐 feedback/route 的 owner、trace/query 关联、`skill_name` 和 owner 查询索引。
2. [ ] 新建 `AgentRuntimeRepository`，用 libpqxx `exec_params` 实现 registry、feedback、route quality、trace/cost；禁止 `popen`、`psql`、拼接 SQL。
3. [ ] `FeedbackAggregator` 从旧 `agent_feedback` / `agent_calls` 迁到 repository；写 feedback 前验证 trace 属于 current owner。无数据返回空数组，不造 metrics。
4. [ ] proto 只追加字段/RPC：登录和 ValidateToken 返回 role；`AuthContext` 保存 role；实现 `requireAdmin()`。Agent 注册、预算策略、admin 数据全部调用它。
5. [ ] 删除或明确禁用 Cron/Canary 的后台调度和前端入口。
6. [ ] 测试普通用户 admin RPC 得 `PERMISSION_DENIED`；A 的 feedback 不影响 B；Redis 重启后 agent/trace/cost 仍在；源码无 `popen(` / `execPsql`。

### 5.5 PR-D、PR-E 执行卡

**PR-D：**

- [ ] Replay exact 用新 request_id 调 C2，生成新 trace；route 仅计算新路由，不改原 trace。
- [ ] Export 仅从 PG messages 导出，保持 HTML escape；跨 owner/不存在均 `NOT_FOUND`。
- [ ] Share 只存 raw token 的 hash、owner、TTL、revoke 时间；raw token 仅首次返回。公开 RPC 只读、脱敏、校验过期/撤销。
- [ ] Template use 仅能在 current owner 下创建真实 conversation；每条路径测越权、过期/revoke、空数据。

**PR-E：**

- [ ] Sandbox 用 `ExecutionOptions{sandbox=true}` 调 C2，独立写 sandbox run，不写长期 memory。
- [ ] Compare 新 RPC 真实执行最多 3 个健康 agent；单 agent 失败必须可见，不能把整体伪装成功。
- [ ] Intervention/Undo 用带 owner 条件的单条 SQL CAS；重复操作返回冲突，不能重复 inverse payload。
- [ ] Autonomy 只允许 1–4，忽略 request user_id，保存值必须实际决定是否创建 intervention。

### 5.6 PR-F、PR-G 执行卡

1. [ ] 每个 proto 改动同步 `frontend/src/types/proto.ts` 和 gateway mapping；加字段级 contract test（错误码、role、stream terminal、新 ID）。
2. [ ] UI 只显示真实 loading/error/empty/result；删除 Cron/Canary 和本地假数组，未支持 RPC 显示明确不可用。
3. [ ] Proxy 稳定映射 `UNAUTHENTICATED`、`PERMISSION_DENIED`、`NOT_FOUND`、`RESOURCE_EXHAUSTED`、`CANCELLED`，不把错误包装成 200 成功 JSON。
4. [ ] 更新 `.env.example`、README、CLAUDE：V001–V012、静态 ADMIN、token 预算、真实测试数和 Compose 限制。Compose 目前没有 A2A/Orchestrator Agent，CI 应提供确定性测试 Agent profile，或将真实下游列为 E2E 前置条件。
5. [ ] 最终执行：

```bash
git diff --check
ctest --test-dir build --output-on-failure
docker compose config
```

```powershell
Set-Location gateway/proxy; npm test
Set-Location ../../frontend; npm run typecheck
```

## 6. 每个 PR 的统一完成定义

- [ ] 仅修改声明的文件范围；无依赖升级、生成目录或锁文件噪声。
- [ ] 新测试先红后绿；不能运行的 WSL/Docker 测试注明原因和未验证状态。
- [ ] 每个成功、失败、取消都能以 owner 条件从 PostgreSQL 查到事实。
- [ ] `git diff --check` 无输出；`git status --short` 不含他人文件；提交使用 `test:` / `feat:` / `fix:`。
- [ ] PR 描述包含改动目的、API/migration 兼容性、命令与摘要、未验证项、回滚方式（回滚单提交；migration 不回删）。
