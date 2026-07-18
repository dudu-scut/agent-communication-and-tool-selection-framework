#!/bin/bash
GRPCURL="$HOME/.local/bin/grpcurl"
SERVER="localhost:50051"

echo "=== Login ==="
LOGIN_RESP=$(echo '{"username":"smoke3","password":"pass1234"}' | $GRPCURL -plaintext -d @ $SERVER agent_communication.auth.UserService/Login 2>&1)
echo "$LOGIN_RESP"
TOKEN=$(echo "$LOGIN_RESP" | python3 -c "import sys,json; print(json.loads(sys.stdin.read()).get('token',''))" 2>/dev/null)
echo "Token: ${TOKEN:0:20}..."

echo ""
echo "=== Register Mock Agent ==="
echo '{"agent_info":{"service_name":"mock-general","skills":["general"],"a2a_version":"1.0","deployment_stage":"STABLE","host":"127.0.0.1","port":5100}}' | \
    $GRPCURL -plaintext -H "Authorization: Bearer $TOKEN" -d @ $SERVER agent_communication.AgentCommunicationService/RegisterAgent 2>&1

echo ""
echo "=== Register Math Agent ==="
echo '{"agent_info":{"service_name":"math-agent","skills":["math","calculation"],"a2a_version":"1.0","deployment_stage":"STABLE","host":"127.0.0.1","port":5100}}' | \
    $GRPCURL -plaintext -H "Authorization: Bearer $TOKEN" -d @ $SERVER agent_communication.AgentCommunicationService/RegisterAgent 2>&1

echo ""
echo "=== Register Translator Agent ==="
echo '{"agent_info":{"service_name":"translator-agent","skills":["translation","language"],"a2a_version":"1.0","deployment_stage":"STABLE","host":"127.0.0.1","port":5100}}' | \
    $GRPCURL -plaintext -H "Authorization: Bearer $TOKEN" -d @ $SERVER agent_communication.AgentCommunicationService/RegisterAgent 2>&1

echo ""
echo "=== Register Echo Agent ==="
echo '{"agent_info":{"service_name":"echo-agent","skills":["echo","test"],"a2a_version":"1.0","deployment_stage":"STABLE","host":"127.0.0.1","port":5100}}' | \
    $GRPCURL -plaintext -H "Authorization: Bearer $TOKEN" -d @ $SERVER agent_communication.AgentCommunicationService/RegisterAgent 2>&1

echo ""
echo "=== GetAgents ==="
echo '{}' | $GRPCURL -plaintext -H "Authorization: Bearer $TOKEN" -d @ $SERVER agent_communication.AgentCommunicationService/GetAgents 2>&1
