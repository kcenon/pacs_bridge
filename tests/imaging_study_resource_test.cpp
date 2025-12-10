/**
 * @file imaging_study_resource_test.cpp
 * @brief Unit tests for FHIR ImagingStudy resource functionality
 *
 * Tests cover:
 * - ImagingStudy status parsing
 * - ImagingStudy resource creation and serialization
 * - ImagingStudy JSON parsing
 * - ImagingStudy handler read/search operations
 * - Study storage operations
 * - Search by patient/identifier/status
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/34
 */

#include "pacs/bridge/fhir/imaging_study_resource.h"
#include "pacs/bridge/fhir/fhir_types.h"
#include "pacs/bridge/fhir/operation_outcome.h"
#include "pacs/bridge/fhir/resource_handler.h"
#include "pacs/bridge/mapping/fhir_dicom_mapper.h"

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
// ImagingStudy Status Tests
// =============================================================================

bool test_imaging_study_status_to_string() {
    TEST_ASSERT(to_string(imaging_study_status::registered) == "registered",
                "registered status string");
    TEST_ASSERT(to_string(imaging_study_status::available) == "available",
                "available status string");
    TEST_ASSERT(to_string(imaging_study_status::cancelled) == "cancelled",
                "cancelled status string");
    TEST_ASSERT(to_string(imaging_study_status::entered_in_error) ==
                    "entered-in-error",
                "entered-in-error status string");
    TEST_ASSERT(to_string(imaging_study_status::unknown) == "unknown",
                "unknown status string");
    return true;
}

bool test_imaging_study_status_parsing() {
    auto registered = parse_imaging_study_status("registered");
    TEST_ASSERT(registered.has_value() &&
                    *registered == imaging_study_status::registered,
                "parse registered");

    auto available = parse_imaging_study_status("available");
    TEST_ASSERT(available.has_value() &&
                    *available == imaging_study_status::available,
                "parse available");

    auto cancelled = parse_imaging_study_status("cancelled");
    TEST_ASSERT(cancelled.has_value() &&
                    *cancelled == imaging_study_status::cancelled,
                "parse cancelled");

    auto entered_error = parse_imaging_study_status("entered-in-error");
    TEST_ASSERT(entered_error.has_value() &&
                    *entered_error == imaging_study_status::entered_in_error,
                "parse entered-in-error");

    auto available_upper = parse_imaging_study_status("AVAILABLE");
    TEST_ASSERT(available_upper.has_value() &&
                    *available_upper == imaging_study_status::available,
                "parse AVAILABLE (uppercase)");

    auto invalid = parse_imaging_study_status("invalid-status");
    TEST_ASSERT(!invalid.has_value(), "invalid status returns nullopt");

    return true;
}

// =============================================================================
// ImagingStudy Resource Tests
// =============================================================================

bool test_imaging_study_resource_creation() {
    imaging_study_resource study;

    TEST_ASSERT(study.type() == resource_type::imaging_study,
                "resource type is imaging_study");
    TEST_ASSERT(study.type_name() == "ImagingStudy",
                "type name is ImagingStudy");

    // Set ID
    study.set_id("study-123");
    TEST_ASSERT(study.id() == "study-123", "id set correctly");

    // Default status is available
    TEST_ASSERT(study.status() == imaging_study_status::available,
                "default status is available");

    // Set status
    study.set_status(imaging_study_status::registered);
    TEST_ASSERT(study.status() == imaging_study_status::registered,
                "status set correctly");

    return true;
}

