/**
 * @file e2e_scenario_test.cpp
 * @brief End-to-end scenario tests for PACS Bridge
 *
 * Tests complete workflows involving multiple system components:
 * - Patient registration to imaging workflow
 * - Order placement to result delivery
 * - Multi-system message routing scenarios
 * - Error recovery and retry scenarios
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/161
 * @see https://github.com/kcenon/pacs_bridge/issues/145
 */

#include "integration_test_base.h"

#include <chrono>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

namespace pacs::bridge::integration::test {

// =============================================================================
// Mock EMR (Electronic Medical Record) Server
// =============================================================================

/**
 * @brief Mock EMR server that simulates order placement and result receipt
 */
class mock_emr_server {
public:
    struct order_record {
        std::string order_id;
        std::string patient_id;
        std::string patient_name;
        std::string procedure_code;
        std::string order_status;  // PLACED, SCHEDULED, IN_PROGRESS, COMPLETED
        std::string result_status; // PENDING, PRELIMINARY, FINAL
    };

    struct config {
        uint16_t port = 12950;
    };

    explicit mock_emr_server(const config& cfg)
        : config_(cfg), running_(false), orders_received_(0),
          results_received_(0) {}

    ~mock_emr_server() {
        stop();
    }

    bool start() {
        if (running_) {
            return false;
        }

        mllp::mllp_server_config server_config;
        server_config.port = config_.port;

        server_ = std::make_unique<mllp::mllp_server>(server_config);

        server_->set_message_handler(
            [this](const mllp::mllp_message& msg,
                   const mllp::mllp_session_info& /*session*/)
                -> std::optional<mllp::mllp_message> {
                return handle_message(msg);
            });

        auto result = server_->start();
        if (!result.has_value()) {
            return false;
        }

        running_ = true;
        return true;
    }

    void stop() {
        if (server_ && running_) {
            server_->stop(true, std::chrono::seconds{5});
            running_ = false;
        }
    }

    bool is_running() const {
        return running_;
    }

    void add_order(const order_record& order) {
        std::lock_guard<std::mutex> lock(mutex_);
        orders_[order.order_id] = order;
    }

