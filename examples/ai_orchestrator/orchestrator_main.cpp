/**
 * @file orchestrator_main.cpp
 * @brief AI Orchestrator - 调度 AI Agent
 * 
 * 基于 a2a-cpp/examples/multi_agent_demo/dynamic_orchestrator.cpp
 * 集成到 agent-communication RPC 框架
 * 集成 MCP 工具支持
 * 
 * Requirements: 12.2, 12.3, 12.4
 * Task 19.4: 集成 MCP 到 Orchestrator Agent
 */

#include "redis_task_store.hpp"
#include "llm_client.hpp"
#include "http_server.hpp"
#include "registry_client.hpp"

// P0-1: 统一路由层
#include <agent_rpc/orchestrator/agent_router.h>

// P4: 多 Agent 任务编排
#include <agent_rpc/orchestrator/task_planner.h>
#include <agent_rpc/orchestrator/task_executor.h>
#include <agent_rpc/orchestrator/result_aggregator.h>

#include <a2a/models/agent_message.hpp>
#include <a2a/models/agent_task.hpp>
#include <a2a/models/task_status.hpp>
#include <a2a/models/message_part.hpp>
#include <a2a/core/jsonrpc_request.hpp>
#include <a2a/core/jsonrpc_response.hpp>
#include <a2a/core/error_code.hpp>

// MCP 集成
#include <agent_rpc/mcp/mcp_agent_integration.h>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>

using namespace a2a;
using json = nlohmann::json;
using namespace agent_rpc::mcp;
using namespace agent_rpc::orchestrator;

// 简单的 HTTP 客户端
class SimpleHttpClient {
public:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
    
    static std::string post(const std::string& url, const std::string& body) {
        CURL* curl = curl_easy_init();
        if (!curl) return "";
        
        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        return response;
    }
};

/**
 * @brief AI Orchestrator - 智能调度器
 * 
 * 功能:
 * - 接收用户问题
 * - 使用 AI 模型分析意图
 * - 路由到合适的专业 Agent
 * - 使用 MCP 工具增强能力
 * - 返回处理结果
 */
class AIOrchestrator {
public:
    AIOrchestrator(const std::string& agent_id,
                   const std::string& listen_address,
                   const std::string& registry_url,
                   const std::string& api_key,
                   const std::string& redis_host,
                   int redis_port,
                   const MCPAgentConfig& mcp_config = MCPAgentConfig())
        : agent_id_(agent_id)
        , listen_address_(listen_address)
        , task_store_(std::make_shared<RedisTaskStore>(redis_host, redis_port))
        , llm_client_(api_key)
        , registry_client_(registry_url)
        , mcp_integration_(std::make_unique<MCPAgentIntegration>())
        , agent_router_(std::make_unique<AgentRouter>())
        , sync_running_(false) {
        
        // 初始化路由层
        agent_router_->initialize(RoutingStrategy::SKILL_MATCH);

        // P4: 初始化多 Agent 编排组件
        TaskPlannerConfig planner_config;
        planner_config.api_key = api_key;
        task_planner_ = std::make_unique<TaskPlanner>(planner_config);

        ExecutorConfig executor_config;
        task_executor_ = std::make_unique<TaskExecutor>(*agent_router_, executor_config);

        AggregatorConfig agg_config;
        agg_config.api_key = api_key;
        agg_config.default_strategy = "llm_synthesize";
        result_aggregator_ = std::make_unique<ResultAggregator>(agg_config);
        
        // 初始化 MCP 集成
        if (!mcp_integration_->initialize(mcp_config)) {
            std::cerr << "[Orchestrator] MCP 初始化失败，将在无 MCP 模式下运行" << std::endl;
        } else if (mcp_integration_->isAvailable()) {
            auto tools = mcp_integration_->getToolNames();
            std::cout << "[Orchestrator] MCP 已启用，可用工具: ";
            for (const auto& tool : tools) {
                std::cout << tool << " ";
            }
            std::cout << std::endl;
        }
        
        std::cout << "[Orchestrator] 初始化完成 (动态路由已启用)" << std::endl;
    }
    
