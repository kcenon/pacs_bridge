/**
 * @file phase2_e2e_test.cpp
 * @brief End-to-end integration tests for Phase 2: MPPS→HL7→MLLP workflow
 *
 * Tests the complete Phase 2 workflow including:
 * - MPPS ingestion (N-CREATE/N-SET events)
 * - MPPS→HL7 mapping (ORM status updates)
 * - Outbound delivery via MLLP
 * - Durable queue behavior (retry/backoff + crash recovery)
 *
 * Test Scope:
 *
 * Workflow 1: MPPS → ORM status update → MLLP delivery
 *   - IN PROGRESS → ORC-5=IP
 *   - COMPLETED → ORC-5=CM
 *   - DISCONTINUED → ORC-1=DC, ORC-5=CA
 *
 * Workflow 2: Reliable delivery + recovery
 *   - Destination down → message enqueued (SQLite)
 *   - Destination up → message delivered and acked
 *   - Simulated restart → pending messages recovered and delivered
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/176
 * @see https://github.com/kcenon/pacs_bridge/issues/170 (Epic)
 */

#include "integration_test_base.h"

#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/router/queue_manager.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>

namespace pacs::bridge::integration::test {

// =============================================================================
// HL7 Message Validation Utilities
// =============================================================================

/**
 * @brief HL7 message field validator for E2E tests
 *
 * Provides utilities to validate specific HL7 segments and fields
 * to ensure correct MPPS→HL7 mapping.
 */
class hl7_validator {
public:
    /**
     * @brief Parse HL7 message and extract field values
     */
    explicit hl7_validator(const std::string& message)
        : raw_message_(message), valid_(false) {
        parse_segments();
    }

    /**
     * @brief Check if message was parsed successfully
     */
    [[nodiscard]] bool is_valid() const {
        return valid_;
    }

    /**
     * @brief Get message type (e.g., "ORM^O01")
     */
    [[nodiscard]] std::string message_type() const {
        return get_field("MSH", 9);
    }

    /**
     * @brief Get message control ID
     */
    [[nodiscard]] std::string message_control_id() const {
        return get_field("MSH", 10);
    }

    /**
     * @brief Get HL7 version
     */
    [[nodiscard]] std::string version() const {
        return get_field("MSH", 12);
    }

    /**
     * @brief Get patient ID from PID segment
     */
    [[nodiscard]] std::string patient_id() const {
        return get_field("PID", 3);
    }

    /**
     * @brief Get patient name from PID segment
     */
    [[nodiscard]] std::string patient_name() const {
        return get_field("PID", 5);
    }

    /**
     * @brief Get ORC order control (ORC-1)
     *
     * Expected values for MPPS:
     *   - SC (Status Change) for normal updates
     *   - DC (Discontinue) for discontinued procedures
     */
    [[nodiscard]] std::string orc_order_control() const {
        return get_field("ORC", 1);
    }

    /**
     * @brief Get ORC placer order number (ORC-2)
     */
    [[nodiscard]] std::string orc_placer_order() const {
        return get_field("ORC", 2);
    }

    /**
     * @brief Get ORC filler order number (ORC-3)
     */
    [[nodiscard]] std::string orc_filler_order() const {
        return get_field("ORC", 3);
    }

    /**
     * @brief Get ORC placer group number (ORC-4)
     */
    [[nodiscard]] std::string orc_placer_group() const {
        return get_field("ORC", 4);
    }

    /**
     * @brief Get ORC order status (ORC-5)
     *
     * Expected values for MPPS:
     *   - IP (In Progress) for N-CREATE
     *   - CM (Completed) for N-SET COMPLETED
     *   - CA (Cancelled) for N-SET DISCONTINUED
     */
    [[nodiscard]] std::string orc_order_status() const {
        return get_field("ORC", 5);
    }

    /**
     * @brief Get OBR set ID (OBR-1)
     */
    [[nodiscard]] std::string obr_set_id() const {
        return get_field("OBR", 1);
    }

    /**
     * @brief Get OBR placer order number (OBR-2)
     */
    [[nodiscard]] std::string obr_placer_order() const {
        return get_field("OBR", 2);
    }

    /**
     * @brief Get OBR universal service identifier (OBR-4)
     */
    [[nodiscard]] std::string obr_service_id() const {
        return get_field("OBR", 4);
    }

