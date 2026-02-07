/**
 * @file hl7_to_mpps_workflow_test.cpp
 * @brief End-to-end workflow tests for HL7 -> MWL -> MPPS -> HL7 pipeline
 *
 * Tests the complete IHE Scheduled Workflow profile:
 *   1. HL7 ORM^O01 order received via MLLP -> MWL entry created
 *   2. Modality queries MWL and starts procedure (MPPS N-CREATE)
 *   3. MPPS IN PROGRESS persisted -> ORM^O01 (IP) sent to RIS
 *   4. Modality completes procedure (MPPS N-SET COMPLETED)
 *   5. MPPS COMPLETED persisted -> ORM^O01 (CM) sent to RIS
 *   6. Result ORU^R01 sent to EMR
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/321
 * @see docs/reference_materials/06_ihe_swf_profile.md
 */

#include "integration_test_base.h"
#include "pacs_system_test_base.h"

#include "pacs/bridge/emr/diagnostic_report_builder.h"
#include "pacs/bridge/emr/patient_lookup.h"
#include "pacs/bridge/emr/result_tracker.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::e2e::test {

using namespace pacs::bridge::integration::test;

// =============================================================================
// Test Macros
// =============================================================================

#define E2E_ASSERT(condition, message)                                         \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_E2E_TEST(test_func)                                                \
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
// HL7 Message Templates
// =============================================================================

namespace hl7_templates {

/**
 * @brief Build ORM^O01 new order message
 */
std::string build_orm_new_order(const std::string& patient_id,
                                const std::string& patient_name,
                                const std::string& order_id,
                                const std::string& accession,
                                const std::string& procedure_code,
                                const std::string& msg_control_id) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char timestamp[15];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                  std::localtime(&time_t_now));

    return "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|" +
           std::string(timestamp) +
           "||ORM^O01|" + msg_control_id + "|P|2.4\r"
           "PID|1||" + patient_id + "|||" + patient_name + "\r"
           "ORC|NW|" + order_id + "||" + accession + "||SC\r"
           "OBR|1|" + order_id + "||" + procedure_code + "\r";
}

/**
 * @brief Build ORM^O01 status update message (IP/CM/DC)
 */
std::string build_orm_status_update(const std::string& patient_id,
                                    const std::string& patient_name,
                                    const std::string& order_id,
                                    const std::string& accession,
                                    const std::string& procedure_code,
                                    const std::string& status_code,
                                    const std::string& msg_control_id) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char timestamp[15];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                  std::localtime(&time_t_now));

    return "MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|" +
           std::string(timestamp) +
           "||ORM^O01|" + msg_control_id + "|P|2.4\r"
           "PID|1||" + patient_id + "|||" + patient_name + "\r"
           "ORC|SC|" + order_id + "||" + accession + "||" + status_code + "\r"
           "OBR|1|" + order_id + "||" + procedure_code + "\r";
}

/**
 * @brief Build ORU^R01 result message
 */
std::string build_oru_result(const std::string& patient_id,
                             const std::string& patient_name,
                             const std::string& order_id,
                             const std::string& procedure_code,
                             const std::string& impression,
                             const std::string& msg_control_id) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char timestamp[15];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                  std::localtime(&time_t_now));

    return "MSH|^~\\&|PACS|RADIOLOGY|EMR|HOSPITAL|" +
           std::string(timestamp) +
           "||ORU^R01|" + msg_control_id + "|P|2.4\r"
           "PID|1||" + patient_id + "|||" + patient_name + "\r"
           "OBR|1|" + order_id + "|" + order_id + "|" + procedure_code +
           "|||" + std::string(timestamp) + "|||||||||||||||F\r"
           "OBX|1|TX|IMPRESSION||" + impression + "||||||F\r";
}

/**
 * @brief Build ORM^O01 order cancellation message
 */
