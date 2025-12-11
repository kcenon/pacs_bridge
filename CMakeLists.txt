# =============================================================================
# PACS Bridge - CMake Build Configuration
# =============================================================================
#
# HL7-DICOM Bridge for Healthcare Integration
# https://github.com/kcenon/pacs_bridge
#
# Build options:
#   -DBRIDGE_BUILD_HL7=ON      Enable HL7 Gateway module (Phase 1)
#   -DBRIDGE_BUILD_FHIR=OFF    Enable FHIR Gateway module (Phase 3)
#   -DBRIDGE_BUILD_TESTS=ON    Build unit tests
#   -DBRIDGE_BUILD_EXAMPLES=ON Build example configurations
#   -DBRIDGE_ENABLE_TLS=ON     Enable TLS support with OpenSSL
#
# Quick start:
#   cmake -B build
#   cmake --build build
#   ctest --test-dir build
#
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# =============================================================================
# Project Definition
# =============================================================================

project(pacs_bridge
    VERSION 0.1.0
    DESCRIPTION "HL7-DICOM Bridge for Healthcare Integration"
    HOMEPAGE_URL "https://github.com/kcenon/pacs_bridge"
    LANGUAGES CXX
)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -B build")
endif()

# =============================================================================
# CMake Module Path
# =============================================================================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# =============================================================================
# Build Options
# =============================================================================

# Module options
option(BRIDGE_BUILD_HL7 "Build HL7 Gateway module (Phase 1)" ON)
option(BRIDGE_BUILD_FHIR "Build FHIR Gateway module (Phase 3)" OFF)

# Build target options
option(BRIDGE_BUILD_TESTS "Build unit tests" ON)
option(BRIDGE_BUILD_EXAMPLES "Build examples" ON)
option(BRIDGE_BUILD_BENCHMARKS "Build benchmarks" OFF)

# Feature options
option(BRIDGE_ENABLE_TLS "Enable TLS support with OpenSSL" ON)

# =============================================================================
# Compiler Settings
# =============================================================================

include(CompilerFlags)

# =============================================================================
# vcpkg Integration (if available)
# =============================================================================

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
    message(STATUS "Using vcpkg from: $ENV{VCPKG_ROOT}")
endif()

# =============================================================================
# Dependencies
# =============================================================================

include(FindPacsSystem)

# =============================================================================
# Output Directories
# =============================================================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Multi-config generators (Visual Studio, Xcode)
foreach(CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib)
endforeach()

# =============================================================================
# Main Library Target
# =============================================================================

# Collect all source files
set(PACS_BRIDGE_SOURCES "")
set(PACS_BRIDGE_HEADERS "")

# Protocol - HL7 (Phase 1)
if(BRIDGE_BUILD_HL7)
    list(APPEND PACS_BRIDGE_SOURCES
        src/protocol/hl7/hl7_types.cpp
        src/protocol/hl7/hl7_message.cpp
        src/protocol/hl7/hl7_parser.cpp
        src/protocol/hl7/hl7_builder.cpp
        src/protocol/hl7/hl7_validator.cpp
        src/protocol/hl7/adt_handler.cpp
        src/protocol/hl7/orm_handler.cpp
        src/protocol/hl7/oru_generator.cpp
        src/protocol/hl7/siu_handler.cpp
    )
    list(APPEND PACS_BRIDGE_HEADERS
        include/pacs/bridge/protocol/hl7/hl7_types.h
        include/pacs/bridge/protocol/hl7/hl7_message.h
        include/pacs/bridge/protocol/hl7/hl7_parser.h
        include/pacs/bridge/protocol/hl7/hl7_builder.h
        include/pacs/bridge/protocol/hl7/hl7_validator.h
        include/pacs/bridge/protocol/hl7/adt_handler.h
        include/pacs/bridge/protocol/hl7/orm_handler.h
        include/pacs/bridge/protocol/hl7/oru_generator.h
        include/pacs/bridge/protocol/hl7/siu_handler.h
    )
endif()

# MLLP Transport
list(APPEND PACS_BRIDGE_SOURCES
    src/mllp/mllp_server.cpp
    src/mllp/mllp_client.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/mllp/mllp_types.h
    include/pacs/bridge/mllp/mllp_server.h
    include/pacs/bridge/mllp/mllp_client.h
)

# Mapping Layer
list(APPEND PACS_BRIDGE_SOURCES
    src/mapping/hl7_dicom_mapper.cpp
    src/mapping/dicom_hl7_mapper.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/mapping/hl7_dicom_mapper.h
    include/pacs/bridge/mapping/dicom_hl7_mapper.h
)

# Router
list(APPEND PACS_BRIDGE_SOURCES
    src/router/message_router.cpp
    src/router/outbound_router.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/router/message_router.h
    include/pacs/bridge/router/outbound_router.h
)

# Queue Manager (requires SQLite)
if(PACS_BRIDGE_HAS_SQLITE)
    list(APPEND PACS_BRIDGE_SOURCES
        src/router/queue_manager.cpp
    )
    list(APPEND PACS_BRIDGE_HEADERS
        include/pacs/bridge/router/queue_manager.h
    )
