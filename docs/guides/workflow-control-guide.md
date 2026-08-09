# 工作流控制指南（Sandbox / Intervention / Undo / Autonomy / Compare）

## 概述

NexusAI 的工作流控制能力让用户在"完全自动"与"步步确认"之间自由选择：沙箱（Sandbox）隔离高风险试跑、自主权分级（Autonomy）决定是否弹窗确认、人工干预（Intervention）承接用户决策、撤销（Undo）以单事务 CAS 原子回滚、对比（Compare）让多个 Agent 并行答题。

这些能力有一个共同的设计底色：**owner-scoped 的单条 SQL CAS（compare-and-swap）**——并发决策靠数据库一行受影响与否来仲裁，而不是应用层加锁；失败路径可安全重试，不会留下半完成状态。

## 核心特性

| 特性 | 描述 |
|------|------|
| 单条 SQL CAS | Intervention 解决与 Undo 回滚都是一条带 WHERE 前置状态的 UPDATE |
| Owner-scoped | 所有 CAS 的 WHERE 子句都绑定 owner_id，跨用户操作必然落空 |
| 事务原子回滚 | Undo 的 CAS 与 inverse 补偿在同一事务，失败整体回滚可重试 |
| 沙箱隔离记忆 | sandbox 请求不触碰长期记忆写入，但照常计入预算 |
| 自主权持久化 | autonomy_settings 落 PostgreSQL，跨重启生效 |
| 并行对比 | CompareAgents 最多 3 个健康 Agent，各自独立 request_id |

## 背景：为什么用单条 SQL CAS

工作流控制的典型并发场景：用户在两个标签页同时对同一个待确认操作点了"继续"和"跳过"。传统做法要在应用层加分布式锁；NexusAI 的做法是把前置状态写进 UPDATE 的 WHERE 子句：

```sql
UPDATE interventions SET state = $3, ...
WHERE id = $1 AND owner_id = $2 AND state = 'pending'
```

只有第一个到达的事务能匹配到 `state = 'pending'` 的行（受影响行数 > 0），后到的事务受影响行数为 0，自然得到"已被解决"的判定。仲裁发生在 PostgreSQL 的行锁层面，无需应用层锁，也无锁泄漏问题。

## 机制一：Sandbox 沙箱查询

`UserExperienceService/SandboxQuery` 是安全试跑入口：请求走的是与正式查询**同一条 durable 管线**（唯一的执行入口），只是带上了 sandbox 标记。

隔离边界：

| 维度 | 沙箱行为 |
|------|------|
| 长期记忆 | 管线守卫 `if (success && !request->sandbox())` —— 沙箱成功也不写长期记忆 |
| Token 预算 | **不豁免**，与主链路共用同一配额 |
| 会话标识 | context_id 使用 `sandbox-<request_id>` 前缀，与真实会话隔离 |
| 台账 | 额外落 `sandbox_runs` 行，与管线的 query_log/trace/成本行并列可审计 |

响应中的 `request_id` 是真实管线执行的标识，由此可追溯 query_log 与 trace。

## 机制二：Autonomy 自主权分级

`AgentLifecycleService/SetAutonomyLevel` 设置用户对某 Agent 的自主程度，持久化在 `autonomy_settings` 表（owner 取认证上下文，请求体的 `user_id` 被忽略；`level` 仅接受 1..4）。

分级对沙箱执行的门控语义：

- **level ≤ 2（或未设置，保守默认）**：`SandboxQuery` 不直接执行，而是创建一条 `pending` 状态的 intervention，响应返回 `intervention_required = true` 与 `intervention_id`；
- **level > 2**：直接执行沙箱查询。

也就是说自主权不是摆设：低自主权下，用户必须先通过 Intervention 决策，执行才会发生——决策具有真实因果效力。

## 机制三：Intervention 人工干预

当执行被自主权门控拦下，会落一条 `interventions` 表的 pending 记录。用户通过 `UserExperienceService/InterventionResponse` 提交决策：

| decision | 语义 |
|------|------|
| `PROCEED` | 确认执行（触发延迟的沙箱执行） |
| `MODIFY` | 修改后执行（`modification_text` 非空时覆盖原请求文本） |
| `SKIP` | 放弃本次执行 |
| `ABORT` | 终止 |

### 单条 CAS 解决语义

服务端 `resolveIntervention` 的核心是一条 SQL：

```sql
UPDATE interventions SET state = $3, decision = $3,
  edited_request = CASE WHEN $4 <> '' THEN $4 ELSE edited_request END,
  updated_at = NOW()
WHERE id = $1 AND owner_id = $2 AND state = 'pending'
```

三种结果，全部由受影响行数推导：

| 结果 | 判定条件 |
|------|------|
| kResolved | 受影响行数 > 0（本次调用完成状态迁移） |
| kAlreadyResolved | 行存在但 state 已不是 pending（并发/重复决策） |
| kNotFound | owner 名下查无此行 |

`PROCEED` / `MODIFY` 决策成功后，响应中的 `executed_request_id` 指向延迟沙箱执行的真实 request_id；可逆决策还会返回 `undo_action_id`。

## 机制四：Undo 原子撤销