std::string build_orm_cancel(const std::string& patient_id,
                             const std::string& patient_name,
                             const std::string& order_id,
                             const std::string& accession,
                             const std::string& procedure_code,
                             const std::string& msg_control_id) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char timestamp[15];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                  std::localtime(&time_t_now));

    return "MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|" +
           std::string(timestamp) +
           "||ORM^O01|" + msg_control_id + "|P|2.4\r"
           "PID|1||" + patient_id + "|||" + patient_name + "\r"
           "ORC|CA|" + order_id + "||" + accession + "||DC\r"
           "OBR|1|" + order_id + "||" + procedure_code + "\r";
}

}  // namespace hl7_templates

// =============================================================================
// Test: Complete HL7 Order -> MWL -> MPPS -> Result Workflow
// =============================================================================

/**
 * @brief Test the complete IHE Scheduled Workflow profile
 *
 * Validates the full round-trip:
 *   HIS --[ORM^O01 NW]--> PACS Bridge --[MWL]--> Create worklist entry
 *   Modality --[MPPS N-CREATE]--> PACS Bridge --[ORM^O01 IP]--> RIS
 *   Modality --[MPPS N-SET CM]--> PACS Bridge --[ORM^O01 CM]--> RIS
 *   PACS Bridge --[ORU^R01]--> EMR
 */
