#!/usr/bin/env python3
"""Translator Agent — 翻译 Demo Agent

简单的中英互译 Agent，用于验证 NexusAI 多 Agent 路由切换。
不依赖外部 LLM，使用规则匹配做演示。

用法:
    pip install flask requests
    python translator_agent.py
"""

import json
import re
import signal
import sys
import threading
import time
import uuid

from flask import Flask, request as flask_request, jsonify, Response
import requests

# ── 配置 ─────────────────────────────────────────────────────────────────
AGENT_NAME = "translator-agent"
AGENT_VERSION = "1.0.0"
AGENT_HOST = "127.0.0.1"
AGENT_PORT = 9092
NEXUSAI_PROXY = "http://127.0.0.1:8081"
HEARTBEAT_INTERVAL = 15

AGENT_CARD = {
    "name": AGENT_NAME,
    "description": "翻译 Agent，支持中英文互译",
    "url": f"http://{AGENT_HOST}:{AGENT_PORT}/",
    "version": AGENT_VERSION,
    "skills": [
        {"name": "translate", "description": "中英文互译，支持短语和句子翻译"},
    ],
}

# ── 注册 ─────────────────────────────────────────────────────────────────

_agent_id = None
_heartbeat_running = False


def register():
    global _agent_id
    resp = requests.post(
        f"{NEXUSAI_PROXY}/agent_communication.AgentCommunicationService/RegisterAgent",
        json={
            "agent_info": {
                "host": AGENT_HOST, "port": AGENT_PORT,
                "service_name": AGENT_NAME, "version": AGENT_VERSION,
                "skills": [s["name"] for s in AGENT_CARD["skills"]],
                "metadata": {"type": "demo"},
                "agent_card": json.dumps(AGENT_CARD, ensure_ascii=False),
            },
            "heartbeat_interval": HEARTBEAT_INTERVAL,
        },
        timeout=10,
    )
    data = resp.json()
    if "error" in data:
        print(f"  注册失败: {data['error']}")
        return False
    _agent_id = data.get("agent_id", "")
    print(f"  注册成功, agent_id={_agent_id}")
    return True


def _heartbeat_loop():
    while _heartbeat_running:
        time.sleep(HEARTBEAT_INTERVAL)
        if not _heartbeat_running or not _agent_id:
            break
        try:
            requests.post(
                f"{NEXUSAI_PROXY}/agent_communication.AgentCommunicationService/Heartbeat",
                json={"agent_id": _agent_id, "agent_info": {
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
            requests.post(
                f"{NEXUSAI_PROXY}/agent_communication.AgentCommunicationService/UnregisterAgent",
                json={"agent_id": _agent_id, "reason": "shutdown"},
                timeout=5,
            )
            print("  已注销")
        except Exception:
            pass


# ── 翻译逻辑（规则匹配演示用） ──────────────────────────────────────────

_ZH_TO_EN = {
    "你好": "Hello", "谢谢": "Thank you", "再见": "Goodbye",
    "早上好": "Good morning", "晚上好": "Good evening",
    "是的": "Yes", "不是": "No", "请": "Please",
    "对不起": "Sorry", "没关系": "It's okay",
    "我是": "I am", "你是": "You are",
}

_EN_TO_ZH = {v.lower(): k for k, v in _ZH_TO_EN.items()}


def _has_chinese(text):
    return bool(re.search(r'[\u4e00-\u9fff]', text))


def translate(text):
    """简单的中英互译（规则匹配，仅作演示）"""
    cleaned = text.strip().strip("。，.！!？?")

    if _has_chinese(cleaned):
        # 中 → 英
        for zh, en in _ZH_TO_EN.items():
            if zh in cleaned:
                return f"[中→英] {cleaned} → {cleaned.replace(zh, en)}"
        return f"[中→英] {cleaned} → (Demo 词典未收录，实际部署请接入 LLM 翻译)"
    else:
        # 英 → 中
        lower = cleaned.lower()
        for en, zh in _EN_TO_ZH.items():
            if en in lower:
                return f"[英→中] {cleaned} → {cleaned.replace(cleaned[lower.index(en):lower.index(en)+len(en)], zh)}"
        return f"[英→中] {cleaned} → (Demo 词典未收录，实际部署请接入 LLM 翻译)"


# ── A2A HTTP 接口 ────────────────────────────────────────────────────────

app = Flask(__name__)


@app.route("/.well-known/agent-card.json")
def agent_card_endpoint():
    return jsonify(AGENT_CARD)


@app.route("/", methods=["POST"])
def a2a_handler():
    body = flask_request.get_json()
    method = body.get("method", "")
    req_id = body.get("id", "")
    params = body.get("params", {})

    if method == "message/send":
        return _handle_send(req_id, params)
    elif method == "message/stream":
        return _handle_stream(req_id, params)
    else:
        return jsonify({
            "jsonrpc": "2.0", "id": req_id,
            "error": {"code": -32601, "message": f"Unknown method: {method}"},
        }), 400


def _extract_text(params):
    for part in params.get("message", {}).get("parts", []):
        if part.get("type") == "text":
            return part.get("text", "")
    return ""


def _handle_send(req_id, params):
    user_text = _extract_text(params)
    answer = translate(user_text)
    print(f"  ← send: {user_text[:60]} → {answer[:60]}")
    return jsonify({
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


def _handle_stream(req_id, params):
    user_text = _extract_text(params)
    print(f"  ← stream: {user_text[:60]}")

    def generate():
        yield f'data: {json.dumps({"jsonrpc":"2.0","id":req_id,"result":{"type":"status","status":{"state":"working","message":{"message_id":str(uuid.uuid4()),"role":"agent","parts":[{"type":"text","text":"翻译中..."}]}}}})}\n\n'
        time.sleep(0.3)
        answer = translate(user_text)
        yield f'data: {json.dumps({"jsonrpc":"2.0","id":req_id,"result":{"type":"status","status":{"state":"completed","message":{"message_id":str(uuid.uuid4()),"role":"agent","parts":[{"type":"text","text":answer}]}}}})}\n\n'

    return Response(generate(), mimetype="text/event-stream")


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
    app.run(host="0.0.0.0", port=AGENT_PORT)


if __name__ == "__main__":
    main()
