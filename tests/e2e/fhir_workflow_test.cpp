/**
 * @file fhir_workflow_test.cpp
 * @brief End-to-end workflow tests for FHIR-based clinical scenarios
 *
 * Tests FHIR-centric workflows:
 *   1. FHIR ServiceRequest -> MWL creation -> MPPS lifecycle
 *   2. MPPS completion -> DiagnosticReport building -> result posting
 *   3. Patient lookup -> MWL demographics enrichment
 *   4. Multi-system integration (HIS, PACS, EMR)
 *   5. Error handling: incomplete data, transient failures
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/321
 */

#include "integration_test_base.h"
#include "pacs_system_test_base.h"

#include "pacs/bridge/emr/diagnostic_report_builder.h"
#include "pacs/bridge/emr/emr_types.h"
#include "pacs/bridge/emr/patient_lookup.h"
#include "pacs/bridge/emr/result_tracker.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::e2e::test {

using namespace pacs::bridge::integration::test;
using namespace pacs::bridge::emr;

// =============================================================================
// Test Macros
// =============================================================================

#define FHIR_E2E_ASSERT(condition, message)                                    \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_FHIR_TEST(test_func)                                               \
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

// =============================================================================
// Test Utilities
// =============================================================================

/**
 * @brief Create a test patient record for FHIR workflow
 */
patient_record create_fhir_test_patient(const std::string& id,
                                        const std::string& mrn,
                                        const std::string& family,
                                        const std::string& given) {
    patient_record patient;
    patient.id = id;
    patient.mrn = mrn;
    patient.active = true;
    patient.sex = "male";
    patient.birth_date = "1980-03-15";

    patient_name name;
    name.family = family;
    name.given = {given};
    name.use = "official";
    patient.names.push_back(name);

    patient_identifier mrn_id;
    mrn_id.value = mrn;
    mrn_id.system = "http://hospital.example.org/mrn";
    mrn_id.type_code = "MR";
    patient.identifiers.push_back(mrn_id);

    return patient;
}

/**
 * @brief Create a test study result for FHIR workflow
 */
study_result create_fhir_test_study_result(const std::string& study_uid,
                                           const std::string& patient_id,
                                           const std::string& accession,
                                           const std::string& modality) {
    study_result result;
    result.study_instance_uid = study_uid;
    result.patient_id = patient_id;
    result.patient_reference = "Patient/" + patient_id;
    result.accession_number = accession;
    result.modality = modality;
    result.study_description = modality + " Study";
    result.study_datetime = "2026-02-07T10:00:00Z";
    result.performing_physician = "Dr. Test Radiologist";
    result.conclusion = "No significant abnormalities identified.";
    result.status = result_status::final_report;
    return result;
}

// =============================================================================
// Test: Complete FHIR ServiceRequest to DiagnosticReport Workflow
// =============================================================================

/**
 * @brief Test complete FHIR workflow from ServiceRequest to DiagnosticReport
 *
 * Simulates:
 *   1. FHIR ServiceRequest received (order placement)
 *   2. Patient demographics looked up from EMR
 *   3. MWL entry created with patient data
 *   4. MPPS lifecycle (IN PROGRESS -> COMPLETED)
 *   5. DiagnosticReport built with study results
 *   6. Result tracked for status monitoring
 */
