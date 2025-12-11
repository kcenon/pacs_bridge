# EMR Module - FHIR R4 Client
#
# Provides FHIR R4 HTTP client functionality for EMR integration.
# See: https://github.com/kcenon/pacs_bridge/issues/102
# See: https://github.com/kcenon/pacs_bridge/issues/104
# See: https://github.com/kcenon/pacs_bridge/issues/105

add_library(pacs_bridge_emr STATIC
    emr_types.cpp
    fhir_bundle.cpp
    http_client_adapter.cpp
    fhir_client.cpp
    patient_lookup.cpp
    fhir_patient_parser.cpp
    patient_matcher.cpp
    result_poster.cpp
    diagnostic_report_builder.cpp
    result_tracker.cpp
)

target_include_directories(pacs_bridge_emr PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(pacs_bridge_emr PUBLIC
    pacs_bridge_security
)

# Require C++23 for std::expected
target_compile_features(pacs_bridge_emr PUBLIC cxx_std_23)

# Alias for cleaner target names
add_library(pacs::bridge::emr ALIAS pacs_bridge_emr)
