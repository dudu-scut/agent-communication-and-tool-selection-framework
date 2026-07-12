#!/usr/bin/env python3
"""Get auth token and test Query response format."""
import subprocess, json, sys, os

GRPC_SERVER = os.environ.get("GRPC_SERVER", "localhost:50051")
GRPCURL = os.path.expanduser("~/.local/bin/grpcurl")

def grpcurl(method, body, auth=None):
    cmd = [GRPCURL, "-plaintext"]
    if auth:
        cmd += ["-H", f"Authorization: Bearer {auth}"]
    cmd += [GRPC_SERVER, method]
    r = subprocess.run(cmd, input=body, capture_output=True, text=True)
    return r.stdout

# Register + Login
grpcurl("agent_communication.auth.UserService/Register", '{"username":"verify-e2e","password":"test123456"}')
resp = grpcurl("agent_communication.auth.UserService/Login", '{"username":"verify-e2e","password":"test123456"}')
data = json.loads(resp) if resp else {}
token = data.get("token", "")
print(f"Token: {token[:20] if token else 'NONE'}...")

# Register agent
grpcurl("agent_communication.AgentCommunicationService/RegisterAgent",
    '{"agent_info":{"service_name":"mock-general","skills":["general"],"a2a_version":"1.0","deployment_stage":"STABLE","host":"localhost","port":5100}}')

# Test Query
print("\n=== Query response ===")
r = grpcurl("agent_communication.AIQueryService/Query",
    '{"request_id":"r99","question":"hello","user_id":"u1","context_id":"c99"}',
    auth=token)
print(r[:1000])
print(f"\nContains status: {'status' in r}")
print(f"Contains answer: {'answer' in r}")
