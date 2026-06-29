#!/bin/bash
# ============================================================================
# grpcwebproxy 启动脚本
# ============================================================================
# 轻量 Go 代理，做 gRPC-Web ↔ gRPC 协议转换。
# 浏览器发 gRPC-Web (HTTP/1.1) → grpcwebproxy → gRPC/2 → NexusAI
#
# 用法:
#   ./grpcwebproxy.sh                    # 默认参数启动
#   ./grpcwebproxy.sh --backend localhost:50051 --port 8081
# ============================================================================

set -euo pipefail

BACKEND_ADDR="${BACKEND_ADDR:-localhost:50051}"
PROXY_PORT="${PROXY_PORT:-8081}"

# 检查 grpcwebproxy 是否已安装
if ! command -v grpcwebproxy &>/dev/null; then
    echo "grpcwebproxy 未安装。"
    echo ""
    echo "安装方式（二选一）："
    echo "  1. Go install:"
    echo "     go install github.com/improbable-eng/grpc-web/go/grpcwebproxy@latest"
    echo ""
    echo "  2. 下载二进制:"
    echo "     https://github.com/improbable-eng/grpc-web/releases"
    echo ""
    echo "如果用 docker-compose 部署，不需要本地安装，容器内自带。"
    exit 1
fi

echo "启动 grpcwebproxy..."
echo "  监听端口: $PROXY_PORT"
echo "  后端地址: $BACKEND_ADDR"

exec grpcwebproxy \
    --backend_addr="$BACKEND_ADDR" \
    --run_tls_server=false \
    --server_http_debug_port="$PROXY_PORT" \
    --allow_all_origins \
    --use_websockets
