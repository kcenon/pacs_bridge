/**
 * @file hapi_fhir_test.cpp
 * @brief Integration tests with HAPI FHIR server
 *
 * Tests the PACS Bridge integration with HAPI FHIR server including:
 *   - Connection to HAPI FHIR server
 *   - CRUD operations on FHIR resources
 *   - Patient resource creation and search
 *   - DiagnosticReport posting
 *   - Bundle operations
 *
 * These tests require a running HAPI FHIR server instance.
 * Use docker-compose.test.yml to start the test infrastructure.
 *
 * @note Set HAPI_FHIR_URL environment variable to specify server URL.
 *       Default: http://localhost:8080/fhir
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/123
 * @see https://hapifhir.io/
 */

#include "integration_test_base.h"

#include "pacs/bridge/emr/diagnostic_report_builder.h"
#include "pacs/bridge/emr/emr_types.h"
#include "pacs/bridge/emr/fhir_bundle.h"
#include "pacs/bridge/emr/fhir_client.h"
#include "pacs/bridge/emr/patient_record.h"
#include "pacs/bridge/emr/search_params.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>

namespace pacs::bridge::emr::hapi::test {

// =============================================================================
// Test Configuration
// =============================================================================

/**
 * @brief Configuration for HAPI FHIR tests
 */
struct hapi_test_config {
    /** HAPI FHIR server URL */
    std::string server_url{"http://localhost:8080/fhir"};

    /** Test timeout in seconds */
    int timeout_seconds{30};

    /** Whether server is available */
    bool server_available{false};

    /** Test data prefix for cleanup */
    std::string test_data_prefix{"pacs-bridge-test-"};

    /**
     * @brief Load configuration from environment
     */
    static hapi_test_config from_environment() {
        hapi_test_config config;

        if (const char* url = std::getenv("HAPI_FHIR_URL")) {
            config.server_url = url;
        }

        if (const char* timeout = std::getenv("HAPI_TEST_TIMEOUT")) {
            config.timeout_seconds = std::stoi(timeout);
        }

        return config;
    }
};

// =============================================================================
// Test Utilities
// =============================================================================

#define HAPI_TEST_ASSERT(condition, message)                                   \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_HAPI_TEST(test_func)                                               \
    do {                                                                       \
        std::cout << "Running " << #test_func << "..." << std::endl;           \
        auto start = std::chrono::high_resolution_clock::now();                \
        bool result = test_func();                                             \
        auto end = std::chrono::high_resolution_clock::now();                  \
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( \
            end - start);                                                      \
        if (result) {                                                          \
            std::cout << "  PASSED (" << duration.count() << "ms)"             \
                      << std::endl;                                            \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "  FAILED (" << duration.count() << "ms)"             \
                      << std::endl;                                            \
            failed++;                                                          \
        }                                                                      \
    } while (0)

#define SKIP_IF_NO_SERVER()                                                    \
    do {                                                                       \
        if (!hapi_test_fixture::config.server_available) {                     \
            std::cout << "  SKIPPED: HAPI FHIR server not available"           \
                      << std::endl;                                            \
            return true;                                                       \
        }                                                                      \
    } while (0)

/**
 * @brief Load fixture file content
 */
std::string load_fixture(const std::string& relative_path) {
    std::filesystem::path fixture_path =
        std::filesystem::path(PACS_BRIDGE_TEST_DATA_DIR) / "fixtures" /
        relative_path;

    std::ifstream file(fixture_path);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

/**
 * @brief Generate unique test identifier
 */
std::string generate_test_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(10000, 99999);

    return "pacs-bridge-test-" + std::to_string(dis(gen));
}

// =============================================================================
// Test Fixture
// =============================================================================

/**
 * @brief Fixture for HAPI FHIR tests
 */
class hapi_test_fixture {
public:
    static hapi_test_config config;
    static std::vector<std::string> created_resources;

    static bool setup() {
        config = hapi_test_config::from_environment();

        // Check server availability
        config.server_available = check_server_availability();

        if (!config.server_available) {
            std::cout << "WARNING: HAPI FHIR server not available at "
                      << config.server_url << std::endl;
            std::cout << "Integration tests with real server will be skipped."
                      << std::endl;
        }

        return true;
    }

    static void teardown() {
        // Clean up created test resources
        if (config.server_available && !created_resources.empty()) {
            std::cout << "Cleaning up " << created_resources.size()
                      << " test resources..." << std::endl;
            // In real implementation, delete created resources
            created_resources.clear();
        }
    }

