#!/usr/bin/env python3
"""NexusAI PR-G release E2E — real processes only, no mocked persistence.

Runs a REAL build/server/rpc_server process against the real Docker
PostgreSQL/Redis, with a minimal real HTTP A2A agent, and verifies every
scenario by querying PostgreSQL directly (docker exec psql or a local psql).

Covered scenarios:
  1. register/login -> QueryStream -> exactly one complete -> PG rows
     (query log / trace / messages / ledger) owned by the caller;
     GetQueryStatus reads the durable PG state (owner scoped).
  2. client abort mid-stream -> query log persists status=cancelled.
  3. budget rejection -> RESOURCE_EXHAUSTED + query log persists rejected.
  4. A/B isolation: B cannot read A's trace / feedback target / export /
     query status.
  5. feedback changes routing: SubmitFeedback -> agent_route_quality row
     appears for the owner.
  6. share TTL/revoke: ReadSharedConversation refuses expired and revoked
     shares.

Honesty rules:
  - Missing prerequisites (grpcurl, built rpc_server, reachable PG/Redis,
    a psql execution path) produce an explicit SKIP with the reason.
  - No psql/PG mocking anywhere: every assertion reads the real database.

Usage (WSL, repo root):
  python3 tests/e2e/e2e_pr_g_release.py
Environment overrides:
  NEXUSAI_POSTGRES_HOST/PORT/DATABASE/USER/PASSWORD, REDIS_HOST/REDIS_PORT,
  GRPCURL_PATH, E2E_PG_CONTAINER (postgres container name for docker exec).
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SERVER_BIN = os.path.join(REPO_ROOT, "build", "server", "rpc_server")
LOGS_DIR = os.path.join(REPO_ROOT, "logs")

PG_HOST = os.environ.get("NEXUSAI_POSTGRES_HOST", "127.0.0.1")
PG_PORT = int(os.environ.get("NEXUSAI_POSTGRES_PORT", "5432"))
PG_DB = os.environ.get("NEXUSAI_POSTGRES_DATABASE", "nexusai")
PG_USER = os.environ.get("NEXUSAI_POSTGRES_USER", "nexusai")
PG_PASSWORD = os.environ.get("NEXUSAI_POSTGRES_PASSWORD", "nexusai-dev-password")
REDIS_HOST = os.environ.get("REDIS_HOST", "127.0.0.1")
REDIS_PORT = int(os.environ.get("REDIS_PORT", "6379"))

RUN_SUFFIX = uuid.uuid4().hex[:10]
PASSWORD = "prg-release-password"
USER_A = f"prg-a-{RUN_SUFFIX}"
USER_B = f"prg-b-{RUN_SUFFIX}"
ADMIN_USER = f"prg-admin-{RUN_SUFFIX}"
AGENT_NAME = f"prg-mock-{RUN_SUFFIX}"

PASS = 0
FAIL = 0
FAILURES = []


def report(ok, label, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  [PASS] {label}")
    else:
        FAIL += 1
        FAILURES.append(label)
        print(f"  [FAIL] {label}")
        if detail:
            print(f"         {detail}")


def skip(reason):
    print(f"SKIP: {reason}")
    print("E2E skipped (missing prerequisite) — this is NOT a green run.")
    sys.exit(0)


def tcp_open(host, port, timeout=2.0):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# PostgreSQL access: real queries only (docker exec psql, or local psql).
PG_EXECUTOR = None  # resolved in preflight: list of argv prefix


def resolve_pg_executor():
    container = os.environ.get("E2E_PG_CONTAINER", "")
    docker = shutil.which("docker")
    if not container and docker:
        try:
            out = subprocess.run(
                [docker, "ps", "--format", "{{.Names}}"],
                capture_output=True, text=True, timeout=15).stdout
            for name in out.split():
                if "postgres" in name:
                    container = name
                    break
        except Exception:
            pass
    if container and docker:
        prefix = [docker, "exec", container,
                  "psql", "-U", PG_USER, "-d", PG_DB, "-tAc"]
        probe = subprocess.run(prefix + ["SELECT 1"],
                               capture_output=True, text=True, timeout=15)
        if probe.returncode == 0 and "1" in probe.stdout:
            return prefix
    psql = shutil.which("psql")
    if psql:
        dsn = f"postgresql://{PG_USER}:{PG_PASSWORD}@{PG_HOST}:{PG_PORT}/{PG_DB}"
        prefix = [psql, dsn, "-tAc"]
        probe = subprocess.run(prefix + ["SELECT 1"],
                               capture_output=True, text=True, timeout=15)
        if probe.returncode == 0 and "1" in probe.stdout:
            return prefix
    return None


def pg(sql, timeout=20):
    """Run one real SQL statement; returns stripped stdout ('' on error)."""
    proc = subprocess.run(PG_EXECUTOR + [sql],
                          capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"psql failed: {proc.stderr.strip()[:300]}")
    return proc.stdout.strip()


def pg_scalar(sql, timeout=20):
    out = pg(sql, timeout)
    return out.splitlines()[0] if out else ""


# grpcurl helpers
GRPCURL = None
SERVER_ADDR = None


def grpc(method, body=None, token=None, timeout=40):
    cmd = [GRPCURL, "-plaintext"]
    if token:
        cmd += ["-H", f"Authorization: Bearer {token}"]
    cmd += ["-d", json.dumps(body if body is not None else {}),
            SERVER_ADDR, method]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    return proc.returncode, proc.stdout, proc.stderr


def grpc_ok(method, body=None, token=None, timeout=40):
    code, out, err = grpc(method, body, token, timeout)
    if code != 0:
        return None, err.strip()[:300]
    try:
        return (json.loads(out) if out.strip() else {}), None
    except json.JSONDecodeError as exc:
        return None, f"bad JSON: {exc}: {out[:200]}"


def grpc_expect_error(method, body, token, wanted_code, timeout=40):
    """Returns (ok, detail); wanted_code is the grpcurl code name."""
    code, out, err = grpc(method, body, token, timeout)
    combined = out + err
    if code != 0 and wanted_code.lower() in combined.lower():
        return True, ""
    return False, f"expected {wanted_code}, rc={code}, out={out[:200]}, err={err[:200]}"


def register_login(username):
    """Register + login a user; returns (user_id, token) or (None, detail)."""
    resp, err = grpc_ok("agent_communication.auth.UserService/Register",
                        {"username": username, "password": PASSWORD,
                         "display_name": username})
    if err and "already exists" not in err.lower() and resp is None:
        # tolerate existing user (re-runs); login will decide
        pass
    resp, err = grpc_ok("agent_communication.auth.UserService/Login",
                        {"username": username, "password": PASSWORD})
    if resp is None:
        return None, None, err
    return resp.get("user_id", ""), resp.get("token", ""), None


# Mock A2A agent (real HTTP server, standard-library only)
class MockA2AHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(length).decode("utf-8", "replace")
        slow = "SLOWAGENT" in body
        if "message/stream" in body:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()

            def emit(state, text):
                payload = {"jsonrpc": "2.0", "id": 1,
                           "result": {"type": "status",
                                      "status": {"state": state,
                                                 "message": {"role": "agent",
                                                             "parts": [{"type": "text",
                                                                        "text": text}]}}}}
                self.wfile.write(("data: " + json.dumps(payload) + "\n\n").encode())
                self.wfile.flush()

            try:
                emit("working", "thinking")
                if slow:
                    for _ in range(40):
                        time.sleep(0.3)
                        emit("working", "still thinking")
                emit("completed", "mock-stream-answer")
            except OSError:
                pass  # client aborted — exactly what scenario 2 expects
        else:
            if slow:
                time.sleep(6)
            payload = {"jsonrpc": "2.0", "id": 1,
                       "result": {"type": "message",
                                  "message": {"message_id": "prg-mock-1",
                                              "role": "agent",
                                              "parts": [{"type": "text",
                                                         "text": "mock-answer"}]}}}
            data = json.dumps(payload).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    def log_message(self, *args):  # silence request logging
        pass


def start_mock_agent():
    server = ThreadingHTTPServer(("127.0.0.1", 0), MockA2AHandler)
    import threading
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, server.server_address[1]


# Real rpc_server lifecycle
def start_rpc_server(port, mock_port):
    env = dict(os.environ)
    # Shared libraries from a user-prefix libpqxx install (no-sudo WSL setup)
    # need to be visible to the freshly built binary.
    extra_ld = os.environ.get("E2E_LD_LIBRARY_PATH", "")
    default_prefix_lib = os.path.expanduser(
        "~/nexusai-deps/prefix/usr/lib/x86_64-linux-gnu")
    if not extra_ld and os.path.isdir(default_prefix_lib):
        extra_ld = default_prefix_lib
    if extra_ld:
        env["LD_LIBRARY_PATH"] = (
            extra_ld + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else ""))
    env.update({
        "NEXUSAI_POSTGRES_HOST": PG_HOST,
        "NEXUSAI_POSTGRES_PORT": str(PG_PORT),
        "NEXUSAI_POSTGRES_DATABASE": PG_DB,
        "NEXUSAI_POSTGRES_USER": PG_USER,
        "NEXUSAI_POSTGRES_PASSWORD": PG_PASSWORD,
        "REDIS_HOST": REDIS_HOST,
        "REDIS_PORT": str(REDIS_PORT),
        "NEXUSAI_ADMIN_USERNAME": ADMIN_USER,
        # Tiny per-conversation session budget: short questions (~64-68
        # estimated tokens) pass, the long budget-buster question is refused.
        "NEXUSAI_BUDGET_SESSION_TOKENS": "70",
        # Force the deterministic single-agent A2A path (no LLM orchestrator).
        "LLM_API_KEY": "",
    })
    os.makedirs(LOGS_DIR, exist_ok=True)
    log_path = os.path.join(LOGS_DIR, "e2e_pr_g_rpc_server.log")
    log_file = open(log_path, "ab")
    proc = subprocess.Popen(
        [SERVER_BIN, "-p", str(port), "-o", f"http://127.0.0.1:{mock_port}"],
        cwd=REPO_ROOT, env=env, stdout=log_file, stderr=subprocess.STDOUT)
    deadline = time.time() + 30
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(
                f"rpc_server exited early (code {proc.returncode}); see {log_path}")
        if tcp_open("127.0.0.1", port):
            return proc, log_path
        time.sleep(0.3)
    proc.kill()
    raise RuntimeError(f"rpc_server never listened on {port}; see {log_path}")


def wait_until(predicate, timeout=15, interval=0.5):
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(interval)
    return predicate()


# Scenarios
def scenario_1_register_login_stream(a_id, a_token):
    print("\n[1] register/login -> QueryStream -> exactly one complete -> PG facts")
    reg_resp, err = grpc_ok("agent_communication.auth.UserService/Register",
                            {"username": ADMIN_USER, "password": PASSWORD,
                             "display_name": ADMIN_USER})
    report(reg_resp is not None and reg_resp.get("role") == "ADMIN",
           "admin registration returns ADMIN role", err or json.dumps(reg_resp)[:200])
    admin_login, err = grpc_ok("agent_communication.auth.UserService/Login",
                               {"username": ADMIN_USER, "password": PASSWORD})
    admin_token = (admin_login or {}).get("token", "")
    report(bool(admin_token), "admin login returns token", str(err))

    reg, err = grpc_ok("agent_communication.AgentCommunicationService/RegisterAgent",
                       {"agent_info": {"service_name": AGENT_NAME,
                                       "skills": ["general"],
                                       "a2a_version": "1.0",
                                       "deployment_stage": "STABLE",
                                       "host": "127.0.0.1", "port": MOCK_PORT},
                        "heartbeat_interval": 30}, token=admin_token)
    report(reg is not None, "admin RegisterAgent succeeds", str(err))
    # RegisterAgent persists agent_id = "<service_name>-<host>-<port>" and
    # display_name = service_name; match on the stable display_name.
    registry_rows = pg_scalar(
        f"SELECT COUNT(*) FROM agent_registry WHERE display_name = '{AGENT_NAME}'")
    report(registry_rows == "1",
           "agent_registry persists the registration in PostgreSQL",
           f"rows={registry_rows!r}")

    req_id = f"prg-q1-{RUN_SUFFIX}"
    ctx_id = f"prg-ctx1-{RUN_SUFFIX}"
    code, out, err = grpc("agent_communication.AIQueryService/QueryStream",
                          {"request_id": req_id, "context_id": ctx_id,
                           "question": "hello durable world"}, token=a_token)
    compact = out.replace(" ", "")
    completes = (compact.count('"event_type":"complete"')
                 + compact.count('"eventType":"complete"'))
    errors = (compact.count('"event_type":"error"')
              + compact.count('"eventType":"error"'))
    report(code == 0 and completes == 1 and errors == 0,
           "QueryStream emits exactly one complete and no error",
           f"rc={code} completes={completes} errors={errors} err={err[:200]}")

    status = pg_scalar(f"SELECT status FROM query_logs WHERE id = '{req_id}'")
    owner = pg_scalar(f"SELECT owner_id FROM query_logs WHERE id = '{req_id}'")
    report(status == "completed" and owner == a_id,
           "query_logs row is completed and owned by the caller",
           f"status={status!r} owner={owner!r} expected_owner={a_id!r}")
    trace_id = pg_scalar(
        f"SELECT id FROM traces WHERE owner_id = '{a_id}' AND query_log_id = '{req_id}'")
    report(bool(trace_id), "trace row persisted for the owner", f"trace={trace_id!r}")
    msg_count = pg_scalar(
        f"SELECT COUNT(*) FROM conversation_messages "
        f"WHERE owner_id = '{a_id}' AND conversation_id = '{ctx_id}'")
    report(int(msg_count or 0) >= 2,
           "conversation messages (user+assistant) persisted for the owner",
           f"messages={msg_count!r}")
    ledger = pg_scalar(
        f"SELECT COUNT(*) FROM token_usage_ledger WHERE query_log_id = '{req_id}'")
    report(int(ledger or 0) == 1, "token ledger entry recorded exactly once",
           f"ledger={ledger!r}")

    qs, err = grpc_ok("agent_communication.AIQueryService/GetQueryStatus",
                      {"task_id": req_id}, token=a_token)
    report(qs is not None and qs.get("task_state") == "completed"
           and len(qs.get("history", [])) >= 2,
           "GetQueryStatus reads the durable completed state with history",
           str(err or qs)[:300])
    return req_id, ctx_id, trace_id


def scenario_2_abort(a_token):
    print("\n[2] client abort mid-stream -> query log persists cancelled")
    req_id = f"prg-q2-{RUN_SUFFIX}"
    ctx_id = f"prg-ctx2-{RUN_SUFFIX}"
    cmd = [GRPCURL, "-plaintext", "-H", f"Authorization: Bearer {a_token}",
           "-d", json.dumps({"request_id": req_id, "context_id": ctx_id,
                             "question": "SLOWAGENT now"}),
           SERVER_ADDR, "agent_communication.AIQueryService/QueryStream"]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2.0)
    proc.kill()
    proc.wait(timeout=10)

    def poll_cancelled():
        value = pg_scalar(f"SELECT status FROM query_logs WHERE id = '{req_id}'")
        return value if value == "cancelled" else None

    final = wait_until(poll_cancelled, timeout=15)
    report(final == "cancelled",
           "aborted stream persists status=cancelled in PostgreSQL",
           f"status={final!r}")


def scenario_3_budget(a_token, a_id):
    print("\n[3] budget rejection -> RESOURCE_EXHAUSTED + rejected persisted")
    req_id = f"prg-q3-{RUN_SUFFIX}"
    ctx_id = f"prg-ctx3-{RUN_SUFFIX}"
    long_question = "budget " + "x" * 200  # ~115 estimated tokens > session 70
    ok, detail = grpc_expect_error(
        "agent_communication.AIQueryService/QueryStream",
        {"request_id": req_id, "context_id": ctx_id, "question": long_question},
        a_token, "ResourceExhausted")
    report(ok, "over-budget query returns RESOURCE_EXHAUSTED", detail)
    status = pg_scalar(f"SELECT status FROM query_logs WHERE id = '{req_id}'")
    owner = pg_scalar(f"SELECT owner_id FROM query_logs WHERE id = '{req_id}'")
    report(status == "rejected" and owner == a_id,
           "query_logs row is rejected and owned by the caller",
           f"status={status!r} owner={owner!r}")


def scenario_4_isolation(a_token, b_id, b_token, req_id_a, ctx_id_a, trace_id_a):
    print("\n[4] A/B isolation: B cannot reach A's trace/feedback/export/status")
    ok, detail = grpc_expect_error(
        "agent_communication.ObservabilityService/GetTraceDetail",
        {"trace_id": trace_id_a}, b_token, "NotFound")
    report(ok, "B cannot read A's trace detail", detail)

    ok, detail = grpc_expect_error(
        "agent_communication.AgentLifecycleService/SubmitFeedback",
        {"trace_id": trace_id_a, "agent_id": AGENT_NAME,
         "skill_name": "general", "rating": 5, "comment": "steal"},
        b_token, "NotFound")
    report(ok, "B cannot submit feedback against A's trace", detail)

    ok, detail = grpc_expect_error(
        "agent_communication.OrchestrationService/ExportConversation",
        {"context_id": ctx_id_a, "format": "markdown"}, b_token, "NotFound")
    report(ok, "B cannot export A's conversation", detail)

    ok, detail = grpc_expect_error(
        "agent_communication.AIQueryService/GetQueryStatus",
        {"task_id": req_id_a}, b_token, "NotFound")
    report(ok, "B cannot read A's query status", detail)


def scenario_5_feedback_routing(a_id, a_token, trace_id_a):
    print("\n[5] feedback changes routing: agent_route_quality moves for owner")
    before = pg_scalar(
        f"SELECT COALESCE(SUM(sample_count),0) FROM agent_route_quality "
        f"WHERE owner_id = '{a_id}' AND agent_id = '{AGENT_NAME}' "
        f"AND skill_name = 'general'")
    resp, err = grpc_ok("agent_communication.AgentLifecycleService/SubmitFeedback",
                        {"trace_id": trace_id_a, "agent_id": AGENT_NAME,
                         "skill_name": "general", "rating": 5,
                         "comment": "great"}, token=a_token)
    report(resp is not None, "owner feedback accepted for own trace", str(err))
    after = wait_until(
        lambda: pg_scalar(
            f"SELECT COALESCE(SUM(sample_count),0) FROM agent_route_quality "
            f"WHERE owner_id = '{a_id}' AND agent_id = '{AGENT_NAME}' "
            f"AND skill_name = 'general'"), timeout=10)
    report(int(after or 0) > int(before or 0),
           "agent_route_quality sample count increased for the owner",
           f"before={before!r} after={after!r}")
    feedback_rows = pg_scalar(
        f"SELECT COUNT(*) FROM feedback WHERE owner_id = '{a_id}' "
        f"AND agent_id = '{AGENT_NAME}'")
    report(int(feedback_rows or 0) >= 1,
           "feedback row persisted for the owner", f"rows={feedback_rows!r}")


def scenario_6_share_ttl_revoke(a_token, ctx_id_a):
    print("\n[6] share TTL/revoke: expired and revoked shares are refused")
    resp, err = grpc_ok("agent_communication.SharingService/ShareSession",
                        {"context_id": ctx_id_a, "mode": "view",
                         "expiry_days": 1}, token=a_token)
    if resp is None:
        report(False, "ShareSession creates a view share", str(err))
        return
    token_raw = resp.get("token", "")
    share_id = resp.get("share_id", "")
    report(bool(token_raw) and bool(share_id),
           "ShareSession returns one-time raw token and share id",
           json.dumps(resp)[:200])

    pub, err = grpc_ok("agent_communication.SharingService/ReadSharedConversation",
                       {"token": token_raw})
    report(pub is not None and len(pub.get("messages", [])) >= 2,
           "public read works while the share is alive", str(err))

    pg(f"UPDATE shares SET expires_at = NOW() - interval '5 minutes' "
       f"WHERE id = '{share_id}'")
    ok, detail = grpc_expect_error(
        "agent_communication.SharingService/ReadSharedConversation",
        {"token": token_raw}, None, "PermissionDenied")
    report(ok, "expired share is refused", detail)

    resp2, err = grpc_ok("agent_communication.SharingService/ShareSession",
                         {"context_id": ctx_id_a, "mode": "view",
                          "expiry_days": 1}, token=a_token)
    if resp2 is None:
        report(False, "second ShareSession for revoke path", str(err))
        return
    rev, err = grpc_ok("agent_communication.SharingService/RevokeShare",
                       {"share_id": resp2.get("share_id", "")}, token=a_token)
    report(rev is not None, "owner revokes the share", str(err))
    ok, detail = grpc_expect_error(
        "agent_communication.SharingService/ReadSharedConversation",
        {"token": resp2.get("token", "")}, None, "PermissionDenied")
    report(ok, "revoked share is refused", detail)


# Main
MOCK_PORT = 0


def main():
    global GRPCURL, SERVER_ADDR, PG_EXECUTOR, MOCK_PORT

    print("NexusAI PR-G release E2E (real rpc_server + real Docker PG/Redis)")
    print(f"run suffix: {RUN_SUFFIX}")

    grpcurl = os.environ.get("GRPCURL_PATH") or (
        shutil.which("grpcurl")
        or os.path.expanduser("~/.local/bin/grpcurl"))
    if not os.path.exists(grpcurl):
        skip(f"grpcurl not found (set GRPCURL_PATH); tried {grpcurl}")
    GRPCURL = grpcurl

    if not os.path.isfile(SERVER_BIN) or not os.access(SERVER_BIN, os.X_OK):
        skip(f"rpc_server binary missing: {SERVER_BIN} (run ./run.sh build first)")

    if not tcp_open(PG_HOST, PG_PORT):
        skip(f"PostgreSQL unreachable at {PG_HOST}:{PG_PORT} "
             "(start the Docker compose postgres service)")
    if not tcp_open(REDIS_HOST, REDIS_PORT):
        skip(f"Redis unreachable at {REDIS_HOST}:{REDIS_PORT}")

    PG_EXECUTOR = resolve_pg_executor()
    if PG_EXECUTOR is None:
        skip("no real psql execution path (docker exec postgres container or "
             "local psql) — refusing to fake database checks")

    mock_server, MOCK_PORT = start_mock_agent()
    rpc_port = free_port()
    server_proc = None
    try:
        server_proc, log_path = start_rpc_server(rpc_port, MOCK_PORT)
        SERVER_ADDR = f"127.0.0.1:{rpc_port}"
        print(f"rpc_server up at {SERVER_ADDR} (log: {log_path})")
        print(f"mock A2A agent at 127.0.0.1:{MOCK_PORT}")

        a_id, a_token, err = register_login(USER_A)
        if not a_token:
            print(f"FATAL: cannot register/login user A: {err}")
            sys.exit(1)
        b_id, b_token, err = register_login(USER_B)
        if not b_token:
            print(f"FATAL: cannot register/login user B: {err}")
            sys.exit(1)

        req_id_a, ctx_id_a, trace_id_a = scenario_1_register_login_stream(a_id, a_token)
        scenario_2_abort(a_token)
        scenario_3_budget(a_token, a_id)
        scenario_4_isolation(a_token, b_id, b_token, req_id_a, ctx_id_a, trace_id_a)
        scenario_5_feedback_routing(a_id, a_token, trace_id_a)
        scenario_6_share_ttl_revoke(a_token, ctx_id_a)
    finally:
        if server_proc:
            server_proc.terminate()
            try:
                server_proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server_proc.kill()
        mock_server.shutdown()

    print(f"\nRESULT: {PASS} passed, {FAIL} failed")
    if FAILURES:
        print("Failed checks:")
        for name in FAILURES:
            print(f"  - {name}")
        sys.exit(1)
    print("E2E PASSED — every assertion verified against real PostgreSQL.")


if __name__ == "__main__":
    main()
