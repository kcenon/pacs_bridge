/**
 * @file service_request_resource_test.cpp
 * @brief Unit tests for FHIR ServiceRequest resource functionality
 *
 * Tests cover:
 * - ServiceRequest status/intent/priority parsing
 * - ServiceRequest resource creation and serialization
 * - ServiceRequest JSON parsing
 * - ServiceRequest handler CRUD operations
 * - MWL storage operations
 * - Search by patient/status/code
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/33
 */

#include "pacs/bridge/fhir/service_request_resource.h"
#include "pacs/bridge/fhir/fhir_types.h"
#include "pacs/bridge/fhir/operation_outcome.h"
#include "pacs/bridge/fhir/resource_handler.h"
#include "pacs/bridge/cache/patient_cache.h"
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
// ServiceRequest Status Tests
// =============================================================================

bool test_service_request_status_to_string() {
    TEST_ASSERT(to_string(service_request_status::draft) == "draft",
                "draft status string");
    TEST_ASSERT(to_string(service_request_status::active) == "active",
                "active status string");
    TEST_ASSERT(to_string(service_request_status::on_hold) == "on-hold",
                "on-hold status string");
    TEST_ASSERT(to_string(service_request_status::revoked) == "revoked",
                "revoked status string");
    TEST_ASSERT(to_string(service_request_status::completed) == "completed",
                "completed status string");
    TEST_ASSERT(to_string(service_request_status::entered_in_error) ==
                    "entered-in-error",
                "entered-in-error status string");
    TEST_ASSERT(to_string(service_request_status::unknown) == "unknown",
                "unknown status string");
    return true;
}

bool test_service_request_status_parsing() {
    auto draft = parse_service_request_status("draft");
    TEST_ASSERT(draft.has_value() && *draft == service_request_status::draft,
                "parse draft");

    auto active = parse_service_request_status("active");
    TEST_ASSERT(active.has_value() && *active == service_request_status::active,
                "parse active");

    auto on_hold = parse_service_request_status("on-hold");
    TEST_ASSERT(on_hold.has_value() &&
                    *on_hold == service_request_status::on_hold,
                "parse on-hold");

    auto completed = parse_service_request_status("COMPLETED");
    TEST_ASSERT(completed.has_value() &&
                    *completed == service_request_status::completed,
                "parse COMPLETED (uppercase)");

    auto invalid = parse_service_request_status("invalid-status");
    TEST_ASSERT(!invalid.has_value(), "invalid status returns nullopt");

    return true;
}

// =============================================================================
// ServiceRequest Intent Tests
// =============================================================================

bool test_service_request_intent_to_string() {
    TEST_ASSERT(to_string(service_request_intent::proposal) == "proposal",
                "proposal intent string");
    TEST_ASSERT(to_string(service_request_intent::plan) == "plan",
                "plan intent string");
    TEST_ASSERT(to_string(service_request_intent::order) == "order",
                "order intent string");
    TEST_ASSERT(to_string(service_request_intent::original_order) ==
                    "original-order",
                "original-order intent string");
    TEST_ASSERT(to_string(service_request_intent::filler_order) ==
                    "filler-order",
                "filler-order intent string");
    return true;
}

bool test_service_request_intent_parsing() {
    auto proposal = parse_service_request_intent("proposal");
    TEST_ASSERT(proposal.has_value() &&
                    *proposal == service_request_intent::proposal,
                "parse proposal");

    auto order = parse_service_request_intent("order");
    TEST_ASSERT(order.has_value() && *order == service_request_intent::order,
                "parse order");

    auto filler = parse_service_request_intent("filler-order");
    TEST_ASSERT(filler.has_value() &&
                    *filler == service_request_intent::filler_order,
                "parse filler-order");

    auto invalid = parse_service_request_intent("invalid");
    TEST_ASSERT(!invalid.has_value(), "invalid intent returns nullopt");

    return true;
}

// =============================================================================
// ServiceRequest Priority Tests
// =============================================================================

bool test_service_request_priority_to_string() {
    TEST_ASSERT(to_string(service_request_priority::routine) == "routine",
                "routine priority string");
    TEST_ASSERT(to_string(service_request_priority::urgent) == "urgent",
                "urgent priority string");
    TEST_ASSERT(to_string(service_request_priority::asap) == "asap",
                "asap priority string");
    TEST_ASSERT(to_string(service_request_priority::stat) == "stat",
                "stat priority string");
    return true;
}

