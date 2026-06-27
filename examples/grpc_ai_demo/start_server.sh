#!/bin/bash

# gRPC AI Server 启动脚本

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN_DIR="$PROJECT_ROOT/build/examples/grpc_ai_demo"

# 检查可执行文件
if [ ! -f "$BIN_DIR/grpc_server" ]; then
    echo "错误: 找不到 grpc_server，请先编译项目"
    echo "  cd $PROJECT_ROOT && cmake -B build && make -C build grpc_server"
    exit 1
fi

# 获取 API Key
API_KEY="${LLM_API_KEY:-sk-REDACTED}"

# 默认参数
PORT="${1:-50051}"
MODEL="${2:-deepseek-v4-pro}"

echo "==========================================="
echo "启动 gRPC AI Server"
echo "==========================================="
echo "端口: $PORT"
echo "模型: $MODEL"
echo ""

# 启动服务器
exec "$BIN_DIR/grpc_server" "$API_KEY" "$PORT" "$MODEL"
