#!/usr/bin/env python3
"""Test if QueryStream crashes the server."""
import subprocess, json, sys, os

GRPCURL = os.path.expanduser("~/.local/bin/grpcurl")
SERVER = "localhost:50051"

def grpcurl(method, body, auth=None):
    cmd = [GRPCURL, "-plaintext"]
    if auth:
        cmd += ["-H", f"Authorization: Bearer {auth}"]
    cmd += ["-d", "@", SERVER, method]
    r = subprocess.run(cmd, input=body, capture_output=True, text=True, timeout=20)
    return r.stdout, r.stderr

# Login with the user we just created
print("=== Login ===")
out, err = grpcurl("agent_communication.auth.UserService/Login",
    json.dumps({"username": "smoke3", "password": "pass1234"}))
print(out[:200])
data = json.loads(out) if out else {}
token = data.get("token", "")
if not token:
    print(f"NO TOKEN. err={err[:200]}")
    sys.exit(1)
print(f"Token OK: {token[:20]}...")

# THE CRASH TEST
print("\n=== CRASH TEST: QueryStream ===")
out, err = grpcurl("agent_communication.AIQueryService/QueryStream",
    json.dumps({"request_id":"crash1","question":"hello world","user_id":"smoke3","context_id":"c1"}),
    auth=token)
lines = out.strip().split("\n") if out else []
print(f"Got {len(lines)} stream events")
for line in lines[:5]:
    print(f"  {line[:120]}")

# Check if server crashed
print("\n=== Health Check ===")
out, err = grpcurl("grpc.health.v1.Health/Check",
    json.dumps({"service": "agent_communication.AIQueryService"}))
if "SERVING" in out:
    print("SERVER ALIVE - crash test PASSED")
elif err:
    print(f"SERVER DEAD! {err[:200]}")
    sys.exit(2)
else:
    print(f"Unexpected: {out[:200]}")