bool test_fhir_service_request_to_diagnostic_report() {
    // Step 1: Simulate FHIR ServiceRequest (order data)
    std::string accession = pacs_system_test_fixture::generate_unique_accession();
    std::string patient_id = "fhir-patient-001";
    std::string study_uid = "1.2.840.113619.2.55.3." + accession;

    // Step 2: Patient demographics (simulating EMR lookup)
    auto patient = create_fhir_test_patient(
        patient_id, "MRN-FHIR-001", "Johnson", "Robert");
    FHIR_E2E_ASSERT(!patient.mrn.empty(), "Patient should have MRN");
    FHIR_E2E_ASSERT(!patient.names.empty(), "Patient should have name");

    auto official_name = patient.official_name();
    FHIR_E2E_ASSERT(official_name != nullptr,
                     "Patient should have official name");
    FHIR_E2E_ASSERT(official_name->family.value_or("") == "Johnson",
                     "Family name should be Johnson");

    // Step 3: Create MWL entry with patient data
    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    auto mwl_item = mwl_test_data_generator::create_item_with_accession(accession);
    mwl_item.patient.patient_id = patient.mrn;
    mwl_item.patient.patient_name = "JOHNSON^ROBERT";
    mwl_item.patient.patient_birth_date = "19800315";
    mwl_item.patient.patient_sex = "M";
    if (!mwl_item.scheduled_steps.empty()) {
        mwl_item.scheduled_steps[0].modality = "DX";
        mwl_item.scheduled_steps[0].scheduled_station_ae_title = "DX_ROOM_1";
    }

    auto add_result = mwl_client.add_entry(mwl_item);
    FHIR_E2E_ASSERT(add_result.has_value(), "MWL entry should be created");

    // Step 4: MPPS lifecycle
    auto mpps_config = pacs_system_test_fixture::create_mpps_test_config();
    auto mpps_handler = pacs_adapter::mpps_handler::create(mpps_config);

    auto mpps_dataset = mpps_test_data_generator::create_in_progress();
    mpps_dataset.accession_number = accession;
    mpps_dataset.patient_id = patient.mrn;
    mpps_dataset.patient_name = "JOHNSON^ROBERT";
    mpps_dataset.modality = "DX";
    mpps_dataset.study_instance_uid = study_uid;

    auto create_result = mpps_handler->on_n_create(mpps_dataset);
    FHIR_E2E_ASSERT(create_result.has_value(), "MPPS N-CREATE should succeed");

    // Complete MPPS
    mpps_dataset.status = pacs_adapter::mpps_event::completed;
    mpps_dataset.end_date = mpps_test_data_generator::get_today_date();
    mpps_dataset.end_time = mpps_test_data_generator::get_offset_time(25);

    auto set_result = mpps_handler->on_n_set(mpps_dataset);
    FHIR_E2E_ASSERT(set_result.has_value(), "MPPS N-SET COMPLETED should succeed");

    // Step 5: Build DiagnosticReport
    auto study = create_fhir_test_study_result(
        study_uid, patient_id, accession, "DX");

    diagnostic_report_builder builder;
    builder.subject("Patient/" + patient_id)
        .status(result_status::final_report)
        .code_loinc("36643-5", "Chest X-ray 2 Views")
        .conclusion(study.conclusion.value_or(""))
        .effective_datetime(study.study_datetime)
        .issued(study.study_datetime)
        .performer("Practitioner/prac-001",
                   study.performing_physician.value_or(""))
        .imaging_study("ImagingStudy/img-" + study_uid)
        .based_on("ServiceRequest/sr-" + accession);

    auto report_json = builder.build();
    FHIR_E2E_ASSERT(report_json.has_value(),
                     "DiagnosticReport should be generated");
    FHIR_E2E_ASSERT(report_json->find("DiagnosticReport") != std::string::npos,
                     "Should contain DiagnosticReport resource type");
    FHIR_E2E_ASSERT(report_json->find("Patient/" + patient_id) != std::string::npos,
                     "Should reference correct patient");
    FHIR_E2E_ASSERT(report_json->find("final") != std::string::npos,
                     "Should have final status");
    FHIR_E2E_ASSERT(report_json->find("36643-5") != std::string::npos,
                     "Should have LOINC code");

    // Step 6: Track result
    result_tracker_config tracker_config;
    tracker_config.max_entries = 1000;
    tracker_config.ttl = std::chrono::hours{24};

    in_memory_result_tracker tracker(tracker_config);

    posted_result posted;
    posted.report_id = "report-" + accession;
    posted.study_instance_uid = study_uid;
    posted.accession_number = accession;
    posted.status = result_status::final_report;
    posted.posted_at = std::chrono::system_clock::now();

    auto track_result = tracker.track(posted);
    FHIR_E2E_ASSERT(track_result.is_ok(), "Result tracking should succeed");

    auto tracked = tracker.get_by_study_uid(study_uid);
    FHIR_E2E_ASSERT(tracked.has_value(), "Tracked result should be findable");
    FHIR_E2E_ASSERT(tracked->status == result_status::final_report,
                     "Tracked status should be final");
    FHIR_E2E_ASSERT(tracked->accession_number == accession,
                     "Accession number should match");

    mpps_handler->stop();
    mwl_client.disconnect();
    return true;
}