endif()

# Cache
list(APPEND PACS_BRIDGE_SOURCES
    src/cache/patient_cache.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/cache/patient_cache.h
)

# Configuration
list(APPEND PACS_BRIDGE_SOURCES
    src/config/bridge_config.cpp
    src/config/config_loader.cpp
    src/config/config_manager.cpp
    src/config/admin_server.cpp
    src/config/emr_config.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/config/bridge_config.h
    include/pacs/bridge/config/config_loader.h
    include/pacs/bridge/config/config_manager.h
    include/pacs/bridge/config/admin_server.h
    include/pacs/bridge/config/emr_config.h
)

# Security
list(APPEND PACS_BRIDGE_SOURCES
    src/security/access_control.cpp
    src/security/audit_logger.cpp
    src/security/basic_auth_provider.cpp
    src/security/input_validator.cpp
    src/security/log_sanitizer.cpp
    src/security/oauth2_client.cpp
    src/security/rate_limiter.cpp
    src/security/smart_discovery.cpp
    src/security/tls_context.cpp
    src/security/tls_socket.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/security/access_control.h
    include/pacs/bridge/security/audit_logger.h
    include/pacs/bridge/security/auth_provider.h
    include/pacs/bridge/security/basic_auth_provider.h
    include/pacs/bridge/security/input_validator.h
    include/pacs/bridge/security/log_sanitizer.h
    include/pacs/bridge/security/oauth2_client.h
    include/pacs/bridge/security/oauth2_types.h
    include/pacs/bridge/security/rate_limiter.h
    include/pacs/bridge/security/smart_configuration.h
    include/pacs/bridge/security/smart_discovery.h
    include/pacs/bridge/security/tls_context.h
    include/pacs/bridge/security/tls_socket.h
    include/pacs/bridge/security/tls_types.h
)

# Monitoring
list(APPEND PACS_BRIDGE_SOURCES
    src/monitoring/health_checker.cpp
    src/monitoring/health_server.cpp
    src/monitoring/bridge_metrics.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/monitoring/health_types.h
    include/pacs/bridge/monitoring/health_checker.h
    include/pacs/bridge/monitoring/health_server.h
    include/pacs/bridge/monitoring/bridge_metrics.h
)

# Performance
list(APPEND PACS_BRIDGE_SOURCES
    src/performance/benchmark_runner.cpp
    src/performance/connection_optimizer.cpp
    src/performance/lockfree_queue.cpp
    src/performance/object_pool.cpp
    src/performance/thread_pool_manager.cpp
    src/performance/zero_copy_parser.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/performance/benchmark_runner.h
    include/pacs/bridge/performance/connection_optimizer.h
    include/pacs/bridge/performance/lockfree_queue.h
    include/pacs/bridge/performance/object_pool.h
    include/pacs/bridge/performance/performance_types.h
    include/pacs/bridge/performance/thread_pool_manager.h
    include/pacs/bridge/performance/zero_copy_parser.h
)

# Testing/Load Testing
list(APPEND PACS_BRIDGE_SOURCES
    src/testing/load_generator.cpp
    src/testing/load_reporter.cpp
    src/testing/load_runner.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/testing/load_types.h
    include/pacs/bridge/testing/load_generator.h
    include/pacs/bridge/testing/load_reporter.h
    include/pacs/bridge/testing/load_runner.h
)

# PACS Adapter (pacs_system Integration)
list(APPEND PACS_BRIDGE_SOURCES
    src/pacs_adapter/mwl_client.cpp
    src/pacs_adapter/mpps_handler.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/pacs_adapter/mwl_client.h
    include/pacs/bridge/pacs_adapter/mpps_handler.h
)

# FHIR Gateway (Phase 3)
if(BRIDGE_BUILD_FHIR)
    list(APPEND PACS_BRIDGE_SOURCES
        src/fhir/fhir_types.cpp
        src/fhir/fhir_resource.cpp
        src/fhir/operation_outcome.cpp
        src/fhir/resource_handler.cpp
        src/fhir/fhir_server.cpp
        src/fhir/patient_resource.cpp
        src/fhir/service_request_resource.cpp
        src/fhir/imaging_study_resource.cpp
        src/fhir/subscription_resource.cpp
        src/fhir/subscription_manager.cpp
        src/mapping/fhir_dicom_mapper.cpp
    )
    list(APPEND PACS_BRIDGE_HEADERS
        include/pacs/bridge/fhir/fhir_types.h
        include/pacs/bridge/fhir/fhir_resource.h
        include/pacs/bridge/fhir/operation_outcome.h
        include/pacs/bridge/fhir/resource_handler.h
        include/pacs/bridge/fhir/fhir_server.h
        include/pacs/bridge/fhir/patient_resource.h
        include/pacs/bridge/fhir/service_request_resource.h
        include/pacs/bridge/fhir/imaging_study_resource.h
        include/pacs/bridge/fhir/subscription_resource.h
        include/pacs/bridge/fhir/subscription_manager.h
        include/pacs/bridge/mapping/fhir_dicom_mapper.h
    )
