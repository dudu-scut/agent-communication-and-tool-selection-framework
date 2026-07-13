#!/bin/bash
# One-click start ALL services — run inside WSL with: bash start_all.sh
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

echo "=== NexusAI — Starting All Services ==="

# 1. Redis
redis-cli ping 2>/dev/null || redis-server --port 6379 --daemonize yes --loglevel notice
echo "[OK] Redis"

# 2. Mock Agent
cd verify/mock-agent
nohup python3 mock_agent_server.py &>/tmp/mock_agent.log &
echo "[OK] Mock Agent (PID $!)"

# 3. Node gRPC Proxy
cd "$ROOT/gateway/proxy"
export PATH="$HOME/.local/bin:$PATH"
nohup node server.mjs &>/tmp/node_proxy.log &
echo "[OK] Node Proxy (PID $!)"
sleep 2

# 4. Orchestrator
cd "$ROOT/examples"
nohup python3 orchestrator_agent.py &>/tmp/orchestrator.log &
echo "[OK] Orchestrator (PID $!)"
sleep 2

# 5. gRPC Server
cd "$ROOT/build/server"
nohup ./rpc_server &>/tmp/rpc_server.log &
RPC_PID=$!
echo "[OK] gRPC Server (PID $RPC_PID)"
sleep 3

echo ""
echo "=== All Services Started ==="
echo "  Redis:           localhost:6379"
echo "  Mock Agent:      localhost:5100"
echo "  Node Proxy:      localhost:8081 → gRPC :50051"
echo "  Orchestrator:    localhost:5000"
echo "  gRPC Server:     localhost:50051"
echo ""
echo "Frontend: cd frontend && npm run dev"
echo "Verify:   ./run.sh verify"
echo ""
echo "Waiting for gRPC server (PID $RPC_PID)..."
trap 'kill $RPC_PID 2>/dev/null; exit 0' INT TERM
while kill -0 $RPC_PID 2>/dev/null; do sleep 10; done
echo "gRPC server died"
