#!/bin/bash
# ============================================================================
# NexusAI 项目运行脚本 (Linux)
#
# 用法:
#   ./run.sh build              编译项目
#   ./run.sh test               运行全部测试
#   ./run.sh clean              清理构建目录
#   ./run.sh frontend-dev       启动前端开发服务器
#   ./run.sh frontend-build     构建前端生产包
#   ./run.sh stop               停止所有服务
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
# stop - 停止所有服务
# ============================================================================
cmd_stop() {
    banner "停止 AI Agent 系统"

    if [ ! -d "$PIDS_DIR" ]; then
        warn "没有正在运行的服务"
        return
    fi

    local stopped=0
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

    if [ "$stopped" -gt 0 ]; then
        info "已停止 $stopped 个进程"
    else
        warn "没有正在运行的进程"
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
    echo "  build         编译项目 (cmake + make)"
    echo "  test          运行全部测试 (ctest)"
    echo "  clean         清理构建目录"
    echo "  frontend-dev  启动前端开发服务器 (Vite HMR)"
    echo "  frontend-build 构建前端生产包"
    echo "  stop          停止所有服务"
    echo "  setup         检测开发环境"
    echo ""
    echo "环境变量:"
    echo "  BUILD_TYPE    构建类型 (默认: Release)"
    echo "  BUILD_JOBS    并行任务数 (默认: nproc)"
    echo ""
    echo "示例:"
    echo "  ./run.sh build"
    echo "  ./run.sh test"
}

case "${1:-}" in
    build)          cmd_build ;;
    test)           shift; cmd_test "$@" ;;
    clean)          cmd_clean ;;
    frontend-dev)   cmd_frontend_dev ;;
    frontend-build) cmd_frontend_build ;;
    stop)           cmd_stop ;;
    setup)          cmd_setup ;;
    *)              usage ;;
esac