    std::optional<order_record> get_order(const std::string& order_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = orders_.find(order_id);
        if (it != orders_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    uint32_t orders_received() const {
        return orders_received_;
    }

    uint32_t results_received() const {
        return results_received_;
    }

    uint16_t port() const {
        return config_.port;
    }

private:
    std::optional<mllp::mllp_message> handle_message(
        const mllp::mllp_message& msg) {
        std::string msg_str = msg.to_string();

        if (msg_str.find("ORU^R01") != std::string::npos) {
            results_received_++;
            return handle_result(msg);
        } else if (msg_str.find("ORM^O01") != std::string::npos) {
            orders_received_++;
            return handle_status_update(msg);
        }

        return generate_ack(msg, "AA");
    }

    std::optional<mllp::mllp_message> handle_result(
        const mllp::mllp_message& msg) {
        hl7::hl7_parser parser;
        auto parse_result = parser.parse(msg.to_string());

        if (parse_result.has_value()) {
            std::string order_id(parse_result->get_value("OBR.3"));
            std::string result_status(parse_result->get_value("OBR.25"));

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = orders_.find(order_id);
            if (it != orders_.end()) {
                it->second.result_status = result_status.empty() ? "FINAL" : result_status;
            }
        }

        return generate_ack(msg, "AA");
    }

    std::optional<mllp::mllp_message> handle_status_update(
        const mllp::mllp_message& msg) {
        hl7::hl7_parser parser;
        auto parse_result = parser.parse(msg.to_string());

        if (parse_result.has_value()) {
            std::string order_id(parse_result->get_value("ORC.2"));
            std::string status(parse_result->get_value("ORC.5"));

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = orders_.find(order_id);
            if (it != orders_.end()) {
                if (status == "IP") {
                    it->second.order_status = "IN_PROGRESS";
                } else if (status == "CM") {
                    it->second.order_status = "COMPLETED";
                }
            }
        }

        return generate_ack(msg, "AA");
    }

    mllp::mllp_message generate_ack(const mllp::mllp_message& original,
                                    const std::string& ack_code) {
        hl7::hl7_parser parser;
        auto parse_result = parser.parse(original.to_string());

        std::string msg_control_id = "0";
        if (parse_result.has_value()) {
            msg_control_id = parse_result->get_value("MSH.10");
        }

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp[15];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                      std::localtime(&time_t_now));

        std::string ack = "MSH|^~\\&|EMR|HOSPITAL|PACS|RADIOLOGY|" +
                          std::string(timestamp) + "||ACK|ACK" + msg_control_id +
                          "|P|2.4\r" + "MSA|" + ack_code + "|" + msg_control_id +
                          "\r";

        return mllp::mllp_message::from_string(ack);
    }

    config config_;
    std::unique_ptr<mllp::mllp_server> server_;
    std::atomic<bool> running_;
    std::atomic<uint32_t> orders_received_;
    std::atomic<uint32_t> results_received_;
    std::map<std::string, order_record> orders_;
    mutable std::mutex mutex_;
};

// =============================================================================
// E2E Scenario: Complete Imaging Workflow
// =============================================================================

/**
 * @brief Test complete imaging workflow from order to result
 *
 * Scenario:
 * 1. EMR places an imaging order (ORM^O01)
 * 2. RIS schedules the procedure
 * 3. Modality starts procedure (MPPS N-CREATE -> ORM^O01 IP)
 * 4. Modality completes procedure (MPPS N-SET COMPLETED -> ORM^O01 CM)
 * 5. PACS sends result back to EMR (ORU^R01)
 */
bool test_e2e_complete_imaging_workflow() {
    // Setup servers
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    uint16_t emr_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;
    mock_ris_server ris(ris_config);

    mock_emr_server::config emr_config;
    emr_config.port = emr_port;
    mock_emr_server emr(emr_config);

    // Add order to EMR
    mock_emr_server::order_record order;
    order.order_id = "ORD001";
    order.patient_id = "PAT001";
    order.patient_name = "DOE^JOHN";
    order.procedure_code = "CT-CHEST";
    order.order_status = "PLACED";
    order.result_status = "PENDING";
    emr.add_order(order);

    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS server");
    INTEGRATION_TEST_ASSERT(emr.start(), "Failed to start EMR server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris, &emr]() { return ris.is_running() && emr.is_running(); },
            std::chrono::milliseconds{2000}),
        "Servers should start");

    // Step 1: Send order status update to RIS (procedure started)
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = ris_port;
        mllp::mllp_client client(client_config);

        INTEGRATION_TEST_ASSERT(client.connect().has_value(),
                                "Should connect to RIS");

        std::string orm_ip =
            "MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|20240115120000||ORM^O01|MSG001|P|2.4\r"
            "PID|1||PAT001|||DOE^JOHN\r"
            "ORC|SC|ORD001||ACC001||IP\r"
            "OBR|1|ORD001||CT-CHEST\r";
        auto msg = mllp::mllp_message::from_string(orm_ip);
        auto result = client.send(msg);
        INTEGRATION_TEST_ASSERT(result.has_value(),
                                "Should send IP status to RIS");

        client.disconnect();
    }

    // Step 2: Send order status update to EMR (procedure started)
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = emr_port;
        mllp::mllp_client client(client_config);

        INTEGRATION_TEST_ASSERT(client.connect().has_value(),
                                "Should connect to EMR");

        std::string orm_ip =
            "MSH|^~\\&|PACS|RADIOLOGY|EMR|HOSPITAL|20240115120100||ORM^O01|MSG002|P|2.4\r"
            "PID|1||PAT001|||DOE^JOHN\r"
            "ORC|SC|ORD001||ACC001||IP\r"
            "OBR|1|ORD001||CT-CHEST\r";
        auto msg = mllp::mllp_message::from_string(orm_ip);
        auto result = client.send(msg);
        INTEGRATION_TEST_ASSERT(result.has_value(),
                                "Should send IP status to EMR");