bool test_service_request_priority_parsing() {
    auto routine = parse_service_request_priority("routine");
    TEST_ASSERT(routine.has_value() &&
                    *routine == service_request_priority::routine,
                "parse routine");

    auto urgent = parse_service_request_priority("urgent");
    TEST_ASSERT(urgent.has_value() &&
                    *urgent == service_request_priority::urgent,
                "parse urgent");

    auto stat = parse_service_request_priority("STAT");
    TEST_ASSERT(stat.has_value() && *stat == service_request_priority::stat,
                "parse STAT (uppercase)");

    auto invalid = parse_service_request_priority("invalid");
    TEST_ASSERT(!invalid.has_value(), "invalid priority returns nullopt");

    return true;
}

// =============================================================================
// ServiceRequest Resource Tests
// =============================================================================

bool test_service_request_resource_creation() {
    service_request_resource request;

    TEST_ASSERT(request.type() == resource_type::service_request,
                "resource type is service_request");
    TEST_ASSERT(request.type_name() == "ServiceRequest",
                "type name is ServiceRequest");

    // Set required fields
    request.set_id("order-123");
    request.set_status(service_request_status::active);
    request.set_intent(service_request_intent::order);

    TEST_ASSERT(request.id() == "order-123", "id set correctly");
    TEST_ASSERT(request.status() == service_request_status::active,
                "status set correctly");
    TEST_ASSERT(request.intent() == service_request_intent::order,
                "intent set correctly");

    return true;
}

bool test_service_request_resource_full() {
    service_request_resource request;

    request.set_id("order-456");
    request.set_status(service_request_status::active);
    request.set_intent(service_request_intent::order);
    request.set_priority(service_request_priority::urgent);

    // Add identifier
    service_request_identifier ident;
    ident.system = "http://hospital.example.org/orders";
    ident.value = "ORD-12345";
    ident.use = "official";
    request.add_identifier(ident);

    // Set code
    service_request_coding coding;
    coding.system = "http://loinc.org";
    coding.code = "24558-9";
    coding.display = "CT Chest";
    service_request_codeable_concept code;
    code.coding.push_back(coding);
    code.text = "CT Chest scan";
    request.set_code(code);

    // Set subject
    service_request_reference subject;
    subject.reference = "Patient/patient-123";
    subject.display = "John Doe";
    request.set_subject(subject);

    // Set requester
    service_request_reference requester;
    requester.reference = "Practitioner/dr-smith";
    requester.display = "Dr. Smith";
    request.set_requester(requester);

    // Add performer
    service_request_reference performer;
    performer.reference = "Location/ct-scanner-1";
    performer.display = "CT Scanner 1";
    request.add_performer(performer);

    // Set occurrence
    request.set_occurrence_date_time("2024-01-15T10:00:00Z");

    // Set note
    request.set_note("Patient has contrast allergy");

    // Validate fields
    TEST_ASSERT(request.priority().has_value() &&
                    *request.priority() == service_request_priority::urgent,
                "priority set correctly");
    TEST_ASSERT(request.identifiers().size() == 1, "one identifier added");
    TEST_ASSERT(request.identifiers()[0].value == "ORD-12345",
                "identifier value correct");
    TEST_ASSERT(request.code().has_value(), "code is set");
    TEST_ASSERT(request.code()->coding[0].code == "24558-9", "code correct");
    TEST_ASSERT(request.subject().has_value(), "subject is set");
    TEST_ASSERT(request.subject()->reference.value() == "Patient/patient-123",
                "subject reference correct");
    TEST_ASSERT(request.occurrence_date_time().has_value(),
                "occurrence is set");
    TEST_ASSERT(request.note().has_value(), "note is set");

    // Test validation (should pass with subject)
    TEST_ASSERT(request.validate(), "validation passes with subject");

    return true;
}

bool test_service_request_validation() {
    service_request_resource request;
    request.set_status(service_request_status::active);
    request.set_intent(service_request_intent::order);

    // Without subject, validation should fail
    TEST_ASSERT(!request.validate(), "validation fails without subject");

    // Add subject
    service_request_reference subject;
    subject.reference = "Patient/123";
    request.set_subject(subject);

    // Now validation should pass
    TEST_ASSERT(request.validate(), "validation passes with subject");

    return true;
}

