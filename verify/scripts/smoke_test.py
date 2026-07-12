#!/usr/bin/env python3
"""Full-chain smoke test: Auth → QueryStream → verify server stays alive."""
import subprocess, json, os, sys

GRPCURL = os.path.expanduser("~/.local/bin/grpcurl")
SERVER = "localhost:50051"
USER = "smoke2"
PASS = "pass123"

def grpcurl(method, body, auth=None):
    cmd = [GRPCURL, "-plaintext"]
    if auth:
        cmd += ["-H", f"Authorization: Bearer {auth}"]
    cmd += [SERVER, method]
    r = subprocess.run(cmd, input=body, capture_output=True, text=True, timeout=15)
    return r.stdout, r.stderr

print("=== 1. Register ===")
out, err = grpcurl("agent_communication.auth.UserService/Register",
    json.dumps({"username": USER, "password": PASS}))
print(out or err)

print("\n=== 2. Login ===")
out, err = grpcurl("agent_communication.auth.UserService/Login",
    json.dumps({"username": USER, "password": PASS}))
print(out)
data = json.loads(out) if out else {}
token = data.get("token", "")
print(f"Token: {token[:20]}..." if token else "NO TOKEN!")

if not token:
    print("FAIL: No auth token")
    sys.exit(1)

print("\n=== 3. QueryStream (THE CRASH TEST) ===")
out, err = grpcurl("agent_communication.AIQueryService/QueryStream",
    json.dumps({"request_id":"r1","question":"hello world","user_id":USER,"context_id":"c1"}),
    auth=token)
print(out[:500] if out else f"STDERR: {err[:500]}")
if err and "UNAVAILABLE" in err:
    print("\n!!! SERVER DIED (ECONNRESET) !!!")
    sys.exit(2)
elif err and "14" in err:
    print("\n!!! SERVER DIED (gRPC error 14) !!!")
    sys.exit(2)

print("\n=== 4. Health check (server still alive?) ===")
out, err = grpcurl("grpc.health.v1.Health/Check",
    json.dumps({"service": "agent_communication.AIQueryService"}))
print(out or err)
if "SERVING" in (out or ""):
    print("SERVER ALIVE ✓")
elif err:
    print(f"SERVER DEAD or unhealthy: {err[:200]}")
    sys.exit(3)

print("\n=== 5. GetMetrics (further verification) ===")
out, err = grpcurl("agent_communication.AIQueryService/GetAgentMetrics",
    json.dumps({"agent_id":"mock-general"}), auth=token)
print(out[:300] if out else err[:300])

print("\n=== ALL TESTS PASSED ===")