// =============================================================================
// Test: Patient Lookup Integration
// =============================================================================

/**
 * @brief Test patient lookup and data validation for MWL creation
 */
bool test_patient_lookup_for_mwl_creation() {
    // Create patient with multiple identifiers
    auto patient = create_fhir_test_patient(
        "patient-002", "MRN-FHIR-002", "Kim", "Seonghyun");

    // Add additional identifier
    patient_identifier other_id;
    other_id.value = "INS-12345";
    other_id.system = "http://hospital.example.org/insurance";
    other_id.type_code = "AN";
    patient.identifiers.push_back(other_id);

    // Validate patient is suitable for MWL
    FHIR_E2E_ASSERT(!patient.mrn.empty(),
                     "Patient should have MRN for MWL");
    FHIR_E2E_ASSERT(patient.active,
                     "Patient should be active for MWL");
    FHIR_E2E_ASSERT(!patient.names.empty() &&
                         patient.names[0].family.has_value(),
                     "Patient should have family name");
    FHIR_E2E_ASSERT(patient.identifiers.size() >= 2,
                     "Patient should have multiple identifiers");

    // Verify patient query capabilities
    patient_query query;
    query.patient_id = patient.mrn;
    query.identifier_system = "http://hospital.example.org/mrn";

    FHIR_E2E_ASSERT(!query.is_empty(),
                     "Patient query should have criteria");
    FHIR_E2E_ASSERT(query.is_mrn_lookup(),
                     "Should be recognized as MRN lookup");

    // Create MWL with patient demographics
    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    std::string accession = pacs_system_test_fixture::generate_unique_accession();
    auto mwl_item = mwl_test_data_generator::create_item_with_accession(accession);
    mwl_item.patient.patient_id = patient.mrn;
    mwl_item.patient.patient_name = "KIM^SEONGHYUN";
    mwl_item.patient.patient_birth_date = "19800315";
    mwl_item.patient.patient_sex = "M";

    auto result = mwl_client.add_entry(mwl_item);
    FHIR_E2E_ASSERT(result.has_value(),
                     "MWL entry with patient demographics should be created");

    // Verify demographics in MWL
    pacs_adapter::mwl_query_filter filter;
    filter.accession_number = accession;
    auto query_result = mwl_client.query(filter);
    FHIR_E2E_ASSERT(query_result.has_value() &&
                         query_result->items.size() == 1,
                     "Should find MWL entry");
    FHIR_E2E_ASSERT(query_result->items[0].patient.patient_id == patient.mrn,
                     "MWL patient ID should match EMR MRN");

    mwl_client.disconnect();
    return true;
}

// =============================================================================
// Test: DiagnosticReport with Multiple Observations
// =============================================================================

/**
 * @brief Test building a DiagnosticReport with comprehensive findings
 */
bool test_diagnostic_report_comprehensive_build() {
    auto study = create_fhir_test_study_result(
        "1.2.840.10008.99.1", "patient-003", "ACC-COMP-001", "CT");

    // Build comprehensive report
    diagnostic_report_builder builder;
    builder.subject("Patient/patient-003")
        .encounter("Encounter/enc-003")
        .status(result_status::final_report)
        .code_loinc("24627-2", "CT Chest")
        .conclusion("1. No pulmonary embolism. "
                     "2. Mild bilateral pleural effusions. "
                     "3. No significant lymphadenopathy.")
        .effective_datetime("2026-02-07T10:00:00Z")
        .issued("2026-02-07T14:30:00Z")
        .performer("Practitioner/prac-002", "Dr. Diagnostic")
        .imaging_study("ImagingStudy/img-001")
        .based_on("ServiceRequest/sr-003");

    auto report_json = builder.build();
    FHIR_E2E_ASSERT(report_json.has_value(),
                     "Comprehensive report should build successfully");

    // Verify all required FHIR fields
    FHIR_E2E_ASSERT(report_json->find("resourceType") != std::string::npos,
                     "Should have resourceType");
    FHIR_E2E_ASSERT(report_json->find("subject") != std::string::npos,
                     "Should have subject");
    FHIR_E2E_ASSERT(report_json->find("encounter") != std::string::npos,
                     "Should have encounter");
    FHIR_E2E_ASSERT(report_json->find("final") != std::string::npos,
                     "Should have final status");
    FHIR_E2E_ASSERT(report_json->find("24627-2") != std::string::npos,
                     "Should have LOINC code");
    FHIR_E2E_ASSERT(report_json->find("conclusion") != std::string::npos,
                     "Should have conclusion");
    FHIR_E2E_ASSERT(report_json->find("performer") != std::string::npos,
                     "Should have performer");
    FHIR_E2E_ASSERT(report_json->find("imagingStudy") != std::string::npos,
                     "Should have imaging study reference");

    return true;
}

