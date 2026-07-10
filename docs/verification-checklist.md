# NexusAI 验证清单 — 手动部分

每个项已验证打 ✅，发现问题备注。预计耗时 ~20 分钟。

## 前置条件

- [ ] `./run.sh start` 启动全部服务（含 Mock Agent: `./run.sh start-mock-agent`）
- [ ] `npm run dev` 启动前端
- [ ] 浏览器打开 http://localhost:5173

---

## Batch 1-2：基础体验

- [ ] ChatView 发送消息后，消息气泡下方显示 trace 摘要（如"路由 12ms → Agent 856ms"）
- [ ] 点赞按钮点击后变绿并保持选中
- [ ] 点踩按钮点击后变红

## Batch 3-4：UX 核心

- [ ] Activity Panel 右侧实时展示 Agent 工作步骤（💭→🔧→✅）
- [ ] DAG 预览：复杂查询触发多 Agent 编排时展示 Mermaid 流程图
- [ ] AgentSelector：多候选 Agent 时展示选择面板（指标对比）

## Batch 5：管理后台

- [ ] AdminView 健康度仪表盘展示 Agent 状态灯（绿/黄/红）
- [ ] AdminView 预算面板展示用户预算使用量
- [ ] AdminView 查询重放：输入 trace_id 查看调用链时间线

## Batch 6：扩展功能

- [ ] AdminView 定时任务管理页（增删改查 + 手动触发 + 执行历史）
- [ ] AdminView 灰度部署面板（流量比例 + 指标对比 + 推进/回滚按钮）
- [ ] ChatView 点击"导出 Markdown"下载文件内容正确（含 NexusAI header + 对话时间线）

## Batch 7：增长留存

- [ ] AgentSandbox 页面：Agent 卡片墙 + "快速试用"按钮
- [ ] CompareView：三列并排展示不同 Agent 回答 + 底部对比摘要（延迟/成本/长度）
- [ ] ShareView：无痕窗口打开分享链接可查看只读会话
- [ ] TemplateMarket：模板卡片展示 + "使用模板"按钮创建新会话

## Batch 8：协议安全

- [ ] （无前端可观测变更 — 纯后端，自动化已覆盖）

---

## 问题记录

| 编号 | 批次 | 问题描述 | 严重度 |
|------|------|---------|--------|
|      |      |         |        |
