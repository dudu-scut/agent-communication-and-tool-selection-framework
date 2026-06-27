# ============================================================================
# AI Agent 系统启动脚本 (Windows PowerShell)
# 支持多种模式:
#   ./run.ps1 orchestrator   - 启动完整多智能体系统 (Registry + Math Agent + Orchestrator)
#   ./run.ps1 grpc           - 启动 gRPC AI Server
#   ./run.ps1 stop           - 停止所有已启动的服务
#   ./run.ps1 build          - 编译项目 (CMake)
#   ./run.ps1 client         - 启动交互式 AI 客户端
# ============================================================================

param(
    [Parameter(Position = 0)]
    [ValidateSet("orchestrator", "grpc", "stop", "build", "client", "redis", "setup")]
    [string]$Command = "orchestrator",

    # gRPC 模式参数
    [string]$GrpcPort = "50051",
    [string]$GrpcModel = "deepseek-v4-pro",

    # 通用配置
    [string]$ApiKey = "",

    # MCP 配置
    [switch]$EnableMcp,
    [switch]$EnableRag,
    [int]$RagTopK = 5,
    [float]$RagThreshold = 0.3,

    # 端口配置
    [int]$RegistryPort = 8500,
    [int]$OrchestratorPort = 5000,
    [int]$MathAgentPort = 5001,

    # Redis
    [string]$RedisHost = "127.0.0.1",
    [int]$RedisPort = 6379,

    # 构建配置
    [string]$BuildType = "Release",
    [int]$BuildJobs = $env:NUMBER_OF_PROCESSORS
)

# API Key 优先级: 命令行参数 > 环境变量 > 内置默认值
if (-not $ApiKey) {
    if ($env:LLM_API_KEY) {
        $ApiKey = $env:LLM_API_KEY
    } else {
        $ApiKey = "sk-REDACTED"
    }
}

# ============================================================================
# 路径解析
# ============================================================================
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = $ScriptDir
$BuildDir = Join-Path $ProjectRoot "build"
$OrchBinDir = Join-Path $BuildDir "examples\ai_orchestrator"
$GrpcBinDir = Join-Path $BuildDir "examples\grpc_ai_demo"
$ClientBinDir = Join-Path $BuildDir "client"
$LogsDir = Join-Path $ScriptDir "logs"
$PidsDir = Join-Path $ScriptDir "pids"

# MCP Server 路径
$McpServerPath = Join-Path $BuildDir "mcp_server_integrated\mcp_server"
$McpPluginsPath = Join-Path $BuildDir "mcp_server_integrated\plugins"
$McpLogsPath = Join-Path $BuildDir "mcp_server_integrated\logs"

# ============================================================================
# 工具函数
# ============================================================================

function Write-Banner {
    param([string]$Title)
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  $Title" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
}

function Write-Step {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}

function Write-Err {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

function Test-Binary {
    param([string]$Path)
    $exePath = if ($Path.EndsWith(".exe", [System.StringComparison]::OrdinalIgnoreCase)) { $Path } else { "$Path.exe" }
    return (Test-Path $exePath)
}

function Start-AgentProcess {
    param(
        [string]$Binary,
        [string]$Arguments,
        [string]$LogFile,
        [string]$PidFile,
        [string]$DisplayName
    )

    $exePath = if ($Binary.EndsWith(".exe", [System.StringComparison]::OrdinalIgnoreCase)) { $Binary } else { "$Binary.exe" }

    if (-not (Test-Path $exePath)) {
        Write-Err "找不到可执行文件: $exePath"
        Write-Err "请先编译项目: .\run.ps1 build"
        return $false
    }

    # 确保日志和 PID 目录存在
    $logDir = Split-Path -Parent $LogFile
    $pidDir = Split-Path -Parent $PidFile
    if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Force -Path $logDir | Out-Null }
    if (-not (Test-Path $pidDir)) { New-Item -ItemType Directory -Force -Path $pidDir | Out-Null }

    Write-Step "启动 $DisplayName ..."

    # 使用 Start-Process 在后台启动进程
    $proc = Start-Process -FilePath $exePath -ArgumentList $Arguments `
        -NoNewWindow -PassThru `
        -RedirectStandardOutput $LogFile -RedirectStandardError "$LogFile.err"

    # 保存 PID
    $proc.Id | Out-File -FilePath $PidFile -NoNewline
    Start-Sleep -Seconds 1

    if ($proc.HasExited) {
        Write-Err "$DisplayName 启动失败 (退出码: $($proc.ExitCode))"
        Write-Err "请查看日志: $LogFile"
        return $false
    }

    Write-Host "  $DisplayName 启动完成 (PID: $($proc.Id))" -ForegroundColor Green
    return $true
}