// =============================================================================
// Test: Result Status Lifecycle (Preliminary -> Final -> Amended)
// =============================================================================

/**
 * @brief Test result status progression through lifecycle stages
 */
bool test_result_status_lifecycle() {
    result_tracker_config config;
    config.max_entries = 1000;
    config.ttl = std::chrono::hours{24};
    in_memory_result_tracker tracker(config);

    std::string study_uid = "1.2.840.10008.99.LIFECYCLE";
    std::string accession = "ACC-LIFECYCLE-001";

    // Stage 1: Preliminary result
    {
        posted_result preliminary;
        preliminary.report_id = "report-prelim-001";
        preliminary.study_instance_uid = study_uid;
        preliminary.accession_number = accession;
        preliminary.status = result_status::preliminary;
        preliminary.posted_at = std::chrono::system_clock::now();

        auto result = tracker.track(preliminary);
        FHIR_E2E_ASSERT(result.is_ok(), "Preliminary tracking should succeed");

        auto tracked = tracker.get_by_study_uid(study_uid);
        FHIR_E2E_ASSERT(tracked.has_value() &&
                             tracked->status == result_status::preliminary,
                         "Should be preliminary status");
    }

    // Stage 2: Final result (update)
    {
        posted_result final_result;
        final_result.report_id = "report-final-001";
        final_result.study_instance_uid = study_uid;
        final_result.accession_number = accession;
        final_result.status = result_status::final_report;
        final_result.posted_at = std::chrono::system_clock::now();

        auto result = tracker.track(final_result);
        FHIR_E2E_ASSERT(result.is_ok(), "Final tracking should succeed");

        auto tracked = tracker.get_by_study_uid(study_uid);
        FHIR_E2E_ASSERT(tracked.has_value() &&
                             tracked->status == result_status::final_report,
                         "Should be final status after update");
    }

    // Stage 3: Amended result (correction)
    {
        posted_result amended;
        amended.report_id = "report-amended-001";
        amended.study_instance_uid = study_uid;
        amended.accession_number = accession;
        amended.status = result_status::amended;
        amended.posted_at = std::chrono::system_clock::now();

        auto result = tracker.track(amended);
        FHIR_E2E_ASSERT(result.is_ok(), "Amended tracking should succeed");

        auto tracked = tracker.get_by_study_uid(study_uid);
        FHIR_E2E_ASSERT(tracked.has_value() &&
                             tracked->status == result_status::amended,
                         "Should be amended status after correction");
    }

    return true;
}

// =============================================================================
// Test: Multi-System Integration (HIS + PACS + EMR)
// =============================================================================

/**
 * @brief Test complete multi-system workflow spanning HIS, PACS, and EMR
 *
 * Validates the full data flow:
 *   HIS (order) -> PACS Bridge (MWL/MPPS) -> EMR (result)
 */