bool test_hl7_order_to_mpps_complete_workflow() {
    // Setup: RIS and EMR mock servers
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    uint16_t emr_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;
    mock_ris_server ris(ris_config);

    mock_ris_server::config emr_config;
    emr_config.port = emr_port;
    emr_config.auto_ack = true;
    mock_ris_server emr(emr_config);

    E2E_ASSERT(ris.start(), "Failed to start RIS server");
    E2E_ASSERT(emr.start(), "Failed to start EMR server");
    E2E_ASSERT(
        integration_test_fixture::wait_for(
            [&ris, &emr]() { return ris.is_running() && emr.is_running(); },
            std::chrono::milliseconds{2000}),
        "Servers should start");

    const std::string patient_id = "E2E_PAT_001";
    const std::string patient_name = "WORKFLOW^COMPLETE^TEST";
    const std::string order_id = "E2E_ORD_001";
    const std::string accession = "E2E_ACC_001";
    const std::string procedure_code = "CT-CHEST";

    // --- Phase 1: Create MWL entry (simulates HIS order receipt) ---

    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    auto mwl_item = mwl_test_data_generator::create_item_with_accession(accession);
    mwl_item.patient.patient_id = patient_id;
    mwl_item.patient.patient_name = patient_name;
    if (!mwl_item.scheduled_steps.empty()) {
        mwl_item.scheduled_steps[0].modality = "CT";
        mwl_item.scheduled_steps[0].scheduled_station_ae_title = "CT_SCANNER_1";
    }

    auto add_result = mwl_client.add_entry(mwl_item);
    E2E_ASSERT(add_result.has_value(), "MWL entry should be created");

    // Verify MWL entry is queryable
    pacs_adapter::mwl_query_filter filter;
    filter.accession_number = accession;
    auto mwl_query = mwl_client.query(filter);
    E2E_ASSERT(mwl_query.has_value() && mwl_query->items.size() == 1,
               "MWL entry should be queryable");
    E2E_ASSERT(mwl_query->items[0].patient.patient_id == patient_id,
               "MWL patient ID should match");

    // --- Phase 2: MPPS N-CREATE (procedure starts) ---

    auto mpps_config = pacs_system_test_fixture::create_mpps_test_config();
    auto mpps_handler = pacs_adapter::mpps_handler::create(mpps_config);

    std::vector<std::pair<std::string, std::string>> mpps_events;
    std::mutex events_mutex;
    mpps_handler->set_callback(
        [&mpps_events, &events_mutex](pacs_adapter::mpps_event event,
                                      const pacs_adapter::mpps_dataset& dataset) {
            std::lock_guard<std::mutex> lock(events_mutex);
            mpps_events.emplace_back(pacs_adapter::to_string(event),
                                     dataset.accession_number);
        });

    auto mpps_dataset = mpps_test_data_generator::create_in_progress();
    mpps_dataset.accession_number = accession;
    mpps_dataset.patient_id = patient_id;
    mpps_dataset.patient_name = patient_name;
    std::string sop_uid = mpps_dataset.sop_instance_uid;

    auto create_result = mpps_handler->on_n_create(mpps_dataset);
    E2E_ASSERT(create_result.has_value(), "MPPS N-CREATE should succeed");

    // Verify MPPS persisted as IN PROGRESS
    auto mpps_query = mpps_handler->query_mpps(sop_uid);
    E2E_ASSERT(mpps_query.has_value() && mpps_query->has_value(),
               "MPPS record should exist");
    E2E_ASSERT(mpps_query->value().status ==
                   pacs_adapter::mpps_event::in_progress,
               "MPPS status should be IN PROGRESS");

    // --- Phase 3: Send ORM^O01 IP to RIS (status update) ---

    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = ris_port;
        mllp::mllp_client client(client_config);

        E2E_ASSERT(client.connect().has_value(), "Should connect to RIS");

        auto orm_ip = hl7_templates::build_orm_status_update(
            patient_id, patient_name, order_id, accession,
            procedure_code, "IP", "E2E_MSG_001");
        auto msg = mllp::mllp_message::from_string(orm_ip);
        auto send_result = client.send(msg);
        E2E_ASSERT(send_result.has_value(),
                    "Should send IP status update to RIS");
        client.disconnect();
    }

    E2E_ASSERT(ris.messages_received() >= 1,
               "RIS should receive IP status update");

    // --- Phase 4: MPPS N-SET COMPLETED (procedure finishes) ---

    mpps_dataset.status = pacs_adapter::mpps_event::completed;
    mpps_dataset.end_date = mpps_test_data_generator::get_today_date();
    mpps_dataset.end_time = mpps_test_data_generator::get_offset_time(30);

    auto set_result = mpps_handler->on_n_set(mpps_dataset);
    E2E_ASSERT(set_result.has_value(), "MPPS N-SET COMPLETED should succeed");

    // Verify MPPS persisted as COMPLETED
    auto mpps_query2 = mpps_handler->query_mpps(sop_uid);
    E2E_ASSERT(mpps_query2.has_value() && mpps_query2->has_value(),
               "MPPS record should still exist");
    E2E_ASSERT(mpps_query2->value().status ==
                   pacs_adapter::mpps_event::completed,
               "MPPS status should be COMPLETED");

    // --- Phase 5: Send ORM^O01 CM to RIS (completion) ---

    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = ris_port;
        mllp::mllp_client client(client_config);

        E2E_ASSERT(client.connect().has_value(), "Should connect to RIS");

        auto orm_cm = hl7_templates::build_orm_status_update(
            patient_id, patient_name, order_id, accession,
            procedure_code, "CM", "E2E_MSG_002");
        auto msg = mllp::mllp_message::from_string(orm_cm);
        auto send_result = client.send(msg);
        E2E_ASSERT(send_result.has_value(),
                    "Should send CM status update to RIS");
        client.disconnect();
    }

    E2E_ASSERT(ris.messages_received() >= 2,
               "RIS should receive CM status update");

    // --- Phase 6: Send ORU^R01 result to EMR ---

    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = emr_port;
        mllp::mllp_client client(client_config);

        E2E_ASSERT(client.connect().has_value(), "Should connect to EMR");

        auto oru_msg = hl7_templates::build_oru_result(
            patient_id, patient_name, order_id, procedure_code,
            "NO ACUTE FINDINGS", "E2E_MSG_003");
        auto msg = mllp::mllp_message::from_string(oru_msg);
        auto send_result = client.send(msg);
        E2E_ASSERT(send_result.has_value(),
                    "Should send result to EMR");
        client.disconnect();
    }

    E2E_ASSERT(emr.messages_received() >= 1,
               "EMR should receive result message");

    // --- Verification: MPPS callback events ---
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        E2E_ASSERT(mpps_events.size() >= 2,
                    "Should have at least 2 MPPS events (IP + CM)");
    }

    // Cleanup
    mpps_handler->stop();
    mwl_client.disconnect();
    ris.stop();
    emr.stop();
    return true;
}

// =============================================================================
// Test: Order Cancellation Workflow (MPPS Discontinuation)
// =============================================================================