        client.disconnect();
    }

    // Verify EMR order status updated
    auto order_ip = emr.get_order("ORD001");
    INTEGRATION_TEST_ASSERT(order_ip.has_value(), "Order should exist");
    INTEGRATION_TEST_ASSERT(order_ip->order_status == "IN_PROGRESS",
                            "Order should be IN_PROGRESS");

    // Step 3: Send completion status to EMR
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = emr_port;
        mllp::mllp_client client(client_config);

        INTEGRATION_TEST_ASSERT(client.connect().has_value(),
                                "Should connect to EMR");

        std::string orm_cm =
            "MSH|^~\\&|PACS|RADIOLOGY|EMR|HOSPITAL|20240115121000||ORM^O01|MSG003|P|2.4\r"
            "PID|1||PAT001|||DOE^JOHN\r"
            "ORC|SC|ORD001||ACC001||CM\r"
            "OBR|1|ORD001||CT-CHEST\r";
        auto msg = mllp::mllp_message::from_string(orm_cm);
        auto result = client.send(msg);
        INTEGRATION_TEST_ASSERT(result.has_value(),
                                "Should send CM status to EMR");

        client.disconnect();
    }

    // Verify EMR order completed
    auto order_cm = emr.get_order("ORD001");
    INTEGRATION_TEST_ASSERT(order_cm->order_status == "COMPLETED",
                            "Order should be COMPLETED");

    // Step 4: Send result (ORU^R01) to EMR
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = emr_port;
        mllp::mllp_client client(client_config);

        INTEGRATION_TEST_ASSERT(client.connect().has_value(),
                                "Should connect to EMR");

        std::string oru_msg =
            "MSH|^~\\&|PACS|RADIOLOGY|EMR|HOSPITAL|20240115122000||ORU^R01|MSG004|P|2.4\r"
            "PID|1||PAT001|||DOE^JOHN\r"
            "OBR|1|ORD001|ORD001|CT-CHEST|||20240115120000|||||||||||||||F\r"
            "OBX|1|TX|IMPRESSION||NO ACUTE FINDINGS||||||F\r";
        auto msg = mllp::mllp_message::from_string(oru_msg);
        auto result = client.send(msg);
        INTEGRATION_TEST_ASSERT(result.has_value(),
                                "Should send result to EMR");

        client.disconnect();
    }

    // Verify result received
    INTEGRATION_TEST_ASSERT(emr.results_received() >= 1,
                            "EMR should receive result");
    auto order_final = emr.get_order("ORD001");
    INTEGRATION_TEST_ASSERT(order_final->result_status == "FINAL" ||
                                order_final->result_status == "F",
                            "Result should be FINAL");

    ris.stop();
    emr.stop();
    return true;
}

// =============================================================================
// E2E Scenario: Multi-Destination Routing
// =============================================================================

/**
 * @brief Test message routing to multiple destinations
 *
 * Scenario:
 * A status update from PACS needs to be routed to both RIS and EMR.
 */