bool test_service_request_json_serialization() {
    service_request_resource request;
    request.set_id("order-789");
    request.set_status(service_request_status::active);
    request.set_intent(service_request_intent::order);
    request.set_priority(service_request_priority::routine);

    service_request_reference subject;
    subject.reference = "Patient/patient-456";
    request.set_subject(subject);

    service_request_coding coding;
    coding.system = "http://loinc.org";
    coding.code = "71020";
    coding.display = "Chest X-ray";
    service_request_codeable_concept code;
    code.coding.push_back(coding);
    request.set_code(code);

    std::string json = request.to_json();

    // Verify JSON contains expected fields
    TEST_ASSERT(json.find("\"resourceType\": \"ServiceRequest\"") !=
                    std::string::npos,
                "JSON contains resourceType");
    TEST_ASSERT(json.find("\"id\": \"order-789\"") != std::string::npos,
                "JSON contains id");
    TEST_ASSERT(json.find("\"status\": \"active\"") != std::string::npos,
                "JSON contains status");
    TEST_ASSERT(json.find("\"intent\": \"order\"") != std::string::npos,
                "JSON contains intent");
    TEST_ASSERT(json.find("\"priority\": \"routine\"") != std::string::npos,
                "JSON contains priority");
    TEST_ASSERT(json.find("Patient/patient-456") != std::string::npos,
                "JSON contains patient reference");
    TEST_ASSERT(json.find("http://loinc.org") != std::string::npos,
                "JSON contains LOINC system");
    TEST_ASSERT(json.find("71020") != std::string::npos,
                "JSON contains procedure code");

    return true;
}

bool test_service_request_json_parsing() {
    std::string json = R"({
        "resourceType": "ServiceRequest",
        "id": "parsed-order-123",
        "status": "active",
        "intent": "order",
        "priority": "urgent",
        "code": {
            "coding": [
                {
                    "system": "http://loinc.org",
                    "code": "24558-9",
                    "display": "CT Chest"
                }
            ]
        },
        "subject": {
            "reference": "Patient/patient-abc"
        },
        "occurrenceDateTime": "2024-02-20T14:30:00Z"
    })";

    auto parsed = service_request_resource::from_json(json);
    TEST_ASSERT(parsed != nullptr, "JSON parsed successfully");
    TEST_ASSERT(parsed->id() == "parsed-order-123", "parsed id correct");
    TEST_ASSERT(parsed->status() == service_request_status::active,
                "parsed status correct");
    TEST_ASSERT(parsed->intent() == service_request_intent::order,
                "parsed intent correct");
    TEST_ASSERT(parsed->priority().has_value() &&
                    *parsed->priority() == service_request_priority::urgent,
                "parsed priority correct");
    TEST_ASSERT(parsed->subject().has_value() &&
                    parsed->subject()->reference.value() == "Patient/patient-abc",
                "parsed subject correct");
    TEST_ASSERT(parsed->occurrence_date_time().has_value() &&
                    *parsed->occurrence_date_time() == "2024-02-20T14:30:00Z",
                "parsed occurrence correct");

    return true;
}

bool test_service_request_json_parsing_invalid() {
    // Wrong resourceType
    std::string wrong_type = R"({
        "resourceType": "Patient",
        "id": "patient-123"
    })";

    auto parsed = service_request_resource::from_json(wrong_type);
    TEST_ASSERT(parsed == nullptr, "returns nullptr for wrong resourceType");

    return true;
}

// =============================================================================
// In-Memory MWL Storage Tests
// =============================================================================

bool test_in_memory_mwl_storage_basic() {
    in_memory_mwl_storage storage;

    // Create a sample MWL item
    mapping::mwl_item item;
    item.patient.patient_id = "PAT-001";
    item.patient.patient_name = "Test^Patient";
    item.imaging_service_request.accession_number = "ACC-001";

    // Store
    TEST_ASSERT(storage.store("item-1", item), "store succeeds");

    // Get
    auto retrieved = storage.get("item-1");
    TEST_ASSERT(retrieved.has_value(), "item retrieved");
    TEST_ASSERT(retrieved->patient.patient_id == "PAT-001",
                "patient ID matches");

    // Keys
    auto keys = storage.keys();
    TEST_ASSERT(keys.size() == 1, "one key present");
    TEST_ASSERT(keys[0] == "item-1", "key is correct");

    // Update
    item.imaging_service_request.accession_number = "ACC-002";
    TEST_ASSERT(storage.update("item-1", item), "update succeeds");

    auto updated = storage.get("item-1");
    TEST_ASSERT(updated.has_value() &&
                    updated->imaging_service_request.accession_number ==
                        "ACC-002",
                "item updated");

    // Remove
    TEST_ASSERT(storage.remove("item-1"), "remove succeeds");
    TEST_ASSERT(!storage.get("item-1").has_value(), "item no longer exists");

    return true;
}