bool test_imaging_study_identifiers() {
    imaging_study_resource study;

    // Add identifiers
    imaging_study_identifier uid_ident;
    uid_ident.system = "urn:dicom:uid";
    uid_ident.value = "1.2.3.4.5.6.7.8.9";
    study.add_identifier(uid_ident);

    imaging_study_identifier accession_ident;
    accession_ident.system = "http://hospital.local/accession";
    accession_ident.value = "ACC123456";
    study.add_identifier(accession_ident);

    TEST_ASSERT(study.identifiers().size() == 2, "two identifiers added");
    TEST_ASSERT(study.identifiers()[0].value == "1.2.3.4.5.6.7.8.9",
                "first identifier is Study Instance UID");
    TEST_ASSERT(study.identifiers()[1].value == "ACC123456",
                "second identifier is accession number");

    // Clear identifiers
    study.clear_identifiers();
    TEST_ASSERT(study.identifiers().empty(), "identifiers cleared");

    return true;
}

bool test_imaging_study_subject() {
    imaging_study_resource study;

    // Initially no subject
    TEST_ASSERT(!study.subject().has_value(), "no subject initially");

    // Set subject
    imaging_study_reference subject;
    subject.reference = "Patient/patient-123";
    subject.display = "John Doe";
    study.set_subject(subject);

    TEST_ASSERT(study.subject().has_value(), "subject has value");
    TEST_ASSERT(study.subject()->reference.has_value(), "subject reference set");
    TEST_ASSERT(*study.subject()->reference == "Patient/patient-123",
                "subject reference correct");
    TEST_ASSERT(*study.subject()->display == "John Doe",
                "subject display correct");

    return true;
}

bool test_imaging_study_counts() {
    imaging_study_resource study;

    // Initially no counts
    TEST_ASSERT(!study.number_of_series().has_value(), "no series count initially");
    TEST_ASSERT(!study.number_of_instances().has_value(),
                "no instance count initially");

    // Set counts
    study.set_number_of_series(3);
    study.set_number_of_instances(150);

    TEST_ASSERT(study.number_of_series().has_value(), "series count has value");
    TEST_ASSERT(*study.number_of_series() == 3, "series count correct");
    TEST_ASSERT(*study.number_of_instances() == 150, "instance count correct");

    return true;
}

bool test_imaging_study_series() {
    imaging_study_resource study;

    // Add series
    imaging_study_series series1;
    series1.uid = "1.2.3.4.5.6.7.8.9.1";
    series1.number = 1;
    series1.modality.system = "http://dicom.nema.org/resources/ontology/DCM";
    series1.modality.code = "CT";
    series1.modality.display = "Computed Tomography";
    series1.description = "Chest CT";
    series1.number_of_instances = 50;
    study.add_series(series1);

    imaging_study_series series2;
    series2.uid = "1.2.3.4.5.6.7.8.9.2";
    series2.number = 2;
    series2.modality.system = "http://dicom.nema.org/resources/ontology/DCM";
    series2.modality.code = "CT";
    series2.modality.display = "Computed Tomography";
    series2.number_of_instances = 100;
    study.add_series(series2);

    TEST_ASSERT(study.series().size() == 2, "two series added");
    TEST_ASSERT(study.series()[0].uid == "1.2.3.4.5.6.7.8.9.1",
                "first series UID correct");
    TEST_ASSERT(*study.series()[0].number == 1, "first series number correct");
    TEST_ASSERT(study.series()[0].modality.code == "CT", "modality code correct");

    // Clear series
    study.clear_series();
    TEST_ASSERT(study.series().empty(), "series cleared");

    return true;
}

// =============================================================================
// JSON Serialization Tests
// =============================================================================

