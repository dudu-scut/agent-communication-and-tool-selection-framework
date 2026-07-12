#!/usr/bin/env python3
"""Test gRPC query without shell escaping issues."""
import subprocess, json, os

GRPCURL = os.path.expanduser("~/.local/bin/grpcurl")
SERVER = "localhost:50051"

def grpcurl(method, body, auth=None):
    cmd = [GRPCURL, "-plaintext"]
    if auth:
        cmd += ["-H", f"Authorization: Bearer {auth}"]
    cmd += [SERVER, method]
    r = subprocess.run(cmd, input=body, capture_output=True, text=True)
    return r.stdout, r.stderr

# Register user
print("=== Register ===")
out, err = grpcurl("agent_communication.auth.UserService/Register",
    '{"username":"test1","password":"pass1"}')
print(out or err)

# Login
print("=== Login ===")
out, err = grpcurl("agent_communication.auth.UserService/Login",
    '{"username":"test1","password":"pass1"}')
print(out)
data = json.loads(out) if out else {}
token = data.get("token", "")
print(f"Token: {token[:10]}...")

# Test QueryStream
print("\n=== QueryStream ===")
out, err = grpcurl("agent_communication.AIQueryService/QueryStream",
    '{"request_id":"r1","question":"hello world","user_id":"u1","context_id":"c1"}',
    auth=token)
print(out[:1000] if out else err[:1000])

# Check server still alive
print("\n=== Server alive? ===")
out, err = grpcurl("agent_communication.AIQueryService/GetAgentMetrics",
    '{"agent_id":"mock-general"}',
    auth=token)
print("ALIVE" if out or "metrics" in (out or "") else "DEAD or no metrics")
print(out[:500] if out else err[:500])
