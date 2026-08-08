# NexusAI 项目交接文档（2026-08-08）

## 交接状态

本轮所有子代理已停止，未运行中的后台实现任务。没有创建或合并远程 PR，也没有推送远程。

主工作区：`C:\Users\wade.liu\Downloads\agent-communication-and-tool-selection-framework`  
当前分支：`codex/pr-b-postgres-local-auth`  
当前 HEAD：`23a870b test: isolate postgres budget contract runs`

主工作区仅有两个**用户已有的未跟踪文件**，不要删除或提交：

- `.superpowers/`
- `TDD.md`

## 已整合并有验证证据的成果

### 本地平台与运行链路

- Node JSON/SSE Proxy + 前端 Nginx + RPC Server + PostgreSQL + Redis 的本地 Compose 链路已建立。
- 容器曾实际验证为 healthy，Windows 和 WSL 侧 `http://127.0.0.1:8080/` 均返回 HTTP 200。
- 浏览器曾正常显示 NexusAI 登录页。
- 已确认 Compose 内 PostgreSQL 不公开宿主端口；测试可通过容器内部 IP / Docker 网络执行。

### PostgreSQL 与认证基础

主分支已包含以下已审核提交：

- `78bd2f3` / `7927d0c` / `12b656a`：同步 PostgreSQL 连接池及 migration advisory lock 修复。
- `423cace` / `841ba8e`：PostgreSQL 用户、会话、scrypt 密码认证与 opaque session token。
- `91b6b35`：V011 durable domain schema（会话、消息、query log、trace、成本账本、反馈、分享、模板、Sandbox、Compare、Intervention、Undo、Autonomy、Agent registry）。
- `8af101c`：`QueryDomainRepository` 基础 CRUD，所有读取带 owner 条件。
- `423d885`：严格 `AuthInterceptor` owner context；Bearer token 校验后从上下文获取 user id。

### PostgreSQL 预算事实源

主分支已整合：

- `a9836bf`：`PostgresBudgetRepository` 与 V012 migration。
- `b6cd14e`、`1afa53f`、`5339c3b`、`d491132`、`aff2e7a`、`23a870b`：幂等键、C++ 编译、测试稳定性修正。

实现位置：

- `common/include/agent_rpc/common/postgres_budget_repository.h`
- `common/src/postgres_budget_repository.cpp`
- `db/migrations/V012__postgres_budget.sql`
- `tests/test_postgres_budget_repository_contract.cpp`

已由主线程实际验证：在 WSL 中使用 Docker Compose 的真实 PostgreSQL，预算契约与集成测试**连续运行两次均 8/8 通过**。覆盖：四层计数、幂等、拒绝无部分写入、owner policy、跨 owner request id、非法输入和跨运行测试隔离。

## 未整合候选（下一个窗口先处理）

### Query Log / Trace 更新仓储

候选 worktree：

`C:\Users\wade.liu\Downloads\agent-communication-and-tool-selection-framework\.luna-worktrees\query-domain-updates`

候选分支：`codex/luna-pr-c-query-domain-updates`  
候选提交（均未整合）：

1. `70ffaa6 feat: add query log and trace updates`
2. `11274b8 fix: scope normalized source in contract test`

改动范围严格为：

- `common/include/agent_rpc/common/query_domain_repository.h`
- `common/src/query_domain_repository.cpp`
- `tests/test_query_domain_repository_contract.cpp`

功能：新增 owner-scoped、参数化的 `updateQueryLog` 与 `updateTrace`。更新不存在记录或 owner 不匹配时返回 false，不执行 upsert。

审查状态：

- 已检查完整 diff、文件范围和 `git diff --check`。
- 第一次 WSL 编译发现测试中 `normalized_source` 作用域错误。
- `11274b8` 已将该变量放到新增测试的局部作用域，但**该修复尚未由主线程重新编译/运行 CTest**，所以不能直接 cherry-pick。

建议先在专用 WSL 验证 clone 执行：

