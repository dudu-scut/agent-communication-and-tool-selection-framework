#!/usr/bin/env python3
"""Translator Agent — 翻译 Demo Agent

简单的中英互译 Agent，验证 NexusAI 多 Agent 路由切换。
纯标准库实现，零外部依赖。

用法:
    python3 translator_agent.py
"""

import json
import logging
import os
import re
import signal
import sys
import threading
import time
import uuid
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.request import Request, urlopen

# 配置
AGENT_NAME = "translator-agent"
AGENT_VERSION = "1.0.0"
AGENT_HOST = os.environ.get("AGENT_HOST", "127.0.0.1")
AGENT_PORT = 9092
NEXUSAI_PROXY = "http://127.0.0.1:8081"
HEARTBEAT_INTERVAL = 15
MAX_CONTENT_LENGTH = 1 * 1024 * 1024  # 1MB limit to prevent DoS

AGENT_CARD = {
    "name": AGENT_NAME,
    "description": "翻译 Agent，支持中英文互译",
    "url": f"http://{AGENT_HOST}:{AGENT_PORT}/",
    "version": AGENT_VERSION,
    "skills": [
        {"name": "translate", "description": "中英文互译，支持短语和句子翻译"},
    ],
}

# Logging
logging.basicConfig(level=logging.INFO,
                    format='[%(asctime)s] %(levelname)s: %(message)s')
logger = logging.getLogger(AGENT_NAME)

# HTTP 工具

def http_post_json(url, data, timeout=10):
    """Simple POST with JSON body, returns parsed JSON response.

    Uses 'with' statement to ensure response is properly closed.
    """
    body = json.dumps(data, ensure_ascii=False).encode("utf-8")
    req = Request(url, data=body, headers={"Content-Type": "application/json"})
    with urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


# 注册

_agent_id = None
_heartbeat_running = threading.Event()  # use threading.Event for thread safety
_shutdown_requested = threading.Event()  # signal handler only sets flag


def register():
    global _agent_id
    try:
        data = http_post_json(
            f"{NEXUSAI_PROXY}/agent_communication.AgentCommunicationService/RegisterAgent",
            {
                "agent_info": {
                    "host": AGENT_HOST, "port": AGENT_PORT,
                    "service_name": AGENT_NAME, "version": AGENT_VERSION,
                    "skills": [s["name"] for s in AGENT_CARD["skills"]],
                    "metadata": {"type": "demo"},
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
        logger.warning(f"注册失败: {e}")
        return False


def _heartbeat_loop():
    while _heartbeat_running.is_set():
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
            logger.warning(f"心跳失败: {e}")


def unregister():
    _heartbeat_running.clear()
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
            logger.warning(f"注销失败: {e}")


# 翻译逻辑（规则匹配演示用）

_ZH_TO_EN = {
    "你好": "Hello", "谢谢": "Thank you", "再见": "Goodbye",
    "早上好": "Good morning", "晚上好": "Good evening",
    "是的": "Yes", "不是": "No", "请": "Please",
    "对不起": "Sorry", "没关系": "It's okay",
    "我是": "I am", "你是": "You are",
}

_EN_TO_ZH = {v.lower(): k for k, v in _ZH_TO_EN.items()}


def _has_chinese(text):
    return bool(re.search(r'[一-鿿]', text))


def translate(text):
    """简单的中英互译（规则匹配，仅作演示）"""
    cleaned = text.strip().strip("。，.！!？?")

    if _has_chinese(cleaned):
        for zh, en in _ZH_TO_EN.items():
            if zh in cleaned:
                return f"[中→英] {cleaned} → {cleaned.replace(zh, en)}"
        return f"[中→英] {cleaned} → (Demo 词典未收录，实际部署请接入 LLM 翻译)"
    else:
        lower = cleaned.lower()
        for en, zh in _EN_TO_ZH.items():
            if en in lower:
                idx = lower.index(en)
                original = cleaned[idx:idx + len(en)]
                return f"[英→中] {cleaned} → {cleaned.replace(original, zh)}"
        return f"[英→中] {cleaned} → (Demo 词典未收录，实际部署请接入 LLM 翻译)"


# A2A HTTP 接口

class A2AHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def do_GET(self):
        if self.path == "/.well-known/agent-card.json":
            self._send_json(AGENT_CARD)
        else:
            self.send_error(404)

    def do_POST(self):
        # JSON parse error handling
        try:
            content_length = int(self.headers.get("Content-Length", 0))
        except (ValueError, TypeError):
            self.send_error(400, "Invalid Content-Length")
            return

        # Content-Length upper bound check
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

    def _handle_send(self, req_id, params):
        user_text = self._extract_text(params)
        answer = translate(user_text)
        logger.info(f"<- send: {user_text[:60]} -> {answer[:60]}")
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
        logger.info(f"<- stream: {user_text[:60]}")

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()

        working = {
            "jsonrpc": "2.0", "id": req_id,
            "result": {"type": "status", "status": {
                "state": "working",
                "message": {"message_id": str(uuid.uuid4()), "role": "agent",
                            "parts": [{"type": "text", "text": "翻译中..."}]},
            }},
        }
        self.wfile.write(f"data: {json.dumps(working)}\n\n".encode("utf-8"))
        self.wfile.flush()

        time.sleep(0.3)
        answer = translate(user_text)

        completed = {
            "jsonrpc": "2.0", "id": req_id,
            "result": {"type": "status", "status": {
                "state": "completed",
                "message": {"message_id": str(uuid.uuid4()), "role": "agent",
                            "parts": [{"type": "text", "text": answer}]},
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


# 主程序

def main():
    logger.info(f"[{AGENT_NAME}] 启动...")
    if not register():
        sys.exit(1)

    _heartbeat_running.set()
    threading.Thread(target=_heartbeat_loop, daemon=True).start()

    server = HTTPServer(("0.0.0.0", AGENT_PORT), A2AHandler)

    # Signal handler only sets flag — no HTTP I/O in signal context
    def shutdown(sig, frame):
        logger.info(f"[{AGENT_NAME}] 收到信号 {sig}，关闭中...")
        _shutdown_requested.set()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    logger.info(f"[{AGENT_NAME}] A2A 监听 http://0.0.0.0:{AGENT_PORT}")

    server.timeout = 1.0
    try:
        while not _shutdown_requested.is_set():
            server.handle_request()
    finally:
        # Properly release socket
        unregister()
        server.server_close()
        logger.info(f"[{AGENT_NAME}] 已关闭")


if __name__ == "__main__":
    main()
