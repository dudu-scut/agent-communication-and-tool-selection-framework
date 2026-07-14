#!/bin/bash
# One-click start ALL services — run inside WSL with: bash start_all.sh
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

echo "=== NexusAI — Starting All Services ==="

# 0. Kill old instances
pkill -f rpc_server 2>/dev/null || true
pkill -f orchestrator_agent 2>/dev/null || true
pkill -f mock_agent_server 2>/dev/null || true
sleep 1

# 1. Redis
redis-cli ping 2>/dev/null || redis-server --port 6379 --daemonize yes --loglevel notice
echo "[OK] Redis"

# 2. Mock Agent (with heartbeat thread)
cd "$ROOT/verify/mock-agent"
nohup python3 mock_agent_server.py &>/tmp/mock_agent.log &
echo "[OK] Mock Agent (PID $!)"

# 3. Node gRPC Proxy (HTTP→gRPC bridge for orchestrator)
cd "$ROOT/gateway/proxy"
export PATH="$HOME/.local/bin:$PATH"
nohup node server.mjs &>/tmp/node_proxy.log &
echo "[OK] Node Proxy (PID $!)"
sleep 2

# 4. Orchestrator (A2A agent on port 5000)
cd "$ROOT/examples"
nohup python3 orchestrator_agent.py &>/tmp/orchestrator.log &
echo "[OK] Orchestrator (PID $!)"
sleep 2

# 5. gRPC Server (port 50051, heartbeat timeout: 300s)
cd "$ROOT/build/server"
nohup ./rpc_server &>/tmp/rpc_server.log &
RPC_PID=$!
echo "[OK] gRPC Server (PID $RPC_PID)"
sleep 4

# 6. Register mock agent so orchestrator can find it
echo ""
echo "=== Registering Agents ==="
cd "$ROOT"
python3 verify/scripts/register_agent.py 2>&1 | head -3

echo ""
echo "=== All Services Ready ==="
echo "  Redis:           localhost:6379"
echo "  Mock Agent:      localhost:5100"
echo "  Node Proxy:      localhost:8081 → gRPC :50051"
echo "  Orchestrator:    localhost:5000"
echo "  gRPC Server:     localhost:50051"
echo ""
echo "Frontend: cd frontend && npm run dev     (Windows terminal)"
echo "E2E Test: python3 e2e_full_test.py       (WSL terminal)"
echo ""
echo "Monitor mode — Ctrl+C to stop all services"
trap 'echo \"\"; echo \"Shutting down...\"; kill $RPC_PID 2>/dev/null; pkill -f orchestrator_agent; pkill -f mock_agent_server; exit 0' INT TERM
while kill -0 $RPC_PID 2>/dev/null; do sleep 10; done
echo "gRPC server died — all services stopped"
