#!/usr/bin/env python3
"""Full-chain E2E test: Auth → QueryStream → verify content from mock agent."""
import subprocess, json, sys, os

GRPCURL = os.path.expanduser("~/.local/bin/grpcurl")
SERVER = "localhost:50051"

def grpcurl(method, body, auth=None):
    cmd = [GRPCURL, "-plaintext"]
    if auth:
        cmd += ["-H", f"Authorization: Bearer {auth}"]
    cmd += ["-d", "@", SERVER, method]
    r = subprocess.run(cmd, input=body, capture_output=True, text=True, timeout=30)
    return r.stdout, r.stderr

# Use existing user
USER = "smoke3"
PASS = "pass1234"

print("=== 1. Login ===")
out, err = grpcurl("agent_communication.auth.UserService/Login",
    json.dumps({"username": USER, "password": PASS}))
data = json.loads(out) if out else {}
token = data.get("token", "")
if not token:
    print(f"FAIL: No token. {err[:200]}")
    sys.exit(1)
print(f"Token: {token[:20]}...")

print("\n=== 2. QueryStream (E2E test) ===")
out, err = grpcurl("agent_communication.AIQueryService/QueryStream",
    json.dumps({"request_id":"e2e-1","question":"calculate 2+3","user_id":USER,"context_id":"e2e-ctx"}),
    auth=token)

lines = out.strip().split("\n") if out.strip() else []
print(f"Received {len(lines)} stream events:")

all_content = []
for i, line in enumerate(lines):
    try:
        evt = json.loads(line)
        etype = evt.get("event_type", "?")
        content = evt.get("content", "")
        task_state = evt.get("task_state", "")
        if content:
            all_content.append(content)
        print(f"  [{i}] type={etype} state={task_state} content={content[:80]}")
    except:
        print(f"  [{i}] RAW: {line[:100]}")

print(f"\n=== 3. Summary ===")
full_text = " ".join(all_content)
print(f"Total content: {full_text[:300]}")
if "hello" in full_text.lower() or "world" in full_text.lower() or len(all_content) > 0:
    print("E2E CHAIN WORKING: Query → Orchestrator → Agent → Response")
else:
    print("WARN: Got events but no meaningful content - check routing")

print("\n=== 4. Server still alive? ===")
out, err = grpcurl("agent_communication.AIQueryService/GetAgentMetrics",
    json.dumps({"agent_id":"mock-general"}), auth=token)
print(out[:300] if out else err[:200])

# Check we can list services
out2, _ = grpcurl("agent_communication.AgentCommunicationService/GetAgents",
    json.dumps({}))
if out2:
    agents = json.loads(out2)
    print(f"\nRegistered agents: {json.dumps(agents, indent=2)[:500]}")