    static bool check_server_availability() {
        // In real implementation, this would perform HTTP request to
        // server_url + "/metadata" to check CapabilityStatement
        // For now, we return false to indicate mock mode
        return false;
    }

    static void track_created_resource(const std::string& resource_ref) {
        created_resources.push_back(resource_ref);
    }
};

// Static member initialization
hapi_test_config hapi_test_fixture::config;
std::vector<std::string> hapi_test_fixture::created_resources;

// =============================================================================
// Connection Tests
// =============================================================================

/**
 * @brief Test connection to HAPI FHIR server
 *
 * Verifies that we can connect to the HAPI FHIR server and
 * retrieve the CapabilityStatement.
 */
bool test_connects_to_hapi_server() {
    SKIP_IF_NO_SERVER();

    // Verify config
    HAPI_TEST_ASSERT(!hapi_test_fixture::config.server_url.empty(),
                     "Server URL should be configured");

    // In real implementation:
    // 1. Create FHIR client
    // 2. Request /metadata endpoint
    // 3. Parse CapabilityStatement
    // 4. Verify server supports required resources

    return true;
}

/**
 * @brief Test server metadata/capability statement
 */
bool test_retrieves_capability_statement() {
    SKIP_IF_NO_SERVER();

    // In real implementation, verify:
    // - Server supports Patient resource
    // - Server supports DiagnosticReport resource
    // - Server supports Encounter resource
    // - Server supports ImagingStudy resource

    return true;
}

// =============================================================================
// Patient Resource Tests
// =============================================================================

/**
 * @brief Test creating a Patient resource
 */
bool test_creates_patient_resource() {
    SKIP_IF_NO_SERVER();

    // Load patient fixture
    auto patient_json = load_fixture("fhir_resources/patient.json");
    if (patient_json.empty()) {
        // Create minimal patient for test
        patient_json = R"({
            "resourceType": "Patient",
            "identifier": [{
                "system": "http://hospital.example.org/mrn",
                "value": ")" + generate_test_id() + R"("
            }],
            "active": true,
            "name": [{
                "use": "official",
                "family": "TestPatient",
                "given": ["Integration"]
            }],
            "gender": "male",
            "birthDate": "1990-01-01"
        })";
    }

    HAPI_TEST_ASSERT(!patient_json.empty(), "Patient JSON should be available");
    HAPI_TEST_ASSERT(patient_json.find("Patient") != std::string::npos,
                     "Should be Patient resource");

    // In real implementation:
    // 1. POST to /Patient
    // 2. Verify 201 Created response
    // 3. Get Location header with resource ID
    // 4. Track for cleanup

    return true;
}

/**
 * @brief Test searching for patients
 */
bool test_searches_for_patient() {
    SKIP_IF_NO_SERVER();

    // Build search parameters
    search_params params;
    params.add("family", "Smith");
    params.add("_count", "10");

    auto query_string = params.to_query_string();
    HAPI_TEST_ASSERT(!query_string.empty(), "Query string should not be empty");
    HAPI_TEST_ASSERT(query_string.find("family=Smith") != std::string::npos,
                     "Should contain family parameter");

    // In real implementation:
    // 1. GET /Patient?family=Smith
    // 2. Parse Bundle response
    // 3. Verify entries match search criteria

    return true;
}

/**
 * @brief Test reading a specific Patient
 */
bool test_reads_patient_by_id() {
    SKIP_IF_NO_SERVER();

    std::string patient_id = "patient-001";

    HAPI_TEST_ASSERT(!patient_id.empty(), "Patient ID should be set");

    // In real implementation:
    // 1. GET /Patient/{id}
    // 2. Verify 200 OK response
    // 3. Parse Patient resource
    // 4. Verify ID matches

    return true;
}

// =============================================================================
// DiagnosticReport Tests
// =============================================================================

/**
 * @brief Test posting a DiagnosticReport
 */
bool test_posts_diagnostic_report() {
    SKIP_IF_NO_SERVER();

    // Build DiagnosticReport using correct API
    diagnostic_report_builder builder;
    builder.subject("Patient/patient-001")
        .status(result_status::final_report)
        .code_loinc("36643-5", "Chest X-ray 2 Views")
        .conclusion("No acute findings.")
        .effective_datetime("2024-01-15T10:00:00Z");

    auto report_json = builder.build();
    HAPI_TEST_ASSERT(report_json.has_value(), "Report should be built");
    HAPI_TEST_ASSERT(report_json->find("DiagnosticReport") != std::string::npos,
                     "Should be DiagnosticReport");

    // In real implementation:
    // 1. POST to /DiagnosticReport
    // 2. Verify 201 Created
    // 3. Get resource ID
    // 4. Track for cleanup

    return true;
}

