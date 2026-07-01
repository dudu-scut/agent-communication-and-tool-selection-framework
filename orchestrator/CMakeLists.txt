# Orchestrator Module CMakeLists.txt
# Task 8.1, 11.6: Orchestrator模块编译配置
# Requirements: 3.1

cmake_minimum_required(VERSION 3.15)

# Source files
set(ORCHESTRATOR_SOURCES
    src/agent_router.cpp
    src/agent_info.cpp
    src/task_planner.cpp
    src/task_executor.cpp
    src/result_aggregator.cpp
)

# Header files
set(ORCHESTRATOR_HEADERS
    include/agent_rpc/orchestrator/agent_info.h
    include/agent_rpc/orchestrator/agent_router.h
    include/agent_rpc/orchestrator/task_planner.h
    include/agent_rpc/orchestrator/task_executor.h
    include/agent_rpc/orchestrator/result_aggregator.h
)

# Create library
add_library(orchestrator ${ORCHESTRATOR_SOURCES} ${ORCHESTRATOR_HEADERS})

target_include_directories(orchestrator
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../a2a/third_party
        ${CMAKE_CURRENT_SOURCE_DIR}/../a2a_adapter/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../mcp/include
        ${CMAKE_BINARY_DIR}/proto
)

target_link_libraries(orchestrator
    PUBLIC
        a2a
        a2a_adapter
        agent_rpc_mcp
        ${GRPC_LIBRARIES}
        ${PROTOBUF_LIBRARIES}
        CURL::libcurl
)

# Embedding routing requires C++20 (MCP module uses C++20)
target_compile_features(orchestrator PUBLIC cxx_std_20)

# Compiler options
if(NOT MSVC)
    target_compile_options(orchestrator PRIVATE -Wall -Wextra)
endif()

# Export target
set_target_properties(orchestrator PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