    ~AIOrchestrator() {
        stop_registry_sync();
        if (agent_router_) {
            agent_router_->shutdown();
        }
        if (mcp_integration_) {
            mcp_integration_->shutdown();
        }
    }
    
    void start(int port) {
        // 启动 HTTP 服务器
        HttpServer server(port);
        
        // A2A 协议端点 - 普通请求
        server.register_handler("/", [this](const std::string& body) {
            return this->handle_request(body);
        });

        // A2A 协议端点 - 流式请求
        server.register_stream_handler("/", [this](const std::string& body, 
            std::function<bool(const std::string&)> write_callback) {
            this->handle_stream_request(body, write_callback);
        });
        
        // Agent Card 端点 (A2A 协议标准)
        server.register_handler("/.well-known/agent-card.json", [this](const std::string&) {
            return this->get_agent_card();
        });
        
        std::cout << "[Orchestrator] 启动在端口 " << port << std::endl;
        
        // 在后台线程中启动服务器
        std::thread server_thread([&server]() {
            server.start();
        });
        
        // 等待服务器启动
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 注册到注册中心
        AgentRegistration registration;
        registration.id = agent_id_;
        registration.name = "AI Orchestrator";
        registration.address = listen_address_;
        registration.tags = {"orchestrator", "coordinator"};
        
        if (registry_client_.register_agent(registration)) {
            std::cout << "[Orchestrator] 已注册到服务中心" << std::endl;
        } else {
            std::cerr << "[Orchestrator] 注册失败" << std::endl;
        }
        
        // 启动 Registry 同步线程（P0-1: 定期从 Registry 拉取 Agent 列表到 Router）
        start_registry_sync();
        
        server_thread.join();
    }

private:
    std::string handle_request(const std::string& body) {
        try {
            auto request_json = json::parse(body);
            auto request = JsonRpcRequest::from_json(body);
            
            if (request.method() == "message/send") {
                auto params_json = request_json["params"];
                auto message = AgentMessage::from_json(params_json["message"].dump());
                
                // 获取文本内容
                std::string user_text;
                if (!message.parts().empty()) {
                    auto text_part = dynamic_cast<TextPart*>(message.parts()[0].get());
                    if (text_part) {
                        user_text = text_part->text();
                    }
                }
                
                std::string context_id = message.context_id().value_or("default");
                
                std::cout << "[Orchestrator] 收到消息: " << user_text << std::endl;
                
                // 保存用户消息
                save_message(context_id, message);
                
                // P4: TaskPlanner 判断单 Agent / 多 Agent
                auto available_skills = agent_router_->getAllSkillDescriptions();
                ExecutionPlan plan = task_planner_->plan(user_text, available_skills);

                std::string response_text;

                if (plan.is_single_agent) {
                    // 快速路径：单 Agent 路由（零额外开销）
                    std::string intent = plan.single_agent_skill;
                    if (intent.empty()) {
                        intent = analyze_intent_dynamic(user_text);
                    }
                    std::cout << "[Orchestrator] 单 Agent 路径, 技能: " << intent << std::endl;

                    if (intent == "none") {
                        response_text = handle_general_query(user_text, context_id);
                    } else {
                        response_text = route_and_call(intent, user_text, context_id);
                    }
                } else {
                    // P4: 多 Agent DAG 路径
                    std::cout << "[Orchestrator] 多 Agent 路径, "
                              << plan.tasks.size() << " 个子任务" << std::endl;
                    response_text = handle_multi_agent_request(plan, context_id);
                }
                
                // 保存 Agent 响应
                auto response_msg = AgentMessage::create()
                    .with_role(MessageRole::Agent)
                    .with_context_id(context_id);
                response_msg.add_text_part(response_text);
                save_message(context_id, response_msg);
                
                // 返回响应
                auto response = JsonRpcResponse::create_success(request.id(), response_msg.to_json());
                return response.to_json();
            }
            
            return JsonRpcResponse::create_error(request.id(), ErrorCode::MethodNotFound, "Method not found").to_json();
            
        } catch (const std::exception& e) {
            std::cerr << "[Orchestrator] 错误: " << e.what() << std::endl;
            return JsonRpcResponse::create_error("1", ErrorCode::InternalError, e.what()).to_json();
        }
    }
    
