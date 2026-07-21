/**
 * NexusAI gRPC ↔ JSON Transcoding Proxy
 *
 * Replaces Envoy + grpcwebproxy for local development.
 * Accepts JSON POST from the Vue frontend, converts to gRPC/protobuf,
 * forwards to the rpc_server, and returns JSON.
 *
 * Listens on :8081 (same port Vite proxy expects).
 */

import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import grpc from '@grpc/grpc-js';
import protoLoader from '@grpc/proto-loader';

// ── Config ──────────────────────────────────────────────────────────────
const PROXY_PORT = parseInt(process.env.PROXY_PORT || '8081', 10);
const GRPC_TARGET = process.env.GRPC_TARGET || 'localhost:50051';
const PROTO_DIR = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../proto'
);

// ── Load Proto Definitions ─────────────────────────────────────────────
const loaderOptions = {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
  includeDirs: [PROTO_DIR],
};

const pkgs = {};

function loadProto(file) {
  const def = protoLoader.loadSync(path.join(PROTO_DIR, file), loaderOptions);
  return grpc.loadPackageDefinition(def);
}

// Load all proto packages
const agentProto = loadProto('agent_service.proto');
const queryProto = loadProto('ai_query.proto');
const observabilityProto = loadProto('observability.proto');
const userProto = loadProto('user.proto');

// ── Create gRPC Clients ─────────────────────────────────────────────────
const clients = {};

function initClients() {
  const creds = grpc.credentials.createInsecure();

  // agent_communication.AgentCommunicationService
  clients['agent_communication.AgentCommunicationService'] =
    new agentProto.agent_communication.AgentCommunicationService(GRPC_TARGET, creds);

  // agent_communication.AIQueryService
  clients['agent_communication.AIQueryService'] =
    new queryProto.agent_communication.AIQueryService(GRPC_TARGET, creds);

  clients['agent_communication.ObservabilityService'] =
    new observabilityProto.agent_communication.ObservabilityService(GRPC_TARGET, creds);

  // agent_communication.auth.UserService
  clients['agent_communication.auth.UserService'] =
    new userProto.agent_communication.auth.UserService(GRPC_TARGET, creds);

  // agent_communication.HealthService
  clients['agent_communication.HealthService'] =
    new agentProto.agent_communication.HealthService(GRPC_TARGET, creds);

  console.log(`gRPC clients initialized → ${GRPC_TARGET}`);
}

// ── Helper: Extract auth metadata ───────────────────────────────────────
function buildMetadata(headers) {
  const meta = new grpc.Metadata();
  const auth = headers['authorization'];
  if (auth) {
    meta.add('authorization', auth);
  }
  return meta;
}

// ── Helper: Convert Buffer fields to base64 ─────────────────────────────
// B-04: protobuf bytes fields come back as Buffer objects from grpc-js.
// JSON.stringify renders them as {"type":"Buffer","data":[...]}.
// This function recursively walks the object and converts Buffers to base64.
function sanitizeBuffers(obj) {
  if (obj == null || typeof obj !== 'object') return obj;
  if (Buffer.isBuffer(obj)) return obj.toString('base64');
  if (Array.isArray(obj)) return obj.map(sanitizeBuffers);
  const result = {};
  for (const key of Object.keys(obj)) {
    result[key] = sanitizeBuffers(obj[key]);
  }
  return result;
}

// ── Unary RPC Handler ───────────────────────────────────────────────────
function unaryCall(serviceName, methodName, body, metadata) {
  return new Promise((resolve, reject) => {
    const client = clients[serviceName];
    if (!client) {
      return reject(new Error(`Unknown service: ${serviceName}`));
    }

    // grpc-js method names are camelCase
    const grpcMethod = methodName[0].toLowerCase() + methodName.slice(1);
    if (typeof client[grpcMethod] !== 'function') {
      return reject(new Error(`Unknown method: ${serviceName}.${methodName}`));
    }

    client[grpcMethod](body, metadata, (err, response) => {
      if (err) {
        reject(err);
      } else {
        resolve(response);
      }
    });
  });
}