bool test_e2e_multi_destination_routing() {
    // Setup multiple destination servers
    uint16_t ris_port = integration_test_fixture::generate_test_port();
    uint16_t emr_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    mock_ris_server ris(ris_config);

    mock_emr_server::config emr_config;
    emr_config.port = emr_port;
    mock_emr_server emr(emr_config);

    // Add order to EMR
    mock_emr_server::order_record order;
    order.order_id = "ORD002";
    order.patient_id = "PAT002";
    order.order_status = "PLACED";
    emr.add_order(order);

    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS");
    INTEGRATION_TEST_ASSERT(emr.start(), "Failed to start EMR");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris, &emr]() { return ris.is_running() && emr.is_running(); },
            std::chrono::milliseconds{2000}),
        "Servers should start");

    // Simulate PACS Bridge routing to both destinations
    std::string orm_msg =
        "MSH|^~\\&|PACS|RADIOLOGY|%DEST%|HOSPITAL|20240115120000||ORM^O01|MSG005|P|2.4\r"
        "PID|1||PAT002|||SMITH^JANE\r"
        "ORC|SC|ORD002||ACC002||IP\r"
        "OBR|1|ORD002||MR-BRAIN\r";

    // Send to RIS
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = ris_port;
        mllp::mllp_client client(client_config);

        INTEGRATION_TEST_ASSERT(client.connect().has_value(),
                                "Should connect to RIS");

        std::string ris_msg = orm_msg;
        size_t pos = ris_msg.find("%DEST%");
        if (pos != std::string::npos) {
            ris_msg.replace(pos, 6, "RIS");
        }

        auto msg = mllp::mllp_message::from_string(ris_msg);
        auto result = client.send(msg);
        INTEGRATION_TEST_ASSERT(result.has_value(), "Should send to RIS");

        client.disconnect();
    }

    // Send to EMR
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = emr_port;
        mllp::mllp_client client(client_config);

        INTEGRATION_TEST_ASSERT(client.connect().has_value(),
                                "Should connect to EMR");

        std::string emr_msg = orm_msg;
        size_t pos = emr_msg.find("%DEST%");
        if (pos != std::string::npos) {
            emr_msg.replace(pos, 6, "EMR");
        }

        auto msg = mllp::mllp_message::from_string(emr_msg);
        auto result = client.send(msg);
        INTEGRATION_TEST_ASSERT(result.has_value(), "Should send to EMR");

        client.disconnect();
    }

    // Verify both received the message
    INTEGRATION_TEST_ASSERT(ris.messages_received() >= 1,
                            "RIS should receive message");
    INTEGRATION_TEST_ASSERT(emr.orders_received() >= 1,
                            "EMR should receive message");

    // Verify EMR order status updated
    auto updated_order = emr.get_order("ORD002");
    INTEGRATION_TEST_ASSERT(updated_order.has_value(), "Order should exist");
    INTEGRATION_TEST_ASSERT(updated_order->order_status == "IN_PROGRESS",
                            "Order should be IN_PROGRESS");

    ris.stop();
    emr.stop();
    return true;
}

// =============================================================================
// E2E Scenario: Failover and Recovery
// =============================================================================

/**
 * @brief Test message delivery with primary destination failure
 *
 * Scenario:
 * Primary RIS fails, message should be delivered to backup RIS.
 */
bool test_e2e_failover_to_backup() {
    uint16_t primary_port = integration_test_fixture::generate_test_port();
    uint16_t backup_port = integration_test_fixture::generate_test_port();

    // Only start backup server (primary is "down")
    mock_ris_server::config backup_config;
    backup_config.port = backup_port;
    mock_ris_server backup(backup_config);

    INTEGRATION_TEST_ASSERT(backup.start(), "Failed to start backup RIS");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&backup]() { return backup.is_running(); },
            std::chrono::milliseconds{1000}),
        "Backup should start");

    // Try primary first (should fail)
    bool primary_success = false;
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = primary_port;
        client_config.connect_timeout = std::chrono::milliseconds{500};
        mllp::mllp_client client(client_config);

        primary_success = client.connect().has_value();
    }

    INTEGRATION_TEST_ASSERT(!primary_success,
                            "Primary should fail (not running)");

    // Failover to backup
    bool backup_success = false;
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = backup_port;
        mllp::mllp_client client(client_config);

        if (client.connect().has_value()) {
            std::string orm_msg =
                "MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|20240115120000||ORM^O01|MSG006|P|2.4\r"
                "PID|1||PAT003|||WILSON^TOM\r"
                "ORC|SC|ORD003||ACC003||IP\r"
                "OBR|1|ORD003||CT-ABD\r";
            auto msg = mllp::mllp_message::from_string(orm_msg);
            backup_success = client.send(msg).has_value();
            client.disconnect();
        }
    }

    INTEGRATION_TEST_ASSERT(backup_success,
                            "Backup delivery should succeed");
    INTEGRATION_TEST_ASSERT(backup.messages_received() >= 1,
                            "Backup should receive message");

    backup.stop();
    return true;
}

