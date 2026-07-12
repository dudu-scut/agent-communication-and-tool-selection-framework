#!/bin/bash
# ============================================================================
# Run all 8 verification batches sequentially
# Usage: bash verify/run-all-batches.sh
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SCRIPTS_DIR="$SCRIPT_DIR/scripts"

# Ensure grpcurl is on PATH
export PATH="$HOME/.local/bin:$PATH"

echo "========================================="
echo " NexusAI E2E 验证测试 — 逐批运行"
echo " 时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "========================================="
echo ""

# ---- Start services ----
echo "--- 启动服务 ---"

# Redis
redis-cli ping 2>/dev/null && echo "Redis: 已运行" || {
    echo "启动 Redis..."
    redis-server --port 6379 --daemonize yes --loglevel notice 2>/dev/null || true
    sleep 1
}

# Mock Agent
curl -s http://localhost:5100/health >/dev/null 2>&1 && echo "Mock Agent: 已运行" || {
    echo "启动 Mock Agent..."
    python3 "$SCRIPT_DIR/mock-agent/mock_agent_server.py" &
    sleep 1
}

# gRPC Server
echo "gRPC Server: 尝试启动..."
cd "$PROJECT_ROOT" && ./run.sh start 2>/dev/null || echo "gRPC Server: 启动失败或已运行"
sleep 2

# Orchestrator (port 5000)
curl -s http://localhost:5000/.well-known/agent-card.json >/dev/null 2>&1 && echo "Orchestrator: 已运行" || {
    echo "启动 Orchestrator..."
    python3 "$PROJECT_ROOT/examples/orchestrator_agent.py" >> "$PROJECT_ROOT/logs/orchestrator.log" 2>&1 &
    sleep 2
}

echo ""
echo "--- 服务状态 ---"
echo -n "Redis: "; redis-cli ping 2>/dev/null || echo "DOWN"
echo -n "Mock Agent: "; curl -s -o /dev/null -w "%{http_code}" http://localhost:5100/health 2>/dev/null; echo ""
echo ""

# ---- Run batches ----
declare -a BATCH_NAMES=(
    "Batch 1 — 基础设施"
    "Batch 2 — 韧性 + 反馈"
    "Batch 3 — 缓存 + 控制"
    "Batch 4 — UX 核心"
    "Batch 5 — 运维工具"
    "Batch 6 — 平台扩展"
    "Batch 7 — 增长留存"
    "Batch 8 — 协议安全"
)

TOTAL_PASS=0
TOTAL_FAIL=0
TOTAL_WARN=0
declare -a BATCH_LINES

for i in 1 2 3 4 5 6 7 8; do
    SCRIPT="$SCRIPTS_DIR/verify-batch${i}.sh"
    BATCH_NAME="${BATCH_NAMES[$((i-1))]}"

    echo "========================================="
    echo "  运行: $BATCH_NAME"
    echo "========================================="

    if [ ! -x "$SCRIPT" ]; then
        echo "  [SKIP] 脚本不存在: $SCRIPT"
        BATCH_LINES+=("$BATCH_NAME: SKIP")
        continue
    fi

    # Run the batch script and capture exit code
    set +e
    bash "$SCRIPT" 2>&1
    EXIT_CODE=$?
    set -e

    if [ $EXIT_CODE -eq 0 ]; then
        BATCH_LINES+=("$BATCH_NAME: PASS")
    else
        BATCH_LINES+=("$BATCH_NAME: FAIL (exit=$EXIT_CODE)")
    fi
    echo ""
done

# ---- Summary ----
echo "========================================="
echo " NexusAI 验证测试报告"
echo " 完成时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "========================================="
echo ""
for line in "${BATCH_LINES[@]}"; do
    echo "  $line"
done
echo ""
echo "========================================="
echo " 结果说明:"
echo "   [PASS] = 该批所有断言通过"
echo "   [FAIL] = 有断言失败"
echo "   [WARN] = 部分测试因缺少依赖而降级（非阻塞）"
echo ""
echo " 关键依赖检查:"
echo -n "   grpcurl: "; command -v grpcurl >/dev/null 2>&1 && echo "已安装" || echo "未安装 (gRPC测试无法通过)"
echo -n "   redis-cli: "; command -v redis-cli >/dev/null 2>&1 && echo "已安装" || echo "未安装"
echo -n "   psql: "; command -v psql >/dev/null 2>&1 && echo "已安装" || echo "未安装 (PG测试无法通过)"
echo -n "   python3: "; command -v python3 >/dev/null 2>&1 && echo "已安装" || echo "未安装"
echo -n "   curl: "; command -v curl >/dev/null 2>&1 && echo "已安装" || echo "未安装"
echo "========================================="
