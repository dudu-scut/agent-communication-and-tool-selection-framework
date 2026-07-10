# NexusAI 端到端验证测试计划 — 设计文档

> 项目启动后模拟真实应用场景的集成验证方案，覆盖全部 8 批次 21 项优化。

## 方案选型

**Shell 脚本驱动的自动化 + 手动验证清单**（方案 A）。

- 基于已有 `./run.sh` 命令体系，新增 `verify` 子命令
- 自动化部分：shell 启动服务 → 发送场景请求 → 检查响应/Redis/PG → 输出通过/失败表格
- 手动部分：`docs/verification-checklist.md` 列出需人工确认的前端 UI 项
- 零新依赖（复用 `grpcurl` 或项目自带 `rpc_client` CLI）

---

## 整体结构

### 入口命令

```bash
./run.sh verify          # 运行全部 8 批验证（自动化部分）
./run.sh verify-batch1   # 单独运行第 1 批
./run.sh verify-batch2   # ...以此类推至 batch8
./run.sh start-mock-agent  # 启动 Mock Agent（后台）
```

### 目录结构

```
verify/
├── mock-agent/
│   └── mock_agent_server.py    # Mock Agent HTTP server
├── scripts/
│   ├── verify-batch1.sh        # 基础设施：调度器 + 追踪 + 成本
│   ├── verify-batch2.sh        # 韧性：熔断 + 反馈 + 透明化
│   ├── verify-batch3.sh        # 缓存控制：语义缓存 + 压缩 + 自主权
│   ├── verify-batch4.sh        # UX 核心：记忆 + 活动流 + DAG
│   ├── verify-batch5.sh        # 运维工具：仪表盘 + 预算 + 重放
│   ├── verify-batch6.sh        # 平台扩展：定时 + 灰度 + 导出
│   ├── verify-batch7.sh        # 增长留存：沙箱 + 共享
│   ├── verify-batch8.sh        # 协议安全：版本协商 + 深度限制
│   └── helpers.sh              # 共享工具函数
├── fixtures/
│   ├── sample-queries.txt      # 预设查询文本
│   └── agent-cards.json        # Mock AgentCard 注册数据
└── expected/
    └── (diff 对比参考输出)
```

### 每个 Batch 脚本执行流程

1. **Pre-check** — 确认 Redis、PostgreSQL、gRPC Server、Mock Agent 已启动；若未启动则自动拉起
2. **Setup** — 注入该 batch 所需的测试数据（注册 Agent、设置配置）
3. **Scenario execution** — 按场景顺序发送请求、收集响应
4. **Assertion** — 检查响应状态码、Redis key 状态、PG 表记录
5. **Teardown** — 清理测试数据，恢复环境
6. **Report** — 输出场景级 `[PASS]` / `[FAIL]` 表格

---

## Batch 验证场景（32 个自动化场景）

### Batch 1 — 基础设施（4 场景）

| # | 场景名 | 操作 | 验证点 |
|---|--------|------|--------|
| 1.1 | 全链路追踪 | 发送 `QueryStream` 请求 | gRPC response 含 `x-trace-id`；PG `trace_spans` 表有记录；`complete` 事件携带 `trace_summary` |
| 1.2 | 后台调度器自检 | 启动服务后等待 5s | `BackgroundScheduler` 注册的 span_flush、feedback_aggregation 等任务至少执行一次（检查日志输出） |
| 1.3 | Token 成本计量 | 发送需要 LLM 调用的查询 | Redis key `cost:{user_id}:{YYYY-MM-DD}` 值 > 0；PG `token_usage` 表有新行 |
| 1.4 | 线程安全隔离 | 并发发送 3 个请求（不同 user_id） | 每个请求的 `trace_id` 不同且互不干扰 |

### Batch 2 — 韧性 + 反馈（4 场景）

| # | 场景名 | 操作 | 验证点 |
|---|--------|------|--------|
| 2.1 | 正常熔断通过 | 调健康 Agent 1 次 | circuit_breaker 保持 CLOSED；响应正常 |
| 2.2 | 熔断触发回退 | Mock Agent 连续 3 次返回 500 → 第 4 次 | 第 4 次不发给已熔断 Agent，自动切备选 Agent（检查 `agent_calls` 的 agent_id 变化） |
| 2.3 | 用户反馈闭环 | 发送 `SubmitFeedback`（rating=3） | PG `agent_feedback` 表有新行；Redis `feedback:{agent_id}:{skill}` 更新 |
| 2.4 | Agent 指标查询 | 发送 `GetAgentMetrics` RPC | 响应含 `success_rate`、`avg_latency_ms`、`approval_rate` 且值合理 |

