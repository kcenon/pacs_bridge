# =============================================================================
# tests/mllp/CMakeLists.txt
# Test configuration for MLLP network adapter
# =============================================================================
#
# Test executables:
#   - mllp_network_adapter_test : Unit tests for adapter interface
#   - bsd_adapter_test           : Integration tests for BSD socket adapter
#
# @see https://github.com/kcenon/pacs_bridge/issues/315
# =============================================================================

# =============================================================================
# Unit Tests
# =============================================================================

# Test: MLLP Network Adapter Interface
add_executable(mllp_network_adapter_test mllp_network_adapter_test.cpp)

target_link_libraries(mllp_network_adapter_test
    PRIVATE
        pacs_bridge
        pacs_bridge_compile_options
)

if(PACS_BRIDGE_HAS_GTEST)
    if(TARGET GTest::gtest_main)
        target_link_libraries(mllp_network_adapter_test PRIVATE GTest::gtest_main GTest::gmock)
    elseif(TARGET gtest_main)
        target_link_libraries(mllp_network_adapter_test PRIVATE gtest_main gmock)
    endif()
endif()

# Discover tests for CTest
if(PACS_BRIDGE_HAS_GTEST)
    gtest_discover_tests(mllp_network_adapter_test
        PROPERTIES
            LABELS "unit;mllp;phase2"
    )
endif()

# =============================================================================
# Integration Tests
# =============================================================================

# Test: BSD Socket Adapter
# Note: BSD adapter integration tests use real network sockets which can hang
# in coverage builds due to instrumentation overhead. Skip for coverage.
if(NOT BRIDGE_ENABLE_COVERAGE)
    add_executable(bsd_adapter_test bsd_adapter_test.cpp)

    target_include_directories(bsd_adapter_test
        PRIVATE
            ${PROJECT_SOURCE_DIR}
    )

    target_link_libraries(bsd_adapter_test
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
    )

    if(PACS_BRIDGE_HAS_GTEST)
        if(TARGET GTest::gtest_main)
            target_link_libraries(bsd_adapter_test PRIVATE GTest::gtest_main GTest::gmock)
        elseif(TARGET gtest_main)
            target_link_libraries(bsd_adapter_test PRIVATE gtest_main gmock)
        endif()
    endif()

    # Platform-specific socket libraries
    if(WIN32)
        target_link_libraries(bsd_adapter_test PRIVATE ws2_32)
    endif()

    # Discover tests for CTest
    if(PACS_BRIDGE_HAS_GTEST)
        gtest_discover_tests(bsd_adapter_test
            PROPERTIES
                LABELS "integration;mllp;network;phase2"
                TIMEOUT 30
        )
    endif()
else()
    message(STATUS "Skipping bsd_adapter_test in coverage build (network I/O tests)")
endif()

# =============================================================================
# TLS Integration Tests
# =============================================================================

# Test: TLS Adapter (requires OpenSSL)
# Note: TLS adapter tests are skipped in coverage builds and when OpenSSL is not available
if(NOT BRIDGE_ENABLE_COVERAGE AND PACS_BRIDGE_HAS_OPENSSL)
    add_executable(tls_adapter_test tls_adapter_test.cpp)

    target_include_directories(tls_adapter_test
        PRIVATE
            ${PROJECT_SOURCE_DIR}
    )

    target_link_libraries(tls_adapter_test
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
    )

    if(PACS_BRIDGE_HAS_GTEST)
        if(TARGET GTest::gtest_main)
            target_link_libraries(tls_adapter_test PRIVATE GTest::gtest_main GTest::gmock)
        elseif(TARGET gtest_main)
            target_link_libraries(tls_adapter_test PRIVATE gtest_main gmock)
        endif()
    endif()

    # Platform-specific socket libraries
    if(WIN32)
        target_link_libraries(tls_adapter_test PRIVATE ws2_32)
    endif()

    # Discover tests for CTest
    if(PACS_BRIDGE_HAS_GTEST)
        gtest_discover_tests(tls_adapter_test
            PROPERTIES
                LABELS "integration;mllp;tls;security;phase2"
                TIMEOUT 60
        )
    endif()
else()
    if(BRIDGE_ENABLE_COVERAGE)
        message(STATUS "Skipping tls_adapter_test in coverage build (network I/O with TLS)")
    elseif(NOT PACS_BRIDGE_HAS_OPENSSL)
        message(STATUS "Skipping tls_adapter_test (OpenSSL not available)")
    endif()
endif()

# =============================================================================
# Performance Tests (Issue #317)
# =============================================================================

# Test: MLLP Performance Benchmarks (throughput, latency)
# Note: Performance tests are skipped in coverage builds (timing interference)
if(NOT BRIDGE_ENABLE_COVERAGE)
    add_executable(mllp_performance_test performance_test.cpp)

    target_include_directories(mllp_performance_test
        PRIVATE
            ${PROJECT_SOURCE_DIR}
    )

    target_link_libraries(mllp_performance_test
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
    )

    if(PACS_BRIDGE_HAS_GTEST)
        if(TARGET GTest::gtest_main)
            target_link_libraries(mllp_performance_test PRIVATE GTest::gtest_main GTest::gmock)
        elseif(TARGET gtest_main)
            target_link_libraries(mllp_performance_test PRIVATE gtest_main gmock)
        endif()
    endif()

    # Platform-specific socket libraries
    if(WIN32)
        target_link_libraries(mllp_performance_test PRIVATE ws2_32)
    endif()

    # Discover tests for CTest
    if(PACS_BRIDGE_HAS_GTEST)
        gtest_discover_tests(mllp_performance_test
            PROPERTIES
                LABELS "performance;mllp;benchmark;phase2"
                TIMEOUT 120
        )
    endif()
else()
    message(STATUS "Skipping mllp_performance_test in coverage build (timing interference)")
endif()

# =============================================================================
# Memory Leak Tests (Issue #317)
# =============================================================================

# Test: MLLP Memory Leak Detection
# Note: Memory leak tests are skipped in coverage builds
if(NOT BRIDGE_ENABLE_COVERAGE)
    add_executable(mllp_memory_leak_test memory_leak_test.cpp)

    target_include_directories(mllp_memory_leak_test
        PRIVATE
            ${PROJECT_SOURCE_DIR}
    )

    target_link_libraries(mllp_memory_leak_test
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
    )

    if(PACS_BRIDGE_HAS_GTEST)
        if(TARGET GTest::gtest_main)
            target_link_libraries(mllp_memory_leak_test PRIVATE GTest::gtest_main GTest::gmock)
        elseif(TARGET gtest_main)
            target_link_libraries(mllp_memory_leak_test PRIVATE gtest_main gmock)
        endif()
    endif()

    # Platform-specific socket libraries
    if(WIN32)
        target_link_libraries(mllp_memory_leak_test PRIVATE ws2_32 psapi)
    endif()

    # Discover tests for CTest
    if(PACS_BRIDGE_HAS_GTEST)
        gtest_discover_tests(mllp_memory_leak_test
            PROPERTIES
                LABELS "memory;mllp;leak-detection;phase2"
                TIMEOUT 180
        )
    endif()
else()
    message(STATUS "Skipping mllp_memory_leak_test in coverage build (memory measurement interference)")
endif()

# =============================================================================
# Test Data and Fixtures
# =============================================================================

# Copy test certificates to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