bool test_in_memory_mwl_storage_not_found() {
    in_memory_mwl_storage storage;

    // Get non-existent
    auto result = storage.get("non-existent");
    TEST_ASSERT(!result.has_value(), "get returns nullopt for non-existent");

    // Update non-existent
    mapping::mwl_item item;
    TEST_ASSERT(!storage.update("non-existent", item),
                "update returns false for non-existent");

    // Remove non-existent
    TEST_ASSERT(!storage.remove("non-existent"),
                "remove returns false for non-existent");

    return true;
}

// =============================================================================
// ServiceRequest Handler Tests
// =============================================================================

bool test_service_request_handler_creation() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    TEST_ASSERT(handler.handled_type() == resource_type::service_request,
                "handled type is service_request");
    TEST_ASSERT(handler.type_name() == "ServiceRequest",
                "type name is ServiceRequest");

    return true;
}

bool test_service_request_handler_supported_interactions() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    TEST_ASSERT(handler.supports_interaction(interaction_type::read),
                "supports read");
    TEST_ASSERT(handler.supports_interaction(interaction_type::create),
                "supports create");
    TEST_ASSERT(handler.supports_interaction(interaction_type::update),
                "supports update");
    TEST_ASSERT(handler.supports_interaction(interaction_type::search),
                "supports search");
    TEST_ASSERT(!handler.supports_interaction(interaction_type::delete_resource),
                "does not support delete");

    auto interactions = handler.supported_interactions();
    TEST_ASSERT(interactions.size() == 4, "four interactions supported");

    return true;
}

bool test_service_request_handler_search_params() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    auto params = handler.supported_search_params();
    TEST_ASSERT(params.find("_id") != params.end(), "supports _id param");
    TEST_ASSERT(params.find("patient") != params.end(),
                "supports patient param");
    TEST_ASSERT(params.find("status") != params.end(), "supports status param");
    TEST_ASSERT(params.find("code") != params.end(), "supports code param");

    return true;
}

bool test_service_request_handler_create_and_read() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    // Create a service request
    auto request = std::make_unique<service_request_resource>();
    request->set_status(service_request_status::active);
    request->set_intent(service_request_intent::order);

    service_request_reference subject;
    subject.reference = "Patient/test-patient";
    request->set_subject(subject);

    service_request_coding coding;
    coding.system = "http://loinc.org";
    coding.code = "24558-9";
    coding.display = "CT Chest";
    service_request_codeable_concept code;
    code.coding.push_back(coding);
    request->set_code(code);

    auto create_result = handler.create(std::move(request));
    TEST_ASSERT(is_success(create_result), "create succeeds");

    auto& created = get_resource(create_result);
    TEST_ASSERT(created != nullptr, "created resource is not null");
    TEST_ASSERT(!created->id().empty(), "created resource has ID");

    std::string created_id = created->id();

    // Read it back
    auto read_result = handler.read(created_id);
    TEST_ASSERT(is_success(read_result), "read succeeds");

    auto& read_resource = get_resource(read_result);
    TEST_ASSERT(read_resource != nullptr, "read resource is not null");
    TEST_ASSERT(read_resource->id() == created_id, "read ID matches");

    return true;
}

bool test_service_request_handler_create_with_id() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    auto request = std::make_unique<service_request_resource>();
    request->set_id("custom-id-123");
    request->set_status(service_request_status::active);
    request->set_intent(service_request_intent::order);

    service_request_reference subject;
    subject.reference = "Patient/patient-xyz";
    request->set_subject(subject);

    auto result = handler.create(std::move(request));
    TEST_ASSERT(is_success(result), "create succeeds");

    auto& created = get_resource(result);
    TEST_ASSERT(created->id() == "custom-id-123", "custom ID preserved");

    return true;
}

bool test_service_request_handler_create_validation_fails() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    // Create without subject (required field)
    auto request = std::make_unique<service_request_resource>();
    request->set_status(service_request_status::active);
    request->set_intent(service_request_intent::order);

    auto result = handler.create(std::move(request));
    TEST_ASSERT(!is_success(result), "create fails without subject");

    return true;
}

