import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';

const root = path.resolve(import.meta.dirname, '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');
const serviceBlock = (compose, service) => {
  const start = compose.indexOf(`  ${service}:`);
  assert.ok(start >= 0, `missing Compose service block: ${service}`);
  const block = compose.slice(start);
  const nextService = block.search(/\n(?=  [a-z0-9_-]+:|networks:|volumes:|secrets:)/);
  return block.slice(0, nextService >= 0 ? nextService : block.length);
};

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

test('Compose gates the JSON gateway and publishes only HTTPS web traffic', () => {
  const compose = read('docker-compose.yml');
  const rpcServer = serviceBlock(compose, 'rpc-server');
  const proxy = serviceBlock(compose, 'proxy');
  const frontend = serviceBlock(compose, 'frontend');

  assert.ok(fs.existsSync(path.join(root, 'docker/Dockerfile.rpc-server')));
  assert.match(compose, /dockerfile:\s*docker\/Dockerfile\.rpc-server/);
  assert.match(rpcServer, /healthcheck:/);
  assert.match(proxy, /depends_on:\s*\n\s+rpc-server:\s*\n\s+condition:\s+service_healthy/);
  assert.match(proxy, /healthcheck:/);
  assert.match(frontend, /depends_on:\s*\n\s+proxy:\s*\n\s+condition:\s+service_healthy/);
  assert.match(frontend, /healthcheck:/);
  assert.match(frontend, /ports:\s*\n\s+-\s*["']?8443:8443/);
  assert.doesNotMatch(compose, /-\s*["']?\d+:8080/);
  assert.doesNotMatch(compose, /-\s*["']?\d+:8081/);
  assert.match(compose, /GRPC_TARGET:\s*rpc-server:50051/);
  assert.doesNotMatch(compose, /envoy:/i);
});

test('Production Nginx terminates TLS and forwards unary and streaming JSON RPCs', () => {
  const nginx = read('frontend/nginx.conf');

  assert.match(nginx, /listen\s+8443\s+ssl/);
  assert.match(nginx, /ssl_certificate\s+\/run\/secrets\/frontend_tls_cert/);
  assert.match(nginx, /ssl_certificate_key\s+\/run\/secrets\/frontend_tls_key/);
  assert.match(nginx, /location\s+\/agent_communication\./);
  assert.match(nginx, /proxy_pass\s+http:\/\/proxy:8081/);
  assert.match(nginx, /proxy_buffering\s+off/);
  assert.match(nginx, /proxy_read_timeout\s+300s/);
  assert.doesNotMatch(nginx, /listen\s+8080/);
  assert.doesNotMatch(nginx, /envoy|grpc-web/i);
});

test('Node workspaces expose the required scripts and keep lockfile roots aligned', () => {
  for (const directory of ['gateway/proxy', 'frontend']) {
    const packageJson = JSON.parse(read(`${directory}/package.json`));
    const lockJson = JSON.parse(read(`${directory}/package-lock.json`));
    for (const script of ['test', 'typecheck', 'lint', 'build']) {
      assert.equal(typeof packageJson.scripts?.[script], 'string', `${directory} missing ${script} script`);
    }
    assert.equal(lockJson.packages?.['']?.name, packageJson.name, `${directory} lockfile root name drifted`);
  }
});

test('WSL bootstrap validates Docker Desktop integration and development TLS material', () => {
  const bootstrap = read('scripts/bootstrap-wsl.sh');

  assert.match(bootstrap, /docker\s+info/);
  assert.match(bootstrap, /docker\s+compose\s+version/);
  assert.match(bootstrap, /subjectAltName/);
  assert.match(bootstrap, /openssl\s+verify/);
  assert.match(bootstrap, /checkend/);
  assert.match(bootstrap, /certs\/dev/);
});

test('Default CMake keeps MCP opt-in without global compiler flags', () => {
  const rootCmake = read('CMakeLists.txt');
  const testsCmake = read('tests/CMakeLists.txt');

  assert.match(rootCmake, /cmake_minimum_required\(VERSION\s+3\.20\)/);
  assert.match(rootCmake, /set\(CMAKE_CXX_STANDARD\s+20\)/);
  assert.match(rootCmake, /option\(ENABLE_MCP[^\n]+OFF\)/);
  assert.doesNotMatch(rootCmake, /CMAKE_CXX_FLAGS/);
  assert.match(testsCmake, /if\(ENABLE_MCP\)[\s\S]*test_mcp_integration[\s\S]*endif\(\)/);
  assert.match(testsCmake, /if\(ENABLE_MCP\)[\s\S]*test_rag_mcp_properties[\s\S]*endif\(\)/);
});

test('run.sh uses portable Bash paths and cleans failed background services', () => {
  const script = read('run.sh');

  assert.match(script, /^#!\/usr\/bin\/env bash/m);
  assert.match(script, /trap\s+/);
  assert.match(script, /wait\s+/);
  assert.match(script, /proxy\.pid/);
  assert.match(script, /proxy\.log/);
  assert.doesNotMatch(script, /npm\s+install/);
  assert.doesNotMatch(script, /\(\([^\n]*\+\+\)\)/);
  assert.doesNotMatch(script, /\/mnt\/c\/Users\//);
});

test('Frontend declares Vite environment types used by the JSON client', () => {
  const envFile = path.join(root, 'frontend/src/env.d.ts');

  assert.ok(fs.existsSync(envFile), 'frontend/src/env.d.ts is required');
  const source = fs.readFileSync(envFile, 'utf8');
  assert.match(source, /vite\/client/);
  assert.match(source, /VITE_API_BASE/);
});
