#!/usr/bin/env python3
"""Math Agent — 数学计算 Demo Agent

支持基础算术和方程求解，用于验证 NexusAI 路由匹配（math/equation skill）。

用法:
    pip install flask requests sympy
    python math_agent.py
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
AGENT_NAME = "math-agent"
AGENT_VERSION = "1.0.0"
AGENT_HOST = "127.0.0.1"
AGENT_PORT = 9091
NEXUSAI_PROXY = "http://127.0.0.1:8081"
HEARTBEAT_INTERVAL = 15

AGENT_CARD = {
    "name": AGENT_NAME,
    "description": "数学计算 Agent，支持算术运算和方程求解",
    "url": f"http://{AGENT_HOST}:{AGENT_PORT}/",
    "version": AGENT_VERSION,
    "skills": [
        {"name": "math", "description": "基础算术运算：加减乘除、幂运算、取余"},
        {"name": "equation", "description": "方程求解：一元方程、简单代数方程"},
    ],
}

# ── 注册（与 echo_agent 相同逻辑） ───────────────────────────────────────

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


# ── 数学计算逻辑 ─────────────────────────────────────────────────────────

def solve_math(text):
    """尝试解析并计算用户输入的数学表达式"""

    # 尝试 sympy 方程求解
    eq_match = re.search(r'(?:解方程|solve|求解)\s*[:：]?\s*(.+)', text, re.IGNORECASE)
    if eq_match:
        try:
            from sympy import symbols, solve as sym_solve, sympify, Eq
            x = symbols('x')
            expr_text = eq_match.group(1).strip()
            # 处理 "x^2 - 4 = 0" 形式
            expr_text = expr_text.replace('^', '**')
            if '=' in expr_text:
                lhs, rhs = expr_text.split('=', 1)
                eq = Eq(sympify(lhs.strip()), sympify(rhs.strip()))
                solutions = sym_solve(eq, x)
            else:
                solutions = sym_solve(sympify(expr_text), x)
            return f"方程 {eq_match.group(1).strip()} 的解: x = {solutions}"
        except ImportError:
            return "sympy 未安装，无法解方程。请先安装: pip install sympy"
        except Exception as e:
            return f"方程求解失败: {e}"

    # 尝试直接算术计算
    # 安全的算术表达式：只允许数字和基本运算符
    safe_expr = text.strip()
    safe_expr = safe_expr.replace('^', '**').replace('×', '*').replace('÷', '/')
    if re.match(r'^[\d\s+\-*/().%]+$', safe_expr):
        try:
            result = eval(safe_expr, {"__builtins__": {}}, {})
            return f"{text.strip()} = {result}"
        except Exception as e:
            return f"计算失败: {e}"

    return (
        f"我是数学 Agent，支持以下功能:\n"
        f"  1. 算术计算: 输入表达式如 '2 + 3 * 4' 或 '(100 - 20) / 4'\n"
        f"  2. 方程求解: 输入 '解方程: x**2 - 4 = 0'\n"
        f"\n你的输入: {text}"
    )


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
    answer = solve_math(user_text)
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
        yield f'data: {json.dumps({"jsonrpc":"2.0","id":req_id,"result":{"type":"status","status":{"state":"working","message":{"message_id":str(uuid.uuid4()),"role":"agent","parts":[{"type":"text","text":"正在计算..."}]}}}})}\n\n'
        time.sleep(0.5)
        answer = solve_math(user_text)
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
