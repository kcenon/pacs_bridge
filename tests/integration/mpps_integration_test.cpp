/**
 * @file mpps_integration_test.cpp
 * @brief Integration tests for MPPS (Modality Performed Procedure Step) flows
 *
 * Tests the complete MPPS workflow including:
 * - MPPS In Progress Flow (N-CREATE -> ORM^O01 with status IP)
 * - MPPS Completion Flow (N-SET COMPLETED -> ORM^O01 with status CM)
 * - MPPS Discontinuation Flow (N-SET DISCONTINUED -> ORM^O01 with status DC)
 *
 * These tests verify the end-to-end message flow from modality MPPS events
 * through the PACS Bridge to the RIS via MLLP transport.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/29
 * @see https://github.com/kcenon/pacs_bridge/issues/23 (MPPS Event Handler)
 * @see https://github.com/kcenon/pacs_bridge/issues/24 (MPPS to HL7 Mapper)
 */

#include "integration_test_base.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace pacs::bridge::integration::test {

// =============================================================================
// MPPS In Progress Flow Tests
// =============================================================================

/**
 * @brief Test basic MPPS N-CREATE -> ORM^O01 (IP) flow
 *
 * Scenario: A modality sends an MPPS N-CREATE message indicating procedure
 * start. The bridge should generate an ORM^O01 message with status code IP
 * (in progress) and deliver it to the RIS via MLLP.
 */
bool test_mpps_in_progress_basic() {
    // Setup: Create mock RIS server
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");

    // Wait for server to be ready
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    // Create MPPS bridge simulator
    mpps_bridge_simulator bridge(ris_port);

    // Generate MPPS event
    auto event = mpps_event_generator::create_sample_event();

    // Execute: Process N-CREATE (procedure start)
    bool result = bridge.process_n_create(event);

    // Verify
    INTEGRATION_TEST_ASSERT(result, "N-CREATE processing should succeed");
    INTEGRATION_TEST_ASSERT(bridge.messages_sent() == 1,
                            "Should have sent 1 message");

    // Wait for message to be received
    bool received = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() > 0; },
        std::chrono::milliseconds{2000});

    INTEGRATION_TEST_ASSERT(received, "RIS should receive the message");
    INTEGRATION_TEST_ASSERT(ris.messages_received() == 1,
                            "RIS should receive exactly 1 message");

    // Verify message content contains IP status
    const auto& messages = ris.received_messages();
    INTEGRATION_TEST_ASSERT(!messages.empty(), "Should have received messages");
    INTEGRATION_TEST_ASSERT(messages[0].find("ORM^O01") != std::string::npos,
                            "Message should be ORM^O01");
    INTEGRATION_TEST_ASSERT(messages[0].find("|IP") != std::string::npos ||
                                messages[0].find("||IP") != std::string::npos,
                            "Message should contain IP status");

    // Cleanup
    ris.stop();
    return true;
}

/**
 * @brief Test MPPS N-CREATE with patient demographics
 *
 * Verifies that patient information from the MPPS event is correctly
 * mapped to the PID segment of the generated ORM message.
 */
bool test_mpps_in_progress_with_patient_data() {
    // Setup
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);

    // Create event with specific patient data
    mpps_bridge_simulator::mpps_event event;
    event.sop_instance_uid = "1.2.3.4.5.6.7.8.9";
    event.patient_id = "PAT12345";
    event.patient_name = "SMITH^JANE^M";
    event.accession_number = "ACC98765";
    event.scheduled_procedure_id = "SPS54321";
    event.modality = "MR";
    event.status = mpps_status::in_progress;
    event.timestamp = std::chrono::system_clock::now();

    // Execute
    bool result = bridge.process_n_create(event);

    // Verify
    INTEGRATION_TEST_ASSERT(result, "N-CREATE should succeed");

    bool received = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() > 0; },
        std::chrono::milliseconds{2000});

    INTEGRATION_TEST_ASSERT(received, "RIS should receive message");

    const auto& messages = ris.received_messages();
    INTEGRATION_TEST_ASSERT(!messages.empty(), "Should have messages");

    // Verify patient data in PID segment
    INTEGRATION_TEST_ASSERT(messages[0].find("PAT12345") != std::string::npos,
                            "Message should contain patient ID");
    INTEGRATION_TEST_ASSERT(
        messages[0].find("SMITH^JANE") != std::string::npos,
        "Message should contain patient name");

    // Verify accession number in ORC segment
    INTEGRATION_TEST_ASSERT(messages[0].find("ACC98765") != std::string::npos,
                            "Message should contain accession number");

    ris.stop();
    return true;
}

