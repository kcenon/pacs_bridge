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
#   - bridge_metrics_test : Bridge metrics collector tests (Issue #40)
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

    # HL7 Extended Tests (Issue #159) - additional parsing, encoding, ACK tests
    add_gtest_test(hl7_extended_test "unit;hl7;encoding;phase1")

    # HL7 Encoding ISO Conversion Tests (Issue #145) - ISO-8859-1, UTF-8, escape sequences
    add_gtest_test(hl7_encoding_iso_conversion_test "unit;hl7;encoding;i18n;phase1")

    # HL7 Validation Edge Cases Tests (Issue #145) - boundary conditions, malformed inputs
    add_gtest_test(hl7_validation_edge_cases_test "unit;hl7;validation;phase1")

    # HL7 Malformed Segment Recovery Tests (Issue #145) - error recovery, partial parsing
    add_gtest_test(hl7_malformed_segment_recovery_test "unit;hl7;recovery;phase1")

    # ORU Generator Tests (Issue #25) - GTest framework
    add_gtest_test(oru_generator_test "unit;hl7;oru;phase2")

    # ADT Message Handler Tests (Issue #14) - custom framework
    add_bridge_test(adt_handler_test "unit;hl7;adt;phase1")

    # ORM Message Handler Tests (Issue #15) - custom framework
    add_bridge_test(orm_handler_test "unit;hl7;orm;phase1")

    # SIU Message Handler Tests (Issue #26) - GTest framework
    add_gtest_test(siu_handler_test "unit;hl7;siu;phase2")

    # MDM Message Handler Tests (Issue #145) - Medical Document Management
    add_gtest_test(mdm_handler_test "unit;hl7;mdm;phase2")

    # BAR Message Handler Tests (Issue #145) - Billing Account Record
    add_gtest_test(bar_handler_test "unit;hl7;bar;phase2")

    # RDE Message Handler Tests (Issue #145) - Pharmacy Order
    add_gtest_test(rde_handler_test "unit;hl7;rde;phase2")

    # QRY Message Handler Tests (Issue #145) - Query Handler
    add_gtest_test(qry_handler_test "unit;hl7;qry;phase2")

    # MFN Message Handler Tests (Issue #145) - Master File Notification
    add_gtest_test(mfn_handler_test "unit;hl7;mfn;phase2")

    # PEX Message Handler Tests (Issue #145) - Product Experience (Adverse Events)
    add_gtest_test(pex_handler_test "unit;hl7;pex;phase2")

    # HL7-DICOM Mapping Tests - custom framework
    add_bridge_test(mapping_test "unit;mapping;phase1")

    # DICOM-HL7 Mapping Tests (MPPS to ORM - Issue #24) - custom framework
    add_bridge_test(dicom_hl7_mapper_test "unit;mapping;phase2")

    # Extended Mapper Tests (Issue #160) - Patient, Study, Order mapping
    add_bridge_test(mapper_extended_test "unit;mapping;patient;study;order")

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
add_bridge_test(config_manager_test "unit;config;hot-reload")
add_bridge_test(emr_config_test "unit;config;emr;phase5")

# =============================================================================
# Security Tests
# =============================================================================

add_bridge_test(security_test "unit;security")

# OAuth2 and Smart-on-FHIR Tests (Issue #103)
add_bridge_test(oauth2_test "unit;security;oauth2;emr")

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

# Bridge Metrics Tests (Issue #40, #90)
# These tests work with the internal implementation regardless of monitoring_system availability
add_gtest_test(bridge_metrics_test "unit;monitoring;metrics")

# Monitoring Metrics Completeness Tests (Issue #145) - counter, gauge, histogram validation
add_gtest_test(monitoring_metrics_completeness_test "unit;monitoring;metrics;completeness")

# Prometheus Endpoint Tests (Issue #88)
# Tests HTTP server, /metrics endpoint, and health endpoints via TCP
add_bridge_test(prometheus_endpoint_test "unit;monitoring;prometheus;phase4")

# =============================================================================
# Distributed Tracing Tests (Issue #144)
# =============================================================================

# Trace Manager Tests
# Tests trace context, span creation, W3C traceparent format
add_gtest_test(trace_manager_test "unit;tracing;phase4")

