function(agent_rpc_resolve_libpqxx_target output_variable)
    # Debian packages may export an unnamespaced pqxx target while other
    # libpqxx packages export pqxx::pqxx. Resolve without inventing aliases.
    set(_agent_rpc_libpqxx_target "")
    if(TARGET pqxx)
        set(_agent_rpc_libpqxx_target pqxx)
    elseif(TARGET pqxx::pqxx)
        set(_agent_rpc_libpqxx_target pqxx::pqxx)
    elseif(TARGET libpqxx::pqxx)
        set(_agent_rpc_libpqxx_target libpqxx::pqxx)
    else()
        # Ubuntu's libpqxx-dev currently ships libpqxx.pc without a CMake
        # package config. Import that target rather than manufacturing one.
        if(NOT COMMAND pkg_check_modules)
            message(FATAL_ERROR "libpqxx requires PkgConfig or a CMake target")
        endif()
        pkg_check_modules(AGENT_RPC_LIBPQXX REQUIRED IMPORTED_TARGET libpqxx)
        if(TARGET PkgConfig::AGENT_RPC_LIBPQXX)
            set(_agent_rpc_libpqxx_target PkgConfig::AGENT_RPC_LIBPQXX)
        else()
            message(FATAL_ERROR "libpqxx pkg-config lookup did not provide an imported target")
        endif()
    endif()

    # Ubuntu 26.04 ships libpqxx 7.10, which has a known process-exit
    # double-free in its shared library (upstream jtv/libpqxx#1195). Do not
    # let a build succeed only to abort in every libpqxx consumer's teardown.
    # Require libpqxx 8+ on that distribution; no network download is hidden
    # in the build, so the developer can choose a pinned/local installation.
    set(_agent_rpc_libpqxx_version "")
    foreach(_agent_rpc_version_variable
            libpqxx_VERSION
            libpqxx_VERSION_STRING
            pqxx_VERSION
            PQXX_VERSION
            AGENT_RPC_LIBPQXX_VERSION)
        if(NOT _agent_rpc_libpqxx_version AND DEFINED ${_agent_rpc_version_variable})
            set(_agent_rpc_libpqxx_version "${${_agent_rpc_version_variable}}")
        endif()
    endforeach()

    # A CMake package may not expose a version variable even when pkg-config
    # is present. Ask pkg-config as a secondary source without changing the
    # already-resolved link target.
    if(NOT _agent_rpc_libpqxx_version AND COMMAND pkg_check_modules)
        pkg_check_modules(AGENT_RPC_LIBPQXX_VERSION_CHECK QUIET libpqxx)
        if(AGENT_RPC_LIBPQXX_VERSION_CHECK_FOUND)
            set(_agent_rpc_libpqxx_version "${AGENT_RPC_LIBPQXX_VERSION_CHECK_VERSION}")
        endif()
    endif()

    set(_agent_rpc_is_ubuntu_2604 OFF)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "/etc/os-release")
        file(READ "/etc/os-release" _agent_rpc_os_release)
        string(REGEX MATCH "(^|\n)ID=(\"?)ubuntu\\2(\n|$)" _agent_rpc_ubuntu_match "${_agent_rpc_os_release}")
        string(REGEX MATCH "(^|\n)VERSION_ID=(\"?)26\\.04\\2(\n|$)" _agent_rpc_ubuntu_version_match "${_agent_rpc_os_release}")
        if(_agent_rpc_ubuntu_match AND _agent_rpc_ubuntu_version_match)
            set(_agent_rpc_is_ubuntu_2604 ON)
        endif()
    endif()

    if(_agent_rpc_is_ubuntu_2604)
        if(NOT _agent_rpc_libpqxx_version)
            message(FATAL_ERROR
                "Ubuntu 26.04 requires libpqxx >= 8.0; the installed libpqxx version "
                "could not be determined. Install a pinned/local libpqxx 8.x package "
                "and reconfigure (the stock libpqxx 7.10 package is unsupported).")
        endif()
        string(REGEX MATCH "^([0-9]+)(\\.([0-9]+))?" _agent_rpc_libpqxx_version_match "${_agent_rpc_libpqxx_version}")
        if(NOT _agent_rpc_libpqxx_version_match OR CMAKE_MATCH_1 LESS 8)
            message(FATAL_ERROR
                "Ubuntu 26.04 with libpqxx ${_agent_rpc_libpqxx_version} is unsupported: "
                "libpqxx 7.x has a known process-exit double-free. Install libpqxx >= 8.0 "
                "from a pinned/local build and reconfigure.")
        endif()
    endif()

    if(_agent_rpc_libpqxx_version)
        message(STATUS "Using libpqxx target ${_agent_rpc_libpqxx_target} (version ${_agent_rpc_libpqxx_version})")
    else()
        message(STATUS "Using libpqxx target ${_agent_rpc_libpqxx_target} (version unknown)")
    endif()
    set(${output_variable} "${_agent_rpc_libpqxx_target}" PARENT_SCOPE)
endfunction()
