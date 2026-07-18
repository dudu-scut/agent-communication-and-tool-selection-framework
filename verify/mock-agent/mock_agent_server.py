#!/usr/bin/env python3
"""
Mock A2A Agent Server for E2E verification testing.

Port: 5100 (avoids conflict with 5000/5001/50051)

Behavior modes (via x-mock-mode request header):
  normal        -> 200 with valid A2A JSON-RPC response
  slow          -> 200 after 5s delay
  error         -> 500 immediately
  delegate      -> 200 with x-delegation-to header in response metadata
  version_v1_0  -> response uses "kind" field
  version_v1_1  -> response uses "type" field
  version_mixed -> response uses "type" field (simulates mismatched version)
"""
import json
import sys
import os
import time
import signal
from http.server import HTTPServer, BaseHTTPRequestHandler

PORT = 5100
PID_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pid.txt")

# Track consecutive failures per agent for circuit breaker testing
failure_counts = {}


def build_a2a_response(query_text, mode):
    """Build an A2A JSON-RPC style response based on mock mode."""
    base = {
        "jsonrpc": "2.0",
        "id": 1,
    }

    if mode == "error":
        return None, 500

    if mode == "slow":
        time.sleep(5)

    if mode in ("version_v1_0", "normal"):
        part_field = "kind"
    else:
        part_field = "type"

    result = {
        "task_id": f"mock-task-{int(time.time())}",
        "status": "completed",
        "artifacts": [{
            "parts": [{
                part_field: "text",
                "text": f"Mock response for: {query_text}"
            }]
        }]
    }

    headers = {}
    if mode == "delegate":
        headers["x-delegation-to"] = "mock-general"

    base["result"] = result
    return base, headers


class MockAgentHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        """Suppress default stderr logging; write to stdout for visibility."""
        print(f"[mock-agent] {args[0]}", flush=True)

    def do_GET(self):
        if self.path.startswith("/health"):
            if "fail=true" in self.path:
                self.send_response(500)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"status": "error"}).encode())
            else:
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"status": "ok"}).encode())
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        # Accept both root path "/" (A2A protocol) and "/tasks/send" (A2A tasks endpoint)
        if self.path not in ("/", "/tasks/send"):
            self.send_response(404)
            self.end_headers()
            return

        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length).decode("utf-8") if content_length > 0 else "{}"

        try:
            request = json.loads(body)
        except json.JSONDecodeError:
            request = {}

        query_text = request.get("params", {}).get("message", {}).get("parts", [{}])[0].get("text", "")
        mode = self.headers.get("x-mock-mode", "normal")
        agent_id = self.headers.get("x-agent-id", "mock-general")

        # Track failures for circuit breaker testing
        if mode == "error":
            failure_counts[agent_id] = failure_counts.get(agent_id, 0) + 1

        response_body, extra_headers = build_a2a_response(query_text, mode)

        if response_body is None:
            self.send_response(500)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"error": "internal error"}).encode())
            return

        # Detect streaming request (A2A client sends Accept: text/event-stream)
        accept_header = self.headers.get("Accept", "")
        is_streaming = "text/event-stream" in accept_header

        if is_streaming:
            self._handle_streaming_post(query_text, mode, agent_id, extra_headers)
        else:
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            for key, value in (extra_headers or {}).items():
                self.send_header(key, value)
            self.end_headers()
            self.wfile.write(json.dumps(response_body).encode())

    def _handle_streaming_post(self, query_text, mode, agent_id, extra_headers):
        """Handle POST with SSE streaming response for A2A streaming protocol."""
        if mode == "error":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            err = {"jsonrpc": "2.0", "error": {"code": -1, "message": "Mock error"}}
            self.wfile.write(f"data: {json.dumps(err)}\n\n".encode())
            self.wfile.flush()
            return

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Access-Control-Allow-Origin", "*")
        for key, value in (extra_headers or {}).items():
            self.send_header(key, value)
        self.end_headers()

        response_text = f"Mock response for: {query_text}"

        # 1. Stream start status
        start_event = {
            "jsonrpc": "2.0",
            "result": {"type": "status", "status": {"state": "working", "status_description": "processing"}}
        }
        self.wfile.write(f"data: {json.dumps(start_event)}\n\n".encode())
        self.wfile.flush()

        # 2. Content chunks (split into words for streaming effect)
        words = response_text.split()
        for i, word in enumerate(words):
            chunk_text = word if i == 0 else f" {word}"
            chunk_event = {
                "jsonrpc": "2.0",
                "result": {"type": "chunk", "content": chunk_text}
            }
            self.wfile.write(f"data: {json.dumps(chunk_event)}\n\n".encode())
            self.wfile.flush()

        # 3. Completion event with text in message parts
        part_field = "kind" if self.headers.get("x-mock-mode", "normal") in ("version_v1_0", "normal") else "type"
        complete_event = {
            "jsonrpc": "2.0",
            "result": {
                "type": "status",
                "status": {
                    "state": "completed",
                    "message": {
                        "parts": [{part_field: "text", "text": response_text}]
                    }
                }
            }
        }
        self.wfile.write(f"data: {json.dumps(complete_event)}\n\n".encode())
        self.wfile.flush()

    def do_PUT(self):
        """Reset failure counters."""
        if self.path == "/reset":
            failure_counts.clear()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"reset": "ok"}).encode())
        else:
            self.send_response(404)
            self.end_headers()


def main():
    # Write PID file
    pid = os.getpid()
    os.makedirs(os.path.dirname(PID_FILE), exist_ok=True)
    with open(PID_FILE, "w") as f:
        f.write(str(pid))

    server = HTTPServer(("0.0.0.0", PORT), MockAgentHandler)
    print(f"[mock-agent] Listening on port {PORT}, PID={pid}", flush=True)

    # Start heartbeat thread to prevent cleanup by gRPC server
    import threading
    heartbeat_stop = threading.Event()
    def send_heartbeat():
        while not heartbeat_stop.is_set():
            try:
                from urllib.request import Request, urlopen
                req = Request(
                    "http://127.0.0.1:8081/agent_communication.AgentCommunicationService/Heartbeat",
                    data=b'{"agent_id":"mock-general-127.0.0.1-5100","load":0}',
                    headers={"Content-Type": "application/json"})
                urlopen(req, timeout=5)
            except Exception:
                pass
            heartbeat_stop.wait(30)  # Send heartbeat every 30s

    heartbeat_thread = threading.Thread(target=send_heartbeat, daemon=True)
    heartbeat_thread.start()

    # Signal handler must NOT call server.shutdown() directly — it deadlocks
    # because shutdown() waits for serve_forever() which is blocked by the signal.
    # Instead, call shutdown() from a separate thread.
    def shutdown(sig, frame):
        print("\n[mock-agent] Shutting down...", flush=True)
        heartbeat_stop.set()
        threading.Thread(target=lambda: (server.shutdown(), sys.exit(0)), daemon=True).start()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        shutdown(None, None)


if __name__ == "__main__":
    main()
