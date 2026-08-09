# Client module
cmake_minimum_required(VERSION 3.15)

# Source files (library)
set(CLIENT_SOURCES
    src/rpc_client.cpp
    src/ai_query_client.cpp
    src/agent_auto_registrar.cpp
)

# Create the static library
add_library(agent_rpc_client STATIC ${CLIENT_SOURCES})

# RPC Client executable
add_executable(rpc_client src/main.cpp)
target_link_libraries(rpc_client
    agent_rpc_client
    agent_rpc_common
    agent_rpc_registry
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    pthread
    ${UUID_LIB}
)

# Link dependencies
target_link_libraries(agent_rpc_client 
    agent_rpc_common
    agent_rpc_registry
    proto_lib
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    pthread
    ${UUID_LIB}
)

# Include directories
target_include_directories(agent_rpc_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

# Compile options
target_compile_options(agent_rpc_client PRIVATE
    -Wall
    -Wextra
    -std=c++17
)

# Install rules
install(TARGETS agent_rpc_client
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/agent_rpc
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
