"""Debug: show raw grpcurl streaming output."""
import subprocess, json, os

GRPCURL = os.path.expanduser("~/.local/bin/grpcurl")
SERVER = "localhost:50051"

# Login
cmd = [GRPCURL, "-plaintext", "-d", json.dumps({"username":"smoke3","password":"pass1234"}), SERVER, "agent_communication.auth.UserService/Login"]
r = subprocess.run(cmd, capture_output=True, text=True)
token = json.loads(r.stdout).get("token", "")

# QueryStream
body = json.dumps({"request_id":"dbg1","question":"hello test","user_id":"smoke3","context_id":"dbg1"})
cmd = [GRPCURL, "-plaintext", "-H", f"Authorization: Bearer {token}", "-d", body, SERVER, "agent_communication.AIQueryService/QueryStream"]
r = subprocess.run(cmd, capture_output=True, text=True, timeout=20)

print("=== RAW STDOUT ===")
print(repr(r.stdout[:500]))
print("\n=== RAW STDERR ===")
print(repr(r.stderr[:500]))
print("\n=== Contains 'status':", "status" in r.stdout)
print("=== Contains 'event_type':", "event_type" in r.stdout)
