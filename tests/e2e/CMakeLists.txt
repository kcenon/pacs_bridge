# =============================================================================
# tests/e2e/CMakeLists.txt
# End-to-End Workflow Tests (Phase 5c - Issue #321)
# =============================================================================
#
# E2E workflow tests that validate complete business scenarios:
#   - HL7 ORM -> MWL -> MPPS -> HL7 response (IHE Scheduled Workflow)
#   - FHIR ServiceRequest -> MWL -> MPPS -> DiagnosticReport
#   - Error recovery and resilience scenarios
#
# =============================================================================

# Macro for E2E workflow tests
# Uses custom test framework (consistent with existing e2e_scenario_test)
macro(add_e2e_test TEST_NAME)
    set(_E2E_LABELS "integration;e2e;workflow;phase5c")
    set(_E2E_TIMEOUT 600)
    if(ARGC GREATER 1)
        set(_E2E_LABELS "${ARGV1}")
    endif()
    if(ARGC GREATER 2)
        set(_E2E_TIMEOUT "${ARGV2}")
    endif()

    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)

    target_link_libraries(${TEST_NAME}
        PRIVATE
            pacs_bridge
            pacs_bridge_compile_options
            GTest::gtest
            GTest::gmock_main
    )

    target_include_directories(${TEST_NAME}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../integration
            ${CMAKE_CURRENT_SOURCE_DIR}/../utils
    )

    target_compile_definitions(${TEST_NAME}
        PRIVATE
            PACS_BRIDGE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data"
    )

    add_test(
        NAME ${TEST_NAME}
        COMMAND ${TEST_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT ${_E2E_TIMEOUT}
        LABELS "${_E2E_LABELS}"
    )
endmacro()

# HL7 to MPPS E2E Workflow Tests
# Tests complete IHE Scheduled Workflow: HL7 ORM -> MWL -> MPPS -> HL7 response
add_e2e_test(hl7_to_mpps_workflow_test "integration;e2e;workflow;hl7;mpps;phase5c" 600)

# FHIR Workflow E2E Tests
# Tests FHIR-based workflow: ServiceRequest -> MWL -> MPPS -> DiagnosticReport
add_e2e_test(fhir_workflow_test "integration;e2e;workflow;fhir;emr;phase5c" 600)

# =============================================================================
# Test Summary
# =============================================================================

message(STATUS "")
message(STATUS "Phase 5c E2E Workflow Tests (Issue #321):")
message(STATUS "  - hl7_to_mpps_workflow_test (IHE Scheduled Workflow)")
message(STATUS "  - fhir_workflow_test (FHIR workflow)")