// =============================================================================
// MPPS Completion Flow Tests
// =============================================================================

/**
 * @brief Test MPPS N-SET COMPLETED -> ORM^O01 (CM) flow
 *
 * Scenario: A modality sends an MPPS N-SET message with COMPLETED status.
 * The bridge should generate an ORM^O01 message with status code CM
 * (completed) and deliver it to the RIS.
 */
bool test_mpps_completion_basic() {
    // Setup
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);
    auto event = mpps_event_generator::create_sample_event();

    // Execute: Process N-SET COMPLETED
    bool result = bridge.process_n_set_completed(event);

    // Verify
    INTEGRATION_TEST_ASSERT(result, "N-SET COMPLETED should succeed");

    bool received = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() > 0; },
        std::chrono::milliseconds{2000});

    INTEGRATION_TEST_ASSERT(received, "RIS should receive the message");

    const auto& messages = ris.received_messages();
    INTEGRATION_TEST_ASSERT(!messages.empty(), "Should have messages");
    INTEGRATION_TEST_ASSERT(messages[0].find("ORM^O01") != std::string::npos,
                            "Message should be ORM^O01");
    INTEGRATION_TEST_ASSERT(messages[0].find("|CM") != std::string::npos ||
                                messages[0].find("||CM") != std::string::npos,
                            "Message should contain CM status");

    ris.stop();
    return true;
}

/**
 * @brief Test complete MPPS workflow: N-CREATE followed by N-SET COMPLETED
 *
 * Verifies the full lifecycle of an MPPS procedure from start to completion.
 */
bool test_mpps_complete_workflow() {
    // Setup
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);
    auto event = mpps_event_generator::create_sample_event();

    // Step 1: N-CREATE (procedure starts)
    bool create_result = bridge.process_n_create(event);
    INTEGRATION_TEST_ASSERT(create_result, "N-CREATE should succeed");

    // Wait for first message
    bool received1 = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 1; },
        std::chrono::milliseconds{2000});
    INTEGRATION_TEST_ASSERT(received1, "Should receive N-CREATE message");

    // Step 2: N-SET COMPLETED (procedure finishes)
    event.status = mpps_status::completed;
    bool complete_result = bridge.process_n_set_completed(event);
    INTEGRATION_TEST_ASSERT(complete_result, "N-SET COMPLETED should succeed");

    // Wait for second message
    bool received2 = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 2; },
        std::chrono::milliseconds{2000});
    INTEGRATION_TEST_ASSERT(received2, "Should receive N-SET message");

    // Verify both messages
    INTEGRATION_TEST_ASSERT(ris.messages_received() == 2,
                            "Should have received 2 messages");
    INTEGRATION_TEST_ASSERT(bridge.messages_sent() == 2,
                            "Bridge should have sent 2 messages");

    const auto& messages = ris.received_messages();
    INTEGRATION_TEST_ASSERT(messages.size() == 2, "Should have 2 messages");

    // First message should be IP, second should be CM
    INTEGRATION_TEST_ASSERT(messages[0].find("|IP") != std::string::npos ||
                                messages[0].find("||IP") != std::string::npos,
                            "First message should be IP");
    INTEGRATION_TEST_ASSERT(messages[1].find("|CM") != std::string::npos ||
                                messages[1].find("||CM") != std::string::npos,
                            "Second message should be CM");

    ris.stop();
    return true;
}

// =============================================================================
// MPPS Discontinuation Tests
// =============================================================================

/**
 * @brief Test MPPS N-SET DISCONTINUED -> ORM^O01 (DC) flow
 *
 * Scenario: A modality sends an MPPS N-SET message with DISCONTINUED status
 * (procedure cancelled). The bridge should generate an ORM^O01 message with
 * status code DC.
 */
bool test_mpps_discontinuation_basic() {
    // Setup
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);
    auto event = mpps_event_generator::create_sample_event();

    // Execute: Process N-SET DISCONTINUED
    bool result = bridge.process_n_set_discontinued(event);

    // Verify
    INTEGRATION_TEST_ASSERT(result, "N-SET DISCONTINUED should succeed");

    bool received = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() > 0; },
        std::chrono::milliseconds{2000});

    INTEGRATION_TEST_ASSERT(received, "RIS should receive the message");

    const auto& messages = ris.received_messages();
    INTEGRATION_TEST_ASSERT(!messages.empty(), "Should have messages");
    INTEGRATION_TEST_ASSERT(messages[0].find("|DC") != std::string::npos ||
                                messages[0].find("||DC") != std::string::npos,
                            "Message should contain DC status");

    ris.stop();
    return true;
}