### Batch 3 — 缓存 + 控制（4 场景）

| # | 场景名 | 操作 | 验证点 |
|---|--------|------|--------|
| 3.1 | 语义缓存命中 | 相同语义不同措辞发两次查询 | 第二次响应 `cache_hit: true`；延迟 < 50ms |
| 3.2 | 上下文压缩触发 | 连续发送 15 轮长对话 | 第 11+ 轮后 span metadata 出现 `context_compressed: true` |
| 3.3 | L1 自主权拦截 | 设 Agent 为 L1 → 发"修改文件"类查询 | 响应为建议模式，不执行实际工具调用 |
| 3.4 | L2 干预确认 | 设 Agent 为 L2 → 发需工具查询 | SSE 流出现 `intervention_required`；发送 `PROCEED` 后恢复执行 |

### Batch 4 — UX 核心（4 场景）

| # | 场景名 | 操作 | 验证点 |
|---|--------|------|--------|
| 4.1 | 统一记忆注入 | 先设 User Profile → 发查询 | Agent 收到的 SystemContext 含用户档案摘要（检查 memory_service 日志） |
| 4.2 | 活动流记录 | 发多步查询 | Redis `activity_feed:{trace_id}` 有活动记录（≥3 条）；SSE 收到 `activity_json` 事件 |
| 4.3 | DAG 计划预览 | 发复杂查询触发多 Agent 编排 | `plan_generated` 事件含 `DAGStructure` JSON（nodes 数组非空） |
| 4.4 | 用户调整执行 | 发 `ExecutePlan`（用户修改后的 DAG） | 按用户指定 Agent 执行（`agent_calls` 的 agent_id 与请求一致） |

### Batch 5 — 运维工具（4 场景）

| # | 场景名 | 操作 | 验证点 |
|---|--------|------|--------|
| 5.1 | 健康度仪表盘数据 | 发请求后查 `GetAgentMetrics` | `active_requests`、`circuit_breaker_trips` 更新；`agent_metrics:{agent_id}` Redis Hash 存在 |
| 5.2 | 预算超限拒绝 | 用户日预算设为 1 → 发请求 | 返回错误含"配额不足"或"budget exceeded" |
| 5.3 | 查询重放（精确） | `ReplayQuery(trace_id, mode=EXACT)` | 返回相同 agent_id 和响应内容 |
| 5.4 | 查询重放（路由） | `ReplayQuery(trace_id, mode=ROUTE)` | 重新走路由管线（可能不同 Agent 但 skill 匹配） |

### Batch 6 — 平台扩展（4 场景）

| # | 场景名 | 操作 | 验证点 |
|---|--------|------|--------|
| 6.1 | 定时任务触发 | 注册 `*/1 * * * *` 任务 → 等 65s | PG `task_results` 有执行记录；`scheduled_tasks.last_run_at` 更新 |
| 6.2 | 灰度流量分流 | 注册 STABLE + CANARY 双 Agent → 发 100 次 | CANARY 收到约 10%（9-11 次） |
| 6.3 | Markdown 导出 | `ExportConversation(ctx_id, format=MD)` | 返回含 `# NexusAI 对话记录`、Agent、时间线 |
| 6.4 | HTML 导出 | `ExportConversation(ctx_id, format=HTML)` | 返回含 `<html>`、内嵌 CSS、气泡样式 |

### Batch 7 — 增长留存（4 场景）

| # | 场景名 | 操作 | 验证点 |
|---|--------|------|--------|
| 7.1 | 沙箱隔离 | 发 `SandboxQuery` | context_id 以 `sandbox_` 开头；对话历史 Redis key TTL=3600 |
| 7.2 | 沙箱成本豁免 | 沙箱请求的 Token 消耗 | `token_usage.component = "sandbox"`；Redis 预算计数器未增加 |
| 7.3 | 会话分享 | `ShareSession(ctx_id, mode=READONLY)` | 返回 share_id UUID；PG `shared_sessions` 有新行 |
| 7.4 | 模板保存使用 | `SaveTemplate` → `UseTemplate` | `UseTemplate` 创建的会话 DAG 结构与模板一致 |

### Batch 8 — 协议安全（4 场景）

| # | 场景名 | 操作 | 验证点 |
|---|--------|------|--------|
| 8.1 | v1.0 序列化 | 调 `a2a_version: "1.0"` 的 Agent | 请求 body 使用 `"kind": "text"` |
| 8.2 | v1.1 序列化 | 调 `a2a_version: "1.1"` 的 Agent | 请求 body 使用 `"type": "text"` |
| 8.3 | 兼容回退 | Mock Agent 声明 v1.0 但响应用 v1.1 格式 | 解析成功（dual-parse fallback）；span metadata 标记 `version_fallback: true` |
| 8.4 | 委派深度限制 | Mock Agent 递归委派到第 6 层 | 第 5 层后返回错误 `"委派深度超过限制（max=5）"` |

