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
# =============================================================================

# Test data directory
set(PACS_BRIDGE_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

# =============================================================================
# Test Executable Macro
# =============================================================================

macro(add_bridge_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)

    target_link_libraries(${TEST_NAME}
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
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

    # Set test properties
    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 60
        LABELS "unit"
    )
endmacro()

# =============================================================================
# Phase 1: HL7 Gateway Tests
# =============================================================================

if(BRIDGE_BUILD_HL7)
    # HL7 Protocol Tests
    add_bridge_test(hl7_test)
    set_tests_properties(hl7_test PROPERTIES
        LABELS "unit;hl7;phase1"
    )

    # HL7-DICOM Mapping Tests
    add_bridge_test(mapping_test)
    set_tests_properties(mapping_test PROPERTIES
        LABELS "unit;mapping;phase1"
    )

    # Message Router Tests
    add_bridge_test(router_test)
    set_tests_properties(router_test PROPERTIES
        LABELS "unit;router;phase1"
    )

    # Patient Cache Tests
    add_bridge_test(cache_test)
    set_tests_properties(cache_test PROPERTIES
        LABELS "unit;cache;phase1"
    )
endif()

# =============================================================================
# Transport Layer Tests
# =============================================================================

# MLLP Server/Client Tests
add_bridge_test(mllp_test)
set_tests_properties(mllp_test PROPERTIES
    LABELS "unit;mllp;transport"
)

# =============================================================================
# Configuration Tests
# =============================================================================

add_bridge_test(config_test)
set_tests_properties(config_test PROPERTIES
    LABELS "unit;config"
)

# =============================================================================
# Security Tests
# =============================================================================

add_bridge_test(security_test)
set_tests_properties(security_test PROPERTIES
    LABELS "unit;security"
)

# TLS Tests (conditional on OpenSSL)
if(PACS_BRIDGE_HAS_OPENSSL)
    add_bridge_test(tls_test)
    set_tests_properties(tls_test PROPERTIES
        LABELS "unit;security;tls"
    )
else()
    message(STATUS "Skipping tls_test: OpenSSL not available")
endif()

# =============================================================================
# Monitoring Tests
# =============================================================================

add_bridge_test(health_check_test)
set_tests_properties(health_check_test PROPERTIES
    LABELS "unit;monitoring"
)

# =============================================================================
# Performance Tests
# =============================================================================

add_bridge_test(performance_test)
set_tests_properties(performance_test PROPERTIES
    LABELS "unit;performance"
    TIMEOUT 120  # Performance tests may take longer
)

# =============================================================================
# Load Testing Framework Tests
# =============================================================================

add_bridge_test(load_test)
set_tests_properties(load_test PROPERTIES
    LABELS "unit;load"
    TIMEOUT 180  # Load tests may take longer
)

# =============================================================================
# PACS Adapter Tests (Phase 1 - pacs_system Integration)
# =============================================================================

add_bridge_test(mwl_client_test)
set_tests_properties(mwl_client_test PROPERTIES
    LABELS "unit;pacs_adapter;phase1"
)

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
    message(STATUS "  - mapping_test")
    message(STATUS "  - router_test")
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
message(STATUS "==========================")
message(STATUS "")