endif()

# EMR Client Module (Phase 5) - FHIR R4 Client for external EMR systems
# See: https://github.com/kcenon/pacs_bridge/issues/102
# See: https://github.com/kcenon/pacs_bridge/issues/104
# See: https://github.com/kcenon/pacs_bridge/issues/105
list(APPEND PACS_BRIDGE_SOURCES
    src/emr/emr_types.cpp
    src/emr/fhir_bundle.cpp
    src/emr/http_client_adapter.cpp
    src/emr/fhir_client.cpp
    src/emr/patient_lookup.cpp
    src/emr/fhir_patient_parser.cpp
    src/emr/patient_matcher.cpp
    src/emr/result_poster.cpp
    src/emr/diagnostic_report_builder.cpp
    src/emr/result_tracker.cpp
)
list(APPEND PACS_BRIDGE_HEADERS
    include/pacs/bridge/emr/emr_types.h
    include/pacs/bridge/emr/search_params.h
    include/pacs/bridge/emr/fhir_bundle.h
    include/pacs/bridge/emr/http_client_adapter.h
    include/pacs/bridge/emr/fhir_client.h
    include/pacs/bridge/emr/patient_record.h
    include/pacs/bridge/emr/patient_lookup.h
    include/pacs/bridge/emr/patient_matcher.h
    include/pacs/bridge/emr/result_poster.h
    include/pacs/bridge/emr/diagnostic_report_builder.h
    include/pacs/bridge/emr/result_tracker.h
)

# =============================================================================
# Create Main Library
# =============================================================================

add_library(pacs_bridge ${PACS_BRIDGE_SOURCES} ${PACS_BRIDGE_HEADERS})
add_library(pacs::bridge ALIAS pacs_bridge)

target_include_directories(pacs_bridge
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pacs_bridge
    PUBLIC
        pacs_bridge_compile_options
        pacs_bridge_dependencies
)

# Module definitions
if(BRIDGE_BUILD_HL7)
    target_compile_definitions(pacs_bridge PUBLIC PACS_BRIDGE_BUILD_HL7)
endif()

if(BRIDGE_BUILD_FHIR)
    target_compile_definitions(pacs_bridge PUBLIC PACS_BRIDGE_BUILD_FHIR)
endif()

# =============================================================================
# Subdirectories
# =============================================================================

# Source subdirectory (for additional module-specific configuration)
add_subdirectory(src)

# Tests
if(BRIDGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(BRIDGE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Benchmarks
if(BRIDGE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# =============================================================================
# Installation
# =============================================================================

include(GNUInstallDirs)

# Determine which targets to install
set(PACS_BRIDGE_INSTALL_TARGETS
    pacs_bridge
    pacs_bridge_compile_options
    pacs_bridge_dependencies
)

# Add stub library to install targets if in standalone mode
if(BRIDGE_STANDALONE_BUILD AND TARGET kcenon_stubs)
    list(APPEND PACS_BRIDGE_INSTALL_TARGETS kcenon_stubs)
endif()

install(TARGETS ${PACS_BRIDGE_INSTALL_TARGETS}
    EXPORT pacs_bridge_targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT pacs_bridge_targets
    FILE pacs_bridge-targets.cmake
    NAMESPACE pacs::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pacs_bridge
)

# =============================================================================
# Package Configuration
# =============================================================================

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/pacs_bridge-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pacs_bridge-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pacs_bridge-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pacs_bridge
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/pacs_bridge-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/pacs_bridge-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pacs_bridge
)

# =============================================================================
# Build Summary
# =============================================================================

message(STATUS "")
message(STATUS "=== PACS Bridge Build Configuration ===")
message(STATUS "Version:            ${PROJECT_VERSION}")
message(STATUS "Build Type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "Modules:")
message(STATUS "  HL7 Gateway:      ${BRIDGE_BUILD_HL7}")
message(STATUS "  FHIR Gateway:     ${BRIDGE_BUILD_FHIR}")
message(STATUS "")
message(STATUS "Build Targets:")
message(STATUS "  Tests:            ${BRIDGE_BUILD_TESTS}")
message(STATUS "  Examples:         ${BRIDGE_BUILD_EXAMPLES}")
message(STATUS "  Benchmarks:       ${BRIDGE_BUILD_BENCHMARKS}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  TLS Support:      ${PACS_BRIDGE_HAS_OPENSSL}")
message(STATUS "  PACS System:      ${PACS_BRIDGE_HAS_PACS_SYSTEM}")
message(STATUS "  Monitoring:       ${PACS_BRIDGE_HAS_MONITORING_SYSTEM}")
message(STATUS "")
message(STATUS "Install Prefix:     ${CMAKE_INSTALL_PREFIX}")
message(STATUS "========================================")
message(STATUS "")
