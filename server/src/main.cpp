/**
 * @file main.cpp
 * @brief RPC Server 主程序
 * 
 * 这是项目的核心服务端程序：
 * - 提供 gRPC 服务，接收客户端请求
 * - 通过 A2A 协议调用 Orchestrator 协调多 Agent
 * - 支持 AI 查询、流式响应等功能
 * 
 * 架构:
 *   rpc_client ──gRPC──> rpc_server ──A2A/HTTP──> Orchestrator ──> Agents
 */

#include "agent_rpc/server/rpc_server.h"
#include "agent_rpc/server/ai_query_service.h"
#include "agent_rpc/a2a_adapter/a2a_config.h"
#include "agent_rpc/common/logger.h"
#include "agent_rpc/common/env_loader.h"
#include "agent_rpc/common/background_scheduler.h"
#include "agent_rpc/orchestrator/feedback_aggregator.h"
#ifdef AGENT_RPC_ENABLE_MCP
#include "agent_rpc/mcp/rag/semantic_cache_index.h"
#endif
#include "agent_rpc/common/profile_summarizer.h"
#include "agent_rpc/common/trace_context.h"
#include "agent_rpc/common/redis_client.h"
#include "agent_rpc/common/postgres_store.h"
#include "agent_rpc/db/migration_runner.h"
#include "agent_rpc/registry/service_registry.h"
#include <curl/curl.h>
#include <filesystem>
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <queue>
#include <mutex>

#ifndef NEXUSAI_MIGRATIONS_DEFAULT_DIR
#error "NEXUSAI_MIGRATIONS_DEFAULT_DIR must be provided by server/CMakeLists.txt"
#endif

using namespace agent_rpc::server;
using namespace agent_rpc::common;

// 全局变量用于优雅关闭
std::atomic<bool> g_running{true};
RpcServer* g_server = nullptr;

// ============================================================================
// Global span queue for span_batch_flush BackgroundScheduler task.
// TraceContext::endSpan() pushes completed spans here via the SpanExporter
// callback; the periodic task drains them into Redis in batches.
// ============================================================================
namespace {

struct QueuedSpan {
    std::string trace_id;
    std::string user_id;
    std::string span_id;
    std::string name;
    std::string component;
    int duration_ms;
    std::string status;
    std::string metadata_json;
};

std::mutex g_span_queue_mutex;
std::queue<QueuedSpan> g_span_queue;

static constexpr size_t kSpanBatchMax = 500;  // max spans per flush

void pushSpanToQueue(const Span& span, const std::string& trace_id,
                     const std::string& user_id) {
    QueuedSpan qs;
    qs.trace_id    = trace_id;
    qs.user_id     = user_id;
    qs.span_id     = span.span_id;
    qs.name        = span.name;
    qs.component   = span.component;
    qs.duration_ms = span.duration_ms;
    qs.status      = span.status;
    qs.metadata_json = span.metadata_json;
    std::lock_guard<std::mutex> lock(g_span_queue_mutex);
    g_span_queue.push(std::move(qs));
}

std::filesystem::path resolveMigrationDirectory() {
    if (const char* environment_directory = std::getenv("NEXUSAI_MIGRATIONS_DIR");
        environment_directory != nullptr && *environment_directory != '\0') {
        return environment_directory;
    }
    return NEXUSAI_MIGRATIONS_DEFAULT_DIR;
}

}  // anonymous namespace

void signalHandler(int signal) {
    std::cout << "\n收到信号 " << signal << ", 正在关闭服务器..." << std::endl;
    g_running = false;
    // 不在信号处理函数中调用 stop()，让主循环处理
}

