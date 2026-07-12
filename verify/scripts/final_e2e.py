#!/usr/bin/env python3
"""Final E2E: gRPC QueryStream → A2A adapter → orchestrator → mock agent"""
import subprocess, json, os

GRPCURL = os.path.expanduser("~/.local/bin/grpcurl")
SERVER = "localhost:50051"

def grpcurl(method, body, auth=None):
    cmd = [GRPCURL, "-plaintext"]
    if auth:
        cmd += ["-H", f"Authorization: Bearer {auth}"]
    cmd += ["-d", "@", SERVER, method]
    r = subprocess.run(cmd, input=body, capture_output=True, text=True, timeout=30)
    return r.stdout, r.stderr

# Login
out, _ = grpcurl("agent_communication.auth.UserService/Login",
    json.dumps({"username": "smoke3", "password": "pass1234"}))
token = json.loads(out).get("token", "")

# CRITICAL: QueryStream through gRPC
print("=== gRPC QueryStream E2E ===")
out, err = grpcurl("agent_communication.AIQueryService/QueryStream",
    json.dumps({"request_id":"e2e-final","question":"hello world","user_id":"smoke3","context_id":"e2e-final"}),
    auth=token)

# Parse streaming output
lines = [l for l in out.strip().split("\n") if l.strip()]
print(f"Events: {len(lines)}")
for l in lines:
    try:
        j = json.loads(l)
        print(f"  type={j.get('event_type','?')} content={j.get('content','')[:80]}")
    except:
        print(f"  RAW: {l[:100]}")

# Check if we got an actual response
if any("mock" in l.lower() or "hello" in l.lower() for l in lines):
    print("\n*** FULL CHAIN WORKING: gRPC → A2A → Orchestrator → Mock Agent ***")
else:
    print("\n*** Chain partially working — check content ***")
    # Check server alive
    out2, err2 = grpcurl("agent_communication.AIQueryService/GetAgentMetrics",
        json.dumps({"agent_id":"mock-general"}), auth=token)
    print(f"Server: {'ALIVE' if out2 else err2[:100]}")