bool test_imaging_study_to_json() {
    imaging_study_resource study;
    study.set_id("study-123");
    study.set_status(imaging_study_status::available);

    // Add identifier
    imaging_study_identifier uid_ident;
    uid_ident.system = "urn:dicom:uid";
    uid_ident.value = "1.2.3.4.5.6.7.8.9";
    study.add_identifier(uid_ident);

    // Set subject
    imaging_study_reference subject;
    subject.reference = "Patient/patient-123";
    study.set_subject(subject);

    // Set started
    study.set_started("2024-01-15T10:30:00Z");

    // Set counts
    study.set_number_of_series(3);
    study.set_number_of_instances(150);

    // Set description
    study.set_description("CT Chest with contrast");

    // Add series
    imaging_study_series series;
    series.uid = "1.2.3.4.5.6.7.8.9.1";
    series.modality.system = "http://dicom.nema.org/resources/ontology/DCM";
    series.modality.code = "CT";
    series.modality.display = "Computed Tomography";
    series.number_of_instances = 50;
    study.add_series(series);

    std::string json = study.to_json();

    // Verify JSON content
    TEST_ASSERT(json.find("\"resourceType\": \"ImagingStudy\"") != std::string::npos,
                "contains resourceType");
    TEST_ASSERT(json.find("\"id\": \"study-123\"") != std::string::npos,
                "contains id");
    TEST_ASSERT(json.find("\"status\": \"available\"") != std::string::npos,
                "contains status");
    TEST_ASSERT(json.find("\"urn:dicom:uid\"") != std::string::npos,
                "contains identifier system");
    TEST_ASSERT(json.find("\"1.2.3.4.5.6.7.8.9\"") != std::string::npos,
                "contains identifier value");
    TEST_ASSERT(json.find("\"Patient/patient-123\"") != std::string::npos,
                "contains subject reference");
    TEST_ASSERT(json.find("\"started\"") != std::string::npos,
                "contains started");
    TEST_ASSERT(json.find("\"numberOfSeries\": 3") != std::string::npos,
                "contains numberOfSeries");
    TEST_ASSERT(json.find("\"numberOfInstances\": 150") != std::string::npos,
                "contains numberOfInstances");
    TEST_ASSERT(json.find("\"series\"") != std::string::npos,
                "contains series");

    return true;
}

bool test_imaging_study_from_json() {
    std::string json = R"({
        "resourceType": "ImagingStudy",
        "id": "study-456",
        "status": "registered",
        "started": "2024-02-20T14:00:00Z",
        "description": "MRI Brain"
    })";

    auto study = imaging_study_resource::from_json(json);
    TEST_ASSERT(study != nullptr, "parsed successfully");
    TEST_ASSERT(study->id() == "study-456", "id parsed correctly");
    TEST_ASSERT(study->status() == imaging_study_status::registered,
                "status parsed correctly");
    TEST_ASSERT(study->started().has_value(), "started parsed");
    TEST_ASSERT(*study->started() == "2024-02-20T14:00:00Z", "started value correct");
    TEST_ASSERT(study->description().has_value(), "description parsed");
    TEST_ASSERT(*study->description() == "MRI Brain", "description value correct");

    return true;
}

bool test_imaging_study_from_json_invalid() {
    // Wrong resource type
    std::string invalid_type = R"({
        "resourceType": "Patient",
        "id": "patient-123"
    })";

    auto result = imaging_study_resource::from_json(invalid_type);
    TEST_ASSERT(result == nullptr, "returns nullptr for wrong resource type");

    return true;
}

// =============================================================================
// Study Storage Tests
// =============================================================================

bool test_in_memory_study_storage_basic() {
    in_memory_study_storage storage;

    // Initially empty
    TEST_ASSERT(storage.keys().empty(), "storage initially empty");

    // Store a study
    mapping::dicom_study study;
    study.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    study.study_date = "20240115";
    study.study_time = "103000";
    study.accession_number = "ACC123456";
    study.patient_id = "patient-123";
    study.patient_name = "Doe^John";
    study.status = "available";

    bool stored = storage.store("study-123", study);
    TEST_ASSERT(stored, "study stored successfully");
    TEST_ASSERT(storage.keys().size() == 1, "one study in storage");

    // Get by ID
    auto result = storage.get("study-123");
    TEST_ASSERT(result.has_value(), "study found by ID");
    TEST_ASSERT(result->study_instance_uid == "1.2.3.4.5.6.7.8.9",
                "study instance UID correct");
    TEST_ASSERT(result->patient_id == "patient-123", "patient ID correct");

    // Get by UID
    auto by_uid = storage.get_by_uid("1.2.3.4.5.6.7.8.9");
    TEST_ASSERT(by_uid.has_value(), "study found by UID");
    TEST_ASSERT(by_uid->accession_number == "ACC123456", "accession number correct");

    // Get non-existent
    auto not_found = storage.get("non-existent");
    TEST_ASSERT(!not_found.has_value(), "non-existent study returns nullopt");

    return true;
}

