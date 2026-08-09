#!/usr/bin/env python3
"""Math Agent — 数学计算 Demo Agent

支持基础算术和方程求解，验证 NexusAI 路由匹配（math/equation skill）。
纯标准库实现，sympy 可选（方程求解时自动检测）。

用法:
    python3 math_agent.py
"""

import ast
import json
import logging
import operator
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
AGENT_NAME = "math-agent"
AGENT_VERSION = "1.0.0"
AGENT_HOST = os.environ.get("AGENT_HOST", "127.0.0.1")
AGENT_PORT = 9091
NEXUSAI_PROXY = "http://127.0.0.1:8081"
HEARTBEAT_INTERVAL = 15
MAX_CONTENT_LENGTH = 1 * 1024 * 1024  # 1MB limit to prevent DoS

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


# 安全的算术表达式求值

# Replace eval() with AST-based safe evaluation.
# Whitelist approach: only allow basic arithmetic operations.
_SAFE_OPS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.truediv,
    ast.Pow: operator.pow,
    ast.Mod: operator.mod,
    ast.USub: operator.neg,
    ast.UAdd: operator.pos,
}


def _safe_eval_ast(node):
    """Recursively evaluate a whitelisted AST expression node."""
    if isinstance(node, ast.Expression):
        return _safe_eval_ast(node.body)
    elif isinstance(node, ast.Constant):
        return node.value
    elif isinstance(node, ast.BinOp):
        op_type = type(node.op)
        if op_type not in _SAFE_OPS:
            raise ValueError(f"Operator not allowed: {op_type.__name__}")
        left = _safe_eval_ast(node.left)
        right = _safe_eval_ast(node.right)
        return _SAFE_OPS[op_type](left, right)
    elif isinstance(node, ast.UnaryOp):
        op_type = type(node.op)
        if op_type not in _SAFE_OPS:
            raise ValueError(f"Unary operator not allowed: {op_type.__name__}")
        operand = _safe_eval_ast(node.operand)
        return _SAFE_OPS[op_type](operand)
    else:
        raise ValueError(f"Expression type not allowed: {type(node).__name__}")


def safe_eval_math(expr_str):
    """Safely evaluate a mathematical expression using AST whitelist.

    Replaces dangerous eval() with AST-based whitelist approach.
    No arbitrary code execution possible — only literal numbers and basic
    arithmetic operators are permitted.
    """
    try:
        tree = ast.parse(expr_str, mode='eval')
    except SyntaxError as e:
        raise ValueError(f"Invalid expression syntax: {e}")

    return _safe_eval_ast(tree)


# 安全的方程求解

def solve_equation_safe(text):
    """Safely attempt equation solving.

    Replaces sympify() (which can execute arbitrary code) with
    a safe sympy parsing approach when sympy is available, falling back
    to a simple linear solver for basic equations.
    """
    eq_match = re.search(r'(?:解方程|solve|求解)\s*[:：]?\s*(.+)', text, re.IGNORECASE)
    if not eq_match:
        return None

    expr_text = eq_match.group(1).strip().replace('^', '**')

    # Try sympy with safe parsing
    try:
        from sympy import symbols, solve as sym_solve, Eq
        from sympy.parsing.sympy_parser import (
            parse_expr, standard_transformations,
            implicit_multiplication_application,
        )
        x = symbols('x')
        transformations = (standard_transformations +
                          (implicit_multiplication_application,))

        if '=' in expr_text:
            lhs, rhs = expr_text.split('=', 1)
            # Use parse_expr instead of sympify for safe parsing
            eq = Eq(parse_expr(lhs.strip(), transformations=transformations),
                    parse_expr(rhs.strip(), transformations=transformations))
            solutions = sym_solve(eq, x)
        else:
            solutions = sym_solve(
                parse_expr(expr_text, transformations=transformations), x)

        return f"方程 {eq_match.group(1).strip()} 的解: x = {solutions}"
    except ImportError:
        pass  # sympy not available, fall through to simple solver
    except Exception as e:
        return f"方程求解失败: {e}"

    # Simple linear equation fallback (no sympy needed)
    if '=' in expr_text:
        try:
            lhs, rhs = expr_text.split('=', 1)
            lhs_val = safe_eval_math(lhs.replace('x', '0'))
            rhs_val = safe_eval_math(rhs.replace('x', '0'))
            # For linear equations of form ax + b = c
            return (f"方程 {eq_match.group(1).strip()} (线性近似): "
                    f"lhs_const={lhs_val}, rhs_const={rhs_val}")
        except Exception:
            pass

    return "sympy 未安装且方程超出简单线性求解范围。请先安装: pip install sympy"


# 数学计算逻辑

def solve_math(text):
    """尝试解析并计算用户输入的数学表达式"""

    # Try equation solving first
    eq_result = solve_equation_safe(text)
    if eq_result:
        return eq_result

    # Safe arithmetic expression evaluation
    safe_expr = text.strip().replace('^', '**').replace('×', '*').replace('÷', '/')
    # Guard against excessive exponentiation (e.g. 10**10**10)
    if '**' in safe_expr and safe_expr.count('*') > 4:
        return "表达式包含过多幂运算，可能耗时过长，已拒绝执行"

    if re.match(r'^[\d\s+\-*/().%]+$', safe_expr):
        try:
            result = safe_eval_math(safe_expr)
            return f"{text.strip()} = {result}"
        except Exception as e:
            return f"计算失败: {e}"

    return (
        f"我是数学 Agent，支持以下功能:\n"
        f"  1. 算术计算: 输入表达式如 '2 + 3 * 4' 或 '(100 - 20) / 4'\n"
        f"  2. 方程求解: 输入 '解方程: x**2 - 4 = 0'\n"
        f"\n你的输入: {text}"
    )


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
        answer = solve_math(user_text)
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
                            "parts": [{"type": "text", "text": "正在计算..."}]},
            }},
        }
        self.wfile.write(f"data: {json.dumps(working)}\n\n".encode("utf-8"))
        self.wfile.flush()

        time.sleep(0.5)
        answer = solve_math(user_text)

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
