# =============================================================================
# CompilerFlags.cmake
# Compiler and platform-specific settings for PACS Bridge
# =============================================================================

# C++23 standard requirement (for std::expected)
# Note: The codebase uses std::expected which requires C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# Compiler Detection and Validation
# =============================================================================

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
        message(FATAL_ERROR "GCC 13+ required for C++23 support (std::expected). Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
    set(PACS_BRIDGE_COMPILER_GNU TRUE)

elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16.0")
        message(FATAL_ERROR "Clang 16+ required for C++23 support (std::expected). Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
    set(PACS_BRIDGE_COMPILER_CLANG TRUE)

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(MSVC_VERSION LESS 1930)
        message(FATAL_ERROR "MSVC 2022 (v17.0+) required for C++23 support. Found: ${MSVC_VERSION}")
    endif()
    set(PACS_BRIDGE_COMPILER_MSVC TRUE)

else()
    message(WARNING "Untested compiler: ${CMAKE_CXX_COMPILER_ID}. Build may fail.")
endif()

# =============================================================================
# Common Warning Flags
# =============================================================================

# Interface library for common compile options
add_library(pacs_bridge_compile_options INTERFACE)

if(PACS_BRIDGE_COMPILER_GNU OR PACS_BRIDGE_COMPILER_CLANG)
    target_compile_options(pacs_bridge_compile_options INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wcast-qual
        -Wformat=2
        -Wundef
        -Werror=return-type
        -Wno-unused-parameter
        $<$<CONFIG:Debug>:-g -O0 -fno-omit-frame-pointer>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
    )

    # Clang-specific flags
    if(PACS_BRIDGE_COMPILER_CLANG)
        target_compile_options(pacs_bridge_compile_options INTERFACE
            -Wno-c++98-compat
            -Wno-c++98-compat-pedantic
        )
    endif()

    # GCC-specific flags
    if(PACS_BRIDGE_COMPILER_GNU)
        target_compile_options(pacs_bridge_compile_options INTERFACE
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op
            -Wno-changes-meaning  # Allow method names that match type names
        )
    endif()

    # Clang on Linux needs libc++ for full C++23 support (std::expected)
    if(PACS_BRIDGE_COMPILER_CLANG AND UNIX AND NOT APPLE)
        target_compile_options(pacs_bridge_compile_options INTERFACE
            -stdlib=libc++
        )
        target_link_options(pacs_bridge_compile_options INTERFACE
            -stdlib=libc++
        )
    endif()

elseif(PACS_BRIDGE_COMPILER_MSVC)
    target_compile_options(pacs_bridge_compile_options INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2 /DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:/O2 /Zi /DNDEBUG>
    )

    # Disable specific warnings
    target_compile_options(pacs_bridge_compile_options INTERFACE
        /wd4100  # unreferenced formal parameter
        /wd4127  # conditional expression is constant
    )

    # Definitions for Windows
    target_compile_definitions(pacs_bridge_compile_options INTERFACE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# =============================================================================
# Platform-Specific Settings
# =============================================================================

if(WIN32)
    target_compile_definitions(pacs_bridge_compile_options INTERFACE
        PACS_BRIDGE_PLATFORM_WINDOWS
    )
    # Link against Winsock
    target_link_libraries(pacs_bridge_compile_options INTERFACE
        ws2_32
        mswsock
    )
elseif(APPLE)
    target_compile_definitions(pacs_bridge_compile_options INTERFACE
        PACS_BRIDGE_PLATFORM_MACOS
    )
elseif(UNIX)
    target_compile_definitions(pacs_bridge_compile_options INTERFACE
        PACS_BRIDGE_PLATFORM_LINUX
    )
    # pthread on Linux
    find_package(Threads REQUIRED)
    target_link_libraries(pacs_bridge_compile_options INTERFACE
        Threads::Threads
    )
endif()

# =============================================================================
# Sanitizers (Debug builds only)
# =============================================================================

option(BRIDGE_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(BRIDGE_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(BRIDGE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(BRIDGE_ENABLE_ASAN)
    if(NOT PACS_BRIDGE_COMPILER_MSVC)
        target_compile_options(pacs_bridge_compile_options INTERFACE
            -fsanitize=address -fno-omit-frame-pointer
        )
        target_link_options(pacs_bridge_compile_options INTERFACE
            -fsanitize=address
        )
    else()
        message(WARNING "AddressSanitizer requires /fsanitize=address on MSVC")
    endif()
endif()

if(BRIDGE_ENABLE_TSAN)
    if(NOT PACS_BRIDGE_COMPILER_MSVC)
        target_compile_options(pacs_bridge_compile_options INTERFACE
            -fsanitize=thread
        )
        target_link_options(pacs_bridge_compile_options INTERFACE
            -fsanitize=thread
        )
    endif()
endif()

if(BRIDGE_ENABLE_UBSAN)
    if(NOT PACS_BRIDGE_COMPILER_MSVC)
        target_compile_options(pacs_bridge_compile_options INTERFACE
            -fsanitize=undefined
        )
        target_link_options(pacs_bridge_compile_options INTERFACE
            -fsanitize=undefined
        )
    endif()
endif()

# =============================================================================
# Output Messages
# =============================================================================

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
