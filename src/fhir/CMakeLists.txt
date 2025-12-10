# FHIR Gateway Module
# Provides FHIR R4 REST API for modern EHR integration.
#
# See: docs/SDS_COMPONENTS.md - Section 3: FHIR Gateway Module
# See: https://github.com/kcenon/pacs_bridge/issues/31

set(FHIR_SOURCES
    fhir_types.cpp
    fhir_resource.cpp
    operation_outcome.cpp
    resource_handler.cpp
    fhir_server.cpp
)

set(FHIR_HEADERS
    ${CMAKE_SOURCE_DIR}/include/pacs/bridge/fhir/fhir_types.h
    ${CMAKE_SOURCE_DIR}/include/pacs/bridge/fhir/fhir_resource.h
    ${CMAKE_SOURCE_DIR}/include/pacs/bridge/fhir/operation_outcome.h
    ${CMAKE_SOURCE_DIR}/include/pacs/bridge/fhir/resource_handler.h
    ${CMAKE_SOURCE_DIR}/include/pacs/bridge/fhir/fhir_server.h
)

# Export sources to parent scope for main library
set(PACS_BRIDGE_FHIR_SOURCES ${FHIR_SOURCES} PARENT_SCOPE)
set(PACS_BRIDGE_FHIR_HEADERS ${FHIR_HEADERS} PARENT_SCOPE)
