#!/usr/bin/env python3
"""Orchestrator Agent — NexusAI A2A 编排路由

纯标准库实现，零外部依赖。
启动后自动注册到 NexusAI 平台，根据技能匹配将请求路由到已注册的 Agent。

用法:
    python3 orchestrator_agent.py
"""

import json
import logging
import os
import random
import signal
import sys
import threading
import time
import uuid
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.request import Request, urlopen
from urllib.error import URLError

# ── 配置 ─────────────────────────────────────────────────────────────────
AGENT_NAME = "orchestrator"
AGENT_VERSION = "1.0.0"
AGENT_HOST = os.environ.get("AGENT_HOST", "127.0.0.1")
AGENT_PORT = 5000
NEXUSAI_PROXY = os.environ.get("NEXUSAI_PROXY", "http://127.0.0.1:8081")
HEARTBEAT_INTERVAL = 15
MAX_CONTENT_LENGTH = 1 * 1024 * 1024  # Fix #37: 1MB limit to prevent DoS

AGENT_CARD = {
    "name": AGENT_NAME,
    "description": "A2A Orchestrator — routes queries to registered agents by skill matching",
    "url": f"http://{AGENT_HOST}:{AGENT_PORT}/",
    "version": AGENT_VERSION,
    "skills": [
        {"name": "general", "description": "General-purpose query orchestration"},
        {"name": "orchestration", "description": "Multi-agent task routing and delegation"},
    ],
}

# ── Logging ──────────────────────────────────────────────────────────────
logging.basicConfig(level=logging.INFO,
                    format='[%(asctime)s] %(levelname)s: %(message)s')
logger = logging.getLogger(AGENT_NAME)

# ── HTTP 工具 ────────────────────────────────────────────────────────────

def http_post_json(url, data, timeout=10):
    """Simple POST with JSON body, returns parsed JSON response.

    Fix #33: Uses 'with' statement to ensure response is properly closed.
    """
    body = json.dumps(data, ensure_ascii=False).encode("utf-8")
    req = Request(url, data=body, headers={"Content-Type": "application/json"})
    with urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


# ── gRPC 注册（通过 HTTP proxy 透传） ──────────────────────────────────

_agent_id = None
_heartbeat_running = threading.Event()  # Fix #35: use threading.Event for thread safety
_shutdown_requested = threading.Event()  # Fix #31: signal handler only sets flag


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
                    "metadata": {"type": "orchestrator"},
                    "agent_card": json.dumps(AGENT_CARD, ensure_ascii=False),
                },
                "heartbeat_interval": HEARTBEAT_INTERVAL,
            },
        )
        if "error" in data:
            logger.warning(f"注册失败: {data['error']}")
            return False
        _agent_id = data.get("agent_id", "")
        logger.info(f"注册成功, agent_id={_agent_id}")
        return True
    except Exception as e:
        logger.warning(f"注册失败: {e}")  # Fix #34: log instead of silent pass
        return False


def _heartbeat_loop():
    while _heartbeat_running.is_set():  # Fix #35: use Event.is_set()
        time.sleep(HEARTBEAT_INTERVAL)
        if not _heartbeat_running.is_set() or not _agent_id:
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
        except ConnectionError:
            logger.warning("心跳失败: 无法连接到 NexusAI")
        except Exception as e:
            logger.warning(f"心跳失败: {e}")  # Fix #34: log instead of silent pass


