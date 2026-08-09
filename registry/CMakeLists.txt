# Registry module
cmake_minimum_required(VERSION 3.15)

# Source files
set(REGISTRY_SOURCES
    src/service_registry.cpp
)

# Create the static library
add_library(agent_rpc_registry STATIC ${REGISTRY_SOURCES})

# Link dependencies
target_link_libraries(agent_rpc_registry 
    agent_rpc_common
    curl
    jsoncpp
    pthread
)

# Include directories
target_include_directories(agent_rpc_registry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

# Compile options
target_compile_options(agent_rpc_registry PRIVATE
    -Wall
    -Wextra
    -std=c++17
)

# Install rules
install(TARGETS agent_rpc_registry
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/agent_rpc
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
