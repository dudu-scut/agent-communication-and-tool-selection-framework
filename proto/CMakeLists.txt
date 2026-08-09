# Generate protobuf and gRPC code
set(PROTO_FILES
    common.proto
    agent_service.proto
    ai_query.proto
    user.proto
    observability.proto
    agent_lifecycle.proto
    user_experience.proto
    orchestration.proto
    sharing.proto
)

foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    
    # Generate protobuf code
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc
               ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h
        COMMAND protoc
        ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
             --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
             ${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_FILE}
        COMMENT "Generating protobuf code for ${PROTO_FILE}"
    )
    
    # Generate gRPC code
    # Find grpc_cpp_plugin (Linux / MSYS2 MinGW / custom path)
    find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin
        PATHS
            /usr/bin
            /mingw64/bin
            /usr/local/bin
        HINTS
            ${GRPC_PLUGIN_DIR}
    )
    if(NOT GRPC_CPP_PLUGIN)
        message(FATAL_ERROR "grpc_cpp_plugin not found! Install gRPC or set GRPC_PLUGIN_DIR.")
    endif()
    message(STATUS "Found grpc_cpp_plugin: ${GRPC_CPP_PLUGIN}")

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.cc
               ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.h
        COMMAND protoc
        ARGS --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
             --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
             --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
             ${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_FILE}
        COMMENT "Generating gRPC code for ${PROTO_FILE}"
    )
    
    list(APPEND PROTO_SOURCES
        ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc
        ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.cc
    )
    
    list(APPEND PROTO_HEADERS
        ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h
        ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.h
    )
endforeach()

# Create proto library
add_library(proto_lib STATIC ${PROTO_SOURCES} ${PROTO_HEADERS})
target_link_libraries(proto_lib ${GRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})
target_include_directories(proto_lib PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

# Export headers
install(FILES ${PROTO_HEADERS} DESTINATION include/proto)