/**
 * @brief Test recovery after temporary failure
 *
 * Scenario:
 * Destination temporarily fails, then recovers. Subsequent messages succeed.
 */
bool test_e2e_recovery_after_failure() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // First attempt: no server running
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = port;
        client_config.connect_timeout = std::chrono::milliseconds{500};
        mllp::mllp_client client(client_config);

        auto connect_result = client.connect();
        INTEGRATION_TEST_ASSERT(!connect_result.has_value(),
                                "First attempt should fail");
    }

    // Start server (recovery)
    mock_ris_server::config ris_config;
    ris_config.port = port;
    mock_ris_server ris(ris_config);

    INTEGRATION_TEST_ASSERT(ris.start(), "Server should start");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "Server should be running");

    // Second attempt: should succeed
    {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = port;
        mllp::mllp_client client(client_config);

        auto connect_result = client.connect();
        INTEGRATION_TEST_ASSERT(connect_result.has_value(),
                                "Second attempt should succeed");

        std::string orm_msg =
            "MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|20240115120000||ORM^O01|MSG007|P|2.4\r"
            "PID|1||PAT004|||BROWN^BOB\r"
            "ORC|SC|ORD004||ACC004||CM\r"
            "OBR|1|ORD004||XR-CHEST\r";
        auto msg = mllp::mllp_message::from_string(orm_msg);
        auto send_result = client.send(msg);
        INTEGRATION_TEST_ASSERT(send_result.has_value(),
                                "Send should succeed after recovery");

        client.disconnect();
    }

    INTEGRATION_TEST_ASSERT(ris.messages_received() >= 1,
                            "Server should receive message after recovery");

    ris.stop();
    return true;
}

// =============================================================================
// E2E Scenario: High Volume Message Processing
// =============================================================================

/**
 * @brief Test processing multiple messages in rapid succession
 *
 * Scenario:
 * Multiple MPPS events occur rapidly and all need to be processed.
 */
