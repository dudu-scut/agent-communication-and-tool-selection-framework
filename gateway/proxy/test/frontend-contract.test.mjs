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
