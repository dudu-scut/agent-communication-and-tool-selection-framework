# 服务器模块
cmake_minimum_required(VERSION 3.15)

# 源文件列表 (库)
set(SERVER_SOURCES
    src/rpc_server.cpp
    src/agent_service.cpp
    src/ai_query_service.cpp
    src/orchestration_service_impl.cpp
    src/multi_agent_handler.cpp
    src/query_helpers.cpp
    src/auth_service.cpp
    src/auth_interceptor.cpp
    src/budget_middleware.cpp
    src/sharing_service.cpp
    src/agent_lifecycle_service.cpp
    src/user_experience_service.cpp
    src/observability_service.cpp
)

# 创建静态库
add_library(agent_rpc_server STATIC ${SERVER_SOURCES})

# ============================================================================
# RPC Server 可执行文件
# ============================================================================
add_executable(rpc_server src/main.cpp)
target_link_libraries(rpc_server
    agent_rpc_server
    agent_rpc_common
    agent_rpc_registry
    orchestrator
    a2a_adapter
    a2a
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    pthread
    ${UUID_LIB}
    CURL::libcurl
    ${HIREDIS_LIBRARIES}
)

# 链接依赖
target_link_libraries(agent_rpc_server
    agent_rpc_common
    agent_rpc_registry
    orchestrator
    a2a_adapter
    a2a
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    grpc++_reflection  # Fix #55: linking directly (TODO: use find_package(gRPC CONFIG) for imported target gRPC::grpc++_reflection)
    pthread
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    ${HIREDIS_LIBRARIES}
)

# 包含目录
target_include_directories(agent_rpc_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

# 编译选项
target_compile_options(agent_rpc_server PRIVATE
    -Wall
    -Wextra
    -std=c++20  # Consistent with root CMakeLists.txt C++20 standard
)

# 安装规则
install(TARGETS agent_rpc_server
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/agent_rpc
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