/**
 * @brief Test order cancellation with MPPS discontinuation
 *
 * Validates:
 *   1. MWL entry created for order
 *   2. MPPS started (IN PROGRESS)
 *   3. Procedure discontinued (patient refused)
 *   4. Cancellation message (DC) sent to RIS
 */
bool test_hl7_order_cancellation_workflow() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    mock_ris_server ris(ris_config);

    E2E_ASSERT(ris.start(), "Failed to start RIS");
    E2E_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{2000}),
        "RIS should start");

    const std::string patient_id = "E2E_PAT_002";
    const std::string patient_name = "CANCEL^WORKFLOW^TEST";
    const std::string order_id = "E2E_ORD_002";
    const std::string accession = "E2E_ACC_002";
    const std::string procedure_code = "MR-BRAIN";

    // Create MWL entry
    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    auto mwl_item = mwl_test_data_generator::create_item_with_accession(accession);
    mwl_item.patient.patient_id = patient_id;
    mwl_item.patient.patient_name = patient_name;
    if (!mwl_item.scheduled_steps.empty()) {
        mwl_item.scheduled_steps[0].modality = "MR";
    }
    (void)mwl_client.add_entry(mwl_item);

    // Start MPPS (IN PROGRESS)
    auto mpps_config = pacs_system_test_fixture::create_mpps_test_config();
    auto mpps_handler = pacs_adapter::mpps_handler::create(mpps_config);

    auto mpps_dataset = mpps_test_data_generator::create_in_progress();
    mpps_dataset.accession_number = accession;
    mpps_dataset.patient_id = patient_id;
    mpps_dataset.modality = "MR";
    std::string sop_uid = mpps_dataset.sop_instance_uid;

    auto create_result = mpps_handler->on_n_create(mpps_dataset);
    E2E_ASSERT(create_result.has_value(), "N-CREATE should succeed");

    // Discontinue MPPS (patient refused)
    mpps_dataset.status = pacs_adapter::mpps_event::discontinued;
    mpps_dataset.end_date = mpps_test_data_generator::get_today_date();
    mpps_dataset.end_time = mpps_test_data_generator::get_offset_time(5);
    mpps_dataset.discontinuation_reason = "Patient refused";

    auto set_result = mpps_handler->on_n_set(mpps_dataset);
    E2E_ASSERT(set_result.has_value(), "N-SET DISCONTINUED should succeed");

    // Verify MPPS status
    auto mpps_query = mpps_handler->query_mpps(sop_uid);
    E2E_ASSERT(mpps_query.has_value() && mpps_query->has_value(),
               "MPPS record should exist");
    E2E_ASSERT(mpps_query->value().status ==
                   pacs_adapter::mpps_event::discontinued,
               "MPPS should be DISCONTINUED");
    E2E_ASSERT(mpps_query->value().discontinuation_reason == "Patient refused",
               "Discontinuation reason should match");

    // Send cancellation to RIS
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = ris_port;
        mllp::mllp_client client(client_config);

        E2E_ASSERT(client.connect().has_value(), "Should connect to RIS");

        auto orm_cancel = hl7_templates::build_orm_cancel(
            patient_id, patient_name, order_id, accession,
            procedure_code, "E2E_MSG_010");
        auto msg = mllp::mllp_message::from_string(orm_cancel);
        auto send_result = client.send(msg);
        E2E_ASSERT(send_result.has_value(),
                    "Should send cancellation to RIS");
        client.disconnect();
    }

    E2E_ASSERT(ris.messages_received() >= 1,
               "RIS should receive cancellation");

    // Verify MWL entry can be cancelled
    auto cancel_result = mwl_client.cancel_entry(accession);
    E2E_ASSERT(cancel_result.has_value(), "MWL cancellation should succeed");

    mpps_handler->stop();
    mwl_client.disconnect();
    ris.stop();
    return true;
}

// =============================================================================
// Test: Multi-Procedure Concurrent Workflow
// =============================================================================

/**
 * @brief Test multiple concurrent procedures across different modalities
 *
 * Validates:
 *   1. Multiple MWL entries created for different patients/modalities
 *   2. MPPS started independently on CT, MR, US scanners
 *   3. Procedures complete in arbitrary order
 *   4. Status updates sent to RIS for each
 */
