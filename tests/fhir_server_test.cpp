/**
 * @file fhir_server_test.cpp
 * @brief Unit tests for FHIR REST server functionality
 *
 * Tests cover:
 * - HTTP type parsing (methods, content types, status codes)
 * - Resource type parsing
 * - URL routing
 * - Content negotiation
 * - OperationOutcome generation
 * - FHIR server request handling
 * - Handler registry
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/31
 */

#include "pacs/bridge/fhir/fhir_server.h"
#include "pacs/bridge/fhir/fhir_types.h"
#include "pacs/bridge/fhir/operation_outcome.h"
#include "pacs/bridge/fhir/resource_handler.h"

#include <cassert>
#include <iostream>
#include <string>

namespace pacs::bridge::fhir::test {

// =============================================================================
// Test Utilities
// =============================================================================

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_TEST(test_fn)                                                      \
    do {                                                                       \
        std::cout << "Running " << #test_fn << "... ";                         \
        if (test_fn()) {                                                       \
            std::cout << "PASSED" << std::endl;                                \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "FAILED" << std::endl;                                \
            failed++;                                                          \
        }                                                                      \
    } while (0)

// =============================================================================
// HTTP Method Tests
// =============================================================================

bool test_http_method_to_string() {
    TEST_ASSERT(to_string(http_method::get) == "GET", "GET method string");
    TEST_ASSERT(to_string(http_method::post) == "POST", "POST method string");
    TEST_ASSERT(to_string(http_method::put) == "PUT", "PUT method string");
    TEST_ASSERT(to_string(http_method::patch) == "PATCH", "PATCH method string");
    TEST_ASSERT(to_string(http_method::delete_method) == "DELETE",
                "DELETE method string");
    TEST_ASSERT(to_string(http_method::head) == "HEAD", "HEAD method string");
    TEST_ASSERT(to_string(http_method::options) == "OPTIONS",
                "OPTIONS method string");
    return true;
}

bool test_http_method_parsing() {
    auto get = parse_http_method("GET");
    TEST_ASSERT(get.has_value() && *get == http_method::get, "parse GET");

    auto post = parse_http_method("post");
    TEST_ASSERT(post.has_value() && *post == http_method::post,
                "parse post (lowercase)");

    auto del = parse_http_method("DELETE");
    TEST_ASSERT(del.has_value() && *del == http_method::delete_method,
                "parse DELETE");

    auto invalid = parse_http_method("INVALID");
    TEST_ASSERT(!invalid.has_value(), "invalid method returns nullopt");

    return true;
}

// =============================================================================
// Content Type Tests
// =============================================================================

bool test_content_type_to_mime() {
    TEST_ASSERT(to_mime_type(content_type::fhir_json) == "application/fhir+json",
                "FHIR JSON mime type");
    TEST_ASSERT(to_mime_type(content_type::fhir_xml) == "application/fhir+xml",
                "FHIR XML mime type");
    TEST_ASSERT(to_mime_type(content_type::json) == "application/json",
                "JSON mime type");
    TEST_ASSERT(to_mime_type(content_type::xml) == "application/xml",
                "XML mime type");
    return true;
}

bool test_content_type_parsing() {
    // FHIR-specific types
    TEST_ASSERT(parse_content_type("application/fhir+json") ==
                    content_type::fhir_json,
                "parse FHIR JSON");
    TEST_ASSERT(parse_content_type("application/fhir+xml") ==
                    content_type::fhir_xml,
                "parse FHIR XML");

    // Generic types
    TEST_ASSERT(parse_content_type("application/json") == content_type::json,
                "parse JSON");
    TEST_ASSERT(parse_content_type("application/xml") == content_type::xml,
                "parse XML");

    // With charset parameter
    TEST_ASSERT(parse_content_type("application/fhir+json; charset=utf-8") ==
                    content_type::fhir_json,
                "parse with charset");

    // Default for empty/wildcard
    TEST_ASSERT(parse_content_type("") == content_type::fhir_json,
                "empty defaults to FHIR JSON");
    TEST_ASSERT(parse_content_type("*/*") == content_type::fhir_json,
                "wildcard defaults to FHIR JSON");

    // Multiple types in Accept header
    TEST_ASSERT(
        parse_content_type("application/fhir+json, application/json;q=0.9") ==
            content_type::fhir_json,
        "multiple types prefers FHIR JSON");

    return true;
}

// =============================================================================
// HTTP Status Tests
// =============================================================================

bool test_http_status_reason_phrases() {
    TEST_ASSERT(get_reason_phrase(http_status::ok) == "OK", "200 OK");
    TEST_ASSERT(get_reason_phrase(http_status::created) == "Created",
                "201 Created");
    TEST_ASSERT(get_reason_phrase(http_status::not_found) == "Not Found",
                "404 Not Found");
    TEST_ASSERT(get_reason_phrase(http_status::internal_server_error) ==
                    "Internal Server Error",
                "500 Internal Server Error");
    return true;
}

// =============================================================================
// Resource Type Tests
// =============================================================================

bool test_resource_type_to_string() {
    TEST_ASSERT(to_string(resource_type::patient) == "Patient", "Patient");
    TEST_ASSERT(to_string(resource_type::service_request) == "ServiceRequest",
                "ServiceRequest");
    TEST_ASSERT(to_string(resource_type::imaging_study) == "ImagingStudy",
                "ImagingStudy");
    TEST_ASSERT(to_string(resource_type::operation_outcome) ==
                    "OperationOutcome",
                "OperationOutcome");
    TEST_ASSERT(to_string(resource_type::bundle) == "Bundle", "Bundle");
    TEST_ASSERT(to_string(resource_type::capability_statement) ==
                    "CapabilityStatement",
                "CapabilityStatement");
    return true;
}

bool test_resource_type_parsing() {
    auto patient = parse_resource_type("Patient");
    TEST_ASSERT(patient.has_value() && *patient == resource_type::patient,
                "parse Patient");

    auto service = parse_resource_type("ServiceRequest");
    TEST_ASSERT(service.has_value() &&
                    *service == resource_type::service_request,
                "parse ServiceRequest");

    auto invalid = parse_resource_type("InvalidType");
    TEST_ASSERT(!invalid.has_value(), "invalid type returns nullopt");

    return true;
}

// =============================================================================
// URL Routing Tests
// =============================================================================

bool test_route_parsing_metadata() {
    auto route = parse_fhir_route(http_method::get, "/fhir/r4/metadata", "/fhir/r4");
    TEST_ASSERT(route.valid, "metadata route is valid");
    TEST_ASSERT(route.interaction == interaction_type::capabilities,
                "metadata interaction");
    return true;
}

bool test_route_parsing_read() {
    auto route =
        parse_fhir_route(http_method::get, "/fhir/r4/Patient/123", "/fhir/r4");
    TEST_ASSERT(route.valid, "read route is valid");
    TEST_ASSERT(route.interaction == interaction_type::read, "read interaction");
    TEST_ASSERT(route.type == resource_type::patient, "resource type is Patient");
    TEST_ASSERT(route.resource_id.has_value() && *route.resource_id == "123",
                "resource id is 123");
    return true;
}

bool test_route_parsing_search() {
    auto route =
        parse_fhir_route(http_method::get, "/fhir/r4/Patient", "/fhir/r4");
    TEST_ASSERT(route.valid, "search route is valid");
    TEST_ASSERT(route.interaction == interaction_type::search,
                "search interaction");
    TEST_ASSERT(route.type == resource_type::patient, "resource type is Patient");
    TEST_ASSERT(!route.resource_id.has_value(), "no resource id");
    return true;
}

bool test_route_parsing_create() {
    auto route =
        parse_fhir_route(http_method::post, "/fhir/r4/Patient", "/fhir/r4");
    TEST_ASSERT(route.valid, "create route is valid");
    TEST_ASSERT(route.interaction == interaction_type::create,
                "create interaction");
    return true;
}

bool test_route_parsing_update() {
    auto route =
        parse_fhir_route(http_method::put, "/fhir/r4/Patient/123", "/fhir/r4");
    TEST_ASSERT(route.valid, "update route is valid");
    TEST_ASSERT(route.interaction == interaction_type::update,
                "update interaction");
    TEST_ASSERT(route.resource_id.has_value() && *route.resource_id == "123",
                "resource id");
    return true;
}

bool test_route_parsing_delete() {
    auto route = parse_fhir_route(http_method::delete_method,
                                  "/fhir/r4/Patient/123", "/fhir/r4");
    TEST_ASSERT(route.valid, "delete route is valid");
    TEST_ASSERT(route.interaction == interaction_type::delete_resource,
                "delete interaction");
    return true;
}

bool test_route_parsing_vread() {
    auto route = parse_fhir_route(http_method::get,
                                  "/fhir/r4/Patient/123/_history/1", "/fhir/r4");
    TEST_ASSERT(route.valid, "vread route is valid");
    TEST_ASSERT(route.interaction == interaction_type::vread,
                "vread interaction");
    TEST_ASSERT(route.resource_id.has_value() && *route.resource_id == "123",
                "resource id");
    TEST_ASSERT(route.version_id.has_value() && *route.version_id == "1",
                "version id");
    return true;
}

// =============================================================================
// Operation Outcome Tests
// =============================================================================

bool test_operation_outcome_not_found() {
    auto outcome = operation_outcome::not_found("Patient", "123");
    TEST_ASSERT(outcome.has_issues(), "has issues");
    TEST_ASSERT(outcome.has_errors(), "has errors");

    const auto& issues = outcome.issues();
    TEST_ASSERT(issues.size() == 1, "single issue");
    TEST_ASSERT(issues[0].code == issue_type::not_found, "not-found code");

    auto json = outcome.to_json();
    TEST_ASSERT(json.find("OperationOutcome") != std::string::npos,
                "JSON contains resourceType");
    TEST_ASSERT(json.find("not-found") != std::string::npos,
                "JSON contains error code");

    return true;
}

bool test_operation_outcome_bad_request() {
    auto outcome = operation_outcome::bad_request("Invalid parameter");
    TEST_ASSERT(outcome.has_errors(), "has errors");

    auto status = outcome_to_http_status(outcome);
    TEST_ASSERT(status == http_status::bad_request, "maps to 400");

    return true;
}

bool test_operation_outcome_validation_error() {
    auto outcome = operation_outcome::validation_error(
        "Required field missing", {"Patient.name"});

    const auto& issues = outcome.issues();
    TEST_ASSERT(issues.size() == 1, "single issue");
    TEST_ASSERT(!issues[0].expression.empty(), "has expression path");
    TEST_ASSERT(issues[0].expression[0] == "Patient.name", "path is correct");

    return true;
}

bool test_operation_outcome_http_status_mapping() {
    TEST_ASSERT(
        outcome_to_http_status(operation_outcome::not_found("X", "1")) ==
            http_status::not_found,
        "not found -> 404");
    TEST_ASSERT(
        outcome_to_http_status(operation_outcome::bad_request("x")) ==
            http_status::bad_request,
        "bad request -> 400");
    TEST_ASSERT(
        outcome_to_http_status(operation_outcome::internal_error("x")) ==
            http_status::internal_server_error,
        "internal error -> 500");
    TEST_ASSERT(
        outcome_to_http_status(operation_outcome::conflict("x")) ==
            http_status::conflict,
        "conflict -> 409");

    return true;
}

// =============================================================================
// Handler Registry Tests
// =============================================================================

// Mock handler for testing
class mock_patient_handler : public resource_handler {
public:
    [[nodiscard]] resource_type handled_type() const noexcept override {
        return resource_type::patient;
    }

