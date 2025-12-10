# =============================================================================
# tests/CMakeLists.txt
# Test build configuration for PACS Bridge
# =============================================================================
#
# Test executables:
#   - hl7_test        : HL7 protocol tests (30 tests)
#   - mapping_test    : HL7-DICOM mapper tests (22 tests)
#   - router_test     : Message router tests (38 tests)
#   - cache_test      : Patient cache tests (36 tests)
#   - mllp_test       : MLLP server/client tests
#   - config_test     : Configuration loader tests
#   - security_test   : Security module tests
#   - tls_test        : TLS functionality tests
#   - health_check_test : Health check endpoint tests
#   - performance_test  : Performance optimization tests
#   - load_test       : Load testing framework tests
#
# Usage:
#   cmake -B build -DBRIDGE_BUILD_TESTS=ON
#   cmake --build build
#   ctest --test-dir build
#
# Test Frameworks:
#   - Google Test (gtest) for unit tests
#   - Google Mock (gmock) for mocking dependencies
#
# Code Coverage:
#   cmake -B build -DBRIDGE_BUILD_TESTS=ON -DBRIDGE_ENABLE_COVERAGE=ON
#   cmake --build build
#   ctest --test-dir build
#   # Generate coverage report with lcov/gcovr
#
# =============================================================================

# =============================================================================
# Google Test Setup
# =============================================================================

if(NOT PACS_BRIDGE_HAS_GTEST)
    message(WARNING "Google Test not available. Tests will be built without gtest integration.")
endif()

