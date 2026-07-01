#!/bin/bash
# ============================================================================
# NexusAI 项目运行脚本 (Linux)
#
# 用法:
#   ./run.sh build              编译项目
#   ./run.sh test               运行全部测试
#   ./run.sh clean              清理构建目录
#   ./run.sh start              启动 gRPC 服务端 (后台运行)
#   ./run.sh redis              启动 Redis 服务
#   ./run.sh gateway            启动 API 网关 (Nginx + Envoy, 需 Docker)
#   ./run.sh frontend-dev       启动前端开发服务器
#   ./run.sh frontend-build     构建前端生产包
#   ./run.sh stop               停止所有服务 (含 Docker 容器)
#   ./run.sh setup              检测开发环境
# ============================================================================

set -euo pipefail

# ============================================================================
# 路径与默认配置
# ============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_ROOT/build"
LOGS_DIR="$PROJECT_ROOT/logs"
PIDS_DIR="$PROJECT_ROOT/pids"
FRONTEND_DIR="$PROJECT_ROOT/frontend"

BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"

REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"

# ============================================================================
# 工具函数
# ============================================================================
banner() {
    echo ""
    echo "=========================================="
    echo "  $1"
    echo "=========================================="
    echo ""
}

info()  { echo -e "\033[32m[INFO]\033[0m  $1"; }
warn()  { echo -e "\033[33m[WARN]\033[0m  $1"; }
error() { echo -e "\033[31m[ERROR]\033[0m $1"; }

# ============================================================================
# build - 编译项目
# ============================================================================
cmd_build() {
    banner "编译项目"

    for cmd in cmake g++ make pkg-config; do
        if ! command -v "$cmd" &>/dev/null; then
            error "未找到 $cmd，请先安装"
            exit 1
        fi
    done

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    info "构建类型: $BUILD_TYPE"
    info "并行任务: $BUILD_JOBS"

    info "CMake 配置..."
    cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    if [ $? -ne 0 ]; then
        error "CMake 配置失败"
        exit 1
    fi

    info "编译中..."
    make -j"$BUILD_JOBS"
    if [ $? -ne 0 ]; then
        error "编译失败"
        exit 1
    fi

    echo ""
    info "编译完成!"
}

# ============================================================================
# test - 运行测试
# ============================================================================
cmd_test() {
    banner "运行测试"

    if [ ! -d "$BUILD_DIR" ]; then
        error "build 目录不存在，请先编译: ./run.sh build"
        exit 1
    fi

    # Redis 前置检查：AuthService 和 MemoryService 依赖 Redis
    if ! check_redis; then
        warn "Redis 未运行 ($REDIS_HOST:$REDIS_PORT)"
        warn "AuthService / MemoryService 相关测试可能失败"
        warn "启动 Redis: redis-server --port $REDIS_PORT --daemonize yes"
        echo ""
    fi

    cd "$BUILD_DIR"
    ctest --output-on-failure --timeout 30 "$@"
    echo ""
    info "测试完成"
}

# ============================================================================
# clean - 清理构建
# ============================================================================
cmd_clean() {
    banner "清理构建"

    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        info "已删除 $BUILD_DIR"
    else
        warn "build 目录不存在"
    fi
}

# ============================================================================
# start - 启动 gRPC 服务端
# ============================================================================
SERVER_PORT="${RPC_SERVER_PORT:-50051}"
ORCHESTRATOR_URL="${ORCHESTRATOR_URL:-http://localhost:5000}"
GATEWAY_COMPOSE="$PROJECT_ROOT/docker-compose.gateway.yaml"

