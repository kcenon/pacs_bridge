/**
 * @file patient_resource_test.cpp
 * @brief Unit tests for FHIR Patient resource functionality
 *
 * Tests cover:
 * - Patient resource creation and serialization
 * - DICOM to FHIR patient conversion
 * - Name format conversion
 * - Date format conversion
 * - Gender conversion
 * - Patient resource handler operations
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/32
 */

#include "pacs/bridge/fhir/patient_resource.h"

#include "pacs/bridge/cache/patient_cache.h"
#include "pacs/bridge/mapping/hl7_dicom_mapper.h"

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
// Administrative Gender Tests
// =============================================================================

bool test_gender_to_string() {
    TEST_ASSERT(to_string(administrative_gender::male) == "male",
                "male gender string");
    TEST_ASSERT(to_string(administrative_gender::female) == "female",
                "female gender string");
    TEST_ASSERT(to_string(administrative_gender::other) == "other",
                "other gender string");
    TEST_ASSERT(to_string(administrative_gender::unknown) == "unknown",
                "unknown gender string");
    return true;
}

bool test_gender_parsing() {
    auto male = parse_gender("male");
    TEST_ASSERT(male.has_value() && *male == administrative_gender::male,
                "parse male");

    auto female = parse_gender("FEMALE");
    TEST_ASSERT(female.has_value() && *female == administrative_gender::female,
                "parse FEMALE (uppercase)");

    auto m = parse_gender("M");
    TEST_ASSERT(m.has_value() && *m == administrative_gender::male,
                "parse M shorthand");

    auto f = parse_gender("f");
    TEST_ASSERT(f.has_value() && *f == administrative_gender::female,
                "parse f shorthand");

    auto invalid = parse_gender("invalid");
    TEST_ASSERT(!invalid.has_value(), "invalid gender returns nullopt");

    return true;
}

// =============================================================================
// Patient Resource Tests
// =============================================================================

bool test_patient_resource_creation() {
    patient_resource patient;
    patient.set_id("patient-123");

    TEST_ASSERT(patient.id() == "patient-123", "patient ID set correctly");
    TEST_ASSERT(patient.type() == resource_type::patient, "resource type is patient");
    TEST_ASSERT(patient.type_name() == "Patient", "type name is Patient");
    TEST_ASSERT(patient.validate(), "empty patient validates");

    return true;
}

bool test_patient_identifiers() {
    patient_resource patient;

    fhir_identifier mrn;
    mrn.use = "usual";
    mrn.system = "urn:oid:1.2.3.4.5";
    mrn.value = "MRN12345";
    patient.add_identifier(mrn);

    fhir_identifier ssn;
    ssn.use = "secondary";
    ssn.value = "SSN98765";
    patient.add_identifier(ssn);

    TEST_ASSERT(patient.identifiers().size() == 2, "two identifiers added");
    TEST_ASSERT(patient.identifiers()[0].value == "MRN12345", "first identifier value");
    TEST_ASSERT(patient.identifiers()[1].value == "SSN98765", "second identifier value");

    patient.clear_identifiers();
    TEST_ASSERT(patient.identifiers().empty(), "identifiers cleared");

    return true;
}

bool test_patient_names() {
    patient_resource patient;

    fhir_human_name name;
    name.use = "official";
    name.family = "Doe";
    name.given = {"John", "Andrew"};
    patient.add_name(name);

    TEST_ASSERT(patient.names().size() == 1, "one name added");
    TEST_ASSERT(patient.names()[0].family.value() == "Doe", "family name");
    TEST_ASSERT(patient.names()[0].given.size() == 2, "two given names");
    TEST_ASSERT(patient.names()[0].given[0] == "John", "first given name");
    TEST_ASSERT(patient.names()[0].given[1] == "Andrew", "second given name");

    patient.clear_names();
    TEST_ASSERT(patient.names().empty(), "names cleared");

    return true;
}

bool test_patient_demographics() {
    patient_resource patient;

    patient.set_gender(administrative_gender::male);
    TEST_ASSERT(patient.gender().has_value(), "gender is set");
    TEST_ASSERT(*patient.gender() == administrative_gender::male, "gender is male");

    patient.set_birth_date("1980-01-15");
    TEST_ASSERT(patient.birth_date().has_value(), "birth date is set");
    TEST_ASSERT(*patient.birth_date() == "1980-01-15", "birth date value");

    patient.set_active(true);
    TEST_ASSERT(patient.active().has_value(), "active is set");
    TEST_ASSERT(*patient.active() == true, "active is true");

    return true;
}