bool test_multi_procedure_concurrent_workflow() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    mock_ris_server ris(ris_config);

    E2E_ASSERT(ris.start(), "Failed to start RIS");
    E2E_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{2000}),
        "RIS should start");

    // Create MWL entries
    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    struct procedure_info {
        std::string patient_id;
        std::string patient_name;
        std::string accession;
        std::string modality;
        std::string station;
    };

    std::vector<procedure_info> procedures = {
        {"E2E_PAT_010", "DOE^JOHN", "E2E_ACC_010", "CT", "CT_SCANNER_1"},
        {"E2E_PAT_011", "SMITH^JANE", "E2E_ACC_011", "MR", "MR_SCANNER_1"},
        {"E2E_PAT_012", "WILSON^TOM", "E2E_ACC_012", "US", "US_SCANNER_1"},
    };

    // Create MWL entries for all procedures
    for (const auto& proc : procedures) {
        auto item = mwl_test_data_generator::create_item_with_accession(
            proc.accession);
        item.patient.patient_id = proc.patient_id;
        item.patient.patient_name = proc.patient_name;
        if (!item.scheduled_steps.empty()) {
            item.scheduled_steps[0].modality = proc.modality;
            item.scheduled_steps[0].scheduled_station_ae_title = proc.station;
        }
        auto result = mwl_client.add_entry(item);
        E2E_ASSERT(result.has_value(),
                    "MWL entry for " + proc.patient_id + " should be created");
    }

    // Start MPPS for all procedures
    auto mpps_config = pacs_system_test_fixture::create_mpps_test_config();
    auto mpps_handler = pacs_adapter::mpps_handler::create(mpps_config);

    std::atomic<int> completed_count{0};
    mpps_handler->set_callback(
        [&completed_count](pacs_adapter::mpps_event event,
                           const pacs_adapter::mpps_dataset& /*dataset*/) {
            if (event == pacs_adapter::mpps_event::completed) {
                completed_count++;
            }
        });

    std::vector<pacs_adapter::mpps_dataset> mpps_datasets;
    for (const auto& proc : procedures) {
        auto ds = mpps_test_data_generator::create_with_station(proc.station);
        ds.accession_number = proc.accession;
        ds.patient_id = proc.patient_id;
        ds.patient_name = proc.patient_name;
        ds.modality = proc.modality;
        mpps_datasets.push_back(ds);

        auto result = mpps_handler->on_n_create(ds);
        E2E_ASSERT(result.has_value(),
                    "N-CREATE for " + proc.modality + " should succeed");
    }

    // Verify all procedures active
    auto active = mpps_handler->get_active_mpps();
    E2E_ASSERT(active.has_value() && active->size() >= 3,
               "Should have 3 active procedures");

    // Complete procedures in reverse order (US, MR, CT)
    for (int i = static_cast<int>(mpps_datasets.size()) - 1; i >= 0; --i) {
        mpps_datasets[i].status = pacs_adapter::mpps_event::completed;
        mpps_datasets[i].end_date = mpps_test_data_generator::get_today_date();
        mpps_datasets[i].end_time =
            mpps_test_data_generator::get_offset_time(20 + i * 10);
        auto result = mpps_handler->on_n_set(mpps_datasets[i]);
        E2E_ASSERT(result.has_value(),
                    "N-SET COMPLETED for procedure " +
                        std::to_string(i) + " should succeed");
    }

    E2E_ASSERT(completed_count == 3, "All 3 procedures should be completed");

    // Verify no active procedures remain
    auto active_after = mpps_handler->get_active_mpps();
    E2E_ASSERT(active_after.has_value() && active_after->empty(),
               "No active procedures should remain");

    // Send all completion status updates to RIS
    int msg_idx = 0;
    for (const auto& proc : procedures) {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = ris_port;
        mllp::mllp_client client(client_config);

        if (client.connect().has_value()) {
            auto orm_cm = hl7_templates::build_orm_status_update(
                proc.patient_id, proc.patient_name,
                "ORD_" + std::to_string(msg_idx), proc.accession,
                proc.modality, "CM",
                "E2E_MULTI_" + std::to_string(msg_idx));
            auto msg = mllp::mllp_message::from_string(orm_cm);
            (void)client.send(msg);
            client.disconnect();
        }
        msg_idx++;
    }

    E2E_ASSERT(ris.messages_received() >= 3,
               "RIS should receive all 3 completion messages");

    mpps_handler->stop();
    mwl_client.disconnect();
    ris.stop();
    return true;
}

