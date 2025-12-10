# =============================================================================
# FindPacsSystem.cmake
# CMake module to find kcenon's pacs_system and ecosystem dependencies
#
# This module finds the following packages from kcenon ecosystem:
#   - common_system (logging, utilities)
#   - thread_system (thread pool, concurrency)
#   - messaging_system (networking, message handling)
#   - container_system (data structures)
#   - pacs_system (DICOM MWL/MPPS components)
#
# Build modes:
#   - BRIDGE_STANDALONE_BUILD=ON : Build without external kcenon dependencies
#   - BRIDGE_STANDALONE_BUILD=OFF: Use FetchContent to download dependencies
#
# Usage:
#   find_package(PacsSystem REQUIRED COMPONENTS common thread messaging)
#
# Defines:
#   PACS_SYSTEM_FOUND        - System was found
#   PACS_SYSTEM_INCLUDE_DIRS - Include directories
#   PACS_SYSTEM_LIBRARIES    - Libraries to link against
#
# Imported targets:
#   kcenon::common_system
#   kcenon::thread_system
#   kcenon::messaging_system
#   kcenon::container_system
#   kcenon::pacs_system (if available)
# =============================================================================

include(FetchContent)

# Standalone build mode (no external kcenon dependencies)
option(BRIDGE_STANDALONE_BUILD "Build without external kcenon ecosystem dependencies" ON)

# Default to finding system-installed packages first
option(PACS_BRIDGE_USE_FETCHCONTENT "Use FetchContent to download dependencies" ON)

# GitHub URLs for kcenon ecosystem
set(KCENON_GITHUB_BASE "https://github.com/kcenon")

# =============================================================================
# Standalone Build Mode
# =============================================================================

if(BRIDGE_STANDALONE_BUILD)
    message(STATUS "Standalone build mode: External kcenon dependencies disabled")
    message(STATUS "  - Logging functionality will use internal stubs")
    message(STATUS "  - Set BRIDGE_STANDALONE_BUILD=OFF to enable external dependencies")

    # Create stub interface library for logging
    add_library(kcenon_stubs INTERFACE)
    target_compile_definitions(kcenon_stubs INTERFACE
        PACS_BRIDGE_STANDALONE_BUILD
    )

    # Skip fetching external components
    set(PACS_BRIDGE_HAS_KCENON_DEPS FALSE CACHE BOOL "kcenon dependencies available" FORCE)