bool test_service_request_handler_read_not_found() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    auto result = handler.read("non-existent-id");
    TEST_ASSERT(!is_success(result), "read fails for non-existent");

    auto& outcome = get_outcome(result);
    TEST_ASSERT(outcome_to_http_status(outcome) == http_status::not_found,
                "returns 404 not found");

    return true;
}

bool test_service_request_handler_update() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    // Create first
    auto request = std::make_unique<service_request_resource>();
    request->set_id("update-test-id");
    request->set_status(service_request_status::active);
    request->set_intent(service_request_intent::order);

    service_request_reference subject;
    subject.reference = "Patient/patient-update";
    request->set_subject(subject);

    auto create_result = handler.create(std::move(request));
    TEST_ASSERT(is_success(create_result), "initial create succeeds");

    // Update with new status
    auto update_request = std::make_unique<service_request_resource>();
    update_request->set_status(service_request_status::completed);
    update_request->set_intent(service_request_intent::order);
    update_request->set_subject(subject);

    auto update_result = handler.update("update-test-id",
                                        std::move(update_request));
    TEST_ASSERT(is_success(update_result), "update succeeds");

    // Verify update
    auto read_result = handler.read("update-test-id");
    TEST_ASSERT(is_success(read_result), "read after update succeeds");

    auto* sr = dynamic_cast<service_request_resource*>(
        get_resource(read_result).get());
    TEST_ASSERT(sr != nullptr, "can cast to service_request_resource");
    TEST_ASSERT(sr->status() == service_request_status::completed,
                "status updated");

    return true;
}

bool test_service_request_handler_update_not_found() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    auto request = std::make_unique<service_request_resource>();
    request->set_status(service_request_status::active);
    request->set_intent(service_request_intent::order);

    service_request_reference subject;
    subject.reference = "Patient/xyz";
    request->set_subject(subject);

    auto result = handler.update("non-existent", std::move(request));
    TEST_ASSERT(!is_success(result), "update fails for non-existent");

    return true;
}

bool test_service_request_handler_search_by_patient() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    // Create multiple requests
    for (int i = 1; i <= 3; ++i) {
        auto request = std::make_unique<service_request_resource>();
        request->set_id("search-test-" + std::to_string(i));
        request->set_status(service_request_status::active);
        request->set_intent(service_request_intent::order);

        service_request_reference subject;
        if (i <= 2) {
            subject.reference = "Patient/patient-A";
        } else {
            subject.reference = "Patient/patient-B";
        }
        request->set_subject(subject);

        [[maybe_unused]] auto _ = handler.create(std::move(request));
    }

    // Search by patient A
    std::map<std::string, std::string> params = {{"patient", "patient-A"}};
    pagination_params pagination;
    pagination.count = 100;

    auto result = handler.search(params, pagination);
    TEST_ASSERT(is_success(result), "search succeeds");

    auto& search_result = get_resource(result);
    TEST_ASSERT(search_result.total == 2, "found 2 results for patient-A");
    TEST_ASSERT(search_result.entries.size() == 2, "2 entries returned");

    return true;
}

bool test_service_request_handler_search_by_status() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    // Create requests with different statuses
    auto active1 = std::make_unique<service_request_resource>();
    active1->set_id("status-test-1");
    active1->set_status(service_request_status::active);
    active1->set_intent(service_request_intent::order);
    service_request_reference subj1;
    subj1.reference = "Patient/p1";
    active1->set_subject(subj1);
    [[maybe_unused]] auto r1 = handler.create(std::move(active1));

    auto active2 = std::make_unique<service_request_resource>();
    active2->set_id("status-test-2");
    active2->set_status(service_request_status::active);
    active2->set_intent(service_request_intent::order);
    service_request_reference subj2;
    subj2.reference = "Patient/p2";
    active2->set_subject(subj2);
    [[maybe_unused]] auto r2 = handler.create(std::move(active2));

    auto completed = std::make_unique<service_request_resource>();
    completed->set_id("status-test-3");
    completed->set_status(service_request_status::completed);
    completed->set_intent(service_request_intent::order);
    service_request_reference subj3;
    subj3.reference = "Patient/p3";
    completed->set_subject(subj3);
    [[maybe_unused]] auto r3 = handler.create(std::move(completed));

    // Search for active
    std::map<std::string, std::string> params = {{"status", "active"}};
    pagination_params pagination;
    pagination.count = 100;

    auto result = handler.search(params, pagination);
    TEST_ASSERT(is_success(result), "search succeeds");

    auto& search_result = get_resource(result);
    TEST_ASSERT(search_result.total == 2, "found 2 active requests");

    return true;
}