```bash
git fetch origin codex/luna-pr-c-query-domain-updates
git checkout -B review-query-domain-updates origin/codex/luna-pr-c-query-domain-updates
cmake --build build-pg-budget-review --target test_query_domain_repository_contract -j2
cd build-pg-budget-review
ctest --output-on-failure -R '^QueryDomainRepositoryContractTest$'
```

通过后，在主工作区整合：

```powershell
git cherry-pick 70ffaa6 11274b8
git diff --check HEAD~2..HEAD
```

不要提交各 worktree 中的 `TASK_REPORT.md`。

## 仍未完成的功能清单

以下均是目标范围内的真实缺口，不能宣称已完成：

### 高优先级：Query / QueryStream 主链路

- `server/src/ai_query_service.cpp` 仍会信任请求体中的 `user_id`；必须始终覆盖为 `AuthInterceptor::currentUserId()`。
- 预算仍通过 `BudgetMiddleware` / Redis 在 Query 路径扣减；需改为 `PostgresBudgetRepository`，Redis 只能作缓存。
- Query/QueryStream 仍把 trace、成本主要写 Redis / `CostTracker`，需真实写入 `query_logs`、`traces`、`token_usage_ledger`、`conversation_messages`。
- Query / stream 取消、失败、成功状态必须更新持久 query log / trace；stream 的 complete 必须严格只发一次。
- 记忆目前仍依赖 `MemoryService` 的旧实现；至少应把正式会话消息和可重建 memory summary 写入 PostgreSQL。
- `RpcServer` 需将 `PostgresStore`、`QueryDomainRepository`、`PostgresBudgetRepository` 注入 `AIQueryServiceImpl`。

### 高优先级：明确占位后端

- `orchestrator/src/replay_service.cpp`：Replay exact / route 仍返回 placeholder。
- `orchestrator/src/export_service.cpp`：导出是 placeholder 示例，不是持久会话数据。
- `server/src/sharing_service.cpp`：ObserveSession 是空/占位流。
- `server/src/agent_lifecycle_service.cpp`：Compare 与 Undo 为 placeholder。
- `server/src/user_experience_service.cpp`：Sandbox / Intervention 尚未形成真实持久执行闭环。
- `orchestrator/src/feedback_aggregator.cpp`：仍使用 `popen("psql ...")`，必须改为 PostgreSQL 仓储查询。

### 高优先级：前端未闭环页面

- `frontend/src/views/ShareView.vue`
- `frontend/src/views/TemplateMarket.vue`
- `frontend/src/views/AgentSandbox.vue`
- `frontend/src/views/CompareView.vue`
- `frontend/src/views/AdminView.vue` 的 Replay 仍提示“开发中”。

这些菜单入口必须改为真实 RPC、真实 loading/error/empty 状态以及持久数据，不能保留假成功。

### 中优先级：Agent / 反馈 / 可观测性

- Agent 注册、heartbeat、health、调用指标需统一写 PG 的 `agent_registry`。
- 提交反馈需写 `feedback`，聚合写 `agent_route_quality` 并真正影响 Router 权重。
- Dashboard / Monitor / GetTraceDetail / GetCostReport 要改为读取 PG durable facts，而不是浏览器臆测或 Redis 唯一数据。
- Admin 仅保留本地目标需要的 Agent 管理、预算设置、反馈查看；Cron/Canary/灰度不在最终本地方案内，应从 UI/文档清理或明确关闭。

### 中低优先级：清理与发布验收

- `common/src/rpc_framework.cpp` 仍有 server startup TODO。
- `common/src/profile_summarizer.cpp` / `server/src/main.cpp` 仍有 no-op placeholder。
- `.env.example`、README、CLAUDE、WSL/Docker 使用说明、能力矩阵与测试说明需在功能完成后同步。
- 完成所有功能后必须跑 `./run.sh setup`、`./run.sh build`、`./run.sh test`、`docker compose up --build` 以及关键 E2E。