bool test_in_memory_study_storage_search() {
    in_memory_study_storage storage;

    // Add multiple studies
    mapping::dicom_study study1;
    study1.study_instance_uid = "1.2.3.4.5";
    study1.patient_id = "patient-A";
    study1.accession_number = "ACC001";
    study1.status = "available";
    storage.store("study-1", study1);

    mapping::dicom_study study2;
    study2.study_instance_uid = "1.2.3.4.6";
    study2.patient_id = "patient-A";
    study2.accession_number = "ACC002";
    study2.status = "available";
    storage.store("study-2", study2);

    mapping::dicom_study study3;
    study3.study_instance_uid = "1.2.3.4.7";
    study3.patient_id = "patient-B";
    study3.accession_number = "ACC003";
    study3.status = "cancelled";
    storage.store("study-3", study3);

    // Search all
    auto all = storage.search(std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    TEST_ASSERT(all.size() == 3, "all studies returned");

    // Search by patient
    auto by_patient = storage.search("patient-A", std::nullopt, std::nullopt, std::nullopt);
    TEST_ASSERT(by_patient.size() == 2, "two studies for patient-A");

    // Search by patient reference format
    auto by_patient_ref = storage.search("Patient/patient-A", std::nullopt,
                                         std::nullopt, std::nullopt);
    TEST_ASSERT(by_patient_ref.size() == 2, "two studies for Patient/patient-A");

    // Search by accession number
    auto by_accession = storage.search(std::nullopt, "ACC001", std::nullopt, std::nullopt);
    TEST_ASSERT(by_accession.size() == 1, "one study with ACC001");

    // Search by status
    auto by_status = storage.search(std::nullopt, std::nullopt, "cancelled", std::nullopt);
    TEST_ASSERT(by_status.size() == 1, "one cancelled study");

    return true;
}

bool test_in_memory_study_storage_remove() {
    in_memory_study_storage storage;

    mapping::dicom_study study;
    study.study_instance_uid = "1.2.3.4.5";
    study.patient_id = "patient-123";
    storage.store("study-1", study);

    TEST_ASSERT(storage.keys().size() == 1, "one study stored");

    bool removed = storage.remove("study-1");
    TEST_ASSERT(removed, "study removed");
    TEST_ASSERT(storage.keys().empty(), "storage empty after remove");

    bool not_removed = storage.remove("non-existent");
    TEST_ASSERT(!not_removed, "cannot remove non-existent");

    return true;
}

// =============================================================================
// Handler Tests
// =============================================================================

bool test_imaging_study_handler_creation() {
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_study_storage>();

    imaging_study_handler handler(mapper, storage);

    TEST_ASSERT(handler.handled_type() == resource_type::imaging_study,
                "handled type is imaging_study");
    TEST_ASSERT(handler.type_name() == "ImagingStudy",
                "type name is ImagingStudy");
    TEST_ASSERT(handler.supports_interaction(interaction_type::read),
                "supports read");
    TEST_ASSERT(handler.supports_interaction(interaction_type::search),
                "supports search");
    TEST_ASSERT(!handler.supports_interaction(interaction_type::create),
                "does not support create");
    TEST_ASSERT(!handler.supports_interaction(interaction_type::update),
                "does not support update");

    auto interactions = handler.supported_interactions();
    TEST_ASSERT(interactions.size() == 2, "two supported interactions");

    auto params = handler.supported_search_params();
    TEST_ASSERT(params.find("_id") != params.end(), "supports _id search");
    TEST_ASSERT(params.find("patient") != params.end(), "supports patient search");
    TEST_ASSERT(params.find("identifier") != params.end(),
                "supports identifier search");
    TEST_ASSERT(params.find("status") != params.end(), "supports status search");

    return true;
}

bool test_imaging_study_handler_read() {
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_study_storage>();

    // Store a study
    mapping::dicom_study dicom_study;
    dicom_study.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    dicom_study.study_date = "20240115";
    dicom_study.study_time = "103000";
    dicom_study.accession_number = "ACC123456";
    dicom_study.patient_id = "patient-123";
    dicom_study.patient_name = "Doe^John";
    dicom_study.study_description = "CT Chest";
    dicom_study.status = "available";
    dicom_study.number_of_series = 3;
    dicom_study.number_of_instances = 150;
    storage->store("study-1-2-3-4-5-6-7-8-9", dicom_study);

    imaging_study_handler handler(mapper, storage);

    // Read by ID
    auto result = handler.read("study-1-2-3-4-5-6-7-8-9");
    TEST_ASSERT(is_success(result), "read successful");

    auto& resource = get_resource(result);
    TEST_ASSERT(resource != nullptr, "resource not null");
    TEST_ASSERT(resource->type() == resource_type::imaging_study,
                "correct resource type");

    // Cast to imaging_study_resource to check details
    auto* study = dynamic_cast<imaging_study_resource*>(resource.get());
    TEST_ASSERT(study != nullptr, "can cast to imaging_study_resource");
    TEST_ASSERT(study->status() == imaging_study_status::available,
                "status converted correctly");
    TEST_ASSERT(study->description().has_value(), "description set");
    TEST_ASSERT(*study->description() == "CT Chest", "description correct");

    // Read non-existent
    auto not_found_result = handler.read("non-existent");
    TEST_ASSERT(!is_success(not_found_result), "not found returns error");

    return true;
}

bool test_imaging_study_handler_search() {
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_study_storage>();

    // Store multiple studies
    mapping::dicom_study study1;
    study1.study_instance_uid = "1.2.3.4.5";
    study1.patient_id = "patient-A";
    study1.accession_number = "ACC001";
    study1.status = "available";
    storage->store("study-1-2-3-4-5", study1);

    mapping::dicom_study study2;
    study2.study_instance_uid = "1.2.3.4.6";
    study2.patient_id = "patient-A";
    study2.accession_number = "ACC002";
    study2.status = "available";
    storage->store("study-1-2-3-4-6", study2);

    mapping::dicom_study study3;
    study3.study_instance_uid = "1.2.3.4.7";
    study3.patient_id = "patient-B";
    study3.accession_number = "ACC003";
    study3.status = "cancelled";
    storage->store("study-1-2-3-4-7", study3);

    imaging_study_handler handler(mapper, storage);
    pagination_params pagination;
    pagination.offset = 0;
    pagination.count = 100;

    // Search by patient
    std::map<std::string, std::string> patient_params = {
        {"patient", "patient-A"}
    };
    auto patient_result = handler.search(patient_params, pagination);
    TEST_ASSERT(is_success(patient_result), "patient search successful");
    auto& patient_search = get_resource(patient_result);
    TEST_ASSERT(patient_search.total == 2, "two studies for patient-A");
    TEST_ASSERT(patient_search.entries.size() == 2, "two entries returned");

    // Search by patient reference format
    std::map<std::string, std::string> patient_ref_params = {
        {"patient", "Patient/patient-A"}
    };
    auto patient_ref_result = handler.search(patient_ref_params, pagination);
    TEST_ASSERT(is_success(patient_ref_result), "patient ref search successful");
    auto& patient_ref_search = get_resource(patient_ref_result);
    TEST_ASSERT(patient_ref_search.total == 2,
                "two studies for Patient/patient-A");

    // Search by identifier (accession)
    std::map<std::string, std::string> accession_params = {
        {"identifier", "ACC001"}
    };
    auto accession_result = handler.search(accession_params, pagination);
    TEST_ASSERT(is_success(accession_result), "accession search successful");
    auto& accession_search = get_resource(accession_result);
    TEST_ASSERT(accession_search.total == 1, "one study with ACC001");

    // Search by status
    std::map<std::string, std::string> status_params = {
        {"status", "cancelled"}
    };
    auto status_result = handler.search(status_params, pagination);
    TEST_ASSERT(is_success(status_result), "status search successful");
    auto& status_search = get_resource(status_result);
    TEST_ASSERT(status_search.total == 1, "one cancelled study");

    // Search by _id
    std::map<std::string, std::string> id_params = {
        {"_id", "study-1-2-3-4-5"}
    };
    auto id_result = handler.search(id_params, pagination);
    TEST_ASSERT(is_success(id_result), "_id search successful");
    auto& id_search = get_resource(id_result);
    TEST_ASSERT(id_search.total == 1, "one study with _id");

    return true;
}

bool test_imaging_study_handler_pagination() {
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_study_storage>();

    // Store 5 studies
    for (int i = 1; i <= 5; ++i) {
        mapping::dicom_study study;
        study.study_instance_uid = "1.2.3.4." + std::to_string(i);
        study.patient_id = "patient-123";
        study.status = "available";
        storage->store("study-" + std::to_string(i), study);
    }

    imaging_study_handler handler(mapper, storage);
    std::map<std::string, std::string> params = {{"patient", "patient-123"}};

    // First page
    pagination_params page1;
    page1.offset = 0;
    page1.count = 2;
    auto result1 = handler.search(params, page1);
    TEST_ASSERT(is_success(result1), "first page successful");
    auto& search1 = get_resource(result1);
    TEST_ASSERT(search1.total == 5, "total is 5");
    TEST_ASSERT(search1.entries.size() == 2, "2 entries on first page");

    // Second page
    pagination_params page2;
    page2.offset = 2;
    page2.count = 2;
    auto result2 = handler.search(params, page2);
    TEST_ASSERT(is_success(result2), "second page successful");
    auto& search2 = get_resource(result2);
    TEST_ASSERT(search2.entries.size() == 2, "2 entries on second page");

    // Third page (partial)
    pagination_params page3;
    page3.offset = 4;
    page3.count = 2;
    auto result3 = handler.search(params, page3);
    TEST_ASSERT(is_success(result3), "third page successful");
    auto& search3 = get_resource(result3);
    TEST_ASSERT(search3.entries.size() == 1, "1 entry on third page");

    // Beyond last page
    pagination_params page4;
    page4.offset = 10;
    page4.count = 2;
    auto result4 = handler.search(params, page4);
    TEST_ASSERT(is_success(result4), "beyond last page successful");
    auto& search4 = get_resource(result4);
    TEST_ASSERT(search4.entries.empty(), "no entries beyond last page");

    return true;
}

// =============================================================================
// Utility Function Tests
// =============================================================================

bool test_study_uid_to_resource_id() {
    std::string uid = "1.2.3.4.5.6.7.8.9";
    std::string resource_id = study_uid_to_resource_id(uid);

    TEST_ASSERT(!resource_id.empty(), "resource ID generated");
    TEST_ASSERT(resource_id.find("study-") == 0, "has study- prefix");
    TEST_ASSERT(resource_id.find('.') == std::string::npos, "no dots in ID");
    TEST_ASSERT(resource_id == "study-1-2-3-4-5-6-7-8-9",
                "correct resource ID format");

    return true;
}

bool test_resource_id_to_study_uid() {
    std::string resource_id = "study-1-2-3-4-5-6-7-8-9";
    std::string uid = resource_id_to_study_uid(resource_id);

    TEST_ASSERT(!uid.empty(), "UID extracted");
    TEST_ASSERT(uid == "1.2.3.4.5.6.7.8.9", "correct UID");

    // Invalid ID (no prefix)
    std::string invalid1 = "invalid-1-2-3-4";
    TEST_ASSERT(resource_id_to_study_uid(invalid1).empty(),
                "empty for invalid prefix");

    // Invalid ID (no dashes to convert)
    std::string invalid2 = "study-nodashes";
    TEST_ASSERT(resource_id_to_study_uid(invalid2).empty(),
                "empty for no dots result");

    return true;
}

bool test_dicom_to_fhir_imaging_study() {
    mapping::dicom_study dicom_study;
    dicom_study.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    dicom_study.study_date = "20240115";
    dicom_study.study_time = "103000";
    dicom_study.accession_number = "ACC123456";
    dicom_study.patient_id = "patient-123";
    dicom_study.patient_name = "Doe^John";
    dicom_study.referring_physician_name = "Smith^Jane";
    dicom_study.study_description = "CT Chest with contrast";
    dicom_study.status = "available";
    dicom_study.number_of_series = 3;
    dicom_study.number_of_instances = 150;

    // Add a series
    mapping::dicom_series series;
    series.series_instance_uid = "1.2.3.4.5.6.7.8.9.1";
    series.series_number = 1;
    series.modality = "CT";
    series.series_description = "Chest Axial";
    series.number_of_instances = 50;
    series.body_part_examined = "CHEST";
    dicom_study.series.push_back(series);

    auto fhir_study = dicom_to_fhir_imaging_study(dicom_study);

    TEST_ASSERT(fhir_study != nullptr, "FHIR study created");
    TEST_ASSERT(!fhir_study->id().empty(), "ID generated");
    TEST_ASSERT(fhir_study->status() == imaging_study_status::available,
                "status mapped correctly");

    // Check identifiers
    TEST_ASSERT(fhir_study->identifiers().size() >= 1,
                "at least one identifier");
    bool found_uid = false;
    for (const auto& ident : fhir_study->identifiers()) {
        if (ident.value == "1.2.3.4.5.6.7.8.9") {
            found_uid = true;
            break;
        }
    }
    TEST_ASSERT(found_uid, "Study Instance UID in identifiers");

    // Check subject
    TEST_ASSERT(fhir_study->subject().has_value(), "subject set");
    TEST_ASSERT(fhir_study->subject()->reference.has_value(),
                "subject reference set");

    // Check started
    TEST_ASSERT(fhir_study->started().has_value(), "started set");

    // Check referrer
    TEST_ASSERT(fhir_study->referrer().has_value(), "referrer set");
    TEST_ASSERT(fhir_study->referrer()->display.has_value(), "referrer display set");

    // Check counts
    TEST_ASSERT(fhir_study->number_of_series().has_value(), "series count set");
    TEST_ASSERT(*fhir_study->number_of_series() == 3, "series count correct");
    TEST_ASSERT(fhir_study->number_of_instances().has_value(),
                "instance count set");
    TEST_ASSERT(*fhir_study->number_of_instances() == 150,
                "instance count correct");

    // Check description
    TEST_ASSERT(fhir_study->description().has_value(), "description set");
    TEST_ASSERT(*fhir_study->description() == "CT Chest with contrast",
                "description correct");

    // Check series
    TEST_ASSERT(fhir_study->series().size() == 1, "one series");
    TEST_ASSERT(fhir_study->series()[0].uid == "1.2.3.4.5.6.7.8.9.1",
                "series UID correct");
    TEST_ASSERT(fhir_study->series()[0].modality.code == "CT",
                "series modality correct");

    return true;
}

bool test_mapping_struct_conversion() {
    // Create imaging_study_resource
    imaging_study_resource original;
    original.set_id("study-123");
    original.set_status(imaging_study_status::available);

    imaging_study_identifier uid_ident;
    uid_ident.system = "urn:dicom:uid";
    uid_ident.value = "1.2.3.4.5.6.7.8.9";
    original.add_identifier(uid_ident);

    imaging_study_reference subject;
    subject.reference = "Patient/patient-123";
    original.set_subject(subject);

    original.set_started("2024-01-15T10:30:00Z");
    original.set_number_of_series(3);
    original.set_number_of_instances(150);
    original.set_description("CT Chest");

    imaging_study_series series;
    series.uid = "1.2.3.4.5.6.7.8.9.1";
    series.modality.system = "http://dicom.nema.org/resources/ontology/DCM";
    series.modality.code = "CT";
    series.number_of_instances = 50;
    original.add_series(series);

    // Convert to mapping struct
    auto mapping_struct = original.to_mapping_struct();

    TEST_ASSERT(mapping_struct.id == "study-123", "ID preserved");
    TEST_ASSERT(mapping_struct.status == "available", "status preserved");
    TEST_ASSERT(!mapping_struct.identifiers.empty(), "identifiers preserved");
    TEST_ASSERT(mapping_struct.subject.reference.has_value(), "subject preserved");
    TEST_ASSERT(mapping_struct.started.has_value(), "started preserved");
    TEST_ASSERT(mapping_struct.number_of_series.has_value(), "series count preserved");
    TEST_ASSERT(!mapping_struct.series.empty(), "series preserved");

    // Convert back from mapping struct
    auto restored = imaging_study_resource::from_mapping_struct(mapping_struct);

    TEST_ASSERT(restored != nullptr, "restored successfully");
    TEST_ASSERT(restored->id() == original.id(), "ID matches");
    TEST_ASSERT(restored->status() == original.status(), "status matches");
    TEST_ASSERT(restored->started().has_value() && original.started().has_value(),
                "started both have value");
    TEST_ASSERT(*restored->started() == *original.started(), "started matches");
    TEST_ASSERT(restored->series().size() == original.series().size(),
                "series count matches");

    return true;
}

}  // namespace pacs::bridge::fhir::test