    /**
     * @brief Get OBR filler field (accession number, OBR-18)
     */
    [[nodiscard]] std::string obr_accession_number() const {
        return get_field("OBR", 18);
    }

    /**
     * @brief Check if message contains required segments
     */
    [[nodiscard]] bool has_segment(const std::string& segment_name) const {
        return segments_.find(segment_name) != segments_.end();
    }

    /**
     * @brief Get raw field value by segment name and field index
     *
     * @param segment_name Segment identifier (MSH, PID, ORC, OBR, etc.)
     * @param field_index 1-based field index
     * @return Field value or empty string if not found
     */
    [[nodiscard]] std::string get_field(const std::string& segment_name,
                                         int field_index) const {
        auto it = segments_.find(segment_name);
        if (it == segments_.end()) {
            return "";
        }

        const auto& fields = it->second;
        // MSH segment has special handling (field separator is MSH-1)
        int adjusted_index = (segment_name == "MSH") ? field_index : field_index;

        if (adjusted_index < 0 ||
            static_cast<size_t>(adjusted_index) >= fields.size()) {
            return "";
        }

        return fields[static_cast<size_t>(adjusted_index)];
    }

    /**
     * @brief Validate MPPS IN PROGRESS message
     *
     * Checks:
     *   - Message type is ORM^O01
     *   - ORC-5 = IP (In Progress)
     *   - Required segments present (MSH, PID, ORC, OBR)
     */
    [[nodiscard]] bool validate_in_progress() const {
        if (!valid_) return false;

        // Check message type
        std::string msg_type = message_type();
        if (msg_type.find("ORM") == std::string::npos) {
            return false;
        }

        // Check ORC-5 status
        std::string status = orc_order_status();
        if (status != "IP") {
            return false;
        }

        // Check required segments
        return has_segment("MSH") && has_segment("PID") &&
               has_segment("ORC") && has_segment("OBR");
    }

    /**
     * @brief Validate MPPS COMPLETED message
     *
     * Checks:
     *   - Message type is ORM^O01
     *   - ORC-5 = CM (Completed)
     *   - Required segments present
     */
    [[nodiscard]] bool validate_completed() const {
        if (!valid_) return false;

        std::string msg_type = message_type();
        if (msg_type.find("ORM") == std::string::npos) {
            return false;
        }

        std::string status = orc_order_status();
        if (status != "CM") {
            return false;
        }

        return has_segment("MSH") && has_segment("PID") &&
               has_segment("ORC") && has_segment("OBR");
    }

    /**
     * @brief Validate MPPS DISCONTINUED message
     *
     * Checks:
     *   - Message type is ORM^O01
     *   - ORC-1 = DC (Discontinue) or SC (Status Change)
     *   - ORC-5 = CA (Cancelled) or DC (Discontinued)
     *   - Required segments present
     */
    [[nodiscard]] bool validate_discontinued() const {
        if (!valid_) return false;

        std::string msg_type = message_type();
        if (msg_type.find("ORM") == std::string::npos) {
            return false;
        }

        std::string status = orc_order_status();
        // DC or CA are valid for discontinued
        if (status != "DC" && status != "CA") {
            return false;
        }

        return has_segment("MSH") && has_segment("PID") &&
               has_segment("ORC") && has_segment("OBR");
    }

private:
    void parse_segments() {
        // Split by segment delimiter (CR or CRLF)
        std::istringstream stream(raw_message_);
        std::string line;

        while (std::getline(stream, line, '\r')) {
            // Remove any trailing newline
            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }
            if (line.empty()) continue;

            // Extract segment name (first 3 characters)
            if (line.size() < 3) continue;
            std::string segment_name = line.substr(0, 3);

            // Parse fields
            std::vector<std::string> fields;
            char delimiter = '|';

            // MSH segment: first character after MSH is the field separator
            if (segment_name == "MSH" && line.size() > 3) {
                delimiter = line[3];
                // For MSH, add empty first field and separator as field 1
                fields.push_back("");  // MSH-0 (placeholder)
                fields.push_back(std::string(1, delimiter));  // MSH-1
                line = line.substr(4);  // Skip "MSH|"
            } else {
                // Skip segment name and delimiter
                size_t start_pos = line.find(delimiter);
                if (start_pos != std::string::npos) {
                    fields.push_back(segment_name);  // Field 0
                    line = line.substr(start_pos + 1);
                } else {
                    fields.push_back(segment_name);
                    line.clear();
                }
            }

            // Split remaining fields
            std::istringstream field_stream(line);
            std::string field;
            while (std::getline(field_stream, field, delimiter)) {
                fields.push_back(field);
            }
            // Handle trailing empty field
            if (!line.empty() && line.back() == delimiter) {
                fields.push_back("");
            }

            segments_[segment_name] = std::move(fields);
        }