function Stop-AllProcesses {
    Write-Banner "停止 AI Agent 系统"

    if (-not (Test-Path $PidsDir)) {
        Write-Warn "没有找到 PID 目录，可能没有正在运行的服务"
        return
    }

    $stopped = 0
    Get-ChildItem -Path "$PidsDir\*.pid" | ForEach-Object {
        $pidContent = Get-Content $_.FullName -Raw
        if ($pidContent -match '\d+') {
            $procId = [int]$Matches[0]
            $proc = Get-Process -Id $procId -ErrorAction SilentlyContinue
            if ($proc) {
                Write-Host "  停止进程 $procId ($($_.BaseName))" -ForegroundColor Yellow
                $proc.Kill()
                $stopped++
            }
        }
        Remove-Item $_.FullName -Force
    }

    Write-Host ""
    if ($stopped -gt 0) {
        Write-Host "  已停止 $stopped 个进程" -ForegroundColor Green
    } else {
        Write-Host "  没有正在运行的进程" -ForegroundColor Yellow
    }
}

# ============================================================================
# 环境初始化
# ============================================================================

function Invoke-InitEnvironment {
    <#
    .SYNOPSIS
    自动检测并初始化运行环境 (MSYS2 MinGW、Redis 等)
    #>
    Write-Step "正在初始化运行环境..."

    # 检测 MSYS2 MinGW-w64 工具链
    $msys2Mingw = "C:\msys64\mingw64\bin"
    if (Test-Path "$msys2Mingw\g++.exe") {
        $env:PATH = "$msys2Mingw;$env:PATH"
        Write-Step "  MSYS2 MinGW-w64 运行时库已加入 PATH"
    } else {
        Write-Warn "  未检测到 MSYS2 MinGW，运行时可能缺少 DLL"
    }

    # 检查 protobuf / gRPC DLL 是否可访问
    $msys2Bin = "C:\msys64\mingw64\bin"
    $dlls = @("libprotobuf.dll", "libgrpc.dll", "libgrpc++.dll")
    $missingDlls = $dlls | Where-Object { -not (Test-Path (Join-Path $msys2Bin $_)) }
    if ($missingDlls) {
        Write-Warn "  缺少以下 DLL，程序可能无法运行: $($missingDlls -join ', ')"
        Write-Warn "  请运行: C:\msys64\usr\bin\bash.exe -lc 'pacman -S --noconfirm mingw-w64-x86_64-protobuf mingw-w64-x86_64-grpc'"
    }
}

function Test-RedisRunning {
    <#
    .SYNOPSIS
    检测 Redis 是否在指定地址运行
    #>
    param([string]$HostAddr = "127.0.0.1", [int]$Port = 6379)
    try {
        $tcp = New-Object System.Net.Sockets.TcpClient
        $result = $tcp.BeginConnect($HostAddr, $Port, $null, $null)
        $connected = $result.AsyncWaitHandle.WaitOne(2000)
        $tcp.Close()
        return $connected
    } catch {
        return $false
    }
}