---

## Mock Agent

轻量 Python HTTP server（`verify/mock-agent/mock_agent_server.py`），模拟不同 Agent 行为。

### 端点

```
端口: 5100（避免与现有 5000/5001 冲突）

GET  /health              → 200 OK（健康）
GET  /health?fail=true    → 500（模拟故障）
POST /tasks/send           → A2A JSON-RPC 处理入口
```

### 行为模式

通过 HTTP header `x-mock-mode` 控制：

| 模式 | 行为 | 验证用途 |
|------|------|---------|
| `normal` | 正常返回 `"Mock response for: {query}"` | 正向流程 |
| `slow` | 延迟 5s 后返回 | 超时验证 |
| `error` | 返回 500 | 熔断触发 |
| `delegate` | 响应中含 `x-delegation-to` header | 委派深度限制 |
| `version_v1_0` | 响应用 `"kind"` 字段 | 版本协商 |
| `version_v1_1` | 响应用 `"type"` 字段 | 版本协商 |
| `version_mixed` | 声明 v1.0 但响应用 v1.1 格式 | 兼容回退 |

### Agent 实例

| Agent ID | Skill | 版本 | 默认模式 |
|----------|-------|------|---------|
| `mock-general` | general | v1.0 | normal |
| `mock-translator` | translation | v1.0 | normal |
| `mock-unstable` | general | v1.0 | error/slow |
| `mock-canary` | general | v1.1 | normal (canary 阶段) |

### 启动方式

```bash
./run.sh start-mock-agent  # 后台启动，写入 verify/mock-agent/pid.txt
./run.sh stop               # 停止全部服务（含 Mock Agent）
```

---

## Helpers（`verify/scripts/helpers.sh`）

共享工具函数库，所有 batch 脚本 source 使用。

### 输出

```bash
PASS='\033[0;32m[PASS]\033[0m'
FAIL='\033[0;31m[FAIL]\033[0m'
WARN='\033[0;33m[WARN]\033[0m'
```

### 断言函数

```bash
assert_http_ok()           # 检查 HTTP 200
assert_grpc_ok()           # 检查 gRPC 响应无 error
assert_redis_key_exists()  # 检查 Redis key 存在
assert_redis_key_ttl()     # 检查 Redis key TTL 在范围内
assert_redis_value_gt()    # 检查 Redis 数值 > N
assert_pg_row_exists()     # 检查 PG 表有匹配行
assert_contains()          # 检查输出含指定字符串
assert_not_contains()      # 检查输出不含指定字符串
```

### 场景脚手架

```bash
scenario()   # 打印场景标题 "=== 场景名 ==="
step()       # 打印步骤 "  → 步骤描述"
verify()     # 执行断言并输出 PASS/FAIL（返回 0/1）
```

### 数据注入

```bash
register_mock_agent()      # 向 Registry 注册 Mock Agent
set_redis_budget()         # 设置 Redis 预算计数器
insert_pg_user_profile()   # 插入用户档案到 PG
```

### gRPC 通信

优先使用项目自带 `rpc_client` CLI（如可用），其次 `grpcurl`。

```bash
send_query()          # 发送同步 Query 请求
send_query_stream()   # 发送流式 QueryStream 请求
call_grpc_method()    # 通用 gRPC 方法调用
```

---

## Report 汇总格式

```
========================================
 NexusAI 验证测试报告
 时间: 2026-07-11 14:30:22
========================================

Batch 1 — 基础设施 ................ 4/4 PASS
Batch 2 — 韧性+反馈 ............... 3/4 PASS (2.2 FAIL)
Batch 3 — 缓存+控制 ............... 4/4 PASS
Batch 4 — UX 核心 ................. 4/4 PASS
Batch 5 — 运维工具 ................ 4/4 PASS
Batch 6 — 平台扩展 ................ 3/4 PASS (6.2 WARN)
Batch 7 — 增长留存 ................ 4/4 PASS
Batch 8 — 协议安全 ................ 4/4 PASS
========================================
 自动化: 30/32 PASS, 1 FAIL, 1 WARN
 手动验证: 见 docs/verification-checklist.md
========================================
```

---

## 手动验证清单

存储为 `docs/verification-checklist.md`，列出需人工确认的前端 UI 项。预计 20 分钟完成。

