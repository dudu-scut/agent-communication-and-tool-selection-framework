add_library(agent_rpc_db STATIC
    src/migration_runner.cpp
)

target_link_libraries(agent_rpc_db
    PUBLIC agent_rpc_common
    PRIVATE OpenSSL::Crypto
)

target_include_directories(agent_rpc_db PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(db_migrate src/main.cpp)
target_link_libraries(db_migrate PRIVATE agent_rpc_db)

install(TARGETS db_migrate RUNTIME DESTINATION bin)
