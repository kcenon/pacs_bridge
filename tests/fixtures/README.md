# Test Fixtures

This directory contains test fixture files for EMR integration testing.

## Directory Structure

```
fixtures/
├── fhir_resources/         # FHIR R4 resource examples
│   ├── patient.json        # Complete Patient resource
│   ├── patient_bundle.json # Patient search result bundle
│   ├── diagnostic_report.json # DiagnosticReport for radiology results
│   ├── encounter.json      # Encounter with radiology context
│   ├── service_request.json # Imaging order (ServiceRequest)
│   └── imaging_study.json  # ImagingStudy with DICOM references
│
├── mock_responses/         # Mock EMR server responses
│   ├── oauth_token.json    # OAuth2 token response
│   ├── smart_configuration.json # Smart-on-FHIR discovery document
│   ├── operation_outcome_error.json # FHIR error response
│   └── empty_bundle.json   # Empty search result bundle
│
└── README.md               # This file
```

## FHIR Resources

### patient.json
A comprehensive Patient resource following US Core profile with:
- Multiple identifiers (MRN, SSN)
- Official and nickname names
- Telecom contacts (phone, email)
- Address information
- Marital status and language preferences
- General practitioner and managing organization references

### patient_bundle.json
A search result Bundle containing multiple Patient resources, demonstrating:
- Bundle pagination (self, next links)
- Search scores for result ranking
- Multiple patient matches for disambiguation testing

### diagnostic_report.json
A radiology DiagnosticReport following US Core profile with:
- Multiple identifiers (accession, report ID)
- Link to ServiceRequest (basedOn)
- Category codes (RAD, LP29684-5)
- Procedure code (LOINC, RadLex)
- Performer and results interpreter references
- Imaging study reference
- Conclusion text and codes
- Presented forms (text and PDF)

### encounter.json
A radiology outpatient Encounter following US Core profile with:
- Visit and encounter identifiers
- Status history tracking
- Ambulatory class with radiology service type
- Participant information (attending, performer)
- Location references
- Reason codes (ICD-10, SNOMED)

### service_request.json
An imaging ServiceRequest following US Core profile with:
- Placer and filler identifiers
- Procedure codes (LOINC, RadLex)
- Encounter and patient references
- Requester and performer information
- Reason codes and supporting information
- Patient instructions

### imaging_study.json
A complete ImagingStudy following US Core profile with:
- DICOM Study Instance UID
- Modality coding (DX)
- Series and instance details
- WADO-RS endpoint reference
- Procedure codes and body site
- Performer information

## Mock Responses

### oauth_token.json
OAuth2 token response for testing authentication:
- Access token (JWT format)
- Token type and expiration
- Refresh token
- Granted scopes

### smart_configuration.json
Smart-on-FHIR well-known configuration for testing OAuth2 discovery:
- Authorization and token endpoints
- Supported scopes and grant types
- PKCE support (S256)
- Smart-on-FHIR capabilities

### operation_outcome_error.json
FHIR OperationOutcome for error handling tests:
- Error severity and code
- Detailed error information
- Diagnostic messages
- Location expressions

### empty_bundle.json
Empty search result Bundle for testing "not found" scenarios:
- Zero total count
- Empty entry array
- Self link with query parameters

## Usage in Tests

### Loading Fixtures in C++

```cpp
#include <fstream>
#include <nlohmann/json.hpp>

// Load fixture from test data directory
std::string load_fixture(const std::string& path) {
    std::ifstream file(PACS_BRIDGE_TEST_DATA_DIR + path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Example usage in test
TEST_F(FhirClientTest, ParsesPatientResource) {
    auto json_str = load_fixture("/fixtures/fhir_resources/patient.json");
    auto patient = parse_patient(json_str);
    EXPECT_EQ(patient.id, "patient-001");
}
```

### Using in Integration Tests

```cpp
class EmrIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Load mock responses for HTTP mock server
        mock_server_.add_response(
            "/Patient/patient-001",
            load_fixture("/fixtures/fhir_resources/patient.json")
        );
    }
};
```

## Updating Fixtures

When adding new fixtures:

1. Follow FHIR R4 specification for resource structure
2. Use realistic but fictional data (no real PHI)
3. Include relevant US Core profile URLs where applicable
4. Add comprehensive comments in complex resources
5. Update this README with documentation
6. Test that fixtures can be parsed by the FHIR client

## References

- [FHIR R4 Specification](https://hl7.org/fhir/R4/)
- [US Core Implementation Guide](https://hl7.org/fhir/us/core/)
- [Smart-on-FHIR Specification](https://docs.smarthealthit.org/)
- [DICOM to FHIR Mapping](https://www.hl7.org/fhir/imagingstudy.html)