bool test_e2e_high_volume_processing() {
    uint16_t port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = port;
    mock_ris_server ris(ris_config);

    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS should start");

    const int message_count = 20;
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    // Send messages concurrently
    for (int i = 0; i < message_count; ++i) {
        threads.emplace_back([&, i]() {
            mllp::mllp_client_config client_config;
            client_config.host = "localhost";
            client_config.port = port;
            mllp::mllp_client client(client_config);

            if (!client.connect().has_value()) {
                return;
            }

            std::string orm_msg =
                "MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|20240115120000||ORM^O01|MSGVOL" +
                std::to_string(i) + "|P|2.4\r" + "PID|1||PATVOL" +
                std::to_string(i) + "|||PATIENT^VOL" + std::to_string(i) +
                "\r" + "ORC|SC|ORDVOL" + std::to_string(i) + "||ACCVOL" +
                std::to_string(i) + "||IP\r" + "OBR|1|ORDVOL" +
                std::to_string(i) + "||CT\r";
            auto msg = mllp::mllp_message::from_string(orm_msg);

            if (client.send(msg).has_value()) {
                success_count++;
            }

            client.disconnect();
        });
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    INTEGRATION_TEST_ASSERT(success_count == message_count,
                            "All " + std::to_string(message_count) +
                                " messages should succeed");
    INTEGRATION_TEST_ASSERT(
        ris.messages_received() >= static_cast<uint32_t>(message_count),
        "RIS should receive all messages");

    ris.stop();
    return true;
}

// =============================================================================
// E2E Scenario: Mixed Message Types
// =============================================================================

/**
 * @brief Test handling of different HL7 message types in sequence
 *
 * Scenario:
 * System receives ADT, ORM, and ORU messages and processes them correctly.
 */
bool test_e2e_mixed_message_types() {
    uint16_t port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = port;
    mock_ris_server ris(ris_config);

    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start RIS");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS should start");

    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    mllp::mllp_client client(client_config);

    INTEGRATION_TEST_ASSERT(client.connect().has_value(),
                            "Should connect to RIS");

    // Message 1: ADT^A01 (Patient Admission)
    {
        std::string adt_msg =
            "MSH|^~\\&|HIS|HOSPITAL|RIS|RADIOLOGY|20240115100000||ADT^A01|ADT001|P|2.4\r"
            "EVN|A01|20240115100000\r"
            "PID|1||PAT100|||JONES^MARY\r"
            "PV1|1|I|RAD^1001^01\r";
        auto msg = mllp::mllp_message::from_string(adt_msg);
        auto result = client.send(msg);
        INTEGRATION_TEST_ASSERT(result.has_value(), "ADT should be accepted");
    }

    // Message 2: ORM^O01 (Order)
    {
        std::string orm_msg =
            "MSH|^~\\&|HIS|HOSPITAL|RIS|RADIOLOGY|20240115100500||ORM^O01|ORM001|P|2.4\r"
            "PID|1||PAT100|||JONES^MARY\r"
            "ORC|NW|ORD100||ACC100||SC\r"
            "OBR|1|ORD100||CT-HEAD\r";
        auto msg = mllp::mllp_message::from_string(orm_msg);
        auto result = client.send(msg);
        INTEGRATION_TEST_ASSERT(result.has_value(), "ORM should be accepted");
    }

    // Message 3: ORM^O01 (Status Update)
    {
        std::string orm_ip =
            "MSH|^~\\&|PACS|RADIOLOGY|RIS|RADIOLOGY|20240115110000||ORM^O01|ORM002|P|2.4\r"
            "PID|1||PAT100|||JONES^MARY\r"
            "ORC|SC|ORD100||ACC100||IP\r"
            "OBR|1|ORD100||CT-HEAD\r";
        auto msg = mllp::mllp_message::from_string(orm_ip);
        auto result = client.send(msg);
        INTEGRATION_TEST_ASSERT(result.has_value(),
                                "Status update should be accepted");
    }

    // Message 4: ORU^R01 (Result)
    {
        std::string oru_msg =
            "MSH|^~\\&|PACS|RADIOLOGY|RIS|RADIOLOGY|20240115120000||ORU^R01|ORU001|P|2.4\r"
            "PID|1||PAT100|||JONES^MARY\r"
            "OBR|1|ORD100|ORD100|CT-HEAD|||20240115110000|||||||||||||||F\r"
            "OBX|1|TX|IMPRESSION||NORMAL STUDY||||||F\r";
        auto msg = mllp::mllp_message::from_string(oru_msg);
        auto result = client.send(msg);
        INTEGRATION_TEST_ASSERT(result.has_value(),
                                "Result should be accepted");
    }

    client.disconnect();

    // Verify all messages received
    INTEGRATION_TEST_ASSERT(ris.messages_received() >= 4,
                            "RIS should receive all 4 messages");

    ris.stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_e2e_scenario_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== End-to-End Scenario Tests ===" << std::endl;
    std::cout << "Testing Issue #161: E2E Scenarios\n" << std::endl;

    std::cout << "\n--- Complete Workflow Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_e2e_complete_imaging_workflow);

    std::cout << "\n--- Multi-Destination Routing Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_e2e_multi_destination_routing);

    std::cout << "\n--- Failover and Recovery Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_e2e_failover_to_backup);
    RUN_INTEGRATION_TEST(test_e2e_recovery_after_failure);

    std::cout << "\n--- High Volume Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_e2e_high_volume_processing);

    std::cout << "\n--- Mixed Message Type Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_e2e_mixed_message_types);

    std::cout << "\n=== E2E Scenario Test Summary ===" << std::endl;
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
    return pacs::bridge::integration::test::run_all_e2e_scenario_tests();
}