bool test_service_request_handler_search_pagination() {
    auto patient_cache = std::make_shared<cache::patient_cache>();
    auto mapper = std::make_shared<mapping::fhir_dicom_mapper>();
    auto storage = std::make_shared<in_memory_mwl_storage>();

    service_request_handler handler(patient_cache, mapper, storage);

    // Create 5 requests
    for (int i = 1; i <= 5; ++i) {
        auto request = std::make_unique<service_request_resource>();
        request->set_id("page-test-" + std::to_string(i));
        request->set_status(service_request_status::active);
        request->set_intent(service_request_intent::order);

        service_request_reference subject;
        subject.reference = "Patient/patient-page";
        request->set_subject(subject);

        [[maybe_unused]] auto _ = handler.create(std::move(request));
    }

    // Get first page (2 items)
    std::map<std::string, std::string> params;
    pagination_params pagination;
    pagination.offset = 0;
    pagination.count = 2;

    auto result = handler.search(params, pagination);
    TEST_ASSERT(is_success(result), "search succeeds");

    auto& search_result = get_resource(result);
    TEST_ASSERT(search_result.total == 5, "total is 5");
    TEST_ASSERT(search_result.entries.size() == 2, "first page has 2 items");

    // Get second page
    pagination.offset = 2;
    auto result2 = handler.search(params, pagination);
    TEST_ASSERT(is_success(result2), "search page 2 succeeds");

    auto& search_result2 = get_resource(result2);
    TEST_ASSERT(search_result2.entries.size() == 2, "second page has 2 items");

    // Get last page
    pagination.offset = 4;
    auto result3 = handler.search(params, pagination);
    TEST_ASSERT(is_success(result3), "search page 3 succeeds");

    auto& search_result3 = get_resource(result3);
    TEST_ASSERT(search_result3.entries.size() == 1, "last page has 1 item");

    return true;
}

// =============================================================================
// Utility Function Tests
// =============================================================================

bool test_generate_resource_id() {
    std::string id1 = generate_resource_id();
    std::string id2 = generate_resource_id();

    TEST_ASSERT(!id1.empty(), "generated ID is not empty");
    TEST_ASSERT(!id2.empty(), "second generated ID is not empty");
    TEST_ASSERT(id1 != id2, "generated IDs are unique");

    return true;
}

// =============================================================================
// Mapping Structure Conversion Tests
// =============================================================================

bool test_service_request_to_mapping_struct() {
    service_request_resource request;
    request.set_id("mapping-test-123");
    request.set_status(service_request_status::active);
    request.set_intent(service_request_intent::order);
    request.set_priority(service_request_priority::urgent);

    service_request_identifier ident;
    ident.system = "urn:oid:1.2.3.4";
    ident.value = "ACC-12345";
    request.add_identifier(ident);

    service_request_coding coding;
    coding.system = "http://loinc.org";
    coding.code = "24558-9";
    coding.display = "CT Chest";
    service_request_codeable_concept code;
    code.coding.push_back(coding);
    request.set_code(code);

    service_request_reference subject;
    subject.reference = "Patient/patient-mapping";
    request.set_subject(subject);

    request.set_occurrence_date_time("2024-03-15T09:00:00Z");

    auto mapping = request.to_mapping_struct();

    TEST_ASSERT(mapping.id == "mapping-test-123", "mapping ID correct");
    TEST_ASSERT(mapping.status == "active", "mapping status correct");
    TEST_ASSERT(mapping.intent == "order", "mapping intent correct");
    TEST_ASSERT(mapping.priority == "urgent", "mapping priority correct");
    TEST_ASSERT(mapping.identifiers.size() == 1, "mapping has identifier");
    TEST_ASSERT(mapping.code.coding.size() == 1, "mapping has code");
    TEST_ASSERT(mapping.code.coding[0].code == "24558-9",
                "mapping code correct");
    TEST_ASSERT(mapping.subject.reference.value() == "Patient/patient-mapping",
                "mapping subject correct");
    TEST_ASSERT(mapping.occurrence_date_time.value() == "2024-03-15T09:00:00Z",
                "mapping occurrence correct");

    return true;
}

