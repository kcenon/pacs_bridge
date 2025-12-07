# =============================================================================
# examples/CMakeLists.txt
# Example configuration files for PACS Bridge
# =============================================================================
#
# This directory contains example configuration files:
#   - config/config.yaml           : Complete reference configuration
#   - config/config-minimal.yaml   : Minimal setup configuration
#   - config/config-production.yaml: Production-ready configuration
#
# Note: Example executables will be added in future phases.
# Current focus is on configuration examples for deployment.
#
# =============================================================================

# Install example configurations
install(DIRECTORY config/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pacs_bridge/examples/config
    FILES_MATCHING
        PATTERN "*.yaml"
        PATTERN "*.json"
)

# =============================================================================
# Future: Example Executables
# =============================================================================

# Placeholder for future example programs
# Example executables will demonstrate:
# - Basic HL7 message processing
# - MLLP server/client usage
# - Configuration loading
# - Health check integration

# add_executable(basic_bridge_example basic_bridge.cpp)
# target_link_libraries(basic_bridge_example PRIVATE pacs_bridge)
