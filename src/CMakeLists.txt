# =============================================================================
# src/CMakeLists.txt
# Module-specific build configuration for PACS Bridge
# =============================================================================
#
# This file provides additional module-specific configuration.
# The main library target is defined in the root CMakeLists.txt.
#
# Module directories:
#   - protocol/hl7   : HL7 v2.x message handling
#   - mllp           : MLLP transport layer
#   - mapping        : HL7-DICOM translation
#   - router         : Message routing engine
#   - cache          : Patient data caching
#   - config         : Configuration management
#   - security       : TLS, access control, audit logging
#   - monitoring     : Health checks, metrics
#   - performance    : Zero-copy parsing, object pools
#   - testing        : Load testing framework
#
# =============================================================================

# No additional targets needed here.
# All source files are compiled as part of the main pacs_bridge library
# defined in the root CMakeLists.txt.
#
# This file exists for:
# 1. Compliance with Issue #2 requirements (src/CMakeLists.txt)
# 2. Future module-specific configuration (e.g., conditional FHIR builds)
# 3. Potential split into multiple libraries

# =============================================================================
# Future: FHIR Gateway Module (Phase 3)
# =============================================================================

if(BRIDGE_BUILD_FHIR)
    message(STATUS "FHIR Gateway module enabled (Phase 3)")
    # FHIR sources will be added here when implemented
    # Currently placeholder for future development
endif()

# =============================================================================
# Module Documentation
# =============================================================================

# Protocol modules
#   protocol/hl7: HL7 v2.x message types, parser, builder
#                 Implements hl7_types, hl7_message, hl7_parser, hl7_builder

# Transport modules
#   mllp: Minimal Lower Layer Protocol (MLLP) for HL7 transport
#         Implements mllp_server, mllp_client with optional TLS

# Translation modules
#   mapping: Bidirectional HL7-DICOM translation
#            Implements hl7_dicom_mapper for ORM to MWL conversion

# Routing modules
#   router: Pattern-based message routing with handler chains
#           Implements message_router with wildcard/regex matching

# Caching modules
#   cache: Thread-safe LRU cache with TTL expiration
#          Implements patient_cache for demographics caching

# Configuration modules
#   config: YAML/JSON configuration loading and validation
#           Implements config_loader, bridge_config

# Security modules
#   security: Comprehensive security hardening layer
#             Implements access_control, audit_logger, rate_limiter
#             TLS support via tls_context, tls_socket

# Monitoring modules
#   monitoring: Health check endpoints for load balancer integration
#               Implements health_checker, health_server

# Performance modules
#   performance: High-performance optimization layer
#                Implements zero_copy_parser, object_pool, lockfree_queue
#                thread_pool_manager with work-stealing

# Testing modules
#   testing: Load and stress testing framework
#            Implements load_generator, load_runner, load_reporter