void crashHandler(int sig) {
    // Best-effort crash diagnostics: flush the async logger before terminating.
    // NOTE: signal handlers run in a restricted context — avoid heap allocation,
    // non-reentrant functions, and I/O beyond async-signal-safe write().
    const char* names[] = {
        "UNKNOWN", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL",
        "SIGTRAP", "SIGABRT", "SIGBUS", "SIGFPE", "SIGKILL",
        "SIGUSR1", "SIGSEGV", "SIGUSR2", "SIGPIPE", "SIGALRM",
        "SIGTERM"
    };
    const char* name = (sig > 0 && sig < 16) ? names[sig] : "UNKNOWN";
    // Write directly to stderr (async-signal-safe).
    // NOTE: We intentionally do NOT call flushLogger() here — it acquires
    // mutexes and is not async-signal-safe. If the crash interrupted code
    // holding any of those mutexes, the handler would deadlock and never
    // produce a core dump.
    const char msg[] = "\n[FATAL] rpc_server crashed with signal ";
    if (write(STDERR_FILENO, msg, sizeof(msg) - 1) < 0) {}
    if (write(STDERR_FILENO, name, strlen(name)) < 0) {}
    const char msg2[] = "\n[FATAL] Check logs for last recorded entries before this point.\n";
    if (write(STDERR_FILENO, msg2, sizeof(msg2) - 1) < 0) {}
    // Re-raise the signal with default handler to produce core dump
    signal(sig, SIG_DFL);
    raise(sig);
}

void printUsage(const char* program) {
    std::cout << "RPC Server - AI Agent 通信服务端" << std::endl;
    std::cout << std::endl;
    std::cout << "用法: " << program << " [选项]" << std::endl;
    std::cout << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -p, --port PORT           gRPC 监听端口 (默认: 50051)" << std::endl;
    std::cout << "  -o, --orchestrator URL    Orchestrator 地址 (默认: http://localhost:5000)" << std::endl;
    std::cout << "  -r, --registry ADDR       注册中心地址，例如 consul://127.0.0.1:8500" << std::endl;
    std::cout << "      --enable-registry     显式启用服务注册" << std::endl;
    std::cout << "  -t, --timeout SECONDS     请求超时时间 (默认: 60)" << std::endl;
    std::cout << "  -h, --help                显示帮助信息" << std::endl;
    std::cout << std::endl;
    std::cout << "环境变量:" << std::endl;
    std::cout << "  RPC_SERVER_PORT           gRPC 监听端口" << std::endl;
    std::cout << "  ORCHESTRATOR_URL          Orchestrator 地址" << std::endl;
    std::cout << "  RPC_REGISTRY_ADDRESS      注册中心地址" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << std::endl;
    std::cout << "  " << program << " -p 50051 -o http://localhost:5000" << std::endl;
    std::cout << std::endl;
    std::cout << "启动顺序:" << std::endl;
    std::cout << "  1. 启动 ai_orchestrator 系统: ./start_system.sh" << std::endl;
    std::cout << "  2. 启动 rpc_server: ./rpc_server" << std::endl;
    std::cout << "  3. 使用 rpc_client 连接: ./rpc_client localhost:50051" << std::endl;
}

