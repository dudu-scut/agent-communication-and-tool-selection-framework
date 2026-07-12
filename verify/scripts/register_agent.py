#!/usr/bin/env python3
"""Register mock agent with the registry so orchestrator can route to it."""
import subprocess, json, os

GRPCURL = os.path.expanduser("~/.local/bin/grpcurl")
SERVER = "localhost:50051"

def grpcurl(method, body, auth=None):
    cmd = [GRPCURL, "-plaintext"]
    if auth:
        cmd += ["-H", f"Authorization: Bearer {auth}"]
    cmd += ["-d", "@", SERVER, method]
    r = subprocess.run(cmd, input=body, capture_output=True, text=True, timeout=15)
    return r.stdout, r.stderr

# Login
out, _ = grpcurl("agent_communication.auth.UserService/Login",
    json.dumps({"username": "smoke3", "password": "pass1234"}))
token = json.loads(out).get("token", "")
print(f"Token: {token[:20]}...")

# Register mock agent
print("\n=== Register Mock Agent ===")
out, err = grpcurl("agent_communication.AgentCommunicationService/RegisterAgent",
    json.dumps({
        "agent_info": {
            "service_name": "mock-general",
            "skills": ["general"],
            "a2a_version": "1.0",
            "deployment_stage": "STABLE",
            "host": "localhost",
            "port": 5100
        }
    }), auth=token)
print(f"Register: {out[:300] if out else err[:300]}")

# Verify
print("\n=== GetAgents ===")
out, err = grpcurl("agent_communication.AgentCommunicationService/GetAgents",
    json.dumps({}), auth=token)
print(f"Agents: {out[:500] if out else err[:300]}")