function Start-Redis {
    <#
    .SYNOPSIS
    尝试启动本地 Redis 服务
    #>
    param([string]$HostAddr = "127.0.0.1", [int]$Port = 6379)

    # 已在运行？
    if (Test-RedisRunning -HostAddr $HostAddr -Port $Port) {
        Write-Step "  Redis 已运行在 ${HostAddr}:${Port}"
        return $true
    }

    # 查找 redis-server
    $redisPaths = @(
        "C:\msys64\mingw64\bin\redis-server.exe",
        "C:\msys64\usr\bin\redis-server.exe",
        (Get-Command redis-server -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
    )

    $redisBin = $null
    foreach ($p in $redisPaths) {
        if ($p -and (Test-Path $p)) { $redisBin = $p; break }
    }

    if (-not $redisBin) {
        Write-Warn "  未找到 Redis，请通过 MSYS2 安装:"
        Write-Warn "    C:\msys64\usr\bin\bash.exe -lc 'pacman -S --noconfirm mingw-w64-x86_64-redis'"
        return $false
    }

    Write-Step "  启动 Redis ($redisBin)..."
    $redisLog = Join-Path $LogsDir "redis.log"
    if (-not (Test-Path $LogsDir)) { New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null }

    $redisProc = Start-Process -FilePath $redisBin -ArgumentList "--port $Port" `
        -NoNewWindow -PassThru `
        -RedirectStandardOutput $redisLog -RedirectStandardError "$redisLog.err"

    Start-Sleep -Seconds 2

    if ($redisProc.HasExited) {
        Write-Err "  Redis 启动失败 (退出码: $($redisProc.ExitCode))"
        Write-Err "  查看日志: $redisLog"
        return $false
    }

    # 保存 PID
    $redisPidFile = Join-Path $PidsDir "redis.pid"
    $redisProc.Id | Out-File -FilePath $redisPidFile -NoNewline
    Write-Step "  Redis 已启动 (PID: $($redisProc.Id), 端口: $Port)"
    return $true
}

# ============================================================================
# Build 命令
# ============================================================================

function Invoke-Build {
    Write-Banner "编译项目"

    # 检查 CMake
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmake) {
        Write-Err "未找到 CMake，请安装 CMake 并添加到 PATH"
        Write-Err "下载地址: https://cmake.org/download/"
        return
    }

    # 自动检测生成器
    $generator = ""
    $msys2Mingw = "C:\msys64\mingw64\bin"
    if (Test-Path "$msys2Mingw\g++.exe") {
        $generator = "MinGW Makefiles"
        Write-Step "检测到 MSYS2 MinGW-w64 工具链"
        $env:PATH = "$msys2Mingw;$env:PATH"
    } elseif (Get-Command cl -ErrorAction SilentlyContinue) {
        $generator = "Visual Studio 17 2022"
        Write-Step "检测到 Visual Studio 工具链"
    } else {
        Write-Err "未找到可用的 C++ 编译器!"
        Write-Err "请安装 MSYS2 (推荐) 或 Visual Studio 2022"
        Write-Err "MSYS2: https://www.msys2.org/"
        return
    }

    Write-Step "生成器: $generator"
    Write-Step "构建类型: $BuildType"
    Write-Step "并行任务数: $BuildJobs"

    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }

    Push-Location $BuildDir
    try {
        $cmakeArgs = @(
            "..",
            "-G", $generator,
            "-DCMAKE_BUILD_TYPE=$BuildType",
            "-DCMAKE_PREFIX_PATH=C:/msys64/mingw64/local",
            "-DCMAKE_CXX_FLAGS=-DPROTOBUF_USE_DLLS",
            "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--allow-multiple-definition"
        )

        Write-Step "运行 CMake 配置..."
        & cmake $cmakeArgs
        if ($LASTEXITCODE -ne 0) {
            Write-Err "CMake 配置失败"
            return
        }

        Write-Step "运行 CMake 构建..."
        cmake --build . --config $BuildType -j $BuildJobs
        if ($LASTEXITCODE -ne 0) {
            Write-Err "CMake 构建失败"
            return
        }

        Write-Host ""
        Write-Host "  编译完成!" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}

# ============================================================================
# Orchestrator 模式: 启动完整多智能体系统
# ============================================================================