bool test_patient_json_serialization() {
    patient_resource patient;
    patient.set_id("test-patient");

    fhir_identifier mrn;
    mrn.use = "usual";
    mrn.system = "http://hospital.example.org";
    mrn.value = "12345";
    patient.add_identifier(mrn);

    fhir_human_name name;
    name.use = "official";
    name.family = "Smith";
    name.given = {"John"};
    patient.add_name(name);

    patient.set_gender(administrative_gender::male);
    patient.set_birth_date("1985-03-20");
    patient.set_active(true);

    std::string json = patient.to_json();

    TEST_ASSERT(json.find("\"resourceType\": \"Patient\"") != std::string::npos,
                "JSON contains resourceType");
    TEST_ASSERT(json.find("\"id\": \"test-patient\"") != std::string::npos,
                "JSON contains id");
    TEST_ASSERT(json.find("\"identifier\"") != std::string::npos,
                "JSON contains identifier");
    TEST_ASSERT(json.find("\"value\": \"12345\"") != std::string::npos,
                "JSON contains identifier value");
    TEST_ASSERT(json.find("\"name\"") != std::string::npos,
                "JSON contains name");
    TEST_ASSERT(json.find("\"family\": \"Smith\"") != std::string::npos,
                "JSON contains family name");
    TEST_ASSERT(json.find("\"gender\": \"male\"") != std::string::npos,
                "JSON contains gender");
    TEST_ASSERT(json.find("\"birthDate\": \"1985-03-20\"") != std::string::npos,
                "JSON contains birthDate");
    TEST_ASSERT(json.find("\"active\": true") != std::string::npos,
                "JSON contains active");

    return true;
}

// =============================================================================
// Name Format Conversion Tests
// =============================================================================

bool test_dicom_name_to_fhir() {
    // Simple name: Family^Given
    auto name1 = dicom_name_to_fhir("DOE^JOHN");
    TEST_ASSERT(name1.family.value() == "DOE", "family name parsed");
    TEST_ASSERT(name1.given.size() == 1, "one given name");
    TEST_ASSERT(name1.given[0] == "JOHN", "given name parsed");

    // Full name: Family^Given^Middle^Prefix^Suffix
    auto name2 = dicom_name_to_fhir("SMITH^JANE^MARIE^DR^MD");
    TEST_ASSERT(name2.family.value() == "SMITH", "family name");
    TEST_ASSERT(name2.given.size() == 2, "given and middle names");
    TEST_ASSERT(name2.given[0] == "JANE", "given name");
    TEST_ASSERT(name2.given[1] == "MARIE", "middle name");
    TEST_ASSERT(name2.prefix.size() == 1, "prefix");
    TEST_ASSERT(name2.prefix[0] == "DR", "prefix value");
    TEST_ASSERT(name2.suffix.size() == 1, "suffix");
    TEST_ASSERT(name2.suffix[0] == "MD", "suffix value");

    // Name with spaces in given: Family^First Second
    auto name3 = dicom_name_to_fhir("JONES^MARY ANN");
    TEST_ASSERT(name3.family.value() == "JONES", "family name");
    TEST_ASSERT(name3.given.size() == 2, "two given names from space");
    TEST_ASSERT(name3.given[0] == "MARY", "first given");
    TEST_ASSERT(name3.given[1] == "ANN", "second given");

    return true;
}

bool test_fhir_name_to_dicom() {
    fhir_human_name name;
    name.family = "DOE";
    name.given = {"JOHN", "JAMES"};
    name.prefix = {"MR"};
    name.suffix = {"JR"};

    std::string dicom = fhir_name_to_dicom(name);
    TEST_ASSERT(dicom == "DOE^JOHN^JAMES^MR^JR", "FHIR name to DICOM");

    // Simple name
    fhir_human_name simple;
    simple.family = "SMITH";
    simple.given = {"JANE"};
    std::string simple_dicom = fhir_name_to_dicom(simple);
    TEST_ASSERT(simple_dicom == "SMITH^JANE", "simple FHIR name to DICOM");

    return true;
}

// =============================================================================
// Date Format Conversion Tests
// =============================================================================

bool test_dicom_date_to_fhir() {
    TEST_ASSERT(dicom_date_to_fhir("19800115") == "1980-01-15",
                "DICOM date to FHIR");
    TEST_ASSERT(dicom_date_to_fhir("20231225") == "2023-12-25",
                "another DICOM date");
    TEST_ASSERT(dicom_date_to_fhir("invalid").empty(), "invalid date returns empty");
    TEST_ASSERT(dicom_date_to_fhir("1980").empty(), "short date returns empty");
    TEST_ASSERT(dicom_date_to_fhir("198001150").empty(), "long date returns empty");

    return true;
}

