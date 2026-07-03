# NexusAI Demo Agents

三个简单的 Demo Agent，用于验证 NexusAI 平台的 A2A 接入全链路。

## 前置条件

- NexusAI 平台已启动（Redis + rpc_server + Node proxy）
- Python 3.8+

```bash
pip install -r requirements.txt
```

## Demo Agents

| Agent | 端口 | 技能 | 说明 |
|-------|------|------|------|
| echo-agent | 9090 | echo | 原样返回用户输入，最简验证 |
| math-agent | 9091 | math, equation | 算术计算和方程求解 |
| translator-agent | 9092 | translate | 中英互译（规则词典演示） |

## 使用方法

### 1. 启动 Demo Agent

```bash
# 终端1: 启动 Echo Agent
python echo_agent.py

# 终端2: 启动 Math Agent
python math_agent.py

# 终端3: 启动 Translator Agent
python translator_agent.py
```

每个 Agent 启动后会自动注册到 NexusAI 平台并维持心跳。

### 2. 直接测试 A2A 接口

```bash
# 测试 Echo Agent（同步）
curl -X POST http://127.0.0.1:9090/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "message/send",
    "id": "test-1",
    "params": {
      "message": {
        "message_id": "m1",
        "context_id": "ctx-1",
        "role": "user",
        "parts": [{"type": "text", "text": "Hello NexusAI!"}]
      }
    }
  }'

# 测试 Math Agent（计算）
curl -X POST http://127.0.0.1:9091/ \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "message/send",
    "id": "test-2",
    "params": {
      "message": {
        "message_id": "m2",
        "context_id": "ctx-1",
        "role": "user",
        "parts": [{"type": "text", "text": "2 + 3 * 4"}]
      }
    }
  }'
```

### 3. 验证平台注册

```bash
# 通过 NexusAI proxy 查看已注册的 Agents
curl -X POST http://127.0.0.1:8081/agent_communication.AgentCommunicationService/GetAgents \
  -H "Content-Type: application/json" \
  -d '{"filter":"","limit":10,"offset":0}'
```

### 4. 前端测试

启动前端后登录，在聊天框输入数学表达式或翻译请求，平台会自动路由到对应 Agent。

## 架构

```
Demo Agent (Flask:9090/9091/9092)
  ├── A2A HTTP 接口: 处理 message/send 和 message/stream
  ├── AgentCard: /.well-known/agent-card.json
  └── gRPC 注册: 通过 Node proxy(:8081) → rpc_server(:50051)
```