cmd_start() {
    banner "启动 gRPC 服务端"

    # 前置检查
    local bin="$BUILD_DIR/server/rpc_server"
    if [ ! -x "$bin" ]; then
        error "未找到 rpc_server 二进制: $bin"
        error "请先编译: ./run.sh build"
        exit 1
    fi

    # Redis 检查
    if ! check_redis; then
        warn "Redis 未运行 ($REDIS_HOST:$REDIS_PORT)，尝试自动启动..."
        cmd_redis
    fi

    # 构建启动参数
    local args=("-p" "$SERVER_PORT" "-o" "$ORCHESTRATOR_URL")
    if [ -n "${RPC_REGISTRY_ADDRESS:-}" ]; then
        args+=("-r" "$RPC_REGISTRY_ADDRESS" "--enable-registry")
    fi

    # 环境变量传递
    local env_vars=""
    if [ -n "${LLM_API_KEY:-}" ]; then
        env_vars="LLM_API_KEY=$LLM_API_KEY"
        info "LLM 多 Agent 编排: 已启用"
    else
        info "LLM 多 Agent 编排: 未启用 (设置 LLM_API_KEY 可启用)"
    fi
    if [ -n "${LLM_MODEL:-}" ]; then
        env_vars="$env_vars LLM_MODEL=$LLM_MODEL"
    fi
    if [ -n "${LLM_API_URL:-}" ]; then
        env_vars="$env_vars LLM_API_URL=$LLM_API_URL"
    fi

    mkdir -p "$LOGS_DIR" "$PIDS_DIR"

    # 检查是否已在运行
    local pid_file="$PIDS_DIR/rpc_server.pid"
    if [ -f "$pid_file" ]; then
        local old_pid
        old_pid=$(cat "$pid_file")
        if kill -0 "$old_pid" 2>/dev/null; then
            warn "rpc_server 已在运行 (PID: $old_pid)"
            warn "如需重启，先执行: ./run.sh stop"
            return 1
        fi
        rm -f "$pid_file"
    fi

    info "启动 rpc_server (端口: $SERVER_PORT)..."
    info "Orchestrator: $ORCHESTRATOR_URL"
    info "Redis: $REDIS_HOST:$REDIS_PORT"
    info "日志: $LOGS_DIR/rpc_server.log"

    # 后台启动
    if [ -n "$env_vars" ]; then
        env $env_vars "$bin" "${args[@]}" >> "$LOGS_DIR/rpc_server.log" 2>&1 &
    else
        "$bin" "${args[@]}" >> "$LOGS_DIR/rpc_server.log" 2>&1 &
    fi
    local pid=$!
    echo "$pid" > "$pid_file"

    # 等待启动
    sleep 1
    if kill -0 "$pid" 2>/dev/null; then
        info "rpc_server 启动成功 (PID: $pid)"
        echo ""
        echo "  gRPC 地址:    0.0.0.0:$SERVER_PORT"
        echo "  日志文件:     $LOGS_DIR/rpc_server.log"
        echo "  PID 文件:     $pid_file"
        echo ""
        echo "  查看日志:     tail -f $LOGS_DIR/rpc_server.log"
        echo "  停止服务:     ./run.sh stop"
    else
        error "rpc_server 启动失败，查看日志:"
        tail -20 "$LOGS_DIR/rpc_server.log" 2>/dev/null || true
        rm -f "$pid_file"
        exit 1
    fi
}

# ============================================================================
# redis - 启动 Redis
# ============================================================================
cmd_redis() {
    banner "启动 Redis"

    if check_redis; then
        info "Redis 已在运行 ($REDIS_HOST:$REDIS_PORT)"
        return 0
    fi

    if ! command -v redis-server &>/dev/null; then
        error "未找到 redis-server，请先安装:"
        error "  Ubuntu: sudo apt install redis-server"
        error "  macOS:  brew install redis"
        exit 1
    fi

    redis-server --port "$REDIS_PORT" --daemonize yes --loglevel notice
    sleep 1

    if check_redis; then
        info "Redis 启动成功 ($REDIS_HOST:$REDIS_PORT)"
    else
        error "Redis 启动失败"
        exit 1
    fi
}

# ============================================================================
# gateway - 启动 API 网关 (Docker)
# ============================================================================
cmd_gateway() {
    banner "启动 API 网关"

    if ! command -v docker &>/dev/null; then
        error "未找到 docker，请先安装 Docker"
        exit 1
    fi

    if [ ! -f "$GATEWAY_COMPOSE" ]; then
        error "未找到 docker-compose 配置: $GATEWAY_COMPOSE"
        exit 1
    fi

    info "启动 Nginx + Envoy 网关..."
    docker compose -f "$GATEWAY_COMPOSE" up -d

    sleep 2

    # 检查容器状态
    if docker compose -f "$GATEWAY_COMPOSE" ps --format json 2>/dev/null | grep -q '"running"'; then
        echo ""
        info "API 网关启动成功"
        echo ""
        echo "  浏览器入口:   http://localhost:8080  (gRPC-Web)"
        echo "  后端入口:     localhost:8082          (gRPC 直连)"
        echo "  Envoy:         localhost:8081"
        echo ""
        echo "  查看日志:     docker compose -f $GATEWAY_COMPOSE logs -f"
        echo "  停止网关:     ./run.sh stop"
    else
        warn "部分容器可能未正常启动，请检查:"
        docker compose -f "$GATEWAY_COMPOSE" ps
    fi
}