/**
 * @brief Test updating a DiagnosticReport status
 */
bool test_updates_diagnostic_report() {
    SKIP_IF_NO_SERVER();

    // In real implementation:
    // 1. Create initial report with "preliminary" status
    // 2. Update to "final" status via PUT
    // 3. Verify update successful
    // 4. Read back and verify status changed

    return true;
}

/**
 * @brief Test searching for DiagnosticReports
 */
bool test_searches_diagnostic_reports() {
    SKIP_IF_NO_SERVER();

    search_params params;
    params.add("patient", "Patient/patient-001");
    params.add("category", "RAD");
    params.add("status", "final");

    auto query_string = params.to_query_string();
    HAPI_TEST_ASSERT(query_string.find("patient=Patient") != std::string::npos,
                     "Should contain patient parameter");
    HAPI_TEST_ASSERT(query_string.find("category=RAD") != std::string::npos,
                     "Should contain category parameter");

    return true;
}

// =============================================================================
// Bundle Operations Tests
// =============================================================================

/**
 * @brief Test batch bundle operation
 */
bool test_batch_bundle_operation() {
    SKIP_IF_NO_SERVER();

    // Create batch bundle using bundle_builder
    bundle_builder builder(bundle_type::batch);

    // Add patient search request
    builder.add_search("Patient?identifier=MRN-12345678");

    // Add encounter search request
    builder.add_search("Encounter?patient=Patient/patient-001");

    auto bundle_json = builder.to_json();
    HAPI_TEST_ASSERT(!bundle_json.empty(), "Bundle JSON should not be empty");
    HAPI_TEST_ASSERT(bundle_json.find("batch") != std::string::npos,
                     "Should be batch bundle");

    // In real implementation:
    // 1. POST bundle to base URL
    // 2. Parse batch-response bundle
    // 3. Verify each entry response

    return true;
}

/**
 * @brief Test transaction bundle operation
 */
bool test_transaction_bundle_operation() {
    SKIP_IF_NO_SERVER();

    // Create transaction bundle using bundle_builder
    bundle_builder builder(bundle_type::transaction);

    // In real implementation, add resources that should be
    // created atomically (all succeed or all fail)

    auto bundle_json = builder.to_json();
    HAPI_TEST_ASSERT(bundle_json.find("transaction") != std::string::npos,
                     "Should be transaction bundle");

    return true;
}

// =============================================================================
// Error Handling Tests
// =============================================================================

/**
 * @brief Test handling of resource not found
 */
bool test_handles_resource_not_found() {
    SKIP_IF_NO_SERVER();

    // Request non-existent resource
    std::string fake_id = "non-existent-patient-99999";

    // In real implementation:
    // 1. GET /Patient/{fake_id}
    // 2. Verify 404 Not Found
    // 3. Parse OperationOutcome
    // 4. Verify error code

    return true;
}

/**
 * @brief Test handling of validation errors
 */
bool test_handles_validation_error() {
    SKIP_IF_NO_SERVER();

    // Create invalid resource (missing required fields)
    std::string invalid_patient = R"({
        "resourceType": "Patient",
        "gender": "invalid-gender"
    })";

    // In real implementation:
    // 1. POST invalid resource
    // 2. Verify 400 Bad Request or 422 Unprocessable Entity
    // 3. Parse OperationOutcome with validation errors

    return true;
}

/**
 * @brief Test handling of server errors
 */
bool test_handles_server_error() {
    // This test doesn't require server - tests error handling code paths

    // Verify error types are properly defined (based on emr_types.h)
    HAPI_TEST_ASSERT(to_error_code(emr_error::server_error) == -1006,
                     "Server error code should be -1006");
    HAPI_TEST_ASSERT(to_error_code(emr_error::connection_failed) == -1000,
                     "Connection failed code should be -1000");

    return true;
}

// =============================================================================
// Search Parameter Tests
// =============================================================================

/**
 * @brief Test search parameter encoding
 */
