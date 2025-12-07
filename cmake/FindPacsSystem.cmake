# =============================================================================
# FindPacsSystem.cmake
# CMake module to find kcenon's pacs_system and ecosystem dependencies
#
# This module finds the following packages from kcenon ecosystem:
#   - common_system (logging, utilities)
#   - thread_system (thread pool, concurrency)
#   - messaging_system (networking, message handling)
#   - container_system (data structures)
#   - pacs_system (DICOM components) - optional
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

    set(PACS_BRIDGE_HAS_KCENON_DEPS TRUE CACHE BOOL "kcenon dependencies available" FORCE)
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

# Export success
set(PACS_SYSTEM_FOUND TRUE CACHE BOOL "PACS System dependencies found" FORCE)