## 推荐继续顺序 / PR Stack

以下顺序遵循“先 schema/repository，再服务接线，再 UI，最后全量验证”的可回滚原则：

1. **PR-C1：整合并验证 QueryDomainRepository 更新候选**  
   先完成上述 `11274b8` 的 WSL 验证，再 cherry-pick 两个候选提交。
2. **PR-C2：Query / QueryStream durable pipeline**  
   由一个新的 Luna Max worktree 负责限定文件：`ai_query_service.{h,cpp}`、`rpc_server.{h,cpp}`、新增针对 Query pipeline 的测试。目标是 owner 强制、PG budget、messages/query log/trace/token ledger、取消状态和一次 complete。
3. **PR-C3：PG observability / feedback / Agent lifecycle**  
   先新增清晰 repository，再接 Agent lifecycle、feedback routing、Observability RPC；删除 `popen` 路径。
4. **PR-D：Replay、Export、Share、Template**  
   先服务端持久实现和 owner/TTL，再接前端四个页面。
5. **PR-E：Sandbox、Compare、Intervention、Undo、Autonomy**  
   复用 C2 的 durable query execution；每项都要独立 owner、成本、trace 与动作审计。
6. **PR-F：前端所有入口与 Admin 本地闭环**  
   移除“开发中”与假成功，完成真实 API 失败态及权限/owner 测试。
7. **PR-G：最终 WSL 回归与文档**  
   全量 build/test/Compose/E2E、README/CLAUDE/.env/Licence 及完成度审计。

## 新窗口的协作规范

- 主线程负责设计、任务拆分、风险判断、真实 diff 审查、测试和最终验收。
- 每个实现任务都由 `luna_worker`（GPT-5.6 Luna Max）在独立 worktree 执行。
- Luna 只能按明确文件所有权修改；不得 push、创建或合并 PR。
- Luna 完成后，主线程必须检查 worktree、branch、status、完整 diff、范围、`git diff --check`，并实际重跑相关 WSL 构建/测试后才可 cherry-pick。
- 发现问题时优先把精确错误和复现命令返还原 Luna；不要仅根据文字报告接受任务。
- 本地最终方案：PostgreSQL 是业务事实源，Redis 仅作缓存/在线状态/限流/短期锁；不启用 TLS/mTLS、MCP、动态 RBAC、Cron/Canary。

## WSL 验证环境备忘

- 专用验证 clone：`/home/wade.liu/nexusai-pr-a-verify-20260807`
- libpqxx 8 路径：`/root/.local/share/nexusai/libpqxx-8.0.1`
- 配置示例：

```bash
env CMAKE_PREFIX_PATH=/root/.local/share/nexusai/libpqxx-8.0.1 \
    PKG_CONFIG_PATH=/root/.local/share/nexusai/libpqxx-8.0.1/lib/pkgconfig \
    cmake -S . -B build-pg-budget-review -DENABLE_MCP=OFF -DBUILD_TESTING=ON
```

- Compose PostgreSQL 内部 IP 曾为 `172.19.0.3`，但这不是稳定配置；重新启动后应使用 `docker inspect` 获取 IP，或在 Docker 网络内运行测试。
- WSL 的 `Start-Process` 传递 `sh -lc` 时必须将整个 shell command 作为单一引用参数，否则可能错误地在 Windows 挂载仓库配置 CMake。优先直接用 `wsl.exe -d Ubuntu --cd <path> -- sh -lc '<command>'`。

## 明确不要做的事

- 不要推送任何当前分支或候选分支。
- 不要删除 `.superpowers/`、`TDD.md` 或历史 worktree，除非用户明确要求清理。
- 不要把各 worktree 的未跟踪 `TASK_REPORT.md` 加入提交。
- 不要把 Redis 重新作为预算、反馈、trace、成本或消息的唯一事实源。
- 不要为了通过测试将未实现 RPC 改成成功码占位。