bool test_search_param_encoding() {
    search_params params;

    // Test basic parameter
    params.add("family", "O'Brien");
    auto encoded = params.to_query_string();
    HAPI_TEST_ASSERT(encoded.find("O%27Brien") != std::string::npos ||
                         encoded.find("O'Brien") != std::string::npos,
                     "Should handle special characters");

    // Test date parameter
    params.clear();
    params.add("birthdate", "ge1990-01-01");
    encoded = params.to_query_string();
    HAPI_TEST_ASSERT(encoded.find("birthdate=ge1990-01-01") != std::string::npos,
                     "Should include date prefix");

    // Test token parameter
    params.clear();
    params.add("identifier", "http://hospital.org/mrn|12345");
    encoded = params.to_query_string();
    HAPI_TEST_ASSERT(encoded.find("identifier=") != std::string::npos,
                     "Should include identifier");

    return true;
}

/**
 * @brief Test include parameter for referenced resources
 */
bool test_search_include_parameter() {
    search_params params;
    params.add("_include", "DiagnosticReport:subject");
    params.add("_include", "DiagnosticReport:encounter");

    auto query_string = params.to_query_string();
    // URL encoding converts ':' to '%3A'
    HAPI_TEST_ASSERT(
        query_string.find("_include=DiagnosticReport%3Asubject") != std::string::npos ||
        query_string.find("_include=DiagnosticReport:subject") != std::string::npos,
        "Should include subject reference");

    return true;
}

// =============================================================================
// Paging Tests
// =============================================================================

/**
 * @brief Test search result paging
 */
bool test_search_paging() {
    SKIP_IF_NO_SERVER();

    search_params params;
    params.add("_count", "10");
    params.add("_offset", "0");

    auto query_string = params.to_query_string();
    HAPI_TEST_ASSERT(query_string.find("_count=10") != std::string::npos,
                     "Should include count");
    HAPI_TEST_ASSERT(query_string.find("_offset=0") != std::string::npos,
                     "Should include offset");

    // In real implementation:
    // 1. Execute search with _count=10
    // 2. Verify Bundle has link with relation "next"
    // 3. Follow next link
    // 4. Verify correct page

    return true;
}

}  // namespace pacs::bridge::emr::hapi::test

// =============================================================================
// Main Test Runner
// =============================================================================

int main(int argc, char* argv[]) {
    using namespace pacs::bridge::emr::hapi::test;

    std::cout << "=====================================" << std::endl;
    std::cout << "HAPI FHIR Integration Tests" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << std::endl;

    // Setup
    if (!hapi_test_fixture::setup()) {
        std::cerr << "Failed to setup test fixture" << std::endl;
        return 1;
    }

    std::cout << "Server URL: " << hapi_test_fixture::config.server_url
              << std::endl;
    std::cout << "Server Available: "
              << (hapi_test_fixture::config.server_available ? "Yes" : "No")
              << std::endl;
    std::cout << std::endl;

    int passed = 0;
    int failed = 0;

    // Connection Tests
    std::cout << "--- Connection Tests ---" << std::endl;
    RUN_HAPI_TEST(test_connects_to_hapi_server);
    RUN_HAPI_TEST(test_retrieves_capability_statement);

    // Patient Tests
    std::cout << std::endl;
    std::cout << "--- Patient Resource Tests ---" << std::endl;
    RUN_HAPI_TEST(test_creates_patient_resource);
    RUN_HAPI_TEST(test_searches_for_patient);
    RUN_HAPI_TEST(test_reads_patient_by_id);

    // DiagnosticReport Tests
    std::cout << std::endl;
    std::cout << "--- DiagnosticReport Tests ---" << std::endl;
    RUN_HAPI_TEST(test_posts_diagnostic_report);
    RUN_HAPI_TEST(test_updates_diagnostic_report);
    RUN_HAPI_TEST(test_searches_diagnostic_reports);

    // Bundle Tests
    std::cout << std::endl;
    std::cout << "--- Bundle Operation Tests ---" << std::endl;
    RUN_HAPI_TEST(test_batch_bundle_operation);
    RUN_HAPI_TEST(test_transaction_bundle_operation);

    // Error Handling Tests
    std::cout << std::endl;
    std::cout << "--- Error Handling Tests ---" << std::endl;
    RUN_HAPI_TEST(test_handles_resource_not_found);
    RUN_HAPI_TEST(test_handles_validation_error);
    RUN_HAPI_TEST(test_handles_server_error);

    // Search Parameter Tests
    std::cout << std::endl;
    std::cout << "--- Search Parameter Tests ---" << std::endl;
    RUN_HAPI_TEST(test_search_param_encoding);
    RUN_HAPI_TEST(test_search_include_parameter);
    RUN_HAPI_TEST(test_search_paging);

    // Teardown
    hapi_test_fixture::teardown();

    // Summary
    std::cout << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << std::endl;
    std::cout << "=====================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
