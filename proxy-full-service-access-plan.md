# NexusAI Node 代理全服务接入实施方案

> **For agentic workers:** 实施时按本文的 Red → Green 顺序执行；不要同时扩展 WebSocket、Envoy 或前端业务页面。

**目标：** 让 Node gRPC-JSON 代理覆盖服务端已经注册的全部 9 个 gRPC Service，同时明确拒绝当前 HTTP JSON 通道无法表达的客户端流和双向流。

**架构：** 沿用现有显式 proto 加载和 client 注册方式，不引入新的抽象层。用两个 RPC 路径集合区分“服务端流”和“不支持的上传流”，再用一项契约测试防止 proto 新增服务后代理再次漏接。

**技术栈：** Node.js ESM、node:http、@grpc/grpc-js、@grpc/proto-loader、protobuf、SSE、node:test。

## 全局约束

- 零新增依赖，不修改 C++ 服务端、proto 定义或前端业务页面。
- 只修改 gateway/proxy/server.mjs 和 gateway/proxy/test/frontend-contract.test.mjs。
- Unary 与 server-streaming 可通过代理；client-streaming 与 bidirectional-streaming 返回 HTTP 501。
- 保留现有 JSON 请求格式、Bearer Token metadata、1 MB body 限制和 gRPC 状态码映射。
- 不做动态 protobuf descriptor 反射；契约测试负责发现显式注册遗漏。

---

## 1. 问题与实际范围

服务端在 server/src/rpc_server.cpp 中注册了 9 个服务，当前代理只初始化了其中 5 个：

| Service | Proto | 当前代理 |
| --- | --- | --- |
| AgentCommunicationService | agent_service.proto | 已接入 |
| HealthService | agent_service.proto | 已接入 |
| AIQueryService | ai_query.proto | 已接入 |
| UserService | user.proto | 已接入 |
| ObservabilityService | observability.proto | 已接入 |
| OrchestrationService | orchestration.proto | 缺失 |
| SharingService | sharing.proto | 缺失 |
| AgentLifecycleService | agent_lifecycle.proto | 缺失 |
| UserExperienceService | user_experience.proto | 缺失 |

遗漏服务会在代理的 clients 表检查阶段返回 Unknown service，请求不会到达 C++ gRPC Server。

### RPC 流类型边界

| 类型 | 项目方法 | 方案 |
| --- | --- | --- |
| Unary | ReplayQuery、ShareSession、SubmitFeedback、SandboxQuery 等 | 继续调用 unaryCall |
| Server streaming | QueryStream、ListenMessages、Watch、ObserveSession | 通过 streamCall 转为 SSE |
| Client streaming | BatchSendMessages | 返回 501 |
| Bidirectional streaming | RealTimeCommunication | 返回 501 |

HTTP JSON 单次 POST 无法表达持续上传的数据流。用 501 明确能力边界，比将这两个方法误送入 unaryCall 后产生运行时错误更可靠。

## 2. 方案比较

### 方案 A：只追加 4 个缺失 clien

改动最小，但 server-streaming 判断仍是散落的 methodName 条件，未来新增 proto 服务仍可能漏接。

### 方案 B：显式补齐 + RPC 类型集合 + 契约测试（推荐）

保留现有结构，仅增加 4 个 proto/client、2 个 Set 和 1 项测试。它修复当前问题，也能在未来服务遗漏时让测试失败，成本和收益最平衡。

### 方案 C：protobuf descriptor 自动发现

可以减少显式配置，但要处理 package、service constructor、RPC 流类型和授权策略，复杂度明显超过当前开发代理的需要。

## 3. 最小代码 Demo

### 3.1 加载缺失 proto

在 gateway/proxy/server.mjs 现有 loadProto 调用后增加：

~~~javascrip
const orchestrationProto = loadProto('orchestration.proto');
const sharingProto = loadProto('sharing.proto');
const lifecycleProto = loadProto('agent_lifecycle.proto');
const experienceProto = loadProto('user_experience.proto');
~~~

### 3.2 初始化缺失 clien

在 initClients() 中复用同一个 GRPC_TARGET 和 creds：

~~~javascrip
clients['agent_communication.OrchestrationService'] =
  new orchestrationProto.agent_communication.OrchestrationService(
    GRPC_TARGET,
    creds,
  );

clients['agent_communication.SharingService'] =
  new sharingProto.agent_communication.SharingService(
    GRPC_TARGET,
    creds,
  );

clients['agent_communication.AgentLifecycleService'] =
  new lifecycleProto.agent_communication.AgentLifecycleService(
    GRPC_TARGET,
    creds,
  );

clients['agent_communication.UserExperienceService'] =
  new experienceProto.agent_communication.UserExperienceService(
    GRPC_TARGET,
    creds,
  );
~~~

不抽取通用工厂：这里只增加 4 个 client，显式代码与当前项目风格一致，也更容易核对 package 路径。

### 3.3 集中声明流式方法

在 clients 定义附近增加：

~~~javascrip
const SERVER_STREAMING_RPCS = new Set([
  'agent_communication.AIQueryService/QueryStream',
  'agent_communication.AgentCommunicationService/ListenMessages',
  'agent_communication.HealthService/Watch',
  'agent_communication.SharingService/ObserveSession',
]);