// ── Server-Streaming RPC Handler ────────────────────────────────────────
function streamCall(serviceName, methodName, body, metadata, res) {
  const client = clients[serviceName];
  if (!client) {
    res.writeHead(500);
    res.end(`Unknown service: ${serviceName}`);
    return;
  }

  const grpcMethod = methodName[0].toLowerCase() + methodName.slice(1);
  if (typeof client[grpcMethod] !== 'function') {
    res.writeHead(500);
    res.end(`Unknown method: ${serviceName}.${methodName}`);
    return;
  }

  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    'Connection': 'keep-alive',
    'Access-Control-Allow-Origin': '*',
  });

  const stream = client[grpcMethod](body, metadata);
  let ended = false;

  stream.on('data', (event) => {
    if (ended) return;
    const json = JSON.stringify(sanitizeBuffers(event));
    res.write(`data: ${json}\n\n`);
  });

  stream.on('end', () => {
    if (ended) return;
    ended = true;
    res.write(`data: ${JSON.stringify({ event_type: 'complete' })}\n\n`);
    res.end();
  });

  stream.on('error', (err) => {
    if (ended) return;
    ended = true;
    const codeLabel = err.code != null ? err.code : 'N/A';
    console.error(`[proxy] Stream error (${serviceName}.${methodName}):`, err.message, `(code: ${codeLabel})`);
    const errJson = JSON.stringify({
      event_type: 'error',
      content: err.message || 'Stream error',
      code: err.code != null ? err.code : 'N/A',
    });
    res.write(`data: ${errJson}\n\n`);
    res.end();
  });

  // Handle client disconnect
  res.on('close', () => {
    if (!ended) {
      ended = true;
      stream.cancel();
    }
  });
}

// ── HTTP Request Handler ────────────────────────────────────────────────
function handleRequest(req, res) {
  // CORS preflight
  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'POST, OPTIONS',
      'Access-Control-Allow-Headers': 'content-type, authorization, x-api-key',
      'Access-Control-Max-Age': '86400',
    });
    return res.end();
  }

  // Only accept POST
  if (req.method !== 'POST') {
    res.writeHead(405);
    return res.end('Method Not Allowed');
  }

  // Parse URL: /<service>/<method>
  const urlPath = req.url.split('?')[0]; // strip query string
  const parts = urlPath.split('/').filter(Boolean);
  if (parts.length !== 2) {
    res.writeHead(400);
    return res.end('Invalid path. Expected /<service>/<method>');
  }

  const [serviceName, methodName] = parts;

  // Check service exists
  if (!clients[serviceName]) {
    res.writeHead(404, { 'Content-Type': 'application/json' });
    return res.end(JSON.stringify({ error: `Unknown service: ${serviceName}` }));
  }

  // Read JSON body (with 1MB size limit to prevent DoS)
  const MAX_BODY_SIZE = 1024 * 1024; // 1MB
  const contentLength = parseInt(req.headers['content-length'] || '0', 10);
  if (contentLength > MAX_BODY_SIZE) {
    res.writeHead(413, { 'Content-Type': 'application/json' });
    return res.end(JSON.stringify({ error: 'Request body too large (max 1MB)' }));
  }

  let body = '';
  let bodySize = 0;
  req.on('data', (chunk) => {
    bodySize += chunk.length;
    if (bodySize > MAX_BODY_SIZE) {
      res.writeHead(413, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'Request body too large (max 1MB)' }));
      req.destroy();
      return;
    }
    body += chunk;
  });
  req.on('end', async () => {
    if (bodySize > MAX_BODY_SIZE) return; // already responded
    let parsed;
    try {
      parsed = body ? JSON.parse(body) : {};
    } catch (e) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      return res.end(JSON.stringify({ error: 'Invalid JSON' }));
    }

    const metadata = buildMetadata(req.headers);

    // Check if server-streaming
    if (methodName === 'QueryStream' || methodName === 'ListenMessages' || methodName === 'Watch') {
      return streamCall(serviceName, methodName, parsed, metadata, res);
    }

    // Unary call
    try {
      const response = await unaryCall(serviceName, methodName, parsed, metadata);
      res.writeHead(200, {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
      });
      res.end(JSON.stringify(sanitizeBuffers(response)));
    } catch (err) {
      const codeLabel = err.code != null ? err.code : 'N/A';
      console.error(`[proxy] RPC error (${serviceName}.${methodName}):`, err.message, `(code: ${codeLabel})`);
      const status = err.code === grpc.status.UNAUTHENTICATED ? 401
        : err.code === grpc.status.NOT_FOUND ? 404
        : err.code === grpc.status.ALREADY_EXISTS ? 409
        : 500;

      res.writeHead(status, {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
      });
      res.end(JSON.stringify({
        error: err.message || 'RPC failed',
        code: err.code != null ? err.code : 'N/A',
        details: err.details,
      }));
    }
  });
}

// ── Start Server ────────────────────────────────────────────────────────
initClients();

const server = http.createServer(handleRequest);
server.listen(PROXY_PORT, () => {
  console.log(`NexusAI gRPC-JSON proxy listening on :${PROXY_PORT}`);
  console.log(`Forwarding to gRPC server at ${GRPC_TARGET}`);
});