// =============================================================================
// Test: MWL-MPPS Accession Number Correlation
// =============================================================================

/**
 * @brief Test that MWL entries and MPPS records correctly correlate
 *        via accession number throughout the workflow
 */
bool test_mwl_mpps_accession_correlation() {
    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    std::string accession = pacs_system_test_fixture::generate_unique_accession();
    std::string patient_id = "E2E_CORR_PAT_001";

    // Create MWL entry
    auto mwl_item = mwl_test_data_generator::create_item_with_accession(accession);
    mwl_item.patient.patient_id = patient_id;
    (void)mwl_client.add_entry(mwl_item);

    // Create MPPS with same accession
    auto mpps_config = pacs_system_test_fixture::create_mpps_test_config();
    auto mpps_handler = pacs_adapter::mpps_handler::create(mpps_config);

    auto mpps_dataset = mpps_test_data_generator::create_in_progress();
    mpps_dataset.accession_number = accession;
    mpps_dataset.patient_id = patient_id;
    (void)mpps_handler->on_n_create(mpps_dataset);

    // Query MWL by accession
    pacs_adapter::mwl_query_filter mwl_filter;
    mwl_filter.accession_number = accession;
    auto mwl_result = mwl_client.query(mwl_filter);
    E2E_ASSERT(mwl_result.has_value() && mwl_result->items.size() == 1,
               "Should find exactly 1 MWL entry");

    // Query MPPS by accession
    pacs_adapter::mpps_handler::mpps_query_params mpps_params;
    mpps_params.accession_number = accession;
    auto mpps_result = mpps_handler->query_mpps(mpps_params);
    E2E_ASSERT(mpps_result.has_value() && mpps_result->size() >= 1,
               "Should find MPPS record by accession");

    // Verify patient ID correlation
    E2E_ASSERT(mwl_result->items[0].patient.patient_id ==
                   (*mpps_result)[0].patient_id,
               "Patient ID should match between MWL and MPPS");

    // Complete MPPS
    mpps_dataset.status = pacs_adapter::mpps_event::completed;
    mpps_dataset.end_date = mpps_test_data_generator::get_today_date();
    mpps_dataset.end_time = mpps_test_data_generator::get_offset_time(25);
    (void)mpps_handler->on_n_set(mpps_dataset);

    // Verify completed status
    auto final_query = mpps_handler->query_mpps(mpps_dataset.sop_instance_uid);
    E2E_ASSERT(final_query.has_value() && final_query->has_value(),
               "Completed MPPS should be queryable");
    E2E_ASSERT(final_query->value().status ==
                   pacs_adapter::mpps_event::completed,
               "Final status should be COMPLETED");

    mpps_handler->stop();
    mwl_client.disconnect();
    return true;
}

// =============================================================================
// Test: RIS Failover During Workflow
// =============================================================================

/**
 * @brief Test workflow continues when primary RIS is unavailable
 *
 * Validates:
 *   1. MPPS workflow proceeds independently of RIS availability
 *   2. Status messages fail gracefully when RIS is down
 *   3. Status messages succeed when backup RIS is available
 */