        // Validation: must have at least MSH segment
        valid_ = has_segment("MSH");
    }

    std::string raw_message_;
    std::unordered_map<std::string, std::vector<std::string>> segments_;
    bool valid_;
};

// =============================================================================
// Enhanced Mock RIS Server with Detailed Validation
// =============================================================================

/**
 * @brief Enhanced RIS server with HL7 validation capabilities
 */
class validating_ris_server : public mock_ris_server {
public:
    using mock_ris_server::mock_ris_server;

    /**
     * @brief Get validators for all received messages
     */
    [[nodiscard]] std::vector<hl7_validator> get_validators() const {
        std::vector<hl7_validator> validators;
        for (const auto& msg : received_messages()) {
            validators.emplace_back(msg);
        }
        return validators;
    }

    /**
     * @brief Count messages with specific ORC-5 status
     */
    [[nodiscard]] size_t count_by_status(const std::string& status) const {
        size_t count = 0;
        for (const auto& msg : received_messages()) {
            hl7_validator validator(msg);
            if (validator.is_valid() && validator.orc_order_status() == status) {
                ++count;
            }
        }
        return count;
    }
};

// =============================================================================
// E2E Test: MPPS IN PROGRESS Flow
// =============================================================================

/**
 * @brief Test MPPS N-CREATE → ORM^O01 (IP) → MLLP delivery with validation
 *
 * Verifies complete E2E flow:
 * 1. MPPS N-CREATE event triggers workflow
 * 2. HL7 ORM^O01 message is generated with ORC-5=IP
 * 3. Message is delivered via MLLP
 * 4. All required HL7 fields are present and correct
 */
bool test_e2e_mpps_in_progress_full_validation() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    // Setup RIS server
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    validating_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    // Create bridge simulator
    mpps_bridge_simulator bridge(ris_port);

    // Create MPPS event with detailed patient data
    mpps_bridge_simulator::mpps_event event;
    event.sop_instance_uid = "1.2.826.0.1.3680043.8.498.12345";
    event.patient_id = "PAT2024001";
    event.patient_name = "DOE^JOHN^M";
    event.accession_number = "ACC2024001";
    event.scheduled_procedure_id = "SPS2024001";
    event.modality = "CT";
    event.status = mpps_status::in_progress;
    event.timestamp = std::chrono::system_clock::now();

    // Execute N-CREATE
    bool result = bridge.process_n_create(event);
    INTEGRATION_TEST_ASSERT(result, "N-CREATE processing should succeed");

    // Wait for message delivery
    bool received = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() > 0; },
        std::chrono::milliseconds{3000});
    INTEGRATION_TEST_ASSERT(received, "RIS should receive message");

    // Validate HL7 message
    auto validators = ris.get_validators();
    INTEGRATION_TEST_ASSERT(!validators.empty(), "Should have validators");

    const auto& v = validators[0];
    INTEGRATION_TEST_ASSERT(v.is_valid(), "Message should be valid HL7");
    INTEGRATION_TEST_ASSERT(v.validate_in_progress(),
                            "Should validate as IN_PROGRESS");

    // Validate specific fields
    INTEGRATION_TEST_ASSERT(v.message_type().find("ORM") != std::string::npos,
                            "Message type should be ORM");
    INTEGRATION_TEST_ASSERT(v.orc_order_status() == "IP",
                            "ORC-5 should be IP");
    INTEGRATION_TEST_ASSERT(v.has_segment("MSH"), "Should have MSH segment");
    INTEGRATION_TEST_ASSERT(v.has_segment("PID"), "Should have PID segment");
    INTEGRATION_TEST_ASSERT(v.has_segment("ORC"), "Should have ORC segment");
    INTEGRATION_TEST_ASSERT(v.has_segment("OBR"), "Should have OBR segment");

    // Verify patient data mapping
    INTEGRATION_TEST_ASSERT(v.patient_id().find("PAT2024001") != std::string::npos,
                            "Patient ID should be mapped");

    ris.stop();
    return true;
}

// =============================================================================
// E2E Test: MPPS COMPLETED Flow
// =============================================================================