    [[nodiscard]] std::string_view type_name() const noexcept override {
        return "Patient";
    }

    [[nodiscard]] resource_result<std::unique_ptr<fhir_resource>>
    read(const std::string& /*id*/) override {
        // Return not found for testing
        return resource_not_found("test-id");
    }

    [[nodiscard]] std::vector<interaction_type>
    supported_interactions() const override {
        return {interaction_type::read, interaction_type::search};
    }

    [[nodiscard]] std::map<std::string, std::string>
    supported_search_params() const override {
        return {{"identifier", "Patient identifier"},
                {"name", "Patient name"}};
    }
};

bool test_handler_registry_registration() {
    handler_registry registry;

    auto handler = std::make_shared<mock_patient_handler>();
    TEST_ASSERT(registry.register_handler(handler), "registration succeeds");
    TEST_ASSERT(!registry.register_handler(handler),
                "duplicate registration fails");

    return true;
}

bool test_handler_registry_lookup() {
    handler_registry registry;

    auto handler = std::make_shared<mock_patient_handler>();
    registry.register_handler(handler);

    auto found = registry.get_handler(resource_type::patient);
    TEST_ASSERT(found != nullptr, "handler found by type");
    TEST_ASSERT(found->type_name() == "Patient", "correct handler");

    auto found_by_name = registry.get_handler("Patient");
    TEST_ASSERT(found_by_name != nullptr, "handler found by name");

    auto not_found = registry.get_handler(resource_type::imaging_study);
    TEST_ASSERT(not_found == nullptr, "unregistered type returns nullptr");

    return true;
}

bool test_handler_supports_interaction() {
    mock_patient_handler handler;

    TEST_ASSERT(handler.supports_interaction(interaction_type::read),
                "supports read");
    TEST_ASSERT(handler.supports_interaction(interaction_type::search),
                "supports search");
    TEST_ASSERT(!handler.supports_interaction(interaction_type::create),
                "doesn't support create");

    return true;
}

// =============================================================================
// FHIR Server Tests
// =============================================================================

bool test_fhir_server_lifecycle() {
    fhir_server_config config;
    config.port = 8090;
    config.base_path = "/fhir/r4";

    fhir_server server(config);

    TEST_ASSERT(!server.is_running(), "server not running initially");
    TEST_ASSERT(server.start().is_ok(), "server starts successfully");
    TEST_ASSERT(server.is_running(), "server is running");
    server.stop();
    TEST_ASSERT(!server.is_running(), "server stopped");

    return true;
}

bool test_fhir_server_config() {
    fhir_server_config config;
    config.port = 8090;
    config.base_path = "/fhir/r4";
    config.fhir_version = "4.0.1";
    config.default_page_size = 20;

    fhir_server server(config);

    TEST_ASSERT(server.config().port == 8090, "port configured");
    TEST_ASSERT(server.config().base_path == "/fhir/r4", "base path configured");
    TEST_ASSERT(server.config().fhir_version == "4.0.1", "FHIR version configured");

    return true;
}

bool test_fhir_server_metadata_request() {
    fhir_server_config config;
    config.base_path = "/fhir/r4";

    fhir_server server(config);
    server.register_handler(std::make_shared<mock_patient_handler>());
    server.start();

    http_request request;
    request.method = http_method::get;
    request.path = "/fhir/r4/metadata";

    auto response = server.handle_request(request);
    TEST_ASSERT(response.status == http_status::ok, "200 OK");
    TEST_ASSERT(response.body.find("CapabilityStatement") != std::string::npos,
                "body contains CapabilityStatement");
    TEST_ASSERT(response.body.find("Patient") != std::string::npos,
                "body contains registered Patient handler");

    server.stop();
    return true;
}

bool test_fhir_server_not_found_resource_type() {
    fhir_server_config config;
    config.base_path = "/fhir/r4";

    fhir_server server(config);
    server.start();

    http_request request;
    request.method = http_method::get;
    request.path = "/fhir/r4/InvalidType";

    auto response = server.handle_request(request);
    TEST_ASSERT(response.status == http_status::bad_request, "400 Bad Request");
    TEST_ASSERT(response.body.find("OperationOutcome") != std::string::npos,
                "error is OperationOutcome");

    server.stop();
    return true;
}

bool test_fhir_server_no_handler() {
    fhir_server_config config;
    config.base_path = "/fhir/r4";

    fhir_server server(config);
    server.start();

    http_request request;
    request.method = http_method::get;
    request.path = "/fhir/r4/Patient/123";

    auto response = server.handle_request(request);
    TEST_ASSERT(response.status == http_status::bad_request,
                "400 when no handler");

    server.stop();
    return true;
}

bool test_fhir_server_statistics() {
    fhir_server_config config;
    config.base_path = "/fhir/r4";

    fhir_server server(config);
    server.start();

    // Make a request
    http_request request;
    request.method = http_method::get;
    request.path = "/fhir/r4/metadata";
    server.handle_request(request);

    auto stats = server.get_statistics();
    TEST_ASSERT(stats.total_requests == 1, "total requests incremented");

    server.stop();
    return true;
}

// =============================================================================
// Pagination Tests
// =============================================================================

bool test_pagination_parsing() {
    fhir_server_config config;
    config.default_page_size = 20;
    config.max_page_size = 100;

    std::map<std::string, std::string> params;
    params["_count"] = "50";
    params["_offset"] = "10";

    auto pagination = parse_pagination(params, config);
    TEST_ASSERT(pagination.count == 50, "count parsed");
    TEST_ASSERT(pagination.offset == 10, "offset parsed");

    // Test max limit
    params["_count"] = "200";
    pagination = parse_pagination(params, config);
    TEST_ASSERT(pagination.count == 100, "count limited to max");

    // Test defaults
    std::map<std::string, std::string> empty_params;
    pagination = parse_pagination(empty_params, config);
    TEST_ASSERT(pagination.count == 20, "default count");
    TEST_ASSERT(pagination.offset == 0, "default offset");

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

}  // namespace pacs::bridge::fhir::test

int main() {
    using namespace pacs::bridge::fhir::test;

    int passed = 0;
    int failed = 0;

    std::cout << "\n=== FHIR Server Test Suite ===\n\n";

    // HTTP Method Tests
    std::cout << "--- HTTP Method Tests ---\n";
    RUN_TEST(test_http_method_to_string);
    RUN_TEST(test_http_method_parsing);

    // Content Type Tests
    std::cout << "\n--- Content Type Tests ---\n";
    RUN_TEST(test_content_type_to_mime);
    RUN_TEST(test_content_type_parsing);

    // HTTP Status Tests
    std::cout << "\n--- HTTP Status Tests ---\n";
    RUN_TEST(test_http_status_reason_phrases);

    // Resource Type Tests
    std::cout << "\n--- Resource Type Tests ---\n";
    RUN_TEST(test_resource_type_to_string);
    RUN_TEST(test_resource_type_parsing);

    // URL Routing Tests
    std::cout << "\n--- URL Routing Tests ---\n";
    RUN_TEST(test_route_parsing_metadata);
    RUN_TEST(test_route_parsing_read);
    RUN_TEST(test_route_parsing_search);
    RUN_TEST(test_route_parsing_create);
    RUN_TEST(test_route_parsing_update);
    RUN_TEST(test_route_parsing_delete);
    RUN_TEST(test_route_parsing_vread);

    // Operation Outcome Tests
    std::cout << "\n--- Operation Outcome Tests ---\n";
    RUN_TEST(test_operation_outcome_not_found);
    RUN_TEST(test_operation_outcome_bad_request);
    RUN_TEST(test_operation_outcome_validation_error);
    RUN_TEST(test_operation_outcome_http_status_mapping);

    // Handler Registry Tests
    std::cout << "\n--- Handler Registry Tests ---\n";
    RUN_TEST(test_handler_registry_registration);
    RUN_TEST(test_handler_registry_lookup);
    RUN_TEST(test_handler_supports_interaction);

    // FHIR Server Tests
    std::cout << "\n--- FHIR Server Tests ---\n";
    RUN_TEST(test_fhir_server_lifecycle);
    RUN_TEST(test_fhir_server_config);
    RUN_TEST(test_fhir_server_metadata_request);
    RUN_TEST(test_fhir_server_not_found_resource_type);
    RUN_TEST(test_fhir_server_no_handler);
    RUN_TEST(test_fhir_server_statistics);

    // Pagination Tests
    std::cout << "\n--- Pagination Tests ---\n";
    RUN_TEST(test_pagination_parsing);

    // Summary
    std::cout << "\n=== Test Summary ===\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";
    std::cout << "Total:  " << (passed + failed) << "\n\n";

    return failed > 0 ? 1 : 0;
}
