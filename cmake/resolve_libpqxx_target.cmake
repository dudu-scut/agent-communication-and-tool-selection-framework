function(agent_rpc_resolve_libpqxx_target output_variable)
    # Debian packages may export an unnamespaced pqxx target while other
    # libpqxx packages export pqxx::pqxx. Resolve without inventing aliases.
    if(TARGET pqxx)
        set(${output_variable} pqxx PARENT_SCOPE)
    elseif(TARGET pqxx::pqxx)
        set(${output_variable} pqxx::pqxx PARENT_SCOPE)
    elseif(TARGET libpqxx::pqxx)
        set(${output_variable} libpqxx::pqxx PARENT_SCOPE)
    else()
        # Ubuntu's libpqxx-dev currently ships libpqxx.pc without a CMake
        # package config. Import that target rather than manufacturing one.
        if(NOT COMMAND pkg_check_modules)
            message(FATAL_ERROR "libpqxx requires PkgConfig or a CMake target")
        endif()
        pkg_check_modules(AGENT_RPC_LIBPQXX REQUIRED IMPORTED_TARGET libpqxx)
        if(TARGET PkgConfig::AGENT_RPC_LIBPQXX)
            set(${output_variable} PkgConfig::AGENT_RPC_LIBPQXX PARENT_SCOPE)
        else()
            message(FATAL_ERROR "libpqxx pkg-config lookup did not provide an imported target")
        endif()
    endif()
endfunction()