else()
    # =============================================================================
    # Component Fetching Function
    # =============================================================================

    function(fetch_kcenon_component name)
        string(TOUPPER ${name} NAME_UPPER)

        # Check if already available via find_package
        find_package(${name} QUIET CONFIG)
        if(${name}_FOUND)
            message(STATUS "Found ${name} via find_package")
            return()
        endif()

        # Use FetchContent if enabled
        if(PACS_BRIDGE_USE_FETCHCONTENT)
            message(STATUS "Fetching ${name} from GitHub...")

            FetchContent_Declare(
                ${name}
                GIT_REPOSITORY ${KCENON_GITHUB_BASE}/${name}.git
                GIT_TAG main
                GIT_SHALLOW TRUE
            )

            # Set options before making available
            set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
            set(${NAME_UPPER}_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(${NAME_UPPER}_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

            FetchContent_MakeAvailable(${name})

            message(STATUS "Fetched ${name} successfully")
        else()
            message(FATAL_ERROR "${name} not found and FetchContent is disabled")
        endif()
    endfunction()

    # =============================================================================
    # Find or Fetch Required Components
    # =============================================================================

    # Common system (always required for logging)
    fetch_kcenon_component(common_system)

    # Thread system (always required for thread pools)
    fetch_kcenon_component(thread_system)

    # Messaging system (required for network communication)
    fetch_kcenon_component(messaging_system)

    # Container system (required for data structures)
    fetch_kcenon_component(container_system)

    # PACS system (optional, for DICOM MWL/MPPS integration)
    option(BRIDGE_BUILD_PACS_INTEGRATION "Enable pacs_system integration for DICOM support" ON)
    if(BRIDGE_BUILD_PACS_INTEGRATION)
        fetch_kcenon_component(pacs_system)
        set(PACS_BRIDGE_HAS_PACS_SYSTEM TRUE CACHE BOOL "pacs_system is available" FORCE)
    else()
        set(PACS_BRIDGE_HAS_PACS_SYSTEM FALSE CACHE BOOL "pacs_system is available" FORCE)
    endif()

    set(PACS_BRIDGE_HAS_KCENON_DEPS TRUE CACHE BOOL "kcenon dependencies available" FORCE)
endif()

# =============================================================================
# SQLite (Required for queue_manager persistence)
# =============================================================================

find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    message(STATUS "Found SQLite3: ${SQLite3_VERSION}")
    set(PACS_BRIDGE_HAS_SQLITE TRUE CACHE BOOL "SQLite3 is available" FORCE)
else()
    # Try pkg-config on Unix systems
    if(UNIX)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(SQLITE3 sqlite3)
            if(SQLITE3_FOUND)
                message(STATUS "Found SQLite3 via pkg-config: ${SQLITE3_VERSION}")
                set(PACS_BRIDGE_HAS_SQLITE TRUE CACHE BOOL "SQLite3 is available" FORCE)
                # Create imported target for consistency
                if(NOT TARGET SQLite::SQLite3)
                    add_library(SQLite::SQLite3 INTERFACE IMPORTED)
                    set_target_properties(SQLite::SQLite3 PROPERTIES
                        INTERFACE_INCLUDE_DIRECTORIES "${SQLITE3_INCLUDE_DIRS}"
                        INTERFACE_LINK_LIBRARIES "${SQLITE3_LIBRARIES}"
                    )
                endif()
            endif()
        endif()
    endif()

    if(NOT PACS_BRIDGE_HAS_SQLITE)
        message(STATUS "SQLite3 not found. Queue persistence will be disabled.")
        set(PACS_BRIDGE_HAS_SQLITE FALSE CACHE BOOL "SQLite3 is available" FORCE)
    endif()
endif()

# =============================================================================
# OpenSSL (Optional, for TLS support)
# =============================================================================

option(BRIDGE_ENABLE_TLS "Enable TLS support with OpenSSL" ON)

if(BRIDGE_ENABLE_TLS)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        message(STATUS "Found OpenSSL: ${OPENSSL_VERSION}")
        set(PACS_BRIDGE_HAS_OPENSSL TRUE CACHE BOOL "OpenSSL is available" FORCE)
    else()
        message(STATUS "OpenSSL not found. TLS support disabled.")
        set(PACS_BRIDGE_HAS_OPENSSL FALSE CACHE BOOL "OpenSSL is available" FORCE)
    endif()
else()
    set(PACS_BRIDGE_HAS_OPENSSL FALSE CACHE BOOL "OpenSSL is available" FORCE)
endif()

# =============================================================================
# Interface Library for All Dependencies
# =============================================================================

add_library(pacs_bridge_dependencies INTERFACE)

if(BRIDGE_STANDALONE_BUILD)
    # Standalone mode: Use stub library
    target_link_libraries(pacs_bridge_dependencies INTERFACE kcenon_stubs)
    target_compile_definitions(pacs_bridge_dependencies INTERFACE
        PACS_BRIDGE_STANDALONE_BUILD
    )
else()
    # Full mode: Link kcenon ecosystem libraries
    if(TARGET common_system)
        target_link_libraries(pacs_bridge_dependencies INTERFACE common_system)
    elseif(TARGET kcenon::common_system)
        target_link_libraries(pacs_bridge_dependencies INTERFACE kcenon::common_system)
    endif()

    if(TARGET thread_system)
        target_link_libraries(pacs_bridge_dependencies INTERFACE thread_system)
    elseif(TARGET kcenon::thread_system)
        target_link_libraries(pacs_bridge_dependencies INTERFACE kcenon::thread_system)
    endif()

    if(TARGET messaging_system)
        target_link_libraries(pacs_bridge_dependencies INTERFACE messaging_system)
    elseif(TARGET kcenon::messaging_system)
        target_link_libraries(pacs_bridge_dependencies INTERFACE kcenon::messaging_system)
    endif()

    if(TARGET container_system)
        target_link_libraries(pacs_bridge_dependencies INTERFACE container_system)
    elseif(TARGET kcenon::container_system)
        target_link_libraries(pacs_bridge_dependencies INTERFACE kcenon::container_system)
    endif()

    # Link pacs_system if available
    if(PACS_BRIDGE_HAS_PACS_SYSTEM)
        if(TARGET pacs_system)
            target_link_libraries(pacs_bridge_dependencies INTERFACE pacs_system)
        elseif(TARGET kcenon::pacs_system)
            target_link_libraries(pacs_bridge_dependencies INTERFACE kcenon::pacs_system)
        endif()
        target_compile_definitions(pacs_bridge_dependencies INTERFACE
            PACS_BRIDGE_HAS_PACS_SYSTEM
        )
    endif()
endif()

# Add SQLite if available
if(PACS_BRIDGE_HAS_SQLITE)
    if(TARGET SQLite::SQLite3)
        target_link_libraries(pacs_bridge_dependencies INTERFACE SQLite::SQLite3)
    endif()
    target_compile_definitions(pacs_bridge_dependencies INTERFACE
        PACS_BRIDGE_HAS_SQLITE
    )
endif()

# Add OpenSSL if available
if(PACS_BRIDGE_HAS_OPENSSL)
    target_link_libraries(pacs_bridge_dependencies INTERFACE
        OpenSSL::SSL
        OpenSSL::Crypto
    )
    target_compile_definitions(pacs_bridge_dependencies INTERFACE
        PACS_BRIDGE_HAS_OPENSSL
    )
endif()

# =============================================================================
# Google Test (for unit testing)
# =============================================================================

if(BRIDGE_BUILD_TESTS)
    # Always use FetchContent to build GoogleTest from source.
    # This ensures ABI compatibility because GTest is built with the same compiler
    # and standard library as the project. System-installed GTest packages may
    # have been built with different compiler/stdlib combinations, causing linker
    # errors (e.g., libc++ vs libstdc++ mismatch on Linux/macOS with clang).
    message(STATUS "Fetching GoogleTest from GitHub...")

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )

    # Prevent overriding parent project's compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    message(STATUS "Fetched GoogleTest successfully")
    set(PACS_BRIDGE_HAS_GTEST TRUE CACHE BOOL "GTest is available" FORCE)
else()
    set(PACS_BRIDGE_HAS_GTEST FALSE CACHE BOOL "GTest is available" FORCE)
endif()

# Export success
set(PACS_SYSTEM_FOUND TRUE CACHE BOOL "PACS System dependencies found" FORCE)