const UNSUPPORTED_STREAMING_RPCS = new Set([
  'agent_communication.AgentCommunicationService/BatchSendMessages',
  'agent_communication.AgentCommunicationService/RealTimeCommunication',
]);
~~~

使用完整的 service/method 路径，避免不同 Service 中出现同名方法时被错误分类。

### 3.4 修改请求分派

替换 handleRequest() 中基于 methodName 的流式判断：

~~~javascrip
const rpcPath = serviceName + '/' + methodName;

if (UNSUPPORTED_STREAMING_RPCS.has(rpcPath)) {
  res.writeHead(501, {
    'Content-Type': 'application/json',
    'Access-Control-Allow-Origin': '*',
  });
  return res.end(JSON.stringify({
    error: 'RPC streaming mode is not supported by the JSON proxy',
    rpc: rpcPath,
  }));
}

if (SERVER_STREAMING_RPCS.has(rpcPath)) {
  return streamCall(serviceName, methodName, parsed, metadata, res);
}
~~~

其余方法继续进入现有 unaryCall，不改变现有响应和错误映射。

## 4. 契约测试 Demo

扩展 gateway/proxy/test/frontend-contract.test.mjs。测试从 8 个 proto 文件提取 package 与 service 名，再确认代理源码包含相应 client key。新增 Service 却忘记代理注册时，测试会立即失败。

~~~javascrip
const protoFiles = [
  'agent_service.proto',
  'ai_query.proto',
  'user.proto',
  'observability.proto',
  'orchestration.proto',
  'sharing.proto',
  'agent_lifecycle.proto',
  'user_experience.proto',
];

function escapeRegExp(value) {
  return value.replace(/[.*+?^$()|[\]\\{}]/g, '\\$&');
}

test('proxy registers every gRPC service declared by project protos', () => {
  for (const file of protoFiles) {
    const source = fs.readFileSync(path.join(root, 'proto', file), 'utf8');
    const packageName = /\bpackage\s+([\w.]+)\s*;/.exec(source)?.[1];

    assert.ok(packageName, 'missing package in ' + file);

    for (const match of source.matchAll(/\bservice\s+(\w+)\s*\{/g)) {
      const fullName = packageName + '.' + match[1];
      const registration = new RegExp(
        "clients\\['" + escapeRegExp(fullName) + "'\\]",
      );
      assert.match(proxy, registration, 'proxy missing ' + fullName);
    }
  }
});

test('proxy explicitly classifies every streaming RPC', () => {
  const serverStreaming = [
    'agent_communication.AIQueryService/QueryStream',
    'agent_communication.AgentCommunicationService/ListenMessages',
    'agent_communication.HealthService/Watch',
    'agent_communication.SharingService/ObserveSession',
  ];
  const unsupported = [
    'agent_communication.AgentCommunicationService/BatchSendMessages',
    'agent_communication.AgentCommunicationService/RealTimeCommunication',
  ];

  for (const rpc of [...serverStreaming, ...unsupported]) {
    assert.match(proxy, new RegExp(escapeRegExp(rpc)));
  }
});
~~~

这项测试不启动 gRPC Server，不依赖 Redis，也不需要新增测试框架。

## 5. 实施步骤

### Task 1：先建立失败的契约测试

**文件：**

- 修改：gateway/proxy/test/frontend-contract.test.mjs
- 读取：proto/*.proto
- 读取：gateway/proxy/server.mjs

- [ ] 加入“9 个 Service 均存在 client key”的测试。
- [ ] 加入“6 个 streaming RPC 均被分类”的测试。
- [ ] 运行 node --test gateway/proxy/test/frontend-contract.test.mjs。
- [ ] 确认测试因缺少 OrchestrationService 等 4 个 client 而失败，而不是语法或路径错误。

### Task 2：补齐代理并变绿

**文件：**

- 修改：gateway/proxy/server.mjs
- 测试：gateway/proxy/test/frontend-contract.test.mjs

- [ ] 加载 4 个缺失 proto。
- [ ] 初始化 4 个缺失 gRPC client。
- [ ] 加入 SERVER_STREAMING_RPCS 与 UNSUPPORTED_STREAMING_RPCS。
- [ ] 按完整 RPC 路径分派 server stream 或返回 501。
- [ ] 运行 node --check gateway/proxy/server.mjs，预期退出码 0。
- [ ] 运行 node --test gateway/proxy/test/frontend-contract.test.mjs，预期全部通过。
- [ ] 运行 git diff --check，预期无输出且退出码 0。

## 6. 验收标准

1. clients 包含服务端注册的全部 9 个 Service。
2. ObserveSession 通过 SSE 路径处理。
3. BatchSendMessages 和 RealTimeCommunication 返回 HTTP 501 JSON，不进入 unaryCall。
4. 原有 Query、QueryStream、登录、Agent 列表、指标和 Observability 行为不变。
5. proto 新增 Service 而代理未注册时，契约测试失败。
6. 无新增 npm dependency、配置文件、生成代码或额外运行进程。

## 7. 后续但不纳入本次范围

只有当前端确实需要持续上传或双向实时通信时，才评估 WebSocket 或完整 gRPC-Web 流代理。届时应把长连接认证、背压、取消传播、心跳和连接恢复作为独立设计，不与本次全服务可达性补丁捆绑。\n