bool test_workflow_with_ris_failover() {
    uint16_t primary_port = integration_test_fixture::generate_test_port();
    uint16_t backup_port = integration_test_fixture::generate_test_port();

    // Only start backup RIS (primary is "down")
    mock_ris_server::config backup_config;
    backup_config.port = backup_port;
    mock_ris_server backup_ris(backup_config);

    E2E_ASSERT(backup_ris.start(), "Backup RIS should start");
    E2E_ASSERT(
        integration_test_fixture::wait_for(
            [&backup_ris]() { return backup_ris.is_running(); },
            std::chrono::milliseconds{2000}),
        "Backup RIS should be running");

    // MPPS workflow proceeds regardless of RIS
    auto mpps_config = pacs_system_test_fixture::create_mpps_test_config();
    auto mpps_handler = pacs_adapter::mpps_handler::create(mpps_config);

    auto mpps_dataset = mpps_test_data_generator::create_in_progress();
    auto create_result = mpps_handler->on_n_create(mpps_dataset);
    E2E_ASSERT(create_result.has_value(),
               "MPPS N-CREATE should succeed regardless of RIS");

    // Try primary RIS (should fail)
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = primary_port;
        client_config.connect_timeout = std::chrono::milliseconds{500};
        mllp::mllp_client client(client_config);

        auto connect_result = client.connect();
        E2E_ASSERT(!connect_result.has_value(),
                    "Primary RIS connection should fail");
    }

    // Failover: send to backup RIS
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = backup_port;
        mllp::mllp_client client(client_config);

        E2E_ASSERT(client.connect().has_value(),
                    "Backup RIS connection should succeed");

        auto orm_ip = hl7_templates::build_orm_status_update(
            mpps_dataset.patient_id, mpps_dataset.patient_name,
            "ORD_FAILOVER", mpps_dataset.accession_number,
            "CT", "IP", "E2E_FAILOVER_001");
        auto msg = mllp::mllp_message::from_string(orm_ip);
        auto send_result = client.send(msg);
        E2E_ASSERT(send_result.has_value(),
                    "Backup RIS should receive message");
        client.disconnect();
    }

    E2E_ASSERT(backup_ris.messages_received() >= 1,
               "Backup RIS should receive failover message");

    // Complete MPPS
    mpps_dataset.status = pacs_adapter::mpps_event::completed;
    mpps_dataset.end_date = mpps_test_data_generator::get_today_date();
    mpps_dataset.end_time = mpps_test_data_generator::get_offset_time(20);
    auto set_result = mpps_handler->on_n_set(mpps_dataset);
    E2E_ASSERT(set_result.has_value(),
               "MPPS completion should succeed regardless of RIS");

    mpps_handler->stop();
    backup_ris.stop();
    return true;
}

// =============================================================================
// Test: Workflow Error Resilience
// =============================================================================

/**
 * @brief Test that workflow continues after individual operation failures
 */
bool test_workflow_error_resilience() {
    auto mpps_config = pacs_system_test_fixture::create_mpps_test_config();
    auto mpps_handler = pacs_adapter::mpps_handler::create(mpps_config);

    int successful_callbacks = 0;
    mpps_handler->set_callback(
        [&successful_callbacks](pacs_adapter::mpps_event /*event*/,
                                const pacs_adapter::mpps_dataset& /*dataset*/) {
            successful_callbacks++;
        });

    // Valid procedure 1
    auto valid1 = mpps_test_data_generator::create_in_progress();
    auto result1 = mpps_handler->on_n_create(valid1);
    E2E_ASSERT(result1.has_value(), "First valid N-CREATE should succeed");

    // Invalid procedure (empty dataset should fail)
    pacs_adapter::mpps_dataset invalid;
    auto invalid_result = mpps_handler->on_n_create(invalid);
    E2E_ASSERT(!invalid_result.has_value(),
               "Invalid N-CREATE should fail gracefully");

    // Valid procedure 2 (workflow continues)
    auto valid2 = mpps_test_data_generator::create_in_progress();
    auto result2 = mpps_handler->on_n_create(valid2);
    E2E_ASSERT(result2.has_value(),
               "Second valid N-CREATE should succeed after error");

    // Complete both valid procedures
    valid1.status = pacs_adapter::mpps_event::completed;
    valid1.end_date = mpps_test_data_generator::get_today_date();
    valid1.end_time = mpps_test_data_generator::get_offset_time(15);
    (void)mpps_handler->on_n_set(valid1);

    valid2.status = pacs_adapter::mpps_event::completed;
    valid2.end_date = mpps_test_data_generator::get_today_date();
    valid2.end_time = mpps_test_data_generator::get_offset_time(20);
    (void)mpps_handler->on_n_set(valid2);

    E2E_ASSERT(successful_callbacks == 4,
               "Should have 4 callbacks (2 create + 2 complete)");

    mpps_handler->stop();
    return true;
}