// =============================================================================
// Main
// =============================================================================

int main() {
    using namespace pacs::bridge::fhir::test;

    int passed = 0;
    int failed = 0;

    std::cout << "=== ImagingStudy Resource Tests ===" << std::endl;
    std::cout << std::endl;

    // Status tests
    std::cout << "--- Status Tests ---" << std::endl;
    RUN_TEST(test_imaging_study_status_to_string);
    RUN_TEST(test_imaging_study_status_parsing);

    // Resource tests
    std::cout << std::endl << "--- Resource Tests ---" << std::endl;
    RUN_TEST(test_imaging_study_resource_creation);
    RUN_TEST(test_imaging_study_identifiers);
    RUN_TEST(test_imaging_study_subject);
    RUN_TEST(test_imaging_study_counts);
    RUN_TEST(test_imaging_study_series);

    // JSON tests
    std::cout << std::endl << "--- JSON Tests ---" << std::endl;
    RUN_TEST(test_imaging_study_to_json);
    RUN_TEST(test_imaging_study_from_json);
    RUN_TEST(test_imaging_study_from_json_invalid);

    // Storage tests
    std::cout << std::endl << "--- Storage Tests ---" << std::endl;
    RUN_TEST(test_in_memory_study_storage_basic);
    RUN_TEST(test_in_memory_study_storage_search);
    RUN_TEST(test_in_memory_study_storage_remove);

    // Handler tests
    std::cout << std::endl << "--- Handler Tests ---" << std::endl;
    RUN_TEST(test_imaging_study_handler_creation);
    RUN_TEST(test_imaging_study_handler_read);
    RUN_TEST(test_imaging_study_handler_search);
    RUN_TEST(test_imaging_study_handler_pagination);

    // Utility tests
    std::cout << std::endl << "--- Utility Tests ---" << std::endl;
    RUN_TEST(test_study_uid_to_resource_id);
    RUN_TEST(test_resource_id_to_study_uid);
    RUN_TEST(test_dicom_to_fhir_imaging_study);
    RUN_TEST(test_mapping_struct_conversion);

    // Summary
    std::cout << std::endl << "=== Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    return failed > 0 ? 1 : 0;
}