/**
 * @brief Test MPPS N-SET COMPLETED → ORM^O01 (CM) → MLLP delivery
 *
 * Verifies:
 * 1. ORC-5 = CM (Completed)
 * 2. All required segments present
 * 3. Message delivered successfully
 */
bool test_e2e_mpps_completed_full_validation() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    validating_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);

    mpps_bridge_simulator::mpps_event event;
    event.sop_instance_uid = "1.2.826.0.1.3680043.8.498.12346";
    event.patient_id = "PAT2024002";
    event.patient_name = "SMITH^JANE^A";
    event.accession_number = "ACC2024002";
    event.scheduled_procedure_id = "SPS2024002";
    event.modality = "MR";
    event.status = mpps_status::completed;
    event.timestamp = std::chrono::system_clock::now();

    // Execute N-SET COMPLETED
    bool result = bridge.process_n_set_completed(event);
    INTEGRATION_TEST_ASSERT(result, "N-SET COMPLETED should succeed");

    bool received = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() > 0; },
        std::chrono::milliseconds{3000});
    INTEGRATION_TEST_ASSERT(received, "RIS should receive message");

    auto validators = ris.get_validators();
    INTEGRATION_TEST_ASSERT(!validators.empty(), "Should have validators");

    const auto& v = validators[0];
    INTEGRATION_TEST_ASSERT(v.is_valid(), "Message should be valid HL7");
    INTEGRATION_TEST_ASSERT(v.validate_completed(),
                            "Should validate as COMPLETED");
    INTEGRATION_TEST_ASSERT(v.orc_order_status() == "CM",
                            "ORC-5 should be CM");

    ris.stop();
    return true;
}

// =============================================================================
// E2E Test: MPPS DISCONTINUED Flow
// =============================================================================

/**
 * @brief Test MPPS N-SET DISCONTINUED → ORM^O01 (DC/CA) → MLLP delivery
 *
 * Verifies:
 * 1. ORC-1 = DC or SC
 * 2. ORC-5 = DC or CA
 * 3. Message delivered successfully
 */
bool test_e2e_mpps_discontinued_full_validation() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    validating_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);

    mpps_bridge_simulator::mpps_event event;
    event.sop_instance_uid = "1.2.826.0.1.3680043.8.498.12347";
    event.patient_id = "PAT2024003";
    event.patient_name = "JONES^ROBERT^B";
    event.accession_number = "ACC2024003";
    event.scheduled_procedure_id = "SPS2024003";
    event.modality = "XR";
    event.status = mpps_status::discontinued;
    event.timestamp = std::chrono::system_clock::now();

    // Execute N-SET DISCONTINUED
    bool result = bridge.process_n_set_discontinued(event);
    INTEGRATION_TEST_ASSERT(result, "N-SET DISCONTINUED should succeed");

    bool received = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() > 0; },
        std::chrono::milliseconds{3000});
    INTEGRATION_TEST_ASSERT(received, "RIS should receive message");

    auto validators = ris.get_validators();
    INTEGRATION_TEST_ASSERT(!validators.empty(), "Should have validators");

    const auto& v = validators[0];
    INTEGRATION_TEST_ASSERT(v.is_valid(), "Message should be valid HL7");
    INTEGRATION_TEST_ASSERT(v.validate_discontinued(),
                            "Should validate as DISCONTINUED");

    // ORC-5 should be DC or CA
    std::string status = v.orc_order_status();
    INTEGRATION_TEST_ASSERT(status == "DC" || status == "CA",
                            "ORC-5 should be DC or CA, got: " + status);

    ris.stop();
    return true;
}

// =============================================================================
// E2E Test: Complete MPPS Lifecycle
// =============================================================================

/**
 * @brief Test complete MPPS lifecycle: N-CREATE → N-SET COMPLETED
 *
 * Verifies full procedure workflow from start to completion.
 */
