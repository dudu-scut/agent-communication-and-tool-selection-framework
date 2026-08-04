import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';

const root = path.resolve(import.meta.dirname, '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');

test('production compose uses the JSON proxy and service DNS', () => {
  const compose = read('docker-compose.yml');

  for (const service of ['postgres', 'redis', 'migrate', 'rpc-server', 'proxy', 'frontend']) {
    assert.match(compose, new RegExp(`^  ${service}:`, 'm'));
  }
  assert.match(compose, /GRPC_TARGET:\s*rpc-server:50051/);
  assert.doesNotMatch(compose, /envoy:/i);
  assert.doesNotMatch(compose, /host\.docker\.internal|172\.\d+\.\d+\.\d+/);
});

test('Vite forwards browser RPCs only to the local JSON proxy', () => {
  const vite = read('frontend/vite.config.ts');

  assert.match(vite, /target:\s*'http:\/\/localhost:8081'/);
  assert.doesNotMatch(vite, /Envoy|grpc-web/i);
});

test('run script manages a proxy PID without set -e unsafe postincrements', () => {
  const script = read('run.sh');

  assert.match(script, /proxy\.pid/);
  assert.match(script, /proxy\.log/);
  assert.doesNotMatch(script, /\(\([^\n]*\+\+\)\)/);
  assert.doesNotMatch(script, /\/mnt\/c\/Users\//);
});

test('WSL bootstrap refuses Windows mounts and documents its package checks', () => {
  const bootstrap = read('scripts/bootstrap-wsl.sh');

  assert.match(bootstrap, /\/mnt\//);
  for (const packageName of ['build-essential', 'cmake', 'pkg-config', 'libgrpc\+\+-dev', 'postgresql-client', 'redis-server']) {
    assert.ok(bootstrap.includes(packageName), `missing package check: ${packageName}`);
  }
});