# Test data directory
set(PACS_BRIDGE_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

# Test utilities include directory
set(PACS_BRIDGE_TEST_UTILS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/utils")

# =============================================================================
# Code Coverage Support
# =============================================================================

option(BRIDGE_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

if(BRIDGE_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Code coverage enabled")
        set(COVERAGE_FLAGS "--coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${COVERAGE_FLAGS}")
    else()
        message(WARNING "Code coverage is only supported with GCC or Clang")
    endif()
endif()

# =============================================================================
# Test Executable Macros
# =============================================================================

# Macro for tests that use Google Test framework
# Uses gtest_discover_tests for automatic test discovery
# Arguments:
#   TEST_NAME - Name of the test executable
#   Additional args (ARGN) - Labels for the test (e.g., "unit;hl7;phase1")
macro(add_gtest_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)

    target_link_libraries(${TEST_NAME}
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
    )

    # Link Google Test
    if(PACS_BRIDGE_HAS_GTEST)
        if(TARGET GTest::gtest_main)
            target_link_libraries(${TEST_NAME} PRIVATE GTest::gtest_main GTest::gmock)
        elseif(TARGET gtest_main)
            target_link_libraries(${TEST_NAME} PRIVATE gtest_main gmock)
        endif()
    endif()

    target_include_directories(${TEST_NAME}
        PRIVATE
            ${PACS_BRIDGE_TEST_UTILS_DIR}
    )

    target_compile_definitions(${TEST_NAME}
        PRIVATE
            PACS_BRIDGE_TEST_DATA_DIR="${PACS_BRIDGE_TEST_DATA_DIR}"
    )

    # Parse optional labels from additional arguments
    set(_TEST_LABELS "unit")
    if(ARGN)
        set(_TEST_LABELS "${ARGN}")
    endif()

    # Parse timeout from labels or use default
    set(_TEST_TIMEOUT 60)
    if(_TEST_LABELS MATCHES "performance")
        set(_TEST_TIMEOUT 120)
    elseif(_TEST_LABELS MATCHES "load")
        set(_TEST_TIMEOUT 180)
    endif()

    if(PACS_BRIDGE_HAS_GTEST)
        # Use gtest_discover_tests for better CTest integration
        include(GoogleTest)
        gtest_discover_tests(${TEST_NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            PROPERTIES
                TIMEOUT ${_TEST_TIMEOUT}
                LABELS "${_TEST_LABELS}"
        )
    else()
        # Fallback if GTest not available
        add_test(
            NAME ${TEST_NAME}
            COMMAND ${TEST_NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(${TEST_NAME} PROPERTIES
            TIMEOUT ${_TEST_TIMEOUT}
            LABELS "${_TEST_LABELS}"
        )
    endif()
endmacro()

# Macro for tests that use custom (non-GTest) test framework
# Uses add_test directly since gtest_discover_tests won't work
# Arguments:
#   TEST_NAME - Name of the test executable
#   Additional args (ARGN) - Labels for the test (e.g., "unit;hl7;phase1")
macro(add_bridge_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)

    target_link_libraries(${TEST_NAME}
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
    )

    target_include_directories(${TEST_NAME}
        PRIVATE
            ${PACS_BRIDGE_TEST_UTILS_DIR}
    )

    target_compile_definitions(${TEST_NAME}
        PRIVATE
            PACS_BRIDGE_TEST_DATA_DIR="${PACS_BRIDGE_TEST_DATA_DIR}"
    )

    # Parse optional labels from additional arguments
    set(_TEST_LABELS "unit")
    if(ARGN)
        set(_TEST_LABELS "${ARGN}")
    endif()

    # Parse timeout from labels or use default
    set(_TEST_TIMEOUT 60)
    if(_TEST_LABELS MATCHES "performance")
        set(_TEST_TIMEOUT 120)
    elseif(_TEST_LABELS MATCHES "load")
        set(_TEST_TIMEOUT 180)
    endif()

    # Register as a single test (custom framework, no gtest discovery)
    add_test(
        NAME ${TEST_NAME}
        COMMAND ${TEST_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT ${_TEST_TIMEOUT}
        LABELS "${_TEST_LABELS}"
    )
endmacro()

# =============================================================================
# Phase 1: HL7 Gateway Tests
# =============================================================================

if(BRIDGE_BUILD_HL7)
    # HL7 Protocol Tests (uses GTest framework)
    add_gtest_test(hl7_test "unit;hl7;phase1")

    # ADT Message Handler Tests (Issue #14) - custom framework
    add_bridge_test(adt_handler_test "unit;hl7;adt;phase1")

    # ORM Message Handler Tests (Issue #15) - custom framework
    add_bridge_test(orm_handler_test "unit;hl7;orm;phase1")

    # HL7-DICOM Mapping Tests - custom framework
    add_bridge_test(mapping_test "unit;mapping;phase1")

    # DICOM-HL7 Mapping Tests (MPPS to ORM - Issue #24) - custom framework
    add_bridge_test(dicom_hl7_mapper_test "unit;mapping;phase2")

    # Message Router Tests - custom framework
    add_bridge_test(router_test "unit;router;phase1")

    # Outbound Router Tests (Issue #28) - GTest framework
    add_gtest_test(outbound_router_test "unit;router;phase2")

    # Queue Manager Tests (Issue #27) - GTest framework (requires SQLite)
    if(PACS_BRIDGE_HAS_SQLITE)
        add_gtest_test(queue_manager_test "unit;router;queue;phase2")
    else()
        message(STATUS "Skipping queue_manager_test: SQLite not available")
    endif()

    # Patient Cache Tests - custom framework
    add_bridge_test(cache_test "unit;cache;phase1")
endif()

# =============================================================================
# Transport Layer Tests
# =============================================================================

# MLLP Server/Client Tests
add_bridge_test(mllp_test "unit;mllp;transport")

# =============================================================================
# Configuration Tests
# =============================================================================

add_bridge_test(config_test "unit;config")

# =============================================================================
# Security Tests
# =============================================================================

add_bridge_test(security_test "unit;security")

# TLS Tests (conditional on OpenSSL)
if(PACS_BRIDGE_HAS_OPENSSL)
    add_bridge_test(tls_test "unit;security;tls")
else()
    message(STATUS "Skipping tls_test: OpenSSL not available")
endif()

# =============================================================================
# Monitoring Tests
# =============================================================================

add_bridge_test(health_check_test "unit;monitoring")

# =============================================================================
# Performance Tests
# =============================================================================

add_bridge_test(performance_test "unit;performance")

# =============================================================================
# Load Testing Framework Tests
# =============================================================================

add_bridge_test(load_test "unit;load")

# =============================================================================
# PACS Adapter Tests (pacs_system Integration)
# =============================================================================

# MWL Client Tests (Phase 1 - Issue #17)
add_bridge_test(mwl_client_test "unit;pacs_adapter;phase1")

# MPPS Handler Tests (Phase 2 - Issue #23)
add_bridge_test(mpps_handler_test "unit;pacs_adapter;mpps;phase2")

# =============================================================================
# Ecosystem Integration Test (Issue #4 - Dependency Setup Verification)
# =============================================================================

add_bridge_test(ecosystem_integration_test "integration;ecosystem;phase0")

# =============================================================================
# Phase 2 Integration Tests (Issue #29 - MPPS & Bidirectional)
# =============================================================================

# Macro for integration tests (longer timeout, different labels)
# Arguments:
#   TEST_NAME - Name of the test executable
#   TEST_LABELS (optional) - Labels for the test (default: "integration;phase2")
#   TEST_TIMEOUT (optional) - Timeout in seconds (default: 300)
macro(add_integration_test TEST_NAME)
    # Parse optional arguments
    set(_INT_TEST_LABELS "integration;phase2")
    set(_INT_TEST_TIMEOUT 300)
    if(ARGC GREATER 1)
        set(_INT_TEST_LABELS "${ARGV1}")
    endif()
    if(ARGC GREATER 2)
        set(_INT_TEST_TIMEOUT "${ARGV2}")
    endif()

    add_executable(${TEST_NAME} integration/${TEST_NAME}.cpp)

    target_link_libraries(${TEST_NAME}
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
    )

    target_include_directories(${TEST_NAME}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/integration
    )

    target_compile_definitions(${TEST_NAME}
        PRIVATE
            PACS_BRIDGE_TEST_DATA_DIR="${PACS_BRIDGE_TEST_DATA_DIR}"
    )

    add_test(
        NAME ${TEST_NAME}
        COMMAND ${TEST_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Integration tests have longer timeout
    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT ${_INT_TEST_TIMEOUT}
        LABELS "${_INT_TEST_LABELS}"
    )
endmacro()

# MPPS Integration Tests (N-CREATE/N-SET flows)
add_integration_test(mpps_integration_test "integration;mpps;phase2")

# Queue Persistence Tests (message recovery) - requires SQLite
if(PACS_BRIDGE_HAS_SQLITE)
    add_integration_test(queue_persistence_test "integration;queue;phase2")
else()
    message(STATUS "Skipping queue_persistence_test: SQLite not available")
endif()

# Failover Routing Tests (RIS failover)
add_integration_test(failover_test "integration;failover;phase2")

# Stress Tests (high volume)
add_integration_test(stress_test "integration;stress;phase2" 600)

# =============================================================================
# Test Summary
# =============================================================================

message(STATUS "")
message(STATUS "=== Test Configuration ===")
message(STATUS "Test data directory: ${PACS_BRIDGE_TEST_DATA_DIR}")
message(STATUS "")
message(STATUS "Tests enabled:")
if(BRIDGE_BUILD_HL7)
    message(STATUS "  - hl7_test")
    message(STATUS "  - adt_handler_test")
    message(STATUS "  - orm_handler_test")
    message(STATUS "  - mapping_test")
    message(STATUS "  - router_test")
    message(STATUS "  - outbound_router_test")
    if(PACS_BRIDGE_HAS_SQLITE)
        message(STATUS "  - queue_manager_test")
    endif()
    message(STATUS "  - cache_test")
endif()
message(STATUS "  - mllp_test")
message(STATUS "  - config_test")
message(STATUS "  - security_test")
if(PACS_BRIDGE_HAS_OPENSSL)
    message(STATUS "  - tls_test")
endif()
message(STATUS "  - health_check_test")
message(STATUS "  - performance_test")
message(STATUS "  - load_test")
message(STATUS "  - mwl_client_test")
message(STATUS "  - mpps_handler_test")
message(STATUS "  - ecosystem_integration_test")
message(STATUS "")
message(STATUS "Phase 2 Integration Tests:")
message(STATUS "  - mpps_integration_test")
if(PACS_BRIDGE_HAS_SQLITE)
    message(STATUS "  - queue_persistence_test")
endif()
message(STATUS "  - failover_test")
message(STATUS "  - stress_test")
message(STATUS "==========================")
message(STATUS "")