function Start-OrchestratorSystem {
    Write-Banner "AI Agent 多智能体系统启动"

    # === 环境初始化 ===
    Invoke-InitEnvironment

    # === Redis ===
    if (-not (Start-Redis -HostAddr $RedisHost -Port $RedisPort)) {
        Write-Err "Redis 不可用，多智能体系统无法启动"
        Write-Err "请安装并启动 Redis 后重试: .\run.ps1 redis"
        return
    }

    # 检查可执行文件
    $requiredBins = @(
        (Join-Path $OrchBinDir "ai_registry_server"),
        (Join-Path $OrchBinDir "ai_math_agent"),
        (Join-Path $OrchBinDir "ai_orchestrator")
    )

    foreach ($bin in $requiredBins) {
        if (-not (Test-Binary $bin)) {
            Write-Err "找不到可执行文件: $bin.exe"
            Write-Err "请先编译项目: .\run.ps1 build"
            return
        }
    }

    # 构建 MCP 参数
    $McpArgs = ""
    if ($EnableMcp) {
        if (Test-Binary $McpServerPath) {
            if (-not (Test-Path $McpLogsPath)) {
                New-Item -ItemType Directory -Force -Path $McpLogsPath | Out-Null
            }
            $McpArgs = "--enable-mcp --mcp-server `"$McpServerPath`" --mcp-args `"-l,$McpLogsPath,-p,$McpPluginsPath`""
            Write-Step "MCP 已启用: $McpServerPath"
            Write-Step "  MCP 插件目录: $McpPluginsPath"
            Write-Step "  MCP 日志目录: $McpLogsPath"
        } else {
            Write-Warn "MCP Server 未编译，跳过 MCP 功能"
        }
    }

    # 构建 RAG-MCP 参数
    $RagArgs = ""
    if ($EnableRag) {
        $RagArgs = "--enable-rag --rag-top-k $RagTopK --rag-threshold $RagThreshold"
        Write-Step "RAG-MCP 已启用: 智能工具选择"
        Write-Step "  Top-K: $RagTopK"
        Write-Step "  相似度阈值: $RagThreshold"
    }

    Write-Host "配置:" -ForegroundColor Cyan
    Write-Host "  API Key        : $($ApiKey.Substring(0, [Math]::Min(15, $ApiKey.Length)))..."
    Write-Host "  模型            : deepseek-v4-pro"
    Write-Host "  Registry 端口   : $RegistryPort"
    Write-Host "  Orchestrator 端口: $OrchestratorPort"
    Write-Host "  Math Agent 端口 : $MathAgentPort"
    Write-Host "  Redis: ${RedisHost}:${RedisPort}"
    Write-Host ""

    # 1. 启动 Registry Server
    $regBinary = Join-Path $OrchBinDir "ai_registry_server"
    $regLog = Join-Path $LogsDir "registry.log"
    $regPid = Join-Path $PidsDir "registry.pid"

    if (-not (Start-AgentProcess -Binary $regBinary -Arguments $RegistryPort `
            -LogFile $regLog -PidFile $regPid -DisplayName "Registry Server ($RegistryPort)")) {
        return
    }

    # 2. 启动 Math Agent
    $mathBinary = Join-Path $OrchBinDir "ai_math_agent"
    $mathLog = Join-Path $LogsDir "math_agent.log"
    $mathPid = Join-Path $PidsDir "math_agent.pid"
    $mathArgs = "math-1 $MathAgentPort http://localhost:$RegistryPort $ApiKey --redis-host $RedisHost --redis-port $RedisPort $McpArgs $RagArgs"

    if (-not (Start-AgentProcess -Binary $mathBinary -Arguments $mathArgs `
            -LogFile $mathLog -PidFile $mathPid -DisplayName "Math Agent ($MathAgentPort)")) {
        return
    }

    # 3. 启动 Orchestrator
    $orchBinary = Join-Path $OrchBinDir "ai_orchestrator"
    $orchLog = Join-Path $LogsDir "orchestrator.log"
    $orchPid = Join-Path $PidsDir "orchestrator.pid"
    $orchArgs = "orch-1 $OrchestratorPort http://localhost:$RegistryPort $ApiKey --redis-host $RedisHost --redis-port $RedisPort $McpArgs $RagArgs"

    if (-not (Start-AgentProcess -Binary $orchBinary -Arguments $orchArgs `
            -LogFile $orchLog -PidFile $orchPid -DisplayName "Orchestrator ($OrchestratorPort)")) {
        return
    }

    # 输出总结
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  系统启动完成!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "服务地址:" -ForegroundColor Cyan
    Write-Host "  Registry:     http://localhost:$RegistryPort"
    Write-Host "  Orchestrator: http://localhost:$OrchestratorPort"
    Write-Host "  Math Agent:   http://localhost:$MathAgentPort"
    Write-Host ""
    Write-Host "客户端连接:" -ForegroundColor Cyan
    Write-Host "  $(Join-Path $OrchBinDir 'ai_client.exe') http://localhost:$OrchestratorPort"
    Write-Host ""
    Write-Host "查看日志:" -ForegroundColor Cyan
    Write-Host "  Get-Content $orchLog -Wait"
    Write-Host ""
    Write-Host "停止系统:" -ForegroundColor Cyan
    Write-Host "  .\run.ps1 stop"
    Write-Host ""
}

# ============================================================================
# gRPC 模式: 启动 gRPC AI Server
# ============================================================================

function Start-GrpcServer {
    Write-Banner "启动 gRPC AI Server"

    # 初始化 MSYS2 运行时环境
    Invoke-InitEnvironment

    $grpcBinary = Join-Path $GrpcBinDir "grpc_server"

    if (-not (Test-Binary $grpcBinary)) {
        Write-Err "找不到 grpc_server.exe，请先编译项目"
        Write-Err "  .\run.ps1 build"
        return
    }

    Write-Host "API Key: $($ApiKey.Substring(0, [Math]::Min(15, $ApiKey.Length)))..."
    Write-Host "端口: $GrpcPort"
    Write-Host "模型: $GrpcModel"
    Write-Host ""

    # 直接在前台运行 gRPC Server
    $grpcExe = "$grpcBinary.exe"
    Write-Step "启动 gRPC Server (端口: $GrpcPort)..."
    & $grpcExe $ApiKey $GrpcPort $GrpcModel
}

# ============================================================================
# Client 模式: 启动交互式客户端
# ============================================================================

function Start-Client {
    param([string]$ServerUrl = "http://localhost:5000")

    # 初始化 MSYS2 运行时环境
    Invoke-InitEnvironment

    $clientBinary = Join-Path $OrchBinDir "ai_client"

    if (-not (Test-Binary $clientBinary)) {
        Write-Err "找不到 ai_client.exe，请先编译项目"
        Write-Err "  .\run.ps1 build"
        return
    }

    Write-Banner "启动 AI 交互式客户端"
    Write-Host "连接到: $ServerUrl"
    Write-Host ""

    $clientExe = "$clientBinary.exe"
    & $clientExe $ServerUrl
}

# ============================================================================
# 主入口
# ============================================================================

switch ($Command) {
    "build" {
        Invoke-Build
    }
    "stop" {
        Stop-AllProcesses
    }
    "orchestrator" {
        Start-OrchestratorSystem
    }
    "grpc" {
        Start-GrpcServer
    }
    "client" {
        Start-Client
    }
    "redis" {
        Write-Banner "Redis 管理"
        Invoke-InitEnvironment
        if (Test-RedisRunning -HostAddr $RedisHost -Port $RedisPort) {
            Write-Step "Redis 已运行在 ${RedisHost}:${RedisPort}"
        } else {
            Write-Warn "Redis 未运行，尝试启动..."
            Start-Redis -HostAddr $RedisHost -Port $RedisPort
        }
    }
    "setup" {
        Write-Banner "环境检测"
        Write-Host "MSYS2 MinGW: $(
            if (Test-Path 'C:\msys64\mingw64\bin\g++.exe') { '✅ 已安装' } else { '❌ 未安装 - 请用 winget install MSYS2.MSYS2' }
        )"
        Write-Host "Redis:       $(
            if (Test-RedisRunning -HostAddr $RedisHost -Port $RedisPort) { '✅ 运行中' } else { '⚠️  未运行 - 请用 .\run.ps1 redis 启动' }
        )"
        Write-Host "CMake:       $(
            if (Get-Command cmake -ErrorAction SilentlyContinue) { '✅ 已安装' } else { '❌ 未安装' }
        )"
        Write-Host "LLM_API_KEY: $(
            if ($ApiKey) { '✅ 已配置' } else { '⚠️  未配置 (使用内置 Key)' }
        )"
        Write-Host "二进制文件:  $(
            if ((Test-Binary (Join-Path $OrchBinDir 'ai_registry_server')) -and
                (Test-Binary (Join-Path $GrpcBinDir 'grpc_server'))) { '✅ 已编译' } else { '⚠️  未编译 - 请用 .\run.ps1 build' }
        )"
        Write-Host ""
    }
    default {
        Write-Host "用法: .\run.ps1 [command] [options]"
        Write-Host ""
        Write-Host "命令:"
        Write-Host "  orchestrator  启动完整多智能体系统 (默认，自动启动 Redis)"
        Write-Host "  grpc          启动 gRPC AI Server"
        Write-Host "  client        启动交互式 AI 客户端"
        Write-Host "  stop          停止所有已启动的服务"
        Write-Host "  build         编译项目"
        Write-Host "  redis         启动 / 检查 Redis 服务"
        Write-Host "  setup         检测开发环境是否就绪"
        Write-Host ""
        Write-Host "示例:"
        Write-Host "  .\run.ps1 setup                               # 检测环境状态"
        Write-Host "  .\run.ps1 build                               # 编译项目"
        Write-Host "  .\run.ps1 orchestrator                        # 一键启动多智能体系统"
        Write-Host "  .\run.ps1 orchestrator -EnableMcp -EnableRag  # 启用 MCP + RAG"
        Write-Host "  .\run.ps1 grpc -GrpcPort 50052               # 启动 gRPC 服务"
        Write-Host "  .\run.ps1 client                              # 启动客户端"
        Write-Host "  .\run.ps1 stop                                # 停止所有服务"
        Write-Host ""
        Write-Host "环境变量:"
        Write-Host "  `$env:LLM_API_KEY   LLM API Key（可选，默认使用内置 Key）"
        Write-Host ""
        Write-Host "默认模型: deepseek-v4-pro"
    }
}