def unregister():
    _heartbeat_running.clear()  # Fix #35: Event.clear()
    if _agent_id:
        try:
            http_post_json(
                f"{NEXUSAI_PROXY}/agent_communication.AgentCommunicationService/UnregisterAgent",
                {"agent_id": _agent_id, "reason": "shutdown"},
                timeout=5,
            )
            logger.info("已注销")
        except ConnectionError:
            logger.warning("注销请求失败: 无法连接到 NexusAI")
        except Exception as e:
            logger.warning(f"注销失败: {e}")  # Fix #34


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
        # Fix #32: JSON parse error handling
        try:
            content_length = int(self.headers.get("Content-Length", 0))
        except (ValueError, TypeError):
            self.send_error(400, "Invalid Content-Length")
            return

        # Fix #37: Content-Length upper bound check
        if content_length > MAX_CONTENT_LENGTH:
            self.send_error(413, "Payload Too Large")
            return

        try:
            body = json.loads(self.rfile.read(content_length).decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            self.send_error(400, f"Invalid JSON: {e}")
            return

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

    def _discover_agents(self):
        try:
            data = http_post_json(
                f"{NEXUSAI_PROXY}/agent_communication.AgentCommunicationService/GetAgents",
                {}, timeout=5)
            return data.get("agents", [])
        except Exception:
            return []

    def _route_to_agent(self, skill_name, params, depth=0):
        agents = self._discover_agents()
        candidates = [a for a in agents
                      if any(s in a.get("skills", []) for s in [skill_name, "general", "orchestration"])]
        if not candidates:
            return {"error": {"code": -32000, "message": f"No agent found for skill: {skill_name}"}}
        canary = [a for a in candidates if a.get("deployment_stage", "STABLE") == "CANARY"]
        stable = [a for a in candidates if a.get("deployment_stage", "STABLE") == "STABLE"]
        selected = random.choice(canary) if (canary and random.randint(1, 100) <= 10) else random.choice(stable or canary or candidates)
        host = selected.get("host", "127.0.0.1")
        port = selected.get("port", 5100)
        agent_url = f"http://{host}:{port}/tasks/send"
        try:
            body = {"jsonrpc": "2.0", "id": str(uuid.uuid4()), "method": "message/send", "params": params}
            body_bytes = json.dumps(body, ensure_ascii=False).encode("utf-8")
            req = Request(agent_url, data=body_bytes,
                          headers={"Content-Type": "application/json", "x-delegation-depth": str(depth)})
            with urlopen(req, timeout=30) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except Exception as e:
            return {"error": {"code": -32000, "message": f"Agent call failed: {e}"}}

    def _infer_skill(self, text):
        t = text.lower()
        if any(w in t for w in ["translate", "translator", "翻译"]): return "translation"
        if any(w in t for w in ["math", "calculate", "计算", "equation"]): return "math"
        if any(w in t for w in ["code", "code review", "bug"]): return "code-review"
        return "general"

    def _handle_send(self, req_id, params):
        user_text = self._extract_text(params)
        skill_match = self._infer_skill(user_text)
        logger.info(f"Routing '{user_text[:40]}' -> skill={skill_match}")
        result = self._route_to_agent(skill_match, params)
        if "result" in result:
            self._send_json(result)
        elif "error" in result:
            self._send_json({"jsonrpc": "2.0", "id": req_id, "result": {
                "type": "message", "message": {
                    "message_id": str(uuid.uuid4()), "context_id": params.get("context_id", ""),
                    "role": "agent", "parts": [{"kind": "text", "text": str(result.get("error", {}))}],
                }}})
        else:
            self._send_json({"jsonrpc": "2.0", "id": req_id, "result": {
                "type": "message", "message": {
                    "message_id": str(uuid.uuid4()), "context_id": params.get("context_id", ""),
                    "role": "agent", "parts": [{"kind": "text", "text": f"Orchestrator: routed to {skill_match}"}],
                }}})

    def _handle_stream(self, req_id, params):
        user_text = self._extract_text(params)
        skill_match = self._infer_skill(user_text)
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        working = {"jsonrpc": "2.0", "id": req_id, "result": {"type": "status", "status": {
            "state": "working", "message": {"message_id": str(uuid.uuid4()), "role": "agent",
            "parts": [{"kind": "text", "text": f"Routing to {skill_match} agent..."}]}}}}
        self.wfile.write(f"data: {json.dumps(working)}\n\n".encode("utf-8"))
        self.wfile.flush()
        result = self._route_to_agent(skill_match, params)
        completed = {"jsonrpc": "2.0", "id": req_id, "result": {"type": "status", "status": {
            "state": "completed", "message": {"message_id": str(uuid.uuid4()), "role": "agent",
            "parts": [{"kind": "text", "text": json.dumps(result)}]}}}}
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
    logger.info(f"[{AGENT_NAME}] 启动...")
    if not register():
        logger.warning(f"[{AGENT_NAME}] 注册失败，继续运行（功能受限）")

    _heartbeat_running.set()  # Fix #35: use Event.set()
    threading.Thread(target=_heartbeat_loop, daemon=True).start()

    server = HTTPServer(("0.0.0.0", AGENT_PORT), A2AHandler)

    # Fix #31: Signal handler only sets flag — no HTTP I/O in signal context
    def shutdown(sig, frame):
        logger.info(f"[{AGENT_NAME}] 收到信号 {sig}，关闭中...")
        _shutdown_requested.set()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    logger.info(f"[{AGENT_NAME}] A2A 监听 http://0.0.0.0:{AGENT_PORT}")

    # Main loop: serve with short timeout to check shutdown flag
    server.timeout = 1.0  # Check shutdown flag every second
    try:
        while not _shutdown_requested.is_set():
            server.handle_request()
    finally:
        # Fix #36: Properly release socket
        unregister()
        server.server_close()
        logger.info(f"[{AGENT_NAME}] 已关闭")


if __name__ == "__main__":
    main()