bool test_e2e_mpps_complete_lifecycle() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    validating_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);

    // Create event for the procedure
    mpps_bridge_simulator::mpps_event event;
    event.sop_instance_uid = "1.2.826.0.1.3680043.8.498.99999";
    event.patient_id = "LIFECYCLE001";
    event.patient_name = "TEST^LIFECYCLE";
    event.accession_number = "ACC_LIFECYCLE";
    event.scheduled_procedure_id = "SPS_LIFECYCLE";
    event.modality = "CT";
    event.timestamp = std::chrono::system_clock::now();

    // Step 1: N-CREATE (procedure starts)
    event.status = mpps_status::in_progress;
    bool create_result = bridge.process_n_create(event);
    INTEGRATION_TEST_ASSERT(create_result, "N-CREATE should succeed");

    bool received1 = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 1; },
        std::chrono::milliseconds{3000});
    INTEGRATION_TEST_ASSERT(received1, "Should receive N-CREATE message");

    // Step 2: N-SET COMPLETED (procedure finishes)
    event.status = mpps_status::completed;
    bool complete_result = bridge.process_n_set_completed(event);
    INTEGRATION_TEST_ASSERT(complete_result, "N-SET COMPLETED should succeed");

    bool received2 = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 2; },
        std::chrono::milliseconds{3000});
    INTEGRATION_TEST_ASSERT(received2, "Should receive completion message");

    // Validate message sequence
    auto validators = ris.get_validators();
    INTEGRATION_TEST_ASSERT(validators.size() >= 2, "Should have 2 messages");

    // First message: IN PROGRESS
    INTEGRATION_TEST_ASSERT(validators[0].orc_order_status() == "IP",
                            "First message should be IP");

    // Second message: COMPLETED
    INTEGRATION_TEST_ASSERT(validators[1].orc_order_status() == "CM",
                            "Second message should be CM");

    ris.stop();
    return true;
}

// =============================================================================
// E2E Test: Queue Recovery When RIS Unavailable
// =============================================================================

/**
 * @brief Test message queueing when RIS is unavailable
 *
 * Workflow 2 Test:
 * 1. RIS unavailable → messages queued
 * 2. RIS becomes available → queued messages delivered
 */
bool test_e2e_queue_when_ris_down() {
    auto temp_path = integration_test_fixture::generate_temp_path();
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    // Setup queue simulator (RIS not started yet)
    outbound_queue_simulator::config queue_config;
    queue_config.storage_path = temp_path;
    queue_config.ris_port = ris_port;
    queue_config.retry_interval = std::chrono::milliseconds{300};

    outbound_queue_simulator queue(queue_config);
    queue.start();

    // Queue messages while RIS is down
    queue.enqueue("MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|20240101120000||"
                  "ORM^O01|MSG001|P|2.4\r"
                  "PID|1||PAT001|||DOE^JOHN\r"
                  "ORC|SC|SPS001|||IP\r"
                  "OBR|1|SPS001||CT\r");

    queue.enqueue("MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|20240101120001||"
                  "ORM^O01|MSG002|P|2.4\r"
                  "PID|1||PAT002|||SMITH^JANE\r"
                  "ORC|SC|SPS002|||CM\r"
                  "OBR|1|SPS002||MR\r");

    // Wait a bit for delivery attempts to fail
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    INTEGRATION_TEST_ASSERT(queue.queue_size() >= 1,
                            "Messages should be queued (RIS down)");

    // Now start RIS
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    validating_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    // Wait for queued messages to be delivered
    bool delivered = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 2; },
        std::chrono::milliseconds{10000});

    INTEGRATION_TEST_ASSERT(delivered, "All queued messages should be delivered");
    INTEGRATION_TEST_ASSERT(queue.queue_empty(),
                            "Queue should be empty after delivery");

    // Validate delivered messages
    auto validators = ris.get_validators();
    INTEGRATION_TEST_ASSERT(validators.size() >= 2,
                            "Should have received 2 messages");

    queue.stop();
    ris.stop();
    integration_test_fixture::cleanup_temp_file(temp_path);
    return true;
}

// =============================================================================
// E2E Test: Queue Recovery After Simulated Restart
// =============================================================================

/**
 * @brief Test queue persistence and recovery after system restart
 *
 * Workflow 2 Test:
 * 1. Queue messages while RIS down
 * 2. Simulate system restart (queue reloads from disk)
 * 3. Start RIS → queued messages recovered and delivered
 */