int main(int argc, char* argv[]) {
    // Initialize CURL globally once before any threads are created.
    // This avoids the undefined behavior of calling curl_global_init from
    // multiple modules concurrently. Subsequent calls from individual modules
    // are safe (libcurl >= 7.36.0 uses reference counting).
    curl_global_init(CURL_GLOBAL_ALL);

    // 加载 .env 文件（必须在所有 getenv 之前）
    agent_rpc::common::loadEnvFile(".env");

    // 默认配置
    std::string port = "50051";
    std::string orchestrator_url = "http://localhost:5000";
    std::string registry_address = "localhost:8500";
    bool enable_registry = false;
    int timeout_seconds = 60;
    
    // 从环境变量读取
    if (const char* env_port = std::getenv("RPC_SERVER_PORT")) {
        port = env_port;
    }
    if (const char* env_url = std::getenv("ORCHESTRATOR_URL")) {
        orchestrator_url = env_url;
    }
    if (const char* env_registry = std::getenv("RPC_REGISTRY_ADDRESS")) {
        registry_address = env_registry;
        enable_registry = true;
    }
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = argv[++i];
        } else if ((arg == "-o" || arg == "--orchestrator") && i + 1 < argc) {
            orchestrator_url = argv[++i];
        } else if ((arg == "-r" || arg == "--registry") && i + 1 < argc) {
            registry_address = argv[++i];
            enable_registry = true;
        } else if (arg == "--enable-registry") {
            enable_registry = true;
        } else if ((arg == "-t" || arg == "--timeout") && i + 1 < argc) {
            timeout_seconds = std::atoi(argv[++i]);
        } else {
            std::cerr << "未知参数: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    // Crash signal handlers for diagnostic logging before termination
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGFPE, crashHandler);
    signal(SIGILL, crashHandler);

    LogConfig log_config;
    log_config.level = LogLevel::Level_INFO;
    log_config.async_logging = true;
    log_config.color_output = true;
    initializeAdvancedLogger(log_config);

    // PostgreSQL is the durable source of truth. Apply migrations before
    // constructing or initializing the RPC server so failures fail closed.
    try {
        const auto postgres_config = PostgresConfig::fromEnvironment();
        PostgresStore postgres_store{postgres_config};
        agent_rpc::db::MigrationRunner migration_runner{postgres_store};
        migration_runner.migrate(resolveMigrationDirectory());
    } catch (const std::exception& error) {
        LOG_ERROR("RPC startup migration failed: " + std::string(error.what()));
        std::cerr << "Error: RPC startup migration failed: " << error.what() << std::endl;
        return 1;
    }
    
    // 配置 RPC Server
    RpcConfig config;
    config.server_address = "0.0.0.0:" + port;
    config.max_message_size = 64 * 1024 * 1024;  // 64MB
    config.max_receive_message_size = 64 * 1024 * 1024;
    config.timeout_seconds = timeout_seconds;
    config.log_level = "INFO";
    config.enable_service_registry = enable_registry;
    if (enable_registry) {
        config.registry_address = registry_address;
    }
    
    // 配置 A2A 适配器
    agent_rpc::a2a_adapter::A2AConfig a2a_config;
    a2a_config.orchestrator_url = orchestrator_url;
    a2a_config.request_timeout_seconds = timeout_seconds;
    
    // 创建并初始化服务器
    RpcServer server;
    g_server = &server;
    
    server.setA2AConfig(a2a_config);
    
    LOG_INFO("正在初始化 RPC Server...");
    
    if (!server.initialize(config)) {
        LOG_ERROR("无法初始化 RPC 服务器");
        std::cerr << "错误: 无法初始化 RPC 服务器" << std::endl;
        return 1;
    }
    
    // 检查 AI 查询服务状态
    auto ai_service = server.getAIQueryService();
    bool ai_available = ai_service && ai_service->isAvailable();
    
    // 启动服务器
    if (!server.start()) {
        LOG_ERROR("无法启动 RPC 服务器");
        std::cerr << "错误: 无法启动 RPC 服务器" << std::endl;
        return 1;
    }
    
    // 打印启动信息
    std::cout << "==========================================" << std::endl;
    std::cout << "RPC Server 启动成功" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "gRPC 地址:      " << config.server_address << std::endl;
    std::cout << "Orchestrator:   " << orchestrator_url << std::endl;
    if (enable_registry) {
        std::cout << "Registry:       " << registry_address << std::endl;
    }
    std::cout << "AI 服务状态:    " << (ai_available ? "可用" : "不可用") << std::endl;
    std::cout << "超时时间:       " << timeout_seconds << " 秒" << std::endl;
    std::cout << std::endl;
    std::cout << "使用客户端连接:" << std::endl;
    std::cout << "  ./rpc_client localhost:" << port << std::endl;
    std::cout << std::endl;
    std::cout << "按 Ctrl+C 停止服务器" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    LOG_INFO("RPC Server 已启动: " + config.server_address);

    // Start BackgroundScheduler for periodic tasks
    agent_rpc::common::BackgroundScheduler::instance().start(2);

    // Batch 2: Register feedback aggregation and metrics aggregation tasks (hourly)
    agent_rpc::common::BackgroundScheduler::instance().scheduleAtFixedRate(
        "feedback_aggregation",
        []() { agent_rpc::orchestrator::FeedbackAggregator::recalculate(); },
        std::chrono::seconds(3600));
    agent_rpc::common::BackgroundScheduler::instance().scheduleAtFixedRate(
        "agent_metrics_aggregation",
        []() { agent_rpc::orchestrator::FeedbackAggregator::recalculateMetrics(); },
        std::chrono::seconds(3600));

