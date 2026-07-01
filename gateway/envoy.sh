#!/bin/bash
# ============================================================================
# Envoy 启动脚本（本地开发用）
# ============================================================================
# gRPC-Web ↔ gRPC 协议转换，替代 improbable/grpcwebproxy。
# 生产环境建议使用 docker-compose 部署。
#
# 用法:
#   ./envoy.sh                          # 默认参数启动
#   ./envoy.sh --backend localhost:50051 --port 8081
# ============================================================================

set -euo pipefail

BACKEND_ADDR="${BACKEND_ADDR:-localhost:50051}"
PROXY_PORT="${PROXY_PORT:-8081}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENVOY_CONFIG="$SCRIPT_DIR/envoy.yaml"

# 检查 envoy 是否已安装
if ! command -v envoy &>/dev/null; then
    echo "Envoy 未安装。"
    echo ""
    echo "安装方式（二选一）："
    echo "  1. 使用 Docker（推荐）:"
    echo "     docker compose -f docker-compose.gateway.yaml up -d envoy"
    echo ""
    echo "  2. 本地安装:"
    echo "     https://www.envoyproxy.io/docs/envoy/latest/start/install"
    echo ""
    echo "如果用 docker-compose 部署，不需要本地安装。"
    exit 1
fi

echo "启动 Envoy..."
echo "  监听端口: $PROXY_PORT"
echo "  后端地址: $BACKEND_ADDR"
echo "  配置文件: $ENVOY_CONFIG"

exec envoy -c "$ENVOY_CONFIG" \
    --service-cluster nexusai-gateway \
    --service-node envoy-local