bool test_service_request_from_mapping_struct() {
    mapping::fhir_service_request mapping;
    mapping.id = "from-mapping-456";
    mapping.status = "completed";
    mapping.intent = "filler-order";
    mapping.priority = "stat";

    mapping.identifiers.emplace_back("http://system.org", "ID-999");

    mapping::fhir_coding coding;
    coding.system = "http://snomed.info/sct";
    coding.code = "77477000";
    coding.display = "CT scan";
    mapping.code.coding.push_back(coding);

    mapping.subject.reference = "Patient/from-mapping-patient";
    mapping.occurrence_date_time = "2024-04-20T11:30:00Z";
    mapping.note = "Test note";

    auto request = service_request_resource::from_mapping_struct(mapping);

    TEST_ASSERT(request != nullptr, "conversion succeeded");
    TEST_ASSERT(request->id() == "from-mapping-456", "id correct");
    TEST_ASSERT(request->status() == service_request_status::completed,
                "status correct");
    TEST_ASSERT(request->intent() == service_request_intent::filler_order,
                "intent correct");
    TEST_ASSERT(request->priority().has_value() &&
                    *request->priority() == service_request_priority::stat,
                "priority correct");
    TEST_ASSERT(request->subject().has_value() &&
                    request->subject()->reference.value() ==
                        "Patient/from-mapping-patient",
                "subject correct");
    TEST_ASSERT(request->note().has_value() && *request->note() == "Test note",
                "note correct");

    return true;
}

}  // namespace pacs::bridge::fhir::test

// =============================================================================
// Main Entry Point
// =============================================================================

int main() {
    using namespace pacs::bridge::fhir::test;

    int passed = 0;
    int failed = 0;

    std::cout << "\n=== ServiceRequest Status Tests ===" << std::endl;
    RUN_TEST(test_service_request_status_to_string);
    RUN_TEST(test_service_request_status_parsing);

    std::cout << "\n=== ServiceRequest Intent Tests ===" << std::endl;
    RUN_TEST(test_service_request_intent_to_string);
    RUN_TEST(test_service_request_intent_parsing);

    std::cout << "\n=== ServiceRequest Priority Tests ===" << std::endl;
    RUN_TEST(test_service_request_priority_to_string);
    RUN_TEST(test_service_request_priority_parsing);

    std::cout << "\n=== ServiceRequest Resource Tests ===" << std::endl;
    RUN_TEST(test_service_request_resource_creation);
    RUN_TEST(test_service_request_resource_full);
    RUN_TEST(test_service_request_validation);
    RUN_TEST(test_service_request_json_serialization);
    RUN_TEST(test_service_request_json_parsing);
    RUN_TEST(test_service_request_json_parsing_invalid);

    std::cout << "\n=== MWL Storage Tests ===" << std::endl;
    RUN_TEST(test_in_memory_mwl_storage_basic);
    RUN_TEST(test_in_memory_mwl_storage_not_found);

    std::cout << "\n=== ServiceRequest Handler Tests ===" << std::endl;
    RUN_TEST(test_service_request_handler_creation);
    RUN_TEST(test_service_request_handler_supported_interactions);
    RUN_TEST(test_service_request_handler_search_params);
    RUN_TEST(test_service_request_handler_create_and_read);
    RUN_TEST(test_service_request_handler_create_with_id);
    RUN_TEST(test_service_request_handler_create_validation_fails);
    RUN_TEST(test_service_request_handler_read_not_found);
    RUN_TEST(test_service_request_handler_update);
    RUN_TEST(test_service_request_handler_update_not_found);
    RUN_TEST(test_service_request_handler_search_by_patient);
    RUN_TEST(test_service_request_handler_search_by_status);
    RUN_TEST(test_service_request_handler_search_pagination);

    std::cout << "\n=== Utility Function Tests ===" << std::endl;
    RUN_TEST(test_generate_resource_id);

    std::cout << "\n=== Mapping Conversion Tests ===" << std::endl;
    RUN_TEST(test_service_request_to_mapping_struct);
    RUN_TEST(test_service_request_from_mapping_struct);

    std::cout << "\n========================================" << std::endl;
    std::cout << "ServiceRequest Resource Tests: " << passed << " passed, "
              << failed << " failed" << std::endl;

    return failed > 0 ? 1 : 0;
}