// =============================================================================
// Test: High-Volume Workflow
// =============================================================================

/**
 * @brief Test high-volume workflow with multiple rapid MPPS operations and
 *        concurrent MLLP message delivery
 */
bool test_high_volume_hl7_mpps_workflow() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    mock_ris_server ris(ris_config);

    E2E_ASSERT(ris.start(), "Failed to start RIS");
    E2E_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{2000}),
        "RIS should start");

    auto mpps_config = pacs_system_test_fixture::create_mpps_test_config();
    auto mpps_handler = pacs_adapter::mpps_handler::create(mpps_config);

    mpps_handler->set_callback(
        [](pacs_adapter::mpps_event, const pacs_adapter::mpps_dataset&) {});

    const size_t num_procedures = 50;
    auto start_time = std::chrono::high_resolution_clock::now();

    // Create and complete procedures rapidly
    for (size_t i = 0; i < num_procedures; ++i) {
        auto ds = mpps_test_data_generator::create_in_progress();
        (void)mpps_handler->on_n_create(ds);

        ds.status = pacs_adapter::mpps_event::completed;
        ds.end_date = mpps_test_data_generator::get_today_date();
        ds.end_time = mpps_test_data_generator::get_offset_time(30);
        (void)mpps_handler->on_n_set(ds);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    std::cout << "  " << num_procedures << " MPPS workflows in "
              << duration.count() << "ms" << std::endl;

    E2E_ASSERT(duration.count() < 30000,
               "Should process 50 procedures in under 30 seconds");

    // Verify statistics
    auto stats = mpps_handler->get_statistics();
    E2E_ASSERT(stats.n_create_count >= num_procedures,
               "Should have all N-CREATEs recorded");
    E2E_ASSERT(stats.completed_count >= num_procedures,
               "Should have all completions recorded");

    // Send concurrent MLLP messages
    const int msg_count = 10;
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < msg_count; ++i) {
        threads.emplace_back([&, i]() {
            mllp::mllp_client_config client_config;
            client_config.host = "localhost";
            client_config.port = ris_port;
            mllp::mllp_client client(client_config);

            if (!client.connect().has_value()) return;

            auto orm_msg = hl7_templates::build_orm_status_update(
                "PAT_VOL" + std::to_string(i), "VOL^PATIENT",
                "ORD_VOL" + std::to_string(i),
                "ACC_VOL" + std::to_string(i), "CT", "CM",
                "VOL_MSG_" + std::to_string(i));
            auto msg = mllp::mllp_message::from_string(orm_msg);
            if (client.send(msg).has_value()) {
                success_count++;
            }
            client.disconnect();
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    E2E_ASSERT(success_count == msg_count,
               "All " + std::to_string(msg_count) +
                   " MLLP messages should succeed");

    mpps_handler->stop();
    ris.stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_hl7_mpps_workflow_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=============================================" << std::endl;
    std::cout << "HL7 -> MWL -> MPPS -> HL7 E2E Workflow Tests" << std::endl;
    std::cout << "Phase 5c - Issue #321" << std::endl;
    std::cout << "=============================================" << std::endl;

    std::cout << "\n--- Complete Workflow Tests ---" << std::endl;
    RUN_E2E_TEST(test_hl7_order_to_mpps_complete_workflow);
    RUN_E2E_TEST(test_hl7_order_cancellation_workflow);

    std::cout << "\n--- Multi-Procedure Tests ---" << std::endl;
    RUN_E2E_TEST(test_multi_procedure_concurrent_workflow);
    RUN_E2E_TEST(test_mwl_mpps_accession_correlation);

    std::cout << "\n--- Resilience Tests ---" << std::endl;
    RUN_E2E_TEST(test_workflow_with_ris_failover);
    RUN_E2E_TEST(test_workflow_error_resilience);

    std::cout << "\n--- Performance Tests ---" << std::endl;
    RUN_E2E_TEST(test_high_volume_hl7_mpps_workflow);

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
    return pacs::bridge::e2e::test::run_all_hl7_mpps_workflow_tests();
}
