import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';

const root = path.resolve(import.meta.dirname, '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');

test('MCP-off build graph does not require MCP targets or headers', () => {
  const rootCmake = read('CMakeLists.txt');
  const orchestratorCmake = read('orchestrator/CMakeLists.txt');
  const main = read('server/src/main.cpp');

  assert.match(rootCmake, /option\(ENABLE_MCP[^\n]+OFF\)/);
  assert.match(orchestratorCmake, /if\(ENABLE_MCP\)[\s\S]*agent_rpc_mcp[\s\S]*endif\(\)/);
  assert.match(orchestratorCmake, /target_compile_definitions\(orchestrator[^)]*AGENT_RPC_ENABLE_MCP/s);
  assert.match(main, /#ifdef AGENT_RPC_ENABLE_MCP/);
});

test('Docker build context excludes secrets and generated artifacts', () => {
  const dockerignore = read('.dockerignore');
  for (const entry of ['.env', 'certs/', 'node_modules/', 'build/', '.git/', '.superpowers/', 'logs/', 'pids/']) {
    assert.ok(dockerignore.split(/\r?\n/).includes(entry), `missing ${entry}`);
  }
  assert.match(read('backend/Dockerfile'), /postgresql-client/);
});

test('deprecated Envoy deployment artifacts are absent from supported paths', () => {
  for (const file of ['gateway/envoy.sh', 'gateway/envoy.yaml', 'gateway/nginx.conf']) {
    assert.equal(fs.existsSync(path.join(root, file)), false, `${file} should be removed`);
  }
  for (const file of ['README.md', 'CLAUDE.md', 'run.sh', 'frontend/vite.config.ts']) {
    assert.doesNotMatch(read(file), /Envoy|gRPC-Web/i);
  }
});