bool test_fhir_date_to_dicom() {
    TEST_ASSERT(fhir_date_to_dicom("1980-01-15") == "19800115",
                "FHIR date to DICOM");
    TEST_ASSERT(fhir_date_to_dicom("2023-12-25") == "20231225",
                "another FHIR date");
    TEST_ASSERT(fhir_date_to_dicom("invalid").empty(), "invalid date returns empty");
    TEST_ASSERT(fhir_date_to_dicom("19800115").empty(), "DICOM format returns empty");

    return true;
}

// =============================================================================
// Gender Conversion Tests
// =============================================================================

bool test_dicom_sex_to_fhir_gender() {
    TEST_ASSERT(dicom_sex_to_fhir_gender("M") == administrative_gender::male,
                "M to male");
    TEST_ASSERT(dicom_sex_to_fhir_gender("m") == administrative_gender::male,
                "m to male");
    TEST_ASSERT(dicom_sex_to_fhir_gender("F") == administrative_gender::female,
                "F to female");
    TEST_ASSERT(dicom_sex_to_fhir_gender("f") == administrative_gender::female,
                "f to female");
    TEST_ASSERT(dicom_sex_to_fhir_gender("O") == administrative_gender::other,
                "O to other");
    TEST_ASSERT(dicom_sex_to_fhir_gender("") == administrative_gender::unknown,
                "empty to unknown");
    TEST_ASSERT(dicom_sex_to_fhir_gender("X") == administrative_gender::unknown,
                "invalid to unknown");

    return true;
}

bool test_fhir_gender_to_dicom_sex() {
    TEST_ASSERT(fhir_gender_to_dicom_sex(administrative_gender::male) == "M",
                "male to M");
    TEST_ASSERT(fhir_gender_to_dicom_sex(administrative_gender::female) == "F",
                "female to F");
    TEST_ASSERT(fhir_gender_to_dicom_sex(administrative_gender::other) == "O",
                "other to O");
    TEST_ASSERT(fhir_gender_to_dicom_sex(administrative_gender::unknown).empty(),
                "unknown to empty");

    return true;
}

// =============================================================================
// DICOM to FHIR Patient Conversion Tests
// =============================================================================

bool test_dicom_to_fhir_patient() {
    mapping::dicom_patient dicom;
    dicom.patient_id = "12345";
    dicom.issuer_of_patient_id = "urn:oid:1.2.3.4.5";
    dicom.patient_name = "DOE^JOHN^JAMES";
    dicom.patient_birth_date = "19800115";
    dicom.patient_sex = "M";
    dicom.other_patient_ids = {"SSN123456"};

    auto patient = dicom_to_fhir_patient(dicom);

    TEST_ASSERT(patient != nullptr, "patient created");
    TEST_ASSERT(patient->id() == "12345", "patient ID from DICOM");

    // Check identifiers
    TEST_ASSERT(patient->identifiers().size() == 2, "two identifiers (primary + other)");
    TEST_ASSERT(patient->identifiers()[0].value == "12345", "primary identifier");
    TEST_ASSERT(patient->identifiers()[0].system.value() == "urn:oid:1.2.3.4.5",
                "identifier system");
    TEST_ASSERT(patient->identifiers()[1].value == "SSN123456", "secondary identifier");

    // Check name
    TEST_ASSERT(!patient->names().empty(), "name present");
    TEST_ASSERT(patient->names()[0].family.value() == "DOE", "family name");
    TEST_ASSERT(patient->names()[0].given[0] == "JOHN", "given name");

    // Check demographics
    TEST_ASSERT(patient->gender().value() == administrative_gender::male, "gender");
    TEST_ASSERT(patient->birth_date().value() == "1980-01-15", "birth date converted");
    TEST_ASSERT(patient->active().value() == true, "active set");

    return true;
}

bool test_dicom_to_fhir_patient_with_custom_id() {
    mapping::dicom_patient dicom;
    dicom.patient_id = "original-id";
    dicom.patient_name = "SMITH^JANE";

    auto patient = dicom_to_fhir_patient(dicom, "custom-id");

    TEST_ASSERT(patient->id() == "custom-id", "custom ID used");

    return true;
}

// =============================================================================
// Patient Resource Handler Tests
// =============================================================================