    /**
     * @brief 处理流式请求 (message/stream)
     * 
     * 支持 A2A 协议的流式消息传输
    */
    void handle_stream_request(const std::string& body, 
                               std::function<bool(const std::string&)> write_callback) {
        try {
            auto request_json = json::parse(body);
            auto request = JsonRpcRequest::from_json(body);
            
            if (request.method() != "message/stream") {
                // 非流式方法，返回错误
                json error_response = {
                    {"jsonrpc", "2.0"},
                    {"id", request.id()},
                    {"error", {
                        {"code", -32601},
                        {"message", "Method not found for streaming"}
                    }}
                };
                write_callback(error_response.dump());
                return;
            }
            
            auto params_json = request_json["params"];
            auto message = AgentMessage::from_json(params_json["message"].dump());
            
            // 获取文本内容
            std::string user_text;
            if (!message.parts().empty()) {
                auto text_part = dynamic_cast<TextPart*>(message.parts()[0].get());
                if (text_part) {
                    user_text = text_part->text();
                }
            }
            
            std::string context_id = message.context_id().value_or("default");
            
            std::cout << "[Orchestrator] 收到流式消息: " << user_text << std::endl;
            
            // 保存用户消息
            save_message(context_id, message);
            
            // 发送开始事件
            json start_event = {
                {"jsonrpc", "2.0"},
                {"id", request.id()},
                {"result", {
                    {"type", "stream_start"},
                    {"contextId", context_id}
                }}
            };
            write_callback(start_event.dump());
            
            // P0-1: 动态意图识别
            std::string intent = analyze_intent_dynamic(user_text);
            std::cout << "[Orchestrator] 识别意图: " << intent << std::endl;
            
            // 发送意图识别事件
            json intent_event = {
                {"jsonrpc", "2.0"},
                {"id", request.id()},
                {"result", {
                    {"type", "intent"},
                    {"intent", intent}
                }}
            };
            write_callback(intent_event.dump());
            
            // 处理查询并流式返回
            std::string response_text;
            
            if (intent == "none") {
                response_text = handle_general_query(user_text, context_id);
            } else {
                // P0-1: 通过 AgentRouter 路由
                response_text = route_and_call(intent, user_text, context_id);
            }
            
            // UTF-8 安全的分块函数
            auto utf8_safe_chunk = [](const std::string& text, size_t start, size_t max_len) -> std::string {
                if (start >= text.length()) return "";
                
                size_t end = std::min(start + max_len, text.length());
                
                // 确保不在 UTF-8 多字节字符中间切断
                while (end > start && end < text.length()) {
                    unsigned char c = static_cast<unsigned char>(text[end]);
                    // 如果是 UTF-8 后续字节 (10xxxxxx)，向前移动
                    if ((c & 0xC0) == 0x80) {
                        end--;
                    } else {
                        break;
                    }
                }
                
                return text.substr(start, end - start);
            };
            
            // 流式输出：UTF-8 安全分块
            const size_t chunk_size = 50;
            size_t pos = 0;
            while (pos < response_text.length()) {
                std::string chunk = utf8_safe_chunk(response_text, pos, chunk_size);
                if (chunk.empty()) break;
                
                pos += chunk.length();
                
                json chunk_event = {
                    {"jsonrpc", "2.0"},
                    {"id", request.id()},
                    {"result", {
                        {"type", "chunk"},
                        {"content", chunk}
                    }}
                };
                
                if (!write_callback(chunk_event.dump())) {
                    std::cerr << "[Orchestrator] 流式写入失败" << std::endl;
                    return;
                }
                
                // 小延迟模拟流式效果
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            // 保存 Agent 响应
            auto response_msg = AgentMessage::create()
                .with_role(MessageRole::Agent)
                .with_context_id(context_id);
            response_msg.add_text_part(response_text);
            save_message(context_id, response_msg);
            
            // 发送完成事件
            json complete_event = {
                {"jsonrpc", "2.0"},
                {"id", request.id()},
                {"result", {
                    {"type", "stream_end"},
                    {"message", response_msg.to_json()}
                }}
            };
            write_callback(complete_event.dump());
            
            std::cout << "[Orchestrator] 流式响应完成" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "[Orchestrator] 流式处理错误: " << e.what() << std::endl;
            json error_event = {
                {"jsonrpc", "2.0"},
                {"id", "1"},
                {"error", {
                    {"code", -32603},
                    {"message", e.what()}
                }}
            };
            write_callback(error_event.dump());
        }
    }

    /**
     * @brief P0-1: 动态意图识别
     * 
     * 使用 AgentRouter.buildDynamicIntentPrompt() 构建包含所有已注册技能的 prompt，
     * 而不是硬编码 math/code/general 三个类别。
     * 返回值为已注册的 skill name 之一，或 "none"。
     */
    std::string analyze_intent_dynamic(const std::string& text) {
        // 用 AgentRouter 构建动态 prompt（包含所有已注册 Agent 的技能）
        std::string prompt = agent_router_->buildDynamicIntentPrompt(text);
        
        std::string result = llm_client_.chat("", prompt);
        
        // Trim whitespace from LLM response
        auto start = result.find_first_not_of(" \t\n\r");
        auto end = result.find_last_not_of(" \t\n\r");
        if (start != std::string::npos) {
            result = result.substr(start, end - start + 1);
        }
        
        // P1-3: 精确匹配 —— LLM 返回的字符串必须与注册的 skill name 完全一致
        auto all_skills = agent_router_->getAllSkillDescriptions();
        for (const auto& [skill_name, _] : all_skills) {
            if (result == skill_name) {
                return skill_name;
            }
        }
        
        // 也检查 agents 的 tags（兼容旧 Agent 只用 tags 注册的情况）
        auto agents = agent_router_->getHealthyAgents();
        for (const auto& agent : agents) {
            for (const auto& tag : agent.tags) {
                if (result == tag) {
                    return tag;
                }
            }
        }
        
        return "none";
    }
    
    /**
     * @brief P0-1: 通过 AgentRouter 路由并调用 Agent
     * 
     * 使用 AgentRouter 选择最合适的 Agent，然后通过其 URL 直接调用。
     * 替代原来的 call_math_agent() / call_code_agent() + call_agent_by_tag() 模式。
     */
    std::string route_and_call(const std::string& intent, 
                                const std::string& query, 
                                const std::string& context_id) {
        // 通过 AgentRouter 选择 Agent
        std::vector<std::string> required_skills = {intent};
        auto selected = agent_router_->selectAgent(query, required_skills);
        
        if (!selected.has_value()) {
            std::cerr << "[Orchestrator] 未找到匹配 Agent: " << intent << std::endl;
            // Fallback: 尝试用 registry_client 按 tag 查找（兼容旧逻辑）
            return call_agent_by_tag_fallback(intent, query, context_id);
        }
        
        const auto& agent = selected.value();
        std::cout << "[Orchestrator] 路由到 Agent: " << agent.name 
                  << " (" << agent.url << ") skill=" << intent << std::endl;
        
        return call_agent_by_url(agent.url, intent, query, context_id);
    }

    /**
     * @brief P4: 多 Agent DAG 执行路径
     *
     * 创建 AgentCallFn 桥接 TaskExecutor 和现有的 Agent 调用链，
     * 执行 DAG 计划后聚合结果返回最终回答。
     */
    std::string handle_multi_agent_request(const ExecutionPlan& plan,
                                            const std::string& context_id) {
        // AgentCallFn: (skill, prompt) → response_text
        // Bridges TaskExecutor with existing selectAgent + call_agent_by_url flow
        AgentCallFn call_agent = [this, context_id](
                const std::string& skill, const std::string& prompt) -> std::string {
            std::vector<std::string> required_skills = {skill};
            auto selected = agent_router_->selectAgent(prompt, required_skills);

            if (!selected.has_value()) {
                throw std::runtime_error("No agent available for skill: " + skill);
            }

            const auto& agent = selected.value();
            std::cout << "[Orchestrator] DAG 路由到 Agent: " << agent.name
                      << " skill=" << skill << std::endl;
            return call_agent_by_url(agent.url, skill, prompt, context_id);
        };

        // Progress callback for logging
        ProgressCallback on_progress = [](const SubTaskEvent& evt) {
            const char* type_str = "UNKNOWN";
            switch (evt.type) {
                case SubTaskEventType::START:    type_str = "START"; break;
                case SubTaskEventType::COMPLETE: type_str = "COMPLETE"; break;
                case SubTaskEventType::FAILED:   type_str = "FAILED"; break;
            }
            std::cout << "[Orchestrator] 子任务 [" << evt.subtask_id << "] "
                      << type_str;
            if (!evt.detail.empty()) {
                std::string preview = evt.detail.substr(0, 80);
                std::cout << " — " << preview;
            }
            std::cout << std::endl;
        };

        try {
            auto results = task_executor_->execute(plan, call_agent, on_progress);
            auto aggregated = result_aggregator_->aggregate(plan, results);

            std::cout << "[Orchestrator] DAG 执行完成, 策略: " << aggregated.strategy
                      << ", 耗时: " << aggregated.total_time_ms << "ms" << std::endl;

            return aggregated.final_answer;
        } catch (const std::exception& e) {
            std::cerr << "[Orchestrator] DAG 执行失败: " << e.what() << std::endl;
            return "抱歉，多 Agent 协作执行出错: " + std::string(e.what());
        }
    }
    
    /**
     * @brief 通用 Agent 调用（通过 URL 直接发送 JSON-RPC 请求）
     */
    std::string call_agent_by_url(const std::string& agent_url,
                                   const std::string& tag,
                                   const std::string& query, 
                                   const std::string& context_id) {
        try {
            // 构造请求
            json request = {
                {"jsonrpc", "2.0"},
                {"id", "1"},
                {"method", "message/send"},
                {"params", {
                    {"message", {
                        {"role", "user"},
                        {"contextId", context_id},
                        {"parts", {{{"kind", "text"}, {"text", query}}}}
                    }},
                    {"historyLength", 5}
                }}
            };
            
            // 发送请求
            std::string response_body = SimpleHttpClient::post(agent_url, request.dump());
            auto response_json = json::parse(response_body);
            
            if (response_json.contains("result") &&
                response_json["result"].contains("parts") &&
                !response_json["result"]["parts"].empty()) {
                return response_json["result"]["parts"][0]["text"].get<std::string>();
            }
            
            return "无法解析响应";
            
        } catch (const std::exception& e) {
            std::cerr << "[Orchestrator] 调用 " << tag << " Agent 失败: " << e.what() << std::endl;
            return "抱歉，" + tag + " 服务暂时不可用，使用通用模型回答";
        }
    }
    
    /**
     * @brief Fallback: 当 AgentRouter 找不到匹配 Agent 时，回退到 RegistryClient tag 查找
     * 
     * 保留兼容性，确保旧 Agent（未注册 skills 只用 tags）仍能被调用。
     */
    std::string call_agent_by_tag_fallback(const std::string& tag, 
                                            const std::string& query, 
                                            const std::string& context_id) {
        try {
            std::string agent_url = registry_client_.select_agent_by_tag(tag);
            return call_agent_by_url(agent_url, tag, query, context_id);
        } catch (const std::exception& e) {
            std::cerr << "[Orchestrator] Fallback 也失败: " << e.what() << std::endl;
            return "抱歉，暂时无法处理此类请求";
        }
    }
    
    std::string handle_general_query(const std::string& query, const std::string& context_id) {
        auto history = task_store_->get_history(context_id, 5);
        std::string history_text;
        for (const auto& msg : history) {
            std::string role_str = to_string(msg.role());
            std::string text;
            if (!msg.parts().empty()) {
                auto text_part = dynamic_cast<TextPart*>(msg.parts()[0].get());
                if (text_part) {
                    text = text_part->text();
                }
            }
            history_text += role_str + ": " + text + "\n";
        }
        
        // 尝试使用 MCP 工具增强回答
        std::string tool_context = tryMCPTools(query);
        if (!tool_context.empty()) {
            history_text += "\n工具辅助信息:\n" + tool_context;
        }
        
        return llm_client_.chat(history_text, query);
    }
    
    /**
     * @brief 尝试使用 MCP 工具获取辅助信息
     */
    std::string tryMCPTools(const std::string& query) {
        if (!mcp_integration_ || !mcp_integration_->isAvailable()) {
            return "";
        }
        
        std::string result;
        auto tools = mcp_integration_->getAvailableTools();
        
        // 根据查询内容选择合适的工具
        for (const auto& tool : tools) {
            // 简单的关键词匹配来决定是否使用工具
            bool should_use = false;
            
            if (tool.name.find("search") != std::string::npos ||
                tool.name.find("query") != std::string::npos) {
                // 搜索类工具
                should_use = true;
            } else if (tool.name.find("time") != std::string::npos ||
                       tool.name.find("date") != std::string::npos) {
                // 时间类工具
                if (query.find("时间") != std::string::npos ||
                    query.find("日期") != std::string::npos ||
                    query.find("time") != std::string::npos) {
                    should_use = true;
                }
            }
            
            if (should_use) {
                json args;
                args["query"] = query;
                
                std::cout << "[Orchestrator] 调用 MCP 工具: " << tool.name << std::endl;
                
                auto tool_result = mcp_integration_->callTool(tool.name, args.dump());
                if (tool_result.success) {
                    result += "[" + tool.name + "]: " + tool_result.result + "\n";
                }
            }
        }
        
        return result;
    }
    
    void save_message(const std::string& context_id, const AgentMessage& message) {
        if (!task_store_->task_exists(context_id)) {
            auto task = AgentTask::create()
                .with_id(context_id)
                .with_context_id(context_id)
                .with_status(TaskState::Running);
            task_store_->set_task(task);
        }
        task_store_->add_history_message(context_id, message);
    }
    
    std::string get_agent_card() {
        json card = {
            {"name", "AI Orchestrator Agent"},
            {"description", "智能协调器，负责意图识别和任务分发"},
            {"version", "1.0.0"},
            {"capabilities", {
                {"streaming", true},
                {"push_notifications", false},
                {"task_management", true}
            }},
            {"skills", json::array({
                {
                    {"name", "意图识别"},
                    {"description", "识别用户意图并路由到相应的专业 Agent"},
                    {"input_modes", json::array({"text"})},
                    {"output_modes", json::array({"text"})}
                },
                {
                    {"name", "任务协调"},
                    {"description", "协调多个 Agent 完成复杂任务"},
                    {"input_modes", json::array({"text"})},
                    {"output_modes", json::array({"text"})}
                }
            })},
            {"provider", {
                {"name", "Agent Communication RPC"},
                {"organization", "A2A Integration"}
            }}
        };
        return card.dump();
    }
    
    std::string agent_id_;
    std::string listen_address_;
    std::shared_ptr<RedisTaskStore> task_store_;
    LLMClient llm_client_;
    RegistryClient registry_client_;
    std::unique_ptr<MCPAgentIntegration> mcp_integration_;
    
    // P0-1: 统一路由层
    std::unique_ptr<AgentRouter> agent_router_;

    // P4: 多 Agent 任务编排
    std::unique_ptr<TaskPlanner> task_planner_;
    std::unique_ptr<TaskExecutor> task_executor_;
    std::unique_ptr<ResultAggregator> result_aggregator_;
    
    // Registry 同步线程
    std::atomic<bool> sync_running_;
    std::thread sync_thread_;
    
    /**
     * @brief 启动 Registry 同步线程
     * 
     * 每 15 秒从 Registry 拉取所有 Agent 列表，同步到 AgentRouter。
     * 这使得 AgentRouter 始终拥有最新的 Agent 信息用于路由决策。
     */
    void start_registry_sync() {
        bool expected = false;
        if (!sync_running_.compare_exchange_strong(expected, true)) return;
        
        // 立即做一次同步
        do_registry_sync();
        
        sync_thread_ = std::thread([this]() {
            while (sync_running_) {
                std::this_thread::sleep_for(std::chrono::seconds(15));
                if (!sync_running_) break;
                do_registry_sync();
            }
        });
        
        std::cout << "[Orchestrator] Registry 同步已启动 (每 15 秒)" << std::endl;
    }
    
    void stop_registry_sync() {
        sync_running_ = false;
        if (sync_thread_.joinable()) {
            sync_thread_.join();
        }
    }
    
    /**
     * @brief 执行一次 Registry 同步
     */
    void do_registry_sync() {
        try {
            auto agents = registry_client_.get_all_agents();
            agent_router_->syncFromRegistry(agents);
            
            size_t healthy = agent_router_->getHealthyAgentCount();
            size_t total = agent_router_->getAgentCount();
            
            if (total > 0) {
                std::cout << "[Orchestrator] Registry 同步完成: " 
                          << healthy << "/" << total << " 个 Agent 健康" << std::endl;
                
                // 打印已注册的技能列表
                auto skills = agent_router_->getAllSkillDescriptions();
                if (!skills.empty()) {
                    std::cout << "[Orchestrator] 已注册技能: ";
                    for (const auto& [name, desc] : skills) {
                        std::cout << name << " ";
                    }
                    std::cout << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[Orchestrator] Registry 同步失败: " << e.what() << std::endl;
        }
    }
};

void print_usage(const char* program) {
    std::cerr << "用法: " << program << " <agent_id> <port> <registry_url> <api_key> [options]" << std::endl;
    std::cerr << "选项:" << std::endl;
    std::cerr << "  --redis-host <host>     Redis 主机 (默认: 127.0.0.1)" << std::endl;
    std::cerr << "  --redis-port <port>     Redis 端口 (默认: 6379)" << std::endl;
    std::cerr << "  --mcp-server <path>     MCP Server 可执行文件路径" << std::endl;
    std::cerr << "  --mcp-args <args>       MCP Server 启动参数 (逗号分隔)" << std::endl;
    std::cerr << "  --enable-mcp            启用 MCP" << std::endl;
    std::cerr << std::endl;
    std::cerr << "示例: " << program << " orch-1 5000 http://localhost:8500 sk-xxx --enable-mcp --mcp-server /path/to/mcp_server" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string agent_id = argv[1];
    int port = std::stoi(argv[2]);
    std::string registry_url = argv[3];
    std::string api_key = argv[4];
    
    // 默认值
    std::string redis_host = "127.0.0.1";
    int redis_port = 6379;
    
    // 解析 MCP 配置
    MCPAgentConfig mcp_config = parseMCPConfigFromArgs(argc, argv);
    
    // 也尝试从环境变量获取 MCP 配置
    if (!mcp_config.enable_mcp) {
        MCPAgentConfig env_config = parseMCPConfigFromEnv();
        if (env_config.enable_mcp) {
            mcp_config = env_config;
        }
    }
    
    // 解析其他命令行参数
    for (int i = 5; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--redis-host" && i + 1 < argc) {
            redis_host = argv[++i];
        } else if (arg == "--redis-port" && i + 1 < argc) {
            redis_port = std::stoi(argv[++i]);
        }
    }
    
    std::string listen_address = "http://localhost:" + std::to_string(port);
    
    try {
        AIOrchestrator orchestrator(agent_id, listen_address, registry_url, api_key, 
                                   redis_host, redis_port, mcp_config);
        orchestrator.start(port);
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
