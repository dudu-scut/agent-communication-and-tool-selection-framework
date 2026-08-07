# 查找Google Test
find_package(GTest REQUIRED)

add_executable(test_rpc_framework test_rpc_framework.cpp)
target_link_libraries(test_rpc_framework
    agent_rpc_common
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_rpc_framework PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)

# 序列化测试
add_executable(test_serialization test_serialization.cpp)

# 链接库
target_link_libraries(test_serialization 
    agent_rpc_common
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
    pthread
    ${UUID_LIB}
)

# 包含目录
target_include_directories(test_serialization PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

# A2A集成测试
add_executable(test_a2a_integration test_a2a_integration.cpp)

target_link_libraries(test_a2a_integration 
    a2a
    GTest::gtest
    GTest::gtest_main
    pthread
    CURL::libcurl
)

target_include_directories(test_a2a_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/third_party
)

# 添加测试
enable_testing()
add_test(NAME RpcFrameworkTest COMMAND test_rpc_framework)
add_test(NAME SerializationTest COMMAND test_serialization)
add_test(NAME A2AIntegrationTest COMMAND test_a2a_integration)

# Proto序列化往返属性测试
# **Feature: a2a-integration, Property 11: Protobuf Serialization Round-Trip**
# **Validates: Requirements 11.5**
find_package(rapidcheck REQUIRED)

add_executable(test_proto_roundtrip test_proto_roundtrip.cpp)

target_link_libraries(test_proto_roundtrip 
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    rapidcheck
    GTest::gtest
    GTest::gtest_main
    pthread
)

target_include_directories(test_proto_roundtrip PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

add_test(NAME ProtoRoundTripTest COMMAND test_proto_roundtrip)

# A2A适配器属性测试
# **Feature: a2a-integration, Property 1, 7, 10**
# **Validates: Requirements 5.1, 5.5, 8.1-8.4, 9.5, 10.1**
add_executable(test_adapter_properties test_adapter_properties.cpp)

target_link_libraries(test_adapter_properties 
    a2a_adapter
    a2a
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    rapidcheck
    GTest::gtest
    GTest::gtest_main
    pthread
    CURL::libcurl
)

target_include_directories(test_adapter_properties PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a_adapter/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/third_party
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

add_test(NAME AdapterPropertiesTest COMMAND test_adapter_properties)

# Task Manager属性测试
# **Feature: a2a-integration, Property 2, 3, 6**
# **Validates: Requirements 3.2, 3.5, 4.2, 5.5, 6.1-6.6**
add_executable(test_task_manager_properties test_task_manager_properties.cpp)

target_link_libraries(test_task_manager_properties 
    a2a_adapter
    a2a
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    rapidcheck
    GTest::gtest
    GTest::gtest_main
    pthread
    CURL::libcurl
)

target_include_directories(test_task_manager_properties PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a_adapter/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/third_party
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

add_test(NAME TaskManagerPropertiesTest COMMAND test_task_manager_properties)

# Agent Router属性测试
# **Feature: a2a-integration, Property 4, 8**
# **Validates: Requirements 2.3, 3.4, 4.5, 10.3**
add_executable(test_agent_router_properties test_agent_router_properties.cpp)

target_link_libraries(test_agent_router_properties 
    orchestrator
    a2a_adapter
    a2a
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    rapidcheck
    GTest::gtest
    GTest::gtest_main
    pthread
    CURL::libcurl
)

target_include_directories(test_agent_router_properties PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../orchestrator/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a_adapter/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/third_party
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

add_test(NAME AgentRouterPropertiesTest COMMAND test_agent_router_properties)
# Link contract guard: consumers of orchestrator must receive agent_rpc_common
# transitively because orchestrator sources reference RedisClient symbols.
add_executable(test_orchestrator_common_link test_orchestrator_common_link.cpp)
target_link_libraries(test_orchestrator_common_link
    PRIVATE
    orchestrator
)
add_test(NAME OrchestratorCommonLinkTest COMMAND test_orchestrator_common_link)

# 安装测试
install(TARGETS test_serialization
    RUNTIME DESTINATION bin
)


# AI Query集成测试
# **Feature: a2a-integration, Task 17: 集成测试**
# **Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5, 9.5, 10.2, 10.5**
add_executable(test_ai_query_integration test_ai_query_integration.cpp)

target_link_libraries(test_ai_query_integration 
    agent_rpc_server
    agent_rpc_client
    a2a_adapter
    a2a
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    rapidcheck
    GTest::gtest
    GTest::gtest_main
    pthread
    ${UUID_LIB}
    CURL::libcurl
)

target_include_directories(test_ai_query_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a_adapter/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

add_test(NAME AIQueryIntegrationTest COMMAND test_ai_query_integration)

if(ENABLE_MCP)
# MCP Agent集成测试
# **Feature: a2a-integration, Task 20: MCP 集成测试**
# **Validates: Requirements 12.1, 12.3, 12.5**
add_executable(test_mcp_integration test_mcp_integration.cpp)

target_link_libraries(test_mcp_integration
    agent_rpc_mcp
    agent_rpc_common
    rapidcheck
    GTest::gtest
    GTest::gtest_main
    pthread
)

target_include_directories(test_mcp_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../mcp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)

add_test(NAME MCPIntegrationTest COMMAND test_mcp_integration)

# RAG-MCP 属性测试
# **Feature: rag-mcp, Property 1-8**
# **Validates: Requirements 1.3, 3.2, 3.3, 3.4, 4.1, 4.3, 7.2, 7.3**
add_executable(test_rag_mcp_properties test_rag_mcp_properties.cpp)

target_link_libraries(test_rag_mcp_properties
    agent_rpc_mcp
    agent_rpc_common
    rapidcheck
    GTest::gtest
    GTest::gtest_main
    pthread
    CURL::libcurl
)

target_include_directories(test_rag_mcp_properties PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../mcp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)

add_test(NAME RAGMCPPropertiesTest COMMAND test_rag_mcp_properties)
endif()


add_executable(test_service_registry test_service_registry.cpp)
target_link_libraries(test_service_registry
    agent_rpc_registry
    agent_rpc_common
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_service_registry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../registry/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
add_test(NAME ServiceRegistryTest COMMAND test_service_registry)

add_executable(test_agent_communication test_agent_communication.cpp)
target_link_libraries(test_agent_communication
    agent_rpc_server
    agent_rpc_client
    agent_rpc_registry
    agent_rpc_common
    GTest::gtest
    GTest::gtest_main
    pthread
    ${UUID_LIB}
    CURL::libcurl
)
target_include_directories(test_agent_communication PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../registry/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)
add_test(NAME AgentCommunicationIntegrationTest COMMAND test_agent_communication)

add_executable(test_background_scheduler test_background_scheduler.cpp)
target_link_libraries(test_background_scheduler
    agent_rpc_common
    rapidcheck
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_background_scheduler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
add_test(NAME BackgroundSchedulerTest COMMAND test_background_scheduler)

# TraceContext 单元测试
add_executable(test_trace_context test_trace_context.cpp)
target_link_libraries(test_trace_context
    agent_rpc_common
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_trace_context PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
add_test(NAME TraceContextTest COMMAND test_trace_context)

# CostTracker 单元测试
add_executable(test_cost_tracker test_cost_tracker.cpp)
target_link_libraries(test_cost_tracker
    agent_rpc_common
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_cost_tracker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
add_test(NAME CostTrackerTest COMMAND test_cost_tracker)

# Redis 集成测试 (MemoryService + RedisClient)
# 需要运行中的 Redis 实例，不可用时自动跳过
add_executable(test_redis_services test_redis_services.cpp)
target_link_libraries(test_redis_services
    agent_rpc_server
    agent_rpc_common
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    ${HIREDIS_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
    pthread
    ${UUID_LIB}
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_include_directories(test_redis_services PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)
add_test(NAME RedisServicesTest COMMAND test_redis_services)

# LoadBalancer 高级测试 (WeightedRoundRobin / ConsistentHash / LeastResponseTime / Manager)
add_executable(test_load_balancer_advanced test_load_balancer_advanced.cpp)
target_link_libraries(test_load_balancer_advanced
    agent_rpc_common
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_load_balancer_advanced PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
add_test(NAME LoadBalancerAdvancedTest COMMAND test_load_balancer_advanced)

# CircuitBreaker 边缘情况测试（bug-hunt: 状态转换、并发、配置验证）
add_executable(test_circuit_breaker_edge test_circuit_breaker_edge.cpp)
target_link_libraries(test_circuit_breaker_edge
    agent_rpc_common
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_circuit_breaker_edge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
add_test(NAME CircuitBreakerEdgeTest COMMAND test_circuit_breaker_edge)

# TaskStateValidator 一致性测试（bug-hunt: isValidTransition ↔ getValidNextStates 一致性）
add_executable(test_task_state_validator test_task_state_validator.cpp)
target_link_libraries(test_task_state_validator
    a2a_adapter
    a2a
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_task_state_validator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a_adapter/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/third_party
)
add_test(NAME TaskStateValidatorTest COMMAND test_task_state_validator)

# LoadBalancer 边缘情况测试（bug-hunt: const_cast、ConsistentHash 回归、空列表）
add_executable(test_load_balancer_edges test_load_balancer_edges.cpp)
target_link_libraries(test_load_balancer_edges
    agent_rpc_common
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_load_balancer_edges PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
add_test(NAME LoadBalancerEdgesTest COMMAND test_load_balancer_edges)

# AgentInfo 边缘情况测试（bug-hunt: hasSkill/hasAnySkill 空输入语义、getSkillDescription 区分度）
add_executable(test_agent_info_edges test_agent_info_edges.cpp)
target_link_libraries(test_agent_info_edges
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_agent_info_edges PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../orchestrator/include
)
add_test(NAME AgentInfoEdgesTest COMMAND test_agent_info_edges)

# ErrorMapper 边缘情况测试（bug-hunt: 错误码映射完整性、网络异常分类）
add_executable(test_error_mapper test_error_mapper.cpp)
target_link_libraries(test_error_mapper
    a2a_adapter
    a2a
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_error_mapper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a_adapter/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/third_party
)
add_test(NAME ErrorMapperTest COMMAND test_error_mapper)

# PostgreSQL persistence foundation. Configuration is hermetic; the migration
# integration test is registered only when a usable Docker daemon is present.
add_executable(test_postgres_config test_postgres_config.cpp)
target_link_libraries(test_postgres_config
    agent_rpc_common
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_postgres_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
add_test(NAME PostgresConfigTest COMMAND test_postgres_config)
add_executable(test_postgres_pool_contract test_postgres_pool_contract.cpp)
target_link_libraries(test_postgres_pool_contract
    agent_rpc_common
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_postgres_pool_contract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
add_test(NAME PostgresPoolContractTest COMMAND test_postgres_pool_contract)

add_executable(test_postgres_migration_api test_postgres_migration_api.cpp)
target_link_libraries(test_postgres_migration_api
    ${AGENT_RPC_PQXX_TARGET}
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_compile_definitions(test_postgres_migration_api PRIVATE
    NEXUSAI_MIGRATION_RUNNER_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../db/src/migration_runner.cpp"
)
add_test(NAME PostgresMigrationApiTest COMMAND test_postgres_migration_api)

# RPC startup migration contract: source/Compose wiring keeps migrations
# before RpcServer::initialize and fails closed when startup migration fails.
add_executable(test_rpc_server_migrations test_rpc_server_migrations.cpp)
target_link_libraries(test_rpc_server_migrations
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_compile_definitions(test_rpc_server_migrations PRIVATE
    RPC_SERVER_MAIN_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../server/src/main.cpp"
    RPC_SERVER_CMAKE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../server/CMakeLists.txt"
    RPC_COMPOSE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../docker-compose.yml"
)
add_test(NAME RpcServerMigrationsTest COMMAND test_rpc_server_migrations)

find_package(Python3 COMPONENTS Interpreter)
find_program(DOCKER_EXECUTABLE docker)
set(NEXUSAI_DOCKER_AVAILABLE OFF)
if(DOCKER_EXECUTABLE)
    execute_process(
        COMMAND ${DOCKER_EXECUTABLE} info
        RESULT_VARIABLE NEXUSAI_DOCKER_INFO_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NEXUSAI_DOCKER_INFO_RESULT EQUAL 0)
        set(NEXUSAI_DOCKER_AVAILABLE ON)
    endif()
endif()
if(Python3_Interpreter_FOUND AND NEXUSAI_DOCKER_AVAILABLE)
    add_test(NAME PostgresMigrationIntegrationTest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/postgres_migrations_integration.py
            --migrator $<TARGET_FILE:db_migrate>
            --migrations ${CMAKE_CURRENT_SOURCE_DIR}/../db/migrations
            --docker ${DOCKER_EXECUTABLE})
    set_tests_properties(PostgresMigrationIntegrationTest PROPERTIES TIMEOUT 180)
endif()

# The local WSL/Compose transport intentionally uses plaintext gRPC.  Keep a
# source-level contract here so certificate plumbing cannot silently return.
add_executable(test_rpc_server_local_transport test_rpc_server_local_transport.cpp)
target_link_libraries(test_rpc_server_local_transport
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_compile_definitions(test_rpc_server_local_transport PRIVATE
    RPC_SERVER_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../server/src/rpc_server.cpp"
    RPC_SERVER_HEADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../server/include/agent_rpc/server/rpc_server.h"
    RPC_TYPES_HEADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../common/include/agent_rpc/common/types.h"
    RPC_CLIENT_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../client/src/rpc_client.cpp"
    RPC_CLIENT_HEADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../client/include/agent_rpc/client/rpc_client.h"
)
add_test(NAME RpcServerLocalTransportTest COMMAND test_rpc_server_local_transport)

add_executable(test_auth_repository_contract test_auth_repository_contract.cpp)
target_link_libraries(test_auth_repository_contract
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_compile_definitions(test_auth_repository_contract PRIVATE
    NEXUSAI_AUTH_MIGRATION_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../db/migrations/V010__local_auth.sql"
    NEXUSAI_AUTH_REPOSITORY_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/../common/include/agent_rpc/common/auth_repository.h"
)
add_test(NAME AuthRepositoryContractTest COMMAND test_auth_repository_contract)
add_executable(test_local_auth_contract test_local_auth_contract.cpp)
target_link_libraries(test_local_auth_contract
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_compile_definitions(test_local_auth_contract PRIVATE
    NEXUSAI_AUTH_SERVICE_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/../server/include/agent_rpc/server/auth_service.h"
    NEXUSAI_AUTH_SERVICE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../server/src/auth_service.cpp"
    NEXUSAI_RPC_SERVER_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/../server/include/agent_rpc/server/rpc_server.h"
    NEXUSAI_RPC_SERVER_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../server/src/rpc_server.cpp"
    NEXUSAI_AUTH_REPOSITORY_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/../common/include/agent_rpc/common/auth_repository.h"
)
add_test(NAME LocalAuthContractTest COMMAND test_local_auth_contract)
add_executable(test_auth_owner_context_contract test_auth_owner_context_contract.cpp)
target_link_libraries(test_auth_owner_context_contract
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_compile_definitions(test_auth_owner_context_contract PRIVATE
    NEXUSAI_AUTH_INTERCEPTOR_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/../server/include/agent_rpc/server/auth_interceptor.h"
    NEXUSAI_AUTH_INTERCEPTOR_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../server/src/auth_interceptor.cpp"
)
add_test(NAME AuthOwnerContextContractTest COMMAND test_auth_owner_context_contract)

add_executable(test_durable_domain_schema_contract test_durable_domain_schema_contract.cpp)
target_link_libraries(test_durable_domain_schema_contract
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_compile_definitions(test_durable_domain_schema_contract PRIVATE
    NEXUSAI_DURABLE_DOMAIN_MIGRATION_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../db/migrations/V011__durable_domain.sql"
)
add_test(NAME DurableDomainSchemaContractTest COMMAND test_durable_domain_schema_contract)

add_executable(test_postgres_budget_repository_contract test_postgres_budget_repository_contract.cpp)
target_link_libraries(test_postgres_budget_repository_contract
    agent_rpc_common
    ${AGENT_RPC_PQXX_TARGET}
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_postgres_budget_repository_contract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
target_compile_definitions(test_postgres_budget_repository_contract PRIVATE
    NEXUSAI_BUDGET_MIGRATION_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../db/migrations/V012__postgres_budget.sql"
    NEXUSAI_BUDGET_REPOSITORY_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/../common/include/agent_rpc/common/postgres_budget_repository.h"
)
add_test(NAME PostgresBudgetRepositoryContractTest COMMAND test_postgres_budget_repository_contract)
add_executable(test_query_domain_repository_contract test_query_domain_repository_contract.cpp)
target_link_libraries(test_query_domain_repository_contract
    agent_rpc_common
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_query_domain_repository_contract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
target_compile_definitions(test_query_domain_repository_contract PRIVATE
    NEXUSAI_QUERY_DOMAIN_REPOSITORY_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/../common/include/agent_rpc/common/query_domain_repository.h"
    NEXUSAI_QUERY_DOMAIN_REPOSITORY_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../common/src/query_domain_repository.cpp"
)
add_test(NAME QueryDomainRepositoryContractTest COMMAND test_query_domain_repository_contract)