bool test_patient_handler_creation() {
    auto cache = std::make_shared<cache::patient_cache>();
    patient_resource_handler handler(cache);

    TEST_ASSERT(handler.handled_type() == resource_type::patient,
                "handled type is patient");
    TEST_ASSERT(handler.type_name() == "Patient", "type name is Patient");
    TEST_ASSERT(handler.supports_interaction(interaction_type::read),
                "supports read");
    TEST_ASSERT(handler.supports_interaction(interaction_type::search),
                "supports search");
    TEST_ASSERT(!handler.supports_interaction(interaction_type::create),
                "does not support create");
    TEST_ASSERT(!handler.supports_interaction(interaction_type::delete_resource),
                "does not support delete");

    return true;
}

bool test_patient_handler_search_params() {
    auto cache = std::make_shared<cache::patient_cache>();
    patient_resource_handler handler(cache);

    auto params = handler.supported_search_params();

    TEST_ASSERT(params.find("_id") != params.end(), "_id parameter supported");
    TEST_ASSERT(params.find("identifier") != params.end(),
                "identifier parameter supported");
    TEST_ASSERT(params.find("name") != params.end(), "name parameter supported");
    TEST_ASSERT(params.find("birthdate") != params.end(),
                "birthdate parameter supported");

    return true;
}

bool test_patient_handler_read_not_found() {
    auto cache = std::make_shared<cache::patient_cache>();
    patient_resource_handler handler(cache);

    auto result = handler.read("nonexistent-id");

    TEST_ASSERT(!is_success(result), "read nonexistent returns error");
    const auto& outcome = get_outcome(result);
    TEST_ASSERT(outcome.has_errors(), "outcome has errors");

    return true;
}

bool test_patient_handler_read() {
    auto cache = std::make_shared<cache::patient_cache>();

    // Add patient to cache
    mapping::dicom_patient dicom;
    dicom.patient_id = "test-123";
    dicom.patient_name = "TEST^PATIENT";
    dicom.patient_sex = "F";
    cache->put("test-123", dicom);

    patient_resource_handler handler(cache);
    auto result = handler.read("test-123");

    TEST_ASSERT(is_success(result), "read returns success");
    const auto& resource = get_resource(result);
    TEST_ASSERT(resource != nullptr, "resource not null");
    TEST_ASSERT(resource->type() == resource_type::patient, "resource is patient");

    return true;
}

bool test_patient_handler_search_by_id() {
    auto cache = std::make_shared<cache::patient_cache>();

    mapping::dicom_patient p1;
    p1.patient_id = "patient-1";
    p1.patient_name = "DOE^JOHN";
    cache->put("patient-1", p1);

    mapping::dicom_patient p2;
    p2.patient_id = "patient-2";
    p2.patient_name = "SMITH^JANE";
    cache->put("patient-2", p2);

    patient_resource_handler handler(cache);

    std::map<std::string, std::string> params = {{"_id", "patient-1"}};
    auto result = handler.search(params, {});

    TEST_ASSERT(is_success(result), "search returns success");
    const auto& search_result = get_resource(result);
    TEST_ASSERT(search_result.total == 1, "found one patient");
    TEST_ASSERT(search_result.entries.size() == 1, "one entry");

    return true;
}

bool test_patient_handler_search_by_name() {
    auto cache = std::make_shared<cache::patient_cache>();

    mapping::dicom_patient p1;
    p1.patient_id = "1";
    p1.patient_name = "JOHNSON^MARY";
    cache->put("1", p1);

    mapping::dicom_patient p2;
    p2.patient_id = "2";
    p2.patient_name = "SMITH^JOHN";
    cache->put("2", p2);

    mapping::dicom_patient p3;
    p3.patient_id = "3";
    p3.patient_name = "JONES^JOHNATHAN";
    cache->put("3", p3);

    patient_resource_handler handler(cache);

    // Search for "JOHN" - should match all three (JOHNSON, JOHN, JOHNATHAN)
    std::map<std::string, std::string> params = {{"name", "JOHN"}};
    auto result = handler.search(params, {});

    TEST_ASSERT(is_success(result), "search returns success");
    const auto& search_result = get_resource(result);
    TEST_ASSERT(search_result.total == 3, "found three patients with JOHN in name");

    // Search for "SMITH" - should match only one
    std::map<std::string, std::string> params2 = {{"name", "SMITH"}};
    auto result2 = handler.search(params2, {});

    TEST_ASSERT(is_success(result2), "search for SMITH returns success");
    const auto& search_result2 = get_resource(result2);
    TEST_ASSERT(search_result2.total == 1, "found one patient with SMITH in name");

    return true;
}