# ============================================================================
# stop - 停止所有服务
# ============================================================================
cmd_stop() {
    banner "停止 AI Agent 系统"

    # 停止本地进程
    local stopped=0
    if [ -d "$PIDS_DIR" ]; then
        for pid_file in "$PIDS_DIR"/*.pid; do
            [ -f "$pid_file" ] || continue
            local pid
            pid=$(cat "$pid_file")
            if kill -0 "$pid" 2>/dev/null; then
                local name
                name=$(basename "$pid_file" .pid)
                info "停止进程 $pid ($name)"
                kill "$pid"
                ((stopped++))
            fi
            rm -f "$pid_file"
        done
    fi

    if [ "$stopped" -gt 0 ]; then
        info "已停止 $stopped 个本地进程"
    fi

    # 停止 Docker 网关容器
    if command -v docker &>/dev/null && [ -f "$GATEWAY_COMPOSE" ]; then
        if docker compose -f "$GATEWAY_COMPOSE" ps --format json 2>/dev/null | grep -q '"running"'; then
            info "停止 API 网关容器..."
            docker compose -f "$GATEWAY_COMPOSE" down
        fi
    fi

    if [ "$stopped" -eq 0 ]; then
        info "没有正在运行的服务"
    fi
}

# ============================================================================
# setup - 环境检测
# ============================================================================
cmd_setup() {
    banner "环境检测"

    check_tool() {
        local name="$1"
        local cmd="$2"
        if command -v "$cmd" &>/dev/null; then
            local ver
            ver=$("$cmd" --version 2>&1 | head -1)
            echo "  $name: ✅ $ver"
        else
            echo "  $name: ❌ 未安装"
        fi
    }

    check_tool "CMake"    cmake
    check_tool "g++"      g++
    check_tool "pkg-config" pkg-config
    check_tool "Redis"    redis-server

    echo ""

    # 检查库依赖
    local libs_ok=true
    for lib in grpc++ protobuf jsoncpp hiredis; do
        if pkg-config --exists "$lib" 2>/dev/null; then
            echo "  $lib: ✅ $(pkg-config --modversion "$lib")"
        else
            echo "  $lib: ❌ 未找到"
            libs_ok=false
        fi
    done

    echo ""

    # Redis 状态
    if check_redis; then
        echo "  Redis 服务: ✅ 运行中 ($REDIS_HOST:$REDIS_PORT)"
    else
        echo "  Redis 服务: ⚠️  未运行"
    fi

    echo ""
}

# ============================================================================
# 辅助函数
# ============================================================================
check_redis() {
    (echo > /dev/tcp/"$REDIS_HOST"/"$REDIS_PORT") 2>/dev/null
}

# ============================================================================
# frontend-dev - 启动前端开发服务器
# ============================================================================
cmd_frontend_dev() {
    banner "前端开发服务器"

    if [ ! -d "$FRONTEND_DIR" ]; then
        error "frontend 目录不存在: $FRONTEND_DIR"
        exit 1
    fi

    if ! command -v npm &>/dev/null; then
        error "未找到 npm，请先安装 Node.js (https://nodejs.org)"
        exit 1
    fi

    cd "$FRONTEND_DIR"

    if [ ! -d "node_modules" ]; then
        info "安装前端依赖..."
        npm install
    fi

    info "启动 Vite 开发服务器 (HMR)..."
    npm run dev
}

# ============================================================================
# frontend-build - 构建前端生产包
# ============================================================================
cmd_frontend_build() {
    banner "前端生产构建"

    if [ ! -d "$FRONTEND_DIR" ]; then
        error "frontend 目录不存在: $FRONTEND_DIR"
        exit 1
    fi

    if ! command -v npm &>/dev/null; then
        error "未找到 npm，请先安装 Node.js (https://nodejs.org)"
        exit 1
    fi

    cd "$FRONTEND_DIR"

    if [ ! -d "node_modules" ]; then
        info "安装前端依赖..."
        npm install
    fi

    info "构建中..."
    npm run build

    echo ""
    info "构建完成! 产物在 $FRONTEND_DIR/dist/"
}

# ============================================================================
# 主入口
# ============================================================================
usage() {
    echo "用法: ./run.sh <command> [options]"
    echo ""
    echo "命令:"
    echo "  build          编译项目 (cmake + make)"
    echo "  test           运行全部测试 (ctest)"
    echo "  clean          清理构建目录"
    echo "  start          启动 gRPC 服务端 (后台运行, 自动启动 Redis)"
    echo "  redis          启动 Redis 服务"
    echo "  gateway        启动 API 网关: Nginx + Envoy (需 Docker)"
    echo "  frontend-dev   启动前端开发服务器 (Vite HMR)"
    echo "  frontend-build 构建前端生产包"
    echo "  stop           停止所有服务 (本地进程 + Docker 容器)"
    echo "  setup          检测开发环境"
    echo ""
    echo "环境变量:"
    echo "  BUILD_TYPE           构建类型 (默认: Release)"
    echo "  BUILD_JOBS           并行任务数 (默认: nproc)"
    echo "  REDIS_HOST           Redis 地址 (默认: 127.0.0.1)"
    echo "  REDIS_PORT           Redis 端口 (默认: 6379)"
    echo "  RPC_SERVER_PORT      gRPC 监听端口 (默认: 50051)"
    echo "  ORCHESTRATOR_URL     Orchestrator 地址 (默认: http://localhost:5000)"
    echo "  LLM_API_KEY          LLM API Key (设置后启用多 Agent 编排)"
    echo "  LLM_MODEL            LLM 模型 (默认: deepseek-v4-pro)"
    echo "  LLM_API_URL          LLM API 地址"
    echo ""
    echo "快速启动:"
    echo "  ./run.sh build && ./run.sh start"
    echo "  ./run.sh gateway       # 如需浏览器访问"
    echo ""
}

case "${1:-}" in
    build)          cmd_build ;;
    test)           shift; cmd_test "$@" ;;
    clean)          cmd_clean ;;
    start)          shift; cmd_start "$@" ;;
    redis)          cmd_redis ;;
    gateway)        cmd_gateway ;;
    frontend-dev)   cmd_frontend_dev ;;
    frontend-build) cmd_frontend_build ;;
    stop)           cmd_stop ;;
    setup)          cmd_setup ;;
    *)              usage ;;
esac
