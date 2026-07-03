#!/usr/bin/env python3
"""Echo Agent — 最简单的 NexusAI A2A 接入示例

纯标准库实现，零外部依赖。
启动后自动注册到 NexusAI 平台，收到任何消息原样返回。

用法:
    python3 echo_agent.py
"""

import json
import signal
import sys
import threading
import time
import uuid
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.request import Request, urlopen
from urllib.error import URLError

# ── 配置 ─────────────────────────────────────────────────────────────────
AGENT_NAME = "echo-agent"
AGENT_VERSION = "1.0.0"
import os
AGENT_HOST = os.environ.get("AGENT_HOST", "127.0.0.1")
AGENT_PORT = 9090
NEXUSAI_PROXY = "http://127.0.0.1:8081"
HEARTBEAT_INTERVAL = 15

AGENT_CARD = {
    "name": AGENT_NAME,
    "description": "回声 Agent，原样返回用户输入，用于测试接入链路",
    "url": f"http://{AGENT_HOST}:{AGENT_PORT}/",
    "version": AGENT_VERSION,
    "skills": [
        {"name": "echo", "description": "回声测试，原样返回用户消息"},
    ],
}

# ── HTTP 工具 ────────────────────────────────────────────────────────────

def http_post_json(url, data, timeout=10):
    """Simple POST with JSON body, returns parsed JSON response."""
    body = json.dumps(data, ensure_ascii=False).encode("utf-8")
    req = Request(url, data=body, headers={"Content-Type": "application/json"})
    resp = urlopen(req, timeout=timeout)
    return json.loads(resp.read().decode("utf-8"))

# ── gRPC 注册（通过 HTTP proxy 透传） ──────────────────────────────────

_agent_id = None
_heartbeat_running = False


def register():
    global _agent_id
    try:
        data = http_post_json(
            f"{NEXUSAI_PROXY}/agent_communication.AgentCommunicationService/RegisterAgent",
            {
                "agent_info": {
                    "host": AGENT_HOST,
                    "port": AGENT_PORT,
                    "service_name": AGENT_NAME,
                    "version": AGENT_VERSION,
                    "skills": [s["name"] for s in AGENT_CARD["skills"]],
                    "metadata": {"type": "demo"},
                    "agent_card": json.dumps(AGENT_CARD, ensure_ascii=False),
                },
                "heartbeat_interval": HEARTBEAT_INTERVAL,
            },
        )
        if "error" in data:
            print(f"  注册失败: {data['error']}")
            return False
        _agent_id = data.get("agent_id", "")
        print(f"  注册成功, agent_id={_agent_id}")
        return True
    except Exception as e:
        print(f"  注册失败: {e}")
        return False


def _heartbeat_loop():
    while _heartbeat_running:
        time.sleep(HEARTBEAT_INTERVAL)
        if not _heartbeat_running or not _agent_id:
            break
        try:
            http_post_json(
                f"{NEXUSAI_PROXY}/agent_communication.AgentCommunicationService/Heartbeat",
                {"agent_id": _agent_id, "agent_info": {
                    "host": AGENT_HOST, "port": AGENT_PORT,
                    "service_name": AGENT_NAME, "version": AGENT_VERSION,
                }},
                timeout=5,
            )
        except Exception:
            pass


def unregister():
    global _heartbeat_running
    _heartbeat_running = False
    if _agent_id:
        try:
            http_post_json(
                f"{NEXUSAI_PROXY}/agent_communication.AgentCommunicationService/UnregisterAgent",
                {"agent_id": _agent_id, "reason": "shutdown"},
                timeout=5,
            )
            print("  已注销")
        except Exception:
            pass


# ── A2A HTTP 接口（标准库 http.server）──────────────────────────────────

class A2AHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass  # Suppress default logging

    def do_GET(self):
        if self.path == "/.well-known/agent-card.json":
            self._send_json(AGENT_CARD)
        else:
            self.send_error(404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(length).decode("utf-8"))
        method = body.get("method", "")
        req_id = body.get("id", "")
        params = body.get("params", {})

        if method == "message/send":
            self._handle_send(req_id, params)
        elif method == "message/stream":
            self._handle_stream(req_id, params)
        else:
            self._send_json({
                "jsonrpc": "2.0", "id": req_id,
                "error": {"code": -32601, "message": f"Unknown method: {method}"},
            }, status=400)

    def _extract_text(self, params):
        for part in params.get("message", {}).get("parts", []):
            if part.get("type") == "text" or part.get("kind") == "text":
                return part.get("text", "")
        return ""

    def _handle_send(self, req_id, params):
        user_text = self._extract_text(params)
        answer = f"[Echo] {user_text}"
        print(f"  <- message/send: {user_text[:60]}")
        self._send_json({
            "jsonrpc": "2.0", "id": req_id,
            "result": {
                "type": "message",
                "message": {
                    "message_id": str(uuid.uuid4()),
                    "context_id": params.get("context_id", ""),
                    "role": "agent",
                    "parts": [{"type": "text", "text": answer}],
                },
            },
        })

    def _handle_stream(self, req_id, params):
        user_text = self._extract_text(params)
        print(f"  <- message/stream: {user_text[:60]}")

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()

        # working 状态
        working = {
            "jsonrpc": "2.0", "id": req_id,
            "result": {"type": "status", "status": {
                "state": "working",
                "message": {"message_id": str(uuid.uuid4()), "role": "agent",
                            "parts": [{"type": "text", "text": "处理中..."}]},
            }},
        }
        self.wfile.write(f"data: {json.dumps(working)}\n\n".encode("utf-8"))
        self.wfile.flush()

        time.sleep(0.3)

        # completed
        completed = {
            "jsonrpc": "2.0", "id": req_id,
            "result": {"type": "status", "status": {
                "state": "completed",
                "message": {"message_id": str(uuid.uuid4()), "role": "agent",
                            "parts": [{"type": "text", "text": f"[Echo] {user_text}"}]},
            }},
        }
        self.wfile.write(f"data: {json.dumps(completed)}\n\n".encode("utf-8"))
        self.wfile.flush()

    def _send_json(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


# ── 主程序 ────────────────────────────────────────────────────────────────

def main():
    global _heartbeat_running

    print(f"[{AGENT_NAME}] 启动...")
    if not register():
        sys.exit(1)

    _heartbeat_running = True
    threading.Thread(target=_heartbeat_loop, daemon=True).start()

    def shutdown(sig, frame):
        print(f"\n[{AGENT_NAME}] 关闭中...")
        unregister()
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    print(f"[{AGENT_NAME}] A2A 监听 http://0.0.0.0:{AGENT_PORT}")
    server = HTTPServer(("0.0.0.0", AGENT_PORT), A2AHandler)
    server.serve_forever()


if __name__ == "__main__":
    main()
