#!/bin/bash
cd /mnt/c/Users/31677/Desktop/NexusAI/agent-communication-and-tool-selection-framework

echo "=== Register Agent ==="
python3 verify/scripts/register_agent.py 2>&1 | grep -E "Token:|Register:|agents"

echo ""
echo "=== Query Test ==="
curl -s -m 10 -X POST http://127.0.0.1:5000/ \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"message/send","params":{"message":{"role":"user","parts":[{"kind":"text","text":"hello"}]}}}' 2>&1

echo ""
echo "=== Service PIDs ==="
pgrep -a 'rpc_server|orchestrator|mock_agent|redis|node' 2>&1 | head -10
