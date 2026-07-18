#!/bin/bash
ROOT="/mnt/c/Users/31677/Desktop/NexusAI/agent-communication-and-tool-selection-framework"
cd "$ROOT"

mkdir -p logs pids

# Load .env
if [ -f "$ROOT/.env" ]; then
    set -a; . "$ROOT/.env"; set +a
fi

# Kill old processes
for f in pids/*.pid; do
    [ -f "$f" ] || continue
    pid=$(cat "$f")
    kill "$pid" 2>/dev/null || true
    rm -f "$f"
done
sleep 1

echo "[1/4] Redis..."
redis-cli ping 2>/dev/null || redis-server --port 6379 --daemonize yes --loglevel notice
sleep 1
redis-cli ping && echo "  Redis OK (6379)" || { echo "  Redis FAIL"; exit 1; }

echo "[2/4] Mock Agent..."
nohup python3 verify/mock-agent/mock_agent_server.py >> logs/mock_agent.log 2>&1 &
echo $! > pids/mock_agent.pid
disown
sleep 1
kill -0 $(cat pids/mock_agent.pid) 2>/dev/null && echo "  Mock Agent OK (PID $(cat pids/mock_agent.pid), port 5100)" || { echo "  Mock Agent FAIL"; exit 1; }

echo "[3/4] gRPC Server..."
export LLM_API_KEY="${LLM_API_KEY:-}"
export LLM_MODEL="${LLM_MODEL:-}"
export LLM_API_URL="${LLM_API_URL:-}"
nohup build/server/rpc_server -p 50051 -o http://localhost:5000 >> logs/rpc_server_stdout.log 2>&1 &
echo $! > pids/rpc_server.pid
disown
sleep 2
kill -0 $(cat pids/rpc_server.pid) 2>/dev/null && echo "  gRPC Server OK (PID $(cat pids/rpc_server.pid), port 50051)" || { echo "  gRPC Server FAIL"; tail -20 logs/rpc_server_stdout.log; exit 1; }

echo "[4/4] Orchestrator..."
nohup python3 examples/orchestrator_agent.py >> logs/orchestrator.log 2>&1 &
echo $! > pids/orchestrator.pid
disown
sleep 2
kill -0 $(cat pids/orchestrator.pid) 2>/dev/null && echo "  Orchestrator OK (PID $(cat pids/orchestrator.pid), port 5000)" || echo "  Orchestrator WARN (may have exited, check logs)"

echo ""
echo "=========================================="
echo "  All Backend Services Started"
echo "=========================================="
echo "  Redis:           localhost:6379"
echo "  Mock Agent:      localhost:5100"
echo "  gRPC Server:     localhost:50051"
echo "  Orchestrator:    localhost:5000"
echo "=========================================="

# Verify ports
echo ""
echo "Port check:"
ss -tlnp 2>/dev/null | grep -E '50051|5100|5000|6379' || echo "  (ss not available, skipping)"