### 前置条件
- [ ] `./run.sh start` 启动全部服务（含 Mock Agent）
- [ ] `npm run dev` 启动前端
- [ ] 浏览器打开 http://localhost:5173

### Batch 1-2：基础体验
- [ ] ChatView 消息气泡下方显示 trace 摘要（如"路由 12ms → Agent 856ms"）
- [ ] 点赞按钮点击后变绿并保持选中
- [ ] 点踩按钮点击后变红

### Batch 3-4：UX 核心
- [ ] Activity Panel 右侧实时展示 Agent 工作步骤（💭→🔧→✅）
- [ ] DAG 预览：复杂查询触发多 Agent 编排时展示 Mermaid 流程图
- [ ] AgentSelector：多候选 Agent 时展示选择面板

### Batch 5：管理后台
- [ ] AdminView 健康度仪表盘展示 Agent 状态灯（绿/黄/红）
- [ ] AdminView 预算面板展示用户预算使用量
- [ ] AdminView 查询重放：输入 trace_id 查看调用链时间线

### Batch 6：扩展功能
- [ ] AdminView 定时任务管理页（增删改查 + 手动触发）
- [ ] AdminView 灰度部署面板（流量比例 + 指标对比）
- [ ] ChatView 导出 Markdown 下载文件内容正确

### Batch 7：增长留存
- [ ] AgentSandbox 页面：Agent 卡片 + 快速试用按钮
- [ ] CompareView：三列并排展示不同 Agent 回答 + 对比指标
- [ ] ShareView：无痕窗口打开分享链接可查看只读会话
- [ ] TemplateMarket：模板卡片展示 + "使用模板"按钮

### Batch 8：协议安全
- [ ] （无前端可观测变更 — 纯后端，自动化已覆盖）

---

## 覆盖率矩阵

| 优化项 | 批次 | 自动化场景编号 | 手动验证 | 覆盖 |
|--------|------|--------------|---------|------|
| P0 成本可观测 | B1 | 1.3 | — | ✅ |
| P0 分布式追踪 | B1 | 1.1 | 前端 trace 摘要 | ✅ |
| BackgroundScheduler | B1 | 1.2 | — | ✅ |
| Thread Safety | B1 | 1.4 | — | ✅ |
| U0 能力透明化 | B2 | 2.4 | AgentSelector 面板 | ✅ |
| P1 熔断回退 | B2 | 2.1, 2.2 | — | ✅ |
| P1 用户反馈闭环 | B2 | 2.3 | 点赞按钮 UI | ✅ |
| P1 语义缓存 | B3 | 3.1 | — | ✅ |
| P1 上下文压缩 | B3 | 3.2 | — | ✅ |
| U1 自主权梯度 | B3 | 3.3, 3.4 | 干预弹窗 UI | ✅ |
| U2 统一记忆 | B4 | 4.1 | — | ✅ |
| U3 活动流 | B4 | 4.2 | Activity Panel | ✅ |
| U4 组合推荐 | B4 | 4.3, 4.4 | Mermaid 流程图 | ✅ |
| P2 健康度仪表盘 | B5 | 5.1 | AdminView 状态灯 | ✅ |
| P2 Token 预算 | B5 | 5.2 | AdminView 预算面板 | ✅ |
| P2 查询重放 | B5 | 5.3, 5.4 | AdminView 时间线 | ✅ |
| P2 定时任务 | B6 | 6.1 | AdminView 管理页 | ✅ |
| P2 灰度发布 | B6 | 6.2 | AdminView 灰度面板 | ✅ |
| P2 对话导出 | B6 | 6.3, 6.4 | 下载文件内容 | ✅ |
| U5 沙箱试用 | B7 | 7.1, 7.2 | AgentSandbox 页 | ✅ |
| U6 协作共享 | B7 | 7.3, 7.4 | ShareView/Template | ✅ |
| P3 版本协商 | B8 | 8.1, 8.2, 8.3 | — | ✅ |
| P3 委派深度限制 | B8 | 8.4 | — | ✅ |

**21/21 优化项全覆盖**，每个至少 1 个自动化验证点，12 个有前端手动验证点。

---

## 验收标准

1. `./run.sh verify` 一键运行全部 8 批 32 个自动化场景
2. 集成到现有 `./run.sh` 体系，与 `build`/`test`/`start`/`stop` 风格一致
3. 每个 batch 可独立运行（`./run.sh verify-batchN`）
4. 自动化覆盖全部 21 项优化的后端数据面
5. 手动验证清单覆盖全部 12 个前端可观测变更
6. Mock Agent 零外部依赖（Python 3 标准库 HTTP server）
7. helpers.sh 断言函数可在单 batch 脚本中直接使用