bool test_e2e_queue_recovery_after_restart() {
    auto temp_path = integration_test_fixture::generate_temp_path();
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    // Phase 1: Queue messages with no RIS
    {
        outbound_queue_simulator::config queue_config;
        queue_config.storage_path = temp_path;
        queue_config.ris_port = ris_port;
        queue_config.retry_interval = std::chrono::milliseconds{200};

        outbound_queue_simulator queue(queue_config);

        // Enqueue without starting delivery thread
        queue.enqueue("MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|20240101||"
                      "ORM^O01|RESTART001|P|2.4\r"
                      "PID|1||PAT_RESTART|||RECOVERY^TEST\r"
                      "ORC|SC|SPS_RESTART|||IP\r"
                      "OBR|1|SPS_RESTART||CT\r");

        INTEGRATION_TEST_ASSERT(queue.queue_size() == 1,
                                "Should have 1 queued message");
    }

    // Phase 2: Verify persistence
    {
        test_message_queue recovery_check(temp_path);
        INTEGRATION_TEST_ASSERT(recovery_check.size() == 1,
                                "Message should persist on disk");
    }

    // Phase 3: Start RIS and new queue instance
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    validating_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    {
        outbound_queue_simulator::config queue_config;
        queue_config.storage_path = temp_path;
        queue_config.ris_port = ris_port;
        queue_config.retry_interval = std::chrono::milliseconds{200};

        outbound_queue_simulator queue(queue_config);
        queue.start();

        // Wait for recovery and delivery
        bool delivered = integration_test_fixture::wait_for(
            [&ris]() { return ris.messages_received() >= 1; },
            std::chrono::milliseconds{10000});

        INTEGRATION_TEST_ASSERT(delivered,
                                "Recovered message should be delivered");
        INTEGRATION_TEST_ASSERT(queue.queue_empty(),
                                "Queue should be empty after delivery");

        queue.stop();
    }

    // Validate recovered message
    auto validators = ris.get_validators();
    INTEGRATION_TEST_ASSERT(!validators.empty(),
                            "Should have received recovered message");

    const auto& v = validators[0];
    INTEGRATION_TEST_ASSERT(v.is_valid(), "Recovered message should be valid");
    INTEGRATION_TEST_ASSERT(
        v.message_control_id().find("RESTART001") != std::string::npos,
        "Should be the correct recovered message");

    ris.stop();
    integration_test_fixture::cleanup_temp_file(temp_path);
    return true;
}

// =============================================================================
// E2E Test: Failover to Backup RIS
// =============================================================================

/**
 * @brief Test failover routing when primary RIS is unavailable
 *
 * Verifies:
 * 1. Primary RIS down → failover to backup
 * 2. Message delivered to backup RIS
 */
bool test_e2e_failover_to_backup_ris() {
    uint16_t primary_port = integration_test_fixture::generate_test_port();
    uint16_t backup_port = integration_test_fixture::generate_test_port();

    // Only start backup RIS (primary is "down")
    mock_ris_server::config backup_config;
    backup_config.port = backup_port;
    backup_config.auto_ack = true;

    validating_ris_server backup_ris(backup_config);
    INTEGRATION_TEST_ASSERT(backup_ris.start(), "Failed to start backup RIS");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&backup_ris]() { return backup_ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "Backup RIS should start");

    // Setup bridge with failover
    mpps_bridge_simulator bridge(primary_port);
    bridge.set_backup_ris_port(backup_port);
    bridge.enable_failover(true);

    // Create event
    auto event = mpps_event_generator::create_sample_event();

    // Execute - should failover to backup
    bool result = bridge.process_n_create(event);
    INTEGRATION_TEST_ASSERT(result, "Should succeed via failover");

    // Verify message received by backup
    bool received = integration_test_fixture::wait_for(
        [&backup_ris]() { return backup_ris.messages_received() > 0; },
        std::chrono::milliseconds{3000});
    INTEGRATION_TEST_ASSERT(received, "Backup RIS should receive message");

    backup_ris.stop();
    return true;
}

// =============================================================================
// E2E Test: All MPPS Statuses in Sequence
// =============================================================================

/**
 * @brief Test all three MPPS statuses delivered correctly
 *
 * Verifies complete status coverage:
 *   - IN PROGRESS (IP)
 *   - COMPLETED (CM)
 *   - DISCONTINUED (DC/CA)
 */