#ifdef AGENT_RPC_ENABLE_MCP
    // Batch 3: Register semantic cache cleanup task (every 10 minutes)
    // The SemanticCacheIndex instance should be shared from wherever it is owned
    // (e.g., held by the MCP module or AIQueryService).  At startup the shared_ptr
    // is null, so the lambda is a safe no-op until the cache is wired up.
    static std::shared_ptr<agent_rpc::mcp::SemanticCacheIndex> semantic_cache;
    agent_rpc::common::BackgroundScheduler::instance().scheduleAtFixedRate(
        "cache_cleanup",
        []() { if (semantic_cache) semantic_cache->cleanup(); },
        std::chrono::seconds(600));
#endif

    // Batch 4 U2: Register profile extraction task (every 5 minutes)
    // Calls ProfileSummarizer::processPending() which is a no-op placeholder
    // until LLM-based extraction is implemented in a future iteration.
    agent_rpc::common::BackgroundScheduler::instance().scheduleAtFixedRate(
        "profile_extraction",
        []() { agent_rpc::common::ProfileSummarizer::processPending(); },
        std::chrono::seconds(300));

    // Batch 5: Register health evaluation task (every 30 seconds)
    agent_rpc::common::BackgroundScheduler::instance().scheduleAtFixedRate(
        "health_evaluation",
        []() {
            agent_rpc::registry::ServiceRegistry::evaluateAllHealth();
        },
        std::chrono::seconds(30));

    // PR-C3: the legacy CronScheduler and canary-evaluation background tasks
    // were removed. Cron execution is re-introduced by PR-D (durable, PG-backed)
    // and canary weighting was dropped together with the CANARY/DEPRECATED
    // router modifiers — routing quality is now owner-scoped PostgreSQL data.

    // ========================================================================
    // Batch 8: Span batch flush (every 1 second)
    // Drains completed spans from the global queue and writes them to Redis
    // as a JSON list under key "trace:spans:<trace_id>".
    // ========================================================================

    // Register the SpanExporter callback so TraceContext::endSpan() pushes
    // completed spans into the global queue.
    agent_rpc::common::TraceContext::setSpanExporter(pushSpanToQueue);

    agent_rpc::common::BackgroundScheduler::instance().scheduleAtFixedRate(
        "span_batch_flush",
        [&server]() {
            // Drain up to kSpanBatchMax spans from the queue
            std::queue<QueuedSpan> batch;
            {
                std::lock_guard<std::mutex> lock(g_span_queue_mutex);
                size_t count = std::min(g_span_queue.size(), kSpanBatchMax);
                for (size_t i = 0; i < count; ++i) {
                    batch.push(std::move(g_span_queue.front()));
                    g_span_queue.pop();
                }
            }

            if (batch.empty()) return;

            auto* redis = server.getRedisClient();
            if (!redis || !redis->isConnected()) {
                // Redis unavailable — put spans back (best-effort)
                size_t requeued = batch.size();
                std::lock_guard<std::mutex> lock(g_span_queue_mutex);
                while (!batch.empty()) {
                    g_span_queue.push(std::move(batch.front()));
                    batch.pop();
                }
                LOG_WARN("Span batch flush: Redis unavailable, re-queued " +
                         std::to_string(requeued) + " spans");
                return;
            }

            // Group spans by trace_id and RPUSH each batch as a JSON string
            size_t flushed = 0;
            while (!batch.empty()) {
                auto& qs = batch.front();
                // Build a compact JSON representation
                std::string json = "{\"trace_id\":\"" + qs.trace_id +
                    "\",\"span_id\":\"" + qs.span_id +
                    "\",\"name\":\"" + qs.name +
                    "\",\"component\":\"" + qs.component +
                    "\",\"duration_ms\":" + std::to_string(qs.duration_ms) +
                    ",\"status\":\"" + qs.status + "\"}";

                std::string key = "trace:spans:" + qs.trace_id;
                redis->rpush(key, json);
                // Set 24h TTL on the trace key (refreshed on each push)
                redis->expire(key, 86400);
                ++flushed;
                batch.pop();
            }

            LOG_DEBUG("Span batch flush: wrote " + std::to_string(flushed) + " spans to Redis");
        },
        std::chrono::seconds(1));

    // 主循环
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Stop BackgroundScheduler before shutting down server
    agent_rpc::common::BackgroundScheduler::instance().stop();

    // 停止服务器
    server.stop();
    LOG_INFO("RPC Server 已停止");
    std::cout << "RPC 服务器已停止" << std::endl;

    curl_global_cleanup();

    return 0;
}
