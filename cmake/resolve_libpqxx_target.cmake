function(agent_rpc_resolve_libpqxx_target output_variable)
    # Debian packages may export an unnamespaced pqxx target while other
    # libpqxx packages export pqxx::pqxx. Resolve without inventing aliases.
    if(TARGET pqxx)
        set(${output_variable} pqxx PARENT_SCOPE)
    elseif(TARGET pqxx::pqxx)
        set(${output_variable} pqxx::pqxx PARENT_SCOPE)
    else()
        message(FATAL_ERROR "libpqxx package did not export pqxx or pqxx::pqxx")
    endif()
endfunction()
