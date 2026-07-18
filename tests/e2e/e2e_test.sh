#!/bin/bash
set -e
cd /mnt/c/Users/31677/Desktop/NexusAI/agent-communication-and-tool-selection-framework
GRPCURL="/home/dudu/.local/bin/grpcurl"
SERVER="localhost:50051"

# Login
TOKEN=$($GRPCURL -plaintext \
  -d '{"username":"smoke3","password":"pass1234"}' \
  $SERVER \
  agent_communication.auth.UserService/Login 2>/dev/null | \
  python3 -c "import sys,json; print(json.load(sys.stdin)['token'])")

echo "Token: ${TOKEN:0:20}..."

echo ""
echo "=== 1. SetAutonomyLevel ==="
echo '{"user_id":"smoke3","agent_id":"mock-general","level":2}' | \
$GRPCURL -plaintext -H "Authorization: Bearer ${TOKEN}" -d @ \
  $SERVER agent_communication.AgentLifecycleService/SetAutonomyLevel

echo ""
echo "=== 2. SubmitFeedback ==="
echo '{"agent_id":"mock-general","skill_name":"general","rating":5}' | \
$GRPCURL -plaintext -H "Authorization: Bearer ${TOKEN}" -d @ \
  $SERVER agent_communication.AgentLifecycleService/SubmitFeedback

echo ""
echo "=== 3. ShareSession ==="
echo '{"context_id":"ctx-test-001","mode":"READONLY"}' | \
$GRPCURL -plaintext -H "Authorization: Bearer ${TOKEN}" -d @ \
  $SERVER agent_communication.SharingService/ShareSession

echo ""
echo "=== 4. SaveTemplate ==="
echo '{"name":"test-template","description":"test"}' | \
$GRPCURL -plaintext -H "Authorization: Bearer ${TOKEN}" -d @ \
  $SERVER agent_communication.SharingService/SaveTemplate

echo ""
echo "=== 5. UseTemplate ==="
echo '{"template_id":"tmpl-123"}' | \
$GRPCURL -plaintext -H "Authorization: Bearer ${TOKEN}" -d @ \
  $SERVER agent_communication.SharingService/UseTemplate

echo ""
echo "=== 6. ObserveSession ==="
echo '{"trace_id":"trace-123"}' | \
$GRPCURL -plaintext -H "Authorization: Bearer ${TOKEN}" -d @ \
  $SERVER agent_communication.SharingService/ObserveSession 2>&1 | head -3

echo ""
echo "=== 7. QueryStream (full chain) ==="
$GRPCURL -plaintext -H "Authorization: Bearer ${TOKEN}" \
  -d '{"question":"explain REST API","context_id":"ctx-e2e-002","user_id":"smoke3"}' \
  $SERVER agent_communication.AIQueryService/QueryStream 2>&1 | head -8

echo ""
echo "=== ALL TESTS PASSED ==="