bool test_multi_system_his_pacs_emr_workflow() {
    // Setup mock servers for RIS and EMR via MLLP
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    uint16_t emr_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    mock_ris_server ris(ris_config);

    mock_ris_server::config emr_config;
    emr_config.port = emr_port;
    mock_ris_server emr(emr_config);

    ris.start();
    emr.start();
    integration_test_fixture::wait_for(
        [&ris, &emr]() { return ris.is_running() && emr.is_running(); },
        std::chrono::milliseconds{2000});

    std::string accession = pacs_system_test_fixture::generate_unique_accession();
    std::string patient_id = "MULTI_SYS_PAT_001";
    std::string study_uid = "1.2.840.10008.99.MULTI." + accession;

    // --- HIS Phase: Order placement (ORM via MLLP to PACS Bridge) ---

    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = ris_port;
        mllp::mllp_client client(client_config);

        FHIR_E2E_ASSERT(client.connect().has_value(),
                         "Should connect to RIS");

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp[15];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                      std::localtime(&time_t_now));

        std::string orm_msg =
            "MSH|^~\\&|HIS|HOSPITAL|RIS|RADIOLOGY|" +
            std::string(timestamp) +
            "||ORM^O01|MULTI_001|P|2.4\r"
            "PID|1||" + patient_id + "|||MULTI^SYSTEM^PATIENT\r"
            "ORC|NW|ORD_MULTI_001||" + accession + "||SC\r"
            "OBR|1|ORD_MULTI_001||DX-CHEST\r";
        auto msg = mllp::mllp_message::from_string(orm_msg);
        auto send_result = client.send(msg);
        FHIR_E2E_ASSERT(send_result.has_value(),
                         "Order should be sent to RIS");
        client.disconnect();
    }

    FHIR_E2E_ASSERT(ris.messages_received() >= 1,
                     "RIS should receive order");

    // --- PACS Phase: MWL + MPPS lifecycle ---

    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    auto mwl_item = mwl_test_data_generator::create_item_with_accession(accession);
    mwl_item.patient.patient_id = patient_id;
    mwl_item.patient.patient_name = "MULTI^SYSTEM^PATIENT";
    (void)mwl_client.add_entry(mwl_item);

    auto mpps_config = pacs_system_test_fixture::create_mpps_test_config();
    auto mpps_handler = pacs_adapter::mpps_handler::create(mpps_config);

    auto mpps_dataset = mpps_test_data_generator::create_in_progress();
    mpps_dataset.accession_number = accession;
    mpps_dataset.patient_id = patient_id;
    mpps_dataset.study_instance_uid = study_uid;
    mpps_dataset.modality = "DX";

    (void)mpps_handler->on_n_create(mpps_dataset);

    mpps_dataset.status = pacs_adapter::mpps_event::completed;
    mpps_dataset.end_date = mpps_test_data_generator::get_today_date();
    mpps_dataset.end_time = mpps_test_data_generator::get_offset_time(20);
    (void)mpps_handler->on_n_set(mpps_dataset);

    // --- EMR Phase: DiagnosticReport and result delivery ---

    // Build FHIR DiagnosticReport
    diagnostic_report_builder builder;
    builder.subject("Patient/" + patient_id)
        .status(result_status::final_report)
        .code_loinc("36643-5", "Chest X-ray 2 Views")
        .conclusion("No acute cardiopulmonary abnormality.")
        .effective_datetime("2026-02-07T10:00:00Z");

    auto report_json = builder.build();
    FHIR_E2E_ASSERT(report_json.has_value(),
                     "DiagnosticReport should be built");

    // Send ORU result to EMR
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = emr_port;
        mllp::mllp_client client(client_config);

        FHIR_E2E_ASSERT(client.connect().has_value(),
                         "Should connect to EMR");

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp[15];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                      std::localtime(&time_t_now));

        std::string oru_msg =
            "MSH|^~\\&|PACS|RADIOLOGY|EMR|HOSPITAL|" +
            std::string(timestamp) +
            "||ORU^R01|MULTI_RES_001|P|2.4\r"
            "PID|1||" + patient_id + "|||MULTI^SYSTEM^PATIENT\r"
            "OBR|1|ORD_MULTI_001|ORD_MULTI_001|DX-CHEST|||" +
            std::string(timestamp) + "|||||||||||||||F\r"
            "OBX|1|TX|IMPRESSION||NO ACUTE CARDIOPULMONARY ABNORMALITY||||||F\r";
        auto msg = mllp::mllp_message::from_string(oru_msg);
        auto send_result = client.send(msg);
        FHIR_E2E_ASSERT(send_result.has_value(),
                         "Result should be sent to EMR");
        client.disconnect();
    }

    FHIR_E2E_ASSERT(emr.messages_received() >= 1,
                     "EMR should receive result");

    // Track result
    result_tracker_config tracker_config;
    tracker_config.max_entries = 1000;
    tracker_config.ttl = std::chrono::hours{24};
    in_memory_result_tracker tracker(tracker_config);

    posted_result posted;
    posted.report_id = "report-multi-" + accession;
    posted.study_instance_uid = study_uid;
    posted.accession_number = accession;
    posted.status = result_status::final_report;
    posted.posted_at = std::chrono::system_clock::now();

    auto track_result = tracker.track(posted);
    FHIR_E2E_ASSERT(track_result.is_ok(), "Result tracking should succeed");

    mpps_handler->stop();
    mwl_client.disconnect();
    ris.stop();
    emr.stop();
    return true;
}