/**
 * @brief Test MPPS workflow with discontinuation after start
 *
 * Verifies N-CREATE followed by N-SET DISCONTINUED (procedure cancelled
 * after starting).
 */
bool test_mpps_discontinuation_after_start() {
    // Setup
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);
    auto event = mpps_event_generator::create_sample_event();

    // Step 1: N-CREATE (procedure starts)
    bool create_result = bridge.process_n_create(event);
    INTEGRATION_TEST_ASSERT(create_result, "N-CREATE should succeed");

    bool received1 = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 1; },
        std::chrono::milliseconds{2000});
    INTEGRATION_TEST_ASSERT(received1, "Should receive N-CREATE message");

    // Step 2: N-SET DISCONTINUED (procedure cancelled)
    bool discontinue_result = bridge.process_n_set_discontinued(event);
    INTEGRATION_TEST_ASSERT(discontinue_result,
                            "N-SET DISCONTINUED should succeed");

    bool received2 = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 2; },
        std::chrono::milliseconds{2000});
    INTEGRATION_TEST_ASSERT(received2, "Should receive discontinue message");

    // Verify
    const auto& messages = ris.received_messages();
    INTEGRATION_TEST_ASSERT(messages.size() == 2, "Should have 2 messages");
    INTEGRATION_TEST_ASSERT(messages[0].find("|IP") != std::string::npos ||
                                messages[0].find("||IP") != std::string::npos,
                            "First message should be IP");
    INTEGRATION_TEST_ASSERT(messages[1].find("|DC") != std::string::npos ||
                                messages[1].find("||DC") != std::string::npos,
                            "Second message should be DC");

    ris.stop();
    return true;
}

// =============================================================================
// Error Handling Tests
// =============================================================================

/**
 * @brief Test MPPS processing when RIS is unavailable
 *
 * Verifies that the bridge handles connection failures gracefully.
 */
bool test_mpps_ris_unavailable() {
    // Use a port with no server running
    uint16_t invalid_port = integration_test_fixture::generate_test_port();

    mpps_bridge_simulator bridge(invalid_port);
    auto event = mpps_event_generator::create_sample_event();

    // Execute: Try to process N-CREATE with no RIS available
    bool result = bridge.process_n_create(event);

    // Verify: Should fail gracefully
    INTEGRATION_TEST_ASSERT(!result,
                            "N-CREATE should fail when RIS is unavailable");
    INTEGRATION_TEST_ASSERT(bridge.messages_sent() == 0,
                            "No messages should be sent");

    return true;
}

/**
 * @brief Test MPPS processing with RIS response delay
 *
 * Verifies that the bridge handles slow RIS responses correctly.
 */
bool test_mpps_slow_ris_response() {
    // Setup with response delay
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;
    ris_config.response_delay = std::chrono::milliseconds{500};

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);
    auto event = mpps_event_generator::create_sample_event();

    // Execute with timing
    auto start = std::chrono::steady_clock::now();
    bool result = bridge.process_n_create(event);
    auto duration = std::chrono::steady_clock::now() - start;

    // Verify
    INTEGRATION_TEST_ASSERT(result, "N-CREATE should succeed despite delay");

    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    INTEGRATION_TEST_ASSERT(duration_ms.count() >= 400,
                            "Should wait for RIS response");

    bool received = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() > 0; },
        std::chrono::milliseconds{2000});
    INTEGRATION_TEST_ASSERT(received, "RIS should eventually receive message");

    ris.stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_mpps_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== MPPS Integration Tests ===" << std::endl;
    std::cout << "Testing Phase 2: MPPS & Bidirectional Messaging\n"
              << std::endl;

    std::cout << "\n--- MPPS In Progress Flow Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_mpps_in_progress_basic);
    RUN_INTEGRATION_TEST(test_mpps_in_progress_with_patient_data);

    std::cout << "\n--- MPPS Completion Flow Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_mpps_completion_basic);
    RUN_INTEGRATION_TEST(test_mpps_complete_workflow);

    std::cout << "\n--- MPPS Discontinuation Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_mpps_discontinuation_basic);
    RUN_INTEGRATION_TEST(test_mpps_discontinuation_after_start);

    std::cout << "\n--- Error Handling Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_mpps_ris_unavailable);
    RUN_INTEGRATION_TEST(test_mpps_slow_ris_response);

    std::cout << "\n=== MPPS Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    if (passed + failed > 0) {
        double pass_rate = (passed * 100.0) / (passed + failed);
        std::cout << "Pass Rate: " << pass_rate << "%" << std::endl;
    }

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::integration::test

int main() {
    return pacs::bridge::integration::test::run_all_mpps_tests();
}