`AgentLifecycleService/UndoAction`（以 `action_id` 定位 `undo_actions` 行）的撤销是**单事务内的 CAS + inverse 补偿**：

```sql
-- 同一事务，两步
UPDATE undo_actions SET undone_at = NOW(), updated_at = NOW()
WHERE id = $2 AND owner_id = $1 AND undone_at IS NULL RETURNING id;

UPDATE interventions SET state = 'pending', decision = '', updated_at = NOW()
WHERE id = $2 AND owner_id = $1 AND state <> 'pending' RETURNING id;
```

关键性质：

1. **CAS on undone_at**：`undone_at IS NULL` 保证撤销动作只被消费一次，重复撤销返回 ALREADY 语义；
2. **inverse 失败 → 整体回滚**：若干预记录不可恢复（不存在/非本 owner/已是 pending），服务端主动抛错让事务回滚——`undone_at` 写入一并撤销，撤销动作**未被消费，用户可安全重试**；
3. **24 小时有效期**：`undo_actions.expires_at = NOW() + INTERVAL '24 hours'`，过期的撤销窗口不保证可逆。

四种结果：kApplied（成功）、kNotFound、kAlreadyUndone、kInverseFailed（已回滚，可重试）。

## 机制五：CompareAgents 并行对比

`AgentLifecycleService/CompareAgents` 让同一个问题并行发给最多 3 个健康 Agent：

- 每个 Agent 走独立的 durable 管线执行（独立 `request_id` / `conversation`），成本照常入账；
- 单个 Agent 失败不会被整体成功掩盖——每个结果各自携带 `status`（completed / failed / cancelled）与 `error`；
- 整轮结果落 `compare_runs` 表（run_status：completed / partial / failed / cancelled），`GetAgentCompare` 返回的 `runs` 就是这份持久化台账的真实读取，空列表即真实空态，绝不伪造指标；
- 重复的 agent_id 会被拒绝，超过 3 个也会被拒绝。

## 使用示例

### 低自主权下的"确认→执行"全流程（经 HTTP 网关）

```bash
GW=http://localhost:8081
AUTH="Authorization: Bearer $TOKEN"

# 1. 设置保守自主权（level=1）
curl -X POST $GW/agent_communication.AgentLifecycleService/SetAutonomyLevel \
  -H "$AUTH" -H 'Content-Type: application/json' \
  -d '{"agent_id":"math-agent-127.0.0.1-9001","level":1}'

# 2. 沙箱查询 → 被门控，返回 intervention_id
curl -X POST $GW/agent_communication.UserExperienceService/SandboxQuery \
  -H "$AUTH" -H 'Content-Type: application/json' \
  -d '{"query_text":"帮我估算一下这道题","agent_id":"math-agent-127.0.0.1-9001"}'
# → {"intervention_required":true,"intervention_id":"ivt-xxxx",...}

# 3. 确认执行
curl -X POST $GW/agent_communication.UserExperienceService/InterventionResponse \
  -H "$AUTH" -H 'Content-Type: application/json' \
  -d '{"intervention_id":"ivt-xxxx","decision":"PROCEED"}'
# → {"new_state":"PROCEED","executed_request_id":"req-xxxx",...}

# 4. 若该决策可逆，用返回的 undo_action_id 撤销
curl -X POST $GW/agent_communication.AgentLifecycleService/UndoAction \
  -H "$AUTH" -H 'Content-Type: application/json' \
  -d '{"action_id":"undo-xxxx"}'
```

### 并发决策的仲裁

两个客户端同时对同一 `intervention_id` 提交 `PROCEED` 与 `SKIP`：第一个写入者完成状态迁移，第二个收到"已解决"语义的结果——干预记录只会落在其中一个终态上，不会既执行又跳过。

### 多 Agent 对比

```bash
curl -X POST $GW/agent_communication.AgentLifecycleService/CompareAgents \
  -H "$AUTH" -H 'Content-Type: application/json' \
  -d '{"question":"解释幂等性","agent_ids":["agent-a","agent-b"]}'
# → {"run_id":"...","run_status":"completed|partial","results":[...]}
```

## 注意事项

1. **owner 只取认证上下文**：`SetAutonomyLevel` 请求体中的 `user_id` 被显式忽略；不要依赖请求体传身份。
2. **Undo 有 24 小时窗口**：`undo_actions` 过期后不可撤销；需要长期可逆的操作应在窗口内处理。
3. **kInverseFailed 可重试**：inverse 失败会整体回滚，撤销动作未被消费；直接重试即可，不要当作"已撤销"处理。
4. **沙箱不是免费额度**：sandbox 流量计入同一份预算，压测或批量试跑时注意配额。
5. **CompareAgents 上限 3 个 Agent**：且只接受健康状态的候选；agent_ids 重复会被拒绝。
6. **决策值区分大小写**：decision 使用 `PROCEED` / `MODIFY` / `SKIP` / `ABORT` 大写常量。

## 相关文档

- [Durable Query Pipeline 指南](durable-query-pipeline-guide.md)
- [分享与资产指南](sharing-and-assets-guide.md)
- [Agent 接入指南](../../agent-integration-guide.md)