// =============================================================================
// Test: Incomplete Study Result Validation
// =============================================================================

/**
 * @brief Test validation catches incomplete study results
 */
bool test_incomplete_study_result_handling() {
    // Result with missing required fields
    study_result incomplete;
    incomplete.study_instance_uid = "1.2.3.4.5";
    // Missing: patient_id, modality, study_datetime, etc.

    FHIR_E2E_ASSERT(!incomplete.is_valid(),
                     "Incomplete result should fail validation");

    // Result with all required fields
    auto complete = create_fhir_test_study_result(
        "1.2.840.10008.99.VALID", "patient-valid", "ACC-VALID", "CT");
    FHIR_E2E_ASSERT(complete.is_valid(),
                     "Complete result should pass validation");

    return true;
}

// =============================================================================
// Test: Retry Policy Configuration
// =============================================================================

/**
 * @brief Test retry policy backoff calculations for transient failures
 */
bool test_retry_policy_backoff() {
    retry_policy policy;
    policy.max_retries = 5;
    policy.initial_backoff = std::chrono::milliseconds{100};
    policy.max_backoff = std::chrono::milliseconds{10000};
    policy.backoff_multiplier = 2.0;

    FHIR_E2E_ASSERT(policy.max_retries == 5,
                     "Max retries should be 5");

    // Verify exponential backoff progression
    auto delay0 = policy.backoff_for(0);
    auto delay1 = policy.backoff_for(1);
    auto delay2 = policy.backoff_for(2);
    auto delay3 = policy.backoff_for(3);

    FHIR_E2E_ASSERT(delay0 < delay1, "Delay should increase");
    FHIR_E2E_ASSERT(delay1 < delay2, "Delay should keep increasing");
    FHIR_E2E_ASSERT(delay2 < delay3, "Delay should continue increasing");

    // Verify max backoff cap
    auto delay_max = policy.backoff_for(100);
    FHIR_E2E_ASSERT(delay_max <= policy.max_backoff,
                     "Delay should not exceed max backoff");

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_fhir_workflow_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=============================================" << std::endl;
    std::cout << "FHIR Workflow E2E Tests" << std::endl;
    std::cout << "Phase 5c - Issue #321" << std::endl;
    std::cout << "=============================================" << std::endl;

    std::cout << "\n--- Complete Workflow Tests ---" << std::endl;
    RUN_FHIR_TEST(test_fhir_service_request_to_diagnostic_report);
    RUN_FHIR_TEST(test_patient_lookup_for_mwl_creation);

    std::cout << "\n--- DiagnosticReport Tests ---" << std::endl;
    RUN_FHIR_TEST(test_diagnostic_report_comprehensive_build);
    RUN_FHIR_TEST(test_result_status_lifecycle);

    std::cout << "\n--- Multi-System Integration ---" << std::endl;
    RUN_FHIR_TEST(test_multi_system_his_pacs_emr_workflow);

    std::cout << "\n--- Error Handling Tests ---" << std::endl;
    RUN_FHIR_TEST(test_incomplete_study_result_handling);
    RUN_FHIR_TEST(test_retry_policy_backoff);

    std::cout << "\n=============================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;
    if (passed + failed > 0) {
        double pass_rate = (passed * 100.0) / (passed + failed);
        std::cout << "Pass Rate: " << pass_rate << "%" << std::endl;
    }
    std::cout << "=============================================" << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::e2e::test

int main() {
    return pacs::bridge::e2e::test::run_all_fhir_workflow_tests();
}
