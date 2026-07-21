import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const proxy = fs.readFileSync(path.join(root, 'gateway/proxy/server.mjs'), 'utf8');
const client = fs.readFileSync(path.join(root, 'frontend/src/services/grpc-client.ts'), 'utf8');

test('proxy exposes ObservabilityService to the JSON frontend', () => {
  assert.match(proxy, /loadProto\('observability\.proto'\)/);
  assert.match(proxy, /\['agent_communication\.ObservabilityService'\]/);
});

test('agent metrics calls AIQueryService and unwraps the metrics response', () => {
  assert.match(
    client,
    /unaryCall<\{ agent_id: string \}, GetAgentMetricsResponse>\(\s*AI_QUERY,\s*'GetAgentMetrics'/s,
  );
  assert.match(client, /data: response\.metrics \?\? null/);
});

test('stream timeout remains active until the response reader finishes', () => {
  const clearBeforeReader = client.indexOf('clearTimeout(timeoutId)');
  const createReader = client.indexOf('const reader = resp.body?.getReader()');

  assert.ok(createReader >= 0);
  assert.ok(clearBeforeReader > createReader);
});

function escapeRegExp(value) {
  return value.replace(/[.*+?^$()|[\]\\{}]/g, '\\$&');
}

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
      assert.match(proxy, registration, 'proxy missing client registration for ' + fullName);
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
    assert.match(
      proxy,
      new RegExp(escapeRegExp(rpc)),
      'proxy source missing streaming RPC classification for ' + rpc,
    );
  }
});