bool test_patient_handler_search_by_identifier() {
    auto cache = std::make_shared<cache::patient_cache>();

    mapping::dicom_patient p1;
    p1.patient_id = "MRN-001";
    p1.patient_name = "DOE^JOHN";
    cache->put("MRN-001", p1);

    patient_resource_handler handler(cache);

    std::map<std::string, std::string> params = {{"identifier", "MRN-001"}};
    auto result = handler.search(params, {});

    TEST_ASSERT(is_success(result), "search returns success");
    const auto& search_result = get_resource(result);
    TEST_ASSERT(search_result.total == 1, "found patient by identifier");

    return true;
}

bool test_patient_handler_search_pagination() {
    auto cache = std::make_shared<cache::patient_cache>();

    // Add 10 patients
    for (int i = 0; i < 10; ++i) {
        mapping::dicom_patient p;
        p.patient_id = "patient-" + std::to_string(i);
        p.patient_name = "TEST^PATIENT" + std::to_string(i);
        cache->put(p.patient_id, p);
    }

    patient_resource_handler handler(cache);

    // Search with pagination: offset 2, count 3
    std::map<std::string, std::string> params;
    pagination_params pagination;
    pagination.offset = 2;
    pagination.count = 3;

    auto result = handler.search(params, pagination);

    TEST_ASSERT(is_success(result), "search returns success");
    const auto& search_result = get_resource(result);
    TEST_ASSERT(search_result.total == 10, "total count is 10");
    TEST_ASSERT(search_result.entries.size() == 3, "returned 3 entries");

    return true;
}

bool test_patient_handler_search_empty() {
    auto cache = std::make_shared<cache::patient_cache>();
    patient_resource_handler handler(cache);

    std::map<std::string, std::string> params;
    auto result = handler.search(params, {});

    TEST_ASSERT(is_success(result), "search on empty cache returns success");
    const auto& search_result = get_resource(result);
    TEST_ASSERT(search_result.total == 0, "no patients found");
    TEST_ASSERT(search_result.entries.empty(), "entries empty");

    return true;
}

}  // namespace pacs::bridge::fhir::test

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    using namespace pacs::bridge::fhir::test;

    int passed = 0;
    int failed = 0;

    std::cout << "=== FHIR Patient Resource Tests ===" << std::endl;
    std::cout << std::endl;

    // Gender tests
    std::cout << "--- Administrative Gender Tests ---" << std::endl;
    RUN_TEST(test_gender_to_string);
    RUN_TEST(test_gender_parsing);
    std::cout << std::endl;

    // Patient resource tests
    std::cout << "--- Patient Resource Tests ---" << std::endl;
    RUN_TEST(test_patient_resource_creation);
    RUN_TEST(test_patient_identifiers);
    RUN_TEST(test_patient_names);
    RUN_TEST(test_patient_demographics);
    RUN_TEST(test_patient_json_serialization);
    std::cout << std::endl;

    // Name format tests
    std::cout << "--- Name Format Conversion Tests ---" << std::endl;
    RUN_TEST(test_dicom_name_to_fhir);
    RUN_TEST(test_fhir_name_to_dicom);
    std::cout << std::endl;

    // Date format tests
    std::cout << "--- Date Format Conversion Tests ---" << std::endl;
    RUN_TEST(test_dicom_date_to_fhir);
    RUN_TEST(test_fhir_date_to_dicom);
    std::cout << std::endl;

    // Gender conversion tests
    std::cout << "--- Gender Conversion Tests ---" << std::endl;
    RUN_TEST(test_dicom_sex_to_fhir_gender);
    RUN_TEST(test_fhir_gender_to_dicom_sex);
    std::cout << std::endl;

    // DICOM to FHIR conversion tests
    std::cout << "--- DICOM to FHIR Patient Tests ---" << std::endl;
    RUN_TEST(test_dicom_to_fhir_patient);
    RUN_TEST(test_dicom_to_fhir_patient_with_custom_id);
    std::cout << std::endl;

    // Handler tests
    std::cout << "--- Patient Handler Tests ---" << std::endl;
    RUN_TEST(test_patient_handler_creation);
    RUN_TEST(test_patient_handler_search_params);
    RUN_TEST(test_patient_handler_read_not_found);
    RUN_TEST(test_patient_handler_read);
    RUN_TEST(test_patient_handler_search_by_id);
    RUN_TEST(test_patient_handler_search_by_name);
    RUN_TEST(test_patient_handler_search_by_identifier);
    RUN_TEST(test_patient_handler_search_pagination);
    RUN_TEST(test_patient_handler_search_empty);
    std::cout << std::endl;

    // Summary
    std::cout << "=== Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed > 0 ? 1 : 0;
}