# Monitoring Trace Validation Tests (Issue #145) - W3C traceparent, span attributes
add_gtest_test(monitoring_trace_validation_test "unit;tracing;validation;phase4")

# =============================================================================
# Performance Tests
# =============================================================================

add_bridge_test(performance_test "unit;performance")

# =============================================================================
# Load Testing Framework Tests
# =============================================================================

add_bridge_test(load_test "unit;load")

# =============================================================================
# High Volume Stress Tests (Issue #145)
# =============================================================================

# Stress High Volume Message Tests - parse throughput, memory stability
add_gtest_test(stress_high_volume_message_test "unit;stress;volume;performance" 180)

# =============================================================================
# Concurrency and Thread Safety Tests (Issue #145)
# =============================================================================

# Concurrency Thread Safety Tests - multi-threaded parsing, shared resources
add_gtest_test(concurrency_thread_safety_test "unit;concurrency;thread-safety")

# =============================================================================
# Performance Benchmarks and Memory Safety Tests (Issue #163)
# =============================================================================

# Benchmark Suite Tests - Message processing, concurrent connections, throughput
add_bridge_test(benchmark_suite_test "unit;benchmark;performance")

# Memory Safety Tests - Memory leak detection and stability
add_bridge_test(memory_safety_test "unit;memory;performance")

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
            GTest::gtest
            GTest::gmock_main
    )

    target_include_directories(${TEST_NAME}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/integration
            ${CMAKE_CURRENT_SOURCE_DIR}/utils
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

# Multi-PACS Failover Tests (Issue #145) - routing strategies, cascading failover
add_integration_test(multi_pacs_failover_test "integration;failover;multi-pacs;phase2")

# Backwards Compatibility Tests (Issue #145) - HL7 v2.3, v2.4, v2.5 interoperability
add_integration_test(backwards_compatibility_test "integration;compatibility;hl7-version;phase2")

# Disaster Recovery Tests (Issue #162) - network failures, message loss, retry logic
if(PACS_BRIDGE_HAS_SQLITE)
    add_integration_test(disaster_recovery_test "integration;disaster-recovery;resilience;phase2" 600)
else()
    message(STATUS "Skipping disaster_recovery_test: SQLite not available (required for queue_manager)")
endif()

# Stress Tests (high volume)
add_integration_test(stress_test "integration;stress;phase2" 600)

# MLLP Connection Tests (Issue #161) - setup/teardown, timeout, reconnection
add_integration_test(mllp_connection_test "integration;mllp;connection;phase2")

# PACS Worklist Tests (Issue #161) - worklist updates, status management
add_integration_test(pacs_worklist_test "integration;pacs;worklist;phase2")

# End-to-End Scenario Tests (Issue #161) - complete workflow scenarios
add_integration_test(e2e_scenario_test "integration;e2e;scenario;phase2" 600)

# Monitoring Integration Tests (Issue #90)
add_integration_test(monitoring_integration_test "integration;monitoring;metrics;phase4" 180)

# =============================================================================
# Messaging Pattern Tests (Issue #146, #142)
# =============================================================================

# HL7 Events Tests (Issue #142)
# Tests Event Bus integration, event types, and publisher/subscriber utilities
# Only available when building with kcenon ecosystem dependencies (requires common_system)
if(NOT BRIDGE_STANDALONE_BUILD)
    add_executable(hl7_events_test messaging/hl7_events_test.cpp)
    target_link_libraries(hl7_events_test
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
    )
    if(PACS_BRIDGE_HAS_GTEST)
        if(TARGET GTest::gtest_main)
            target_link_libraries(hl7_events_test PRIVATE GTest::gtest_main GTest::gmock)
        elseif(TARGET gtest_main)
            target_link_libraries(hl7_events_test PRIVATE gtest_main gmock)
        endif()
    endif()
    target_include_directories(hl7_events_test
        PRIVATE
            ${PACS_BRIDGE_TEST_UTILS_DIR}
    )
    target_compile_definitions(hl7_events_test
        PRIVATE
            PACS_BRIDGE_TEST_DATA_DIR="${PACS_BRIDGE_TEST_DATA_DIR}"
    )
    if(PACS_BRIDGE_HAS_GTEST)
        include(GoogleTest)
        gtest_discover_tests(hl7_events_test
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            PROPERTIES
                TIMEOUT 120
                LABELS "unit;messaging;events;phase4"
        )
    else()
        add_test(
            NAME hl7_events_test
            COMMAND hl7_events_test
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(hl7_events_test PROPERTIES
            TIMEOUT 120
            LABELS "unit;messaging;events;phase4"
        )
    endif()
else()
    message(STATUS "Skipping hl7_events_test: requires common_system Event Bus (BRIDGE_STANDALONE_BUILD=OFF)")
endif()

# Messaging Pattern Integration Tests (Issue #146)
# Tests Pub/Sub, Request/Reply, Pipeline, and Backend selection
add_executable(messaging_pattern_test messaging/messaging_pattern_test.cpp)
target_link_libraries(messaging_pattern_test
    PRIVATE
        pacs_bridge
        pacs_bridge_compile_options
)
if(PACS_BRIDGE_HAS_GTEST)
    if(TARGET GTest::gtest_main)
        target_link_libraries(messaging_pattern_test PRIVATE GTest::gtest_main GTest::gmock)
    elseif(TARGET gtest_main)
        target_link_libraries(messaging_pattern_test PRIVATE gtest_main gmock)
    endif()
endif()
target_include_directories(messaging_pattern_test
    PRIVATE
        ${PACS_BRIDGE_TEST_UTILS_DIR}
)
target_compile_definitions(messaging_pattern_test
    PRIVATE
        PACS_BRIDGE_TEST_DATA_DIR="${PACS_BRIDGE_TEST_DATA_DIR}"
)
if(PACS_BRIDGE_HAS_GTEST)
    include(GoogleTest)
    gtest_discover_tests(messaging_pattern_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PROPERTIES
            TIMEOUT 120
            LABELS "unit;messaging;phase4"
    )
else()
    add_test(
        NAME messaging_pattern_test
        COMMAND messaging_pattern_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(messaging_pattern_test PROPERTIES
        TIMEOUT 120
        LABELS "unit;messaging;phase4"
    )
endif()

# =============================================================================
# Phase 3: FHIR Gateway Tests (Issue #31)
# =============================================================================

if(BRIDGE_BUILD_FHIR)
    # FHIR Server Tests (Phase 3 - Issue #31)
    add_bridge_test(fhir_server_test "unit;fhir;phase3")

    # Patient Resource Tests (Phase 3 - Issue #32)
    add_bridge_test(patient_resource_test "unit;fhir;patient;phase3")

    # ServiceRequest Resource Tests (Phase 3 - Issue #33)
    add_bridge_test(service_request_resource_test "unit;fhir;servicerequest;phase3")

    # ImagingStudy Resource Tests (Phase 3 - Issue #34)
    add_bridge_test(imaging_study_resource_test "unit;fhir;imagingstudy;phase3")

    # FHIR-DICOM Mapper Tests (Phase 3 - Issue #35)
    add_gtest_test(fhir_dicom_mapper_test "unit;fhir;mapping;phase3")

    # Subscription Tests (Phase 3 - Issue #36)
    add_bridge_test(subscription_test "unit;fhir;subscription;phase3")
endif()

# =============================================================================
# Phase 5: EMR Client Tests (Issue #102, #104, #105, #106, #107, #121)
# =============================================================================

# FHIR R4 Client Tests (Issue #102)
add_gtest_test(fhir_client_test "unit;emr;fhir;phase5")

# Patient Lookup Tests (Issue #104)
add_gtest_test(patient_lookup_test "unit;emr;patient;phase5")

# Result Poster Tests (Issue #105)
add_gtest_test(result_poster_test "unit;emr;result;phase5")

# Encounter Context Tests (Issue #106, #120)
add_gtest_test(encounter_context_test "unit;emr;encounter;phase5")

# EMR Adapter Tests (Issue #107, #121)
add_gtest_test(emr_adapter_test "unit;emr;adapter;phase5")

# =============================================================================
# Phase 5: EMR E2E Integration Tests (Issue #123)
# =============================================================================

# EMR E2E Integration Tests (full workflow tests)
add_integration_test(emr_e2e_test "integration;emr;e2e;phase5" 300)

# HAPI FHIR Integration Tests (requires HAPI FHIR server)
add_integration_test(hapi_fhir_test "integration;emr;hapi;phase5" 300)

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
    message(STATUS "  - hl7_extended_test")
    message(STATUS "  - hl7_encoding_iso_conversion_test (Issue #145)")
    message(STATUS "  - hl7_validation_edge_cases_test (Issue #145)")
    message(STATUS "  - hl7_malformed_segment_recovery_test (Issue #145)")
    message(STATUS "  - oru_generator_test")
    message(STATUS "  - adt_handler_test")
    message(STATUS "  - orm_handler_test")
    message(STATUS "  - siu_handler_test")
    message(STATUS "  - mdm_handler_test (Issue #145)")
    message(STATUS "  - bar_handler_test (Issue #145)")
    message(STATUS "  - rde_handler_test (Issue #145)")
    message(STATUS "  - qry_handler_test (Issue #145)")
    message(STATUS "  - mfn_handler_test (Issue #145)")
    message(STATUS "  - pex_handler_test (Issue #145)")
    message(STATUS "  - mapping_test")
    message(STATUS "  - mapper_extended_test")
    message(STATUS "  - router_test")
    message(STATUS "  - outbound_router_test")
    if(PACS_BRIDGE_HAS_SQLITE)
        message(STATUS "  - queue_manager_test")
    endif()
    message(STATUS "  - cache_test")
endif()
message(STATUS "  - mllp_test")
message(STATUS "  - config_test")
message(STATUS "  - config_manager_test")
message(STATUS "  - emr_config_test")
message(STATUS "  - security_test")
message(STATUS "  - oauth2_test")
if(PACS_BRIDGE_HAS_OPENSSL)
    message(STATUS "  - tls_test")
endif()
message(STATUS "  - health_check_test")
message(STATUS "  - bridge_metrics_test")
message(STATUS "  - monitoring_metrics_completeness_test (Issue #145)")
message(STATUS "  - trace_manager_test")
message(STATUS "  - monitoring_trace_validation_test (Issue #145)")
message(STATUS "  - performance_test")
message(STATUS "  - load_test")
message(STATUS "  - stress_high_volume_message_test (Issue #145)")
message(STATUS "  - concurrency_thread_safety_test (Issue #145)")
message(STATUS "  - benchmark_suite_test (Issue #163)")
message(STATUS "  - memory_safety_test (Issue #163)")
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
message(STATUS "  - multi_pacs_failover_test (Issue #145)")
message(STATUS "  - backwards_compatibility_test (Issue #145)")
if(PACS_BRIDGE_HAS_SQLITE)
    message(STATUS "  - disaster_recovery_test (Issue #162)")
endif()
message(STATUS "  - stress_test")
message(STATUS "  - mllp_connection_test (Issue #161)")
message(STATUS "  - pacs_worklist_test (Issue #161)")
message(STATUS "  - e2e_scenario_test (Issue #161)")
message(STATUS "")
message(STATUS "Phase 4 Integration Tests:")
message(STATUS "  - monitoring_integration_test")
if(BRIDGE_BUILD_FHIR)
    message(STATUS "")
    message(STATUS "Phase 3 FHIR Tests:")
    message(STATUS "  - fhir_server_test")
    message(STATUS "  - patient_resource_test")
    message(STATUS "  - service_request_resource_test")
    message(STATUS "  - imaging_study_resource_test")
    message(STATUS "  - fhir_dicom_mapper_test")
    message(STATUS "  - subscription_test")
endif()
message(STATUS "")
message(STATUS "Phase 5 EMR Client Tests:")
message(STATUS "  - fhir_client_test")
message(STATUS "  - patient_lookup_test")
message(STATUS "  - result_poster_test")
message(STATUS "  - encounter_context_test")
message(STATUS "  - emr_adapter_test")
message(STATUS "")
message(STATUS "Phase 5 EMR Integration Tests:")
message(STATUS "  - emr_e2e_test")
message(STATUS "  - hapi_fhir_test")
message(STATUS "==========================")
message(STATUS "")