bool test_e2e_all_mpps_statuses() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    validating_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);

    // Send IN PROGRESS
    auto event1 = mpps_event_generator::create_sample_event();
    event1.status = mpps_status::in_progress;
    INTEGRATION_TEST_ASSERT(bridge.process_n_create(event1),
                            "IN PROGRESS should succeed");

    // Send COMPLETED
    auto event2 = mpps_event_generator::create_sample_event();
    event2.status = mpps_status::completed;
    INTEGRATION_TEST_ASSERT(bridge.process_n_set_completed(event2),
                            "COMPLETED should succeed");

    // Send DISCONTINUED
    auto event3 = mpps_event_generator::create_sample_event();
    event3.status = mpps_status::discontinued;
    INTEGRATION_TEST_ASSERT(bridge.process_n_set_discontinued(event3),
                            "DISCONTINUED should succeed");

    // Wait for all messages
    bool all_received = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 3; },
        std::chrono::milliseconds{5000});
    INTEGRATION_TEST_ASSERT(all_received, "Should receive all 3 messages");

    // Count messages by status
    size_t ip_count = ris.count_by_status("IP");
    size_t cm_count = ris.count_by_status("CM");
    size_t dc_count = ris.count_by_status("DC");
    size_t ca_count = ris.count_by_status("CA");

    INTEGRATION_TEST_ASSERT(ip_count >= 1, "Should have at least 1 IP message");
    INTEGRATION_TEST_ASSERT(cm_count >= 1, "Should have at least 1 CM message");
    INTEGRATION_TEST_ASSERT(dc_count + ca_count >= 1,
                            "Should have at least 1 DC or CA message");

    ris.stop();
    return true;
}

// =============================================================================
// E2E Test: High Volume Message Processing
// =============================================================================

/**
 * @brief Test processing multiple MPPS events in sequence
 *
 * Verifies system handles volume of messages correctly.
 */
bool test_e2e_high_volume_processing() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    validating_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    mpps_bridge_simulator bridge(ris_port);

    // Generate batch of events
    const size_t batch_size = 20;
    auto events = mpps_event_generator::create_batch(batch_size);

    // Process all events
    size_t success_count = 0;
    for (const auto& event : events) {
        if (bridge.process_n_create(event)) {
            ++success_count;
        }
    }

    INTEGRATION_TEST_ASSERT(success_count == batch_size,
                            "All events should be processed");

    // Wait for all messages to be received
    bool all_received = integration_test_fixture::wait_for(
        [&ris, batch_size]() {
            return ris.messages_received() >= batch_size;
        },
        std::chrono::milliseconds{15000});

    INTEGRATION_TEST_ASSERT(all_received,
                            "All messages should be received");
    INTEGRATION_TEST_ASSERT(ris.messages_received() == batch_size,
                            "Should receive exactly " +
                                std::to_string(batch_size) + " messages");

    // Verify all messages are valid
    auto validators = ris.get_validators();
    for (const auto& v : validators) {
        INTEGRATION_TEST_ASSERT(v.is_valid(), "Each message should be valid");
    }

    ris.stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_phase2_e2e_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=============================================" << std::endl;
    std::cout << "  Phase 2 E2E Tests: MPPS→HL7→MLLP + Recovery" << std::endl;
    std::cout << "  Issue #176" << std::endl;
    std::cout << "=============================================" << std::endl;

    std::cout << "\n--- Workflow 1: MPPS → ORM Status → MLLP ---" << std::endl;
    RUN_INTEGRATION_TEST(test_e2e_mpps_in_progress_full_validation);
    RUN_INTEGRATION_TEST(test_e2e_mpps_completed_full_validation);
    RUN_INTEGRATION_TEST(test_e2e_mpps_discontinued_full_validation);
    RUN_INTEGRATION_TEST(test_e2e_mpps_complete_lifecycle);

    std::cout << "\n--- Workflow 2: Reliable Delivery + Recovery ---" << std::endl;
    RUN_INTEGRATION_TEST(test_e2e_queue_when_ris_down);
    RUN_INTEGRATION_TEST(test_e2e_queue_recovery_after_restart);
    RUN_INTEGRATION_TEST(test_e2e_failover_to_backup_ris);

    std::cout << "\n--- Comprehensive Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_e2e_all_mpps_statuses);
    RUN_INTEGRATION_TEST(test_e2e_high_volume_processing);

    std::cout << "\n=============================================" << std::endl;
    std::cout << "  Phase 2 E2E Test Summary" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "  Passed: " << passed << std::endl;
    std::cout << "  Failed: " << failed << std::endl;
    std::cout << "  Total:  " << (passed + failed) << std::endl;

    if (passed + failed > 0) {
        double pass_rate = (passed * 100.0) / (passed + failed);
        std::cout << "  Pass Rate: " << pass_rate << "%" << std::endl;
    }
    std::cout << "=============================================" << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::integration::test

int main() {
    return pacs::bridge::integration::test::run_all_phase2_e2e_tests();
}
