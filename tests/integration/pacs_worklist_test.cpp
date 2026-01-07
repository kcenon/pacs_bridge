/**
 * @file pacs_worklist_test.cpp
 * @brief Integration tests for PACS worklist and MWL operations
 *
 * Tests for PACS integration including:
 * - Worklist query and update operations
 * - MWL (Modality Worklist) synchronization
 * - MPPS status update propagation
 * - Order status lifecycle management
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/161
 * @see https://github.com/kcenon/pacs_bridge/issues/145
 */

#include "integration_test_base.h"

#include <chrono>
#include <iostream>
#include <map>
#include <thread>

namespace pacs::bridge::integration::test {

// =============================================================================
// Mock PACS Server for Worklist Testing
// =============================================================================

/**
 * @brief Mock PACS server that simulates worklist operations
 *
 * Provides worklist query responses and tracks order status updates.
 */
class mock_pacs_server {
public:
    struct worklist_item {
        std::string accession_number;
        std::string patient_id;
        std::string patient_name;
        std::string scheduled_procedure_id;
        std::string modality;
        std::string scheduled_station_ae;
        std::string status;  // SCHEDULED, IN_PROGRESS, COMPLETED, CANCELLED
        std::chrono::system_clock::time_point scheduled_time;
    };

    struct config {
        uint16_t port = 12900;
        bool auto_ack = true;
    };

    explicit mock_pacs_server(const config& cfg)
        : config_(cfg), running_(false), queries_received_(0),
          updates_received_(0) {}

    ~mock_pacs_server() {
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

    // Worklist management
    void add_worklist_item(const worklist_item& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        worklist_[item.accession_number] = item;
    }

    void clear_worklist() {
        std::lock_guard<std::mutex> lock(mutex_);
        worklist_.clear();
    }

    std::optional<worklist_item> get_worklist_item(
        const std::string& accession_number) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = worklist_.find(accession_number);
        if (it != worklist_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    size_t worklist_size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return worklist_.size();
    }

    uint32_t queries_received() const {
        return queries_received_;
    }

    uint32_t updates_received() const {
        return updates_received_;
    }

    uint16_t port() const {
        return config_.port;
    }

private:
    std::optional<mllp::mllp_message> handle_message(
        const mllp::mllp_message& msg) {
        std::string msg_str = msg.to_string();

        // Parse message type
        if (msg_str.find("QRY^") != std::string::npos ||
            msg_str.find("QBP^") != std::string::npos) {
            queries_received_++;
            return handle_worklist_query(msg);
        } else if (msg_str.find("ORM^O01") != std::string::npos) {
            updates_received_++;
            return handle_order_update(msg);
        }

        // Default ACK
        return generate_ack(msg, "AA");
    }

    std::optional<mllp::mllp_message> handle_worklist_query(
        const mllp::mllp_message& msg) {
        // Generate RSP (response) with worklist items
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp[15];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                      std::localtime(&time_t_now));

        std::ostringstream oss;
        oss << "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|" << timestamp
            << "||RSP^K23|RSP001|P|2.4\r"
            << "MSA|AA|QRY001\r"
            << "QAK|QRY001|OK\r";

        // Add worklist items as SIU segments (simplified)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int seq = 1;
            for (const auto& [acc, item] : worklist_) {
                oss << "SCH|" << seq++ << "||" << item.accession_number
                    << "||" << item.status << "\r"
                    << "PID|1||" << item.patient_id << "|||" << item.patient_name
                    << "\r"
                    << "RGS|1\r"
                    << "AIS|1||" << item.modality << "\r";
            }
        }

        return mllp::mllp_message::from_string(oss.str());
    }

    std::optional<mllp::mllp_message> handle_order_update(
        const mllp::mllp_message& msg) {
        // Parse ORM message and update worklist
        hl7::hl7_parser parser;
        auto parse_result = parser.parse(msg.to_string());

        if (parse_result.is_ok()) {
            std::string accession(parse_result.value().get_value("ORC.4"));
            std::string status(parse_result.value().get_value("ORC.5"));

            if (!accession.empty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = worklist_.find(accession);
                if (it != worklist_.end()) {
                    // Map HL7 status to internal status
                    if (status == "IP") {
                        it->second.status = "IN_PROGRESS";
                    } else if (status == "CM") {
                        it->second.status = "COMPLETED";
                    } else if (status == "DC") {
                        it->second.status = "CANCELLED";
                    }
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
        if (parse_result.is_ok()) {
            msg_control_id = parse_result.value().get_value("MSH.10");
        }

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp[15];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                      std::localtime(&time_t_now));

        std::string ack = "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|" +
                          std::string(timestamp) + "||ACK|ACK" + msg_control_id +
                          "|P|2.4\r" + "MSA|" + ack_code + "|" + msg_control_id +
                          "\r";

        return mllp::mllp_message::from_string(ack);
    }

    config config_;
    std::unique_ptr<mllp::mllp_server> server_;
    std::atomic<bool> running_;
    std::atomic<uint32_t> queries_received_;
    std::atomic<uint32_t> updates_received_;
    std::map<std::string, worklist_item> worklist_;
    mutable std::mutex mutex_;
};

// =============================================================================
// Worklist Query Tests
// =============================================================================

/**
 * @brief Test basic worklist query operation
 *
 * Verifies that a worklist query can be sent and response received.
 */
bool test_worklist_query_basic() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup PACS server
    mock_pacs_server::config pacs_config;
    pacs_config.port = port;
    mock_pacs_server pacs(pacs_config);

    // Add worklist items
    mock_pacs_server::worklist_item item1;
    item1.accession_number = "ACC001";
    item1.patient_id = "PAT001";
    item1.patient_name = "DOE^JOHN";
    item1.scheduled_procedure_id = "SPS001";
    item1.modality = "CT";
    item1.status = "SCHEDULED";
    pacs.add_worklist_item(item1);

    INTEGRATION_TEST_ASSERT(pacs.start(), "Failed to start mock PACS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&pacs]() { return pacs.is_running(); },
            std::chrono::milliseconds{1000}),
        "PACS server should start");

    // Send worklist query
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{5000};

    mllp::mllp_client client(client_config);
    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(), "Client should connect");

    // Create QBP (Query By Parameter) message
    std::string qry_msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115120000||QBP^Q11|QRY001|P|2.4\r"
        "QPD|IHE MWL|QRY001|CT\r"
        "RCP|I\r";
    auto msg = mllp::mllp_message::from_string(qry_msg);
    auto send_result = client.send(msg);

    INTEGRATION_TEST_ASSERT(send_result.has_value(), "Query should succeed");
    INTEGRATION_TEST_ASSERT(!send_result->response.content.empty(),
                            "Should receive response");

    // Verify query was received
    INTEGRATION_TEST_ASSERT(pacs.queries_received() == 1,
                            "PACS should receive query");

    // Verify response contains worklist data
    std::string response_str = send_result->response.to_string();
    INTEGRATION_TEST_ASSERT(response_str.find("RSP") != std::string::npos,
                            "Response should be RSP message");

    client.disconnect();
    pacs.stop();
    return true;
}

/**
 * @brief Test worklist query with multiple items
 *
 * Verifies that multiple worklist items are returned correctly.
 */
bool test_worklist_query_multiple_items() {
    uint16_t port = integration_test_fixture::generate_test_port();

    mock_pacs_server::config pacs_config;
    pacs_config.port = port;
    mock_pacs_server pacs(pacs_config);

    // Add multiple worklist items
    for (int i = 1; i <= 5; ++i) {
        mock_pacs_server::worklist_item item;
        item.accession_number = "ACC00" + std::to_string(i);
        item.patient_id = "PAT00" + std::to_string(i);
        item.patient_name = "PATIENT^" + std::to_string(i);
        item.scheduled_procedure_id = "SPS00" + std::to_string(i);
        item.modality = (i % 2 == 0) ? "MR" : "CT";
        item.status = "SCHEDULED";
        pacs.add_worklist_item(item);
    }

    INTEGRATION_TEST_ASSERT(pacs.start(), "Failed to start mock PACS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&pacs]() { return pacs.is_running(); },
            std::chrono::milliseconds{1000}),
        "PACS server should start");

    INTEGRATION_TEST_ASSERT(pacs.worklist_size() == 5,
                            "Should have 5 worklist items");

    // Query worklist
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    mllp::mllp_client client(client_config);
    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(), "Client should connect");

    std::string qry_msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115120000||QBP^Q11|QRY002|P|2.4\r"
        "QPD|IHE MWL|QRY002\r"
        "RCP|I\r";
    auto msg = mllp::mllp_message::from_string(qry_msg);
    auto send_result = client.send(msg);

    INTEGRATION_TEST_ASSERT(send_result.has_value(), "Query should succeed");

    // Verify response contains multiple items
    std::string response_str = send_result->response.to_string();
    INTEGRATION_TEST_ASSERT(response_str.find("ACC001") != std::string::npos,
                            "Response should contain ACC001");
    INTEGRATION_TEST_ASSERT(response_str.find("ACC005") != std::string::npos,
                            "Response should contain ACC005");

    client.disconnect();
    pacs.stop();
    return true;
}

// =============================================================================
// Worklist Update Tests
// =============================================================================

/**
 * @brief Test order status update to IN_PROGRESS
 *
 * Verifies that sending an MPPS IN_PROGRESS status updates the worklist.
 */
bool test_worklist_update_in_progress() {
    uint16_t port = integration_test_fixture::generate_test_port();

    mock_pacs_server::config pacs_config;
    pacs_config.port = port;
    mock_pacs_server pacs(pacs_config);

    // Add worklist item
    mock_pacs_server::worklist_item item;
    item.accession_number = "ACC100";
    item.patient_id = "PAT100";
    item.patient_name = "SMITH^JANE";
    item.status = "SCHEDULED";
    pacs.add_worklist_item(item);

    INTEGRATION_TEST_ASSERT(pacs.start(), "Failed to start mock PACS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&pacs]() { return pacs.is_running(); },
            std::chrono::milliseconds{1000}),
        "PACS server should start");

    // Verify initial status
    auto initial_item = pacs.get_worklist_item("ACC100");
    INTEGRATION_TEST_ASSERT(initial_item.has_value(), "Item should exist");
    INTEGRATION_TEST_ASSERT(initial_item->status == "SCHEDULED",
                            "Initial status should be SCHEDULED");

    // Send ORM with IP status
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    mllp::mllp_client client(client_config);
    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(), "Client should connect");

    std::string orm_msg =
        "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115120000||ORM^O01|MSG100|P|2.4\r"
        "PID|1||PAT100|||SMITH^JANE\r"
        "ORC|SC|SPS100||ACC100||IP\r"
        "OBR|1|SPS100||CT\r";
    auto msg = mllp::mllp_message::from_string(orm_msg);
    auto send_result = client.send(msg);

    INTEGRATION_TEST_ASSERT(send_result.has_value(), "Update should succeed");
    INTEGRATION_TEST_ASSERT(pacs.updates_received() == 1,
                            "PACS should receive update");

    // Verify status changed
    auto updated_item = pacs.get_worklist_item("ACC100");
    INTEGRATION_TEST_ASSERT(updated_item.has_value(), "Item should still exist");
    INTEGRATION_TEST_ASSERT(updated_item->status == "IN_PROGRESS",
                            "Status should be IN_PROGRESS");

    client.disconnect();
    pacs.stop();
    return true;
}

/**
 * @brief Test order status update to COMPLETED
 *
 * Verifies that sending an MPPS COMPLETED status updates the worklist.
 */
bool test_worklist_update_completed() {
    uint16_t port = integration_test_fixture::generate_test_port();

    mock_pacs_server::config pacs_config;
    pacs_config.port = port;
    mock_pacs_server pacs(pacs_config);

    // Add worklist item in progress
    mock_pacs_server::worklist_item item;
    item.accession_number = "ACC200";
    item.patient_id = "PAT200";
    item.patient_name = "BROWN^BOB";
    item.status = "IN_PROGRESS";
    pacs.add_worklist_item(item);

    INTEGRATION_TEST_ASSERT(pacs.start(), "Failed to start mock PACS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&pacs]() { return pacs.is_running(); },
            std::chrono::milliseconds{1000}),
        "PACS server should start");

    // Send ORM with CM status
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    mllp::mllp_client client(client_config);
    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(), "Client should connect");

    std::string orm_msg =
        "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115120000||ORM^O01|MSG200|P|2.4\r"
        "PID|1||PAT200|||BROWN^BOB\r"
        "ORC|SC|SPS200||ACC200||CM\r"
        "OBR|1|SPS200||MR\r";
    auto msg = mllp::mllp_message::from_string(orm_msg);
    auto send_result = client.send(msg);

    INTEGRATION_TEST_ASSERT(send_result.has_value(), "Update should succeed");

    // Verify status changed to COMPLETED
    auto updated_item = pacs.get_worklist_item("ACC200");
    INTEGRATION_TEST_ASSERT(updated_item.has_value(), "Item should exist");
    INTEGRATION_TEST_ASSERT(updated_item->status == "COMPLETED",
                            "Status should be COMPLETED");

    client.disconnect();
    pacs.stop();
    return true;
}

/**
 * @brief Test order status update to CANCELLED (discontinued)
 *
 * Verifies that sending an MPPS DISCONTINUED status updates the worklist.
 */
bool test_worklist_update_cancelled() {
    uint16_t port = integration_test_fixture::generate_test_port();

    mock_pacs_server::config pacs_config;
    pacs_config.port = port;
    mock_pacs_server pacs(pacs_config);

    // Add worklist item
    mock_pacs_server::worklist_item item;
    item.accession_number = "ACC300";
    item.patient_id = "PAT300";
    item.patient_name = "JONES^MARY";
    item.status = "IN_PROGRESS";
    pacs.add_worklist_item(item);

    INTEGRATION_TEST_ASSERT(pacs.start(), "Failed to start mock PACS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&pacs]() { return pacs.is_running(); },
            std::chrono::milliseconds{1000}),
        "PACS server should start");

    // Send ORM with DC status
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    mllp::mllp_client client(client_config);
    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(), "Client should connect");

    std::string orm_msg =
        "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115120000||ORM^O01|MSG300|P|2.4\r"
        "PID|1||PAT300|||JONES^MARY\r"
        "ORC|SC|SPS300||ACC300||DC\r"
        "OBR|1|SPS300||CT\r";
    auto msg = mllp::mllp_message::from_string(orm_msg);
    auto send_result = client.send(msg);

    INTEGRATION_TEST_ASSERT(send_result.has_value(), "Update should succeed");

    // Verify status changed to CANCELLED
    auto updated_item = pacs.get_worklist_item("ACC300");
    INTEGRATION_TEST_ASSERT(updated_item.has_value(), "Item should exist");
    INTEGRATION_TEST_ASSERT(updated_item->status == "CANCELLED",
                            "Status should be CANCELLED");

    client.disconnect();
    pacs.stop();
    return true;
}

// =============================================================================
// Complete Workflow Tests
// =============================================================================

/**
 * @brief Test complete worklist lifecycle
 *
 * Verifies full workflow: SCHEDULED -> IN_PROGRESS -> COMPLETED
 */
bool test_worklist_complete_lifecycle() {
    uint16_t port = integration_test_fixture::generate_test_port();

    mock_pacs_server::config pacs_config;
    pacs_config.port = port;
    mock_pacs_server pacs(pacs_config);

    // Add scheduled item
    mock_pacs_server::worklist_item item;
    item.accession_number = "ACC400";
    item.patient_id = "PAT400";
    item.patient_name = "WILSON^TOM";
    item.scheduled_procedure_id = "SPS400";
    item.modality = "CT";
    item.status = "SCHEDULED";
    pacs.add_worklist_item(item);

    INTEGRATION_TEST_ASSERT(pacs.start(), "Failed to start mock PACS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&pacs]() { return pacs.is_running(); },
            std::chrono::milliseconds{1000}),
        "PACS server should start");

    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    mllp::mllp_client client(client_config);
    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(), "Client should connect");

    // Step 1: Start procedure (SCHEDULED -> IN_PROGRESS)
    std::string orm_ip =
        "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115120000||ORM^O01|MSG401|P|2.4\r"
        "PID|1||PAT400|||WILSON^TOM\r"
        "ORC|SC|SPS400||ACC400||IP\r"
        "OBR|1|SPS400||CT\r";
    auto msg_ip = mllp::mllp_message::from_string(orm_ip);
    auto result_ip = client.send(msg_ip);
    INTEGRATION_TEST_ASSERT(result_ip.has_value(), "IP update should succeed");

    auto item_ip = pacs.get_worklist_item("ACC400");
    INTEGRATION_TEST_ASSERT(item_ip->status == "IN_PROGRESS",
                            "Status should be IN_PROGRESS after N-CREATE");

    // Step 2: Complete procedure (IN_PROGRESS -> COMPLETED)
    std::string orm_cm =
        "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115121000||ORM^O01|MSG402|P|2.4\r"
        "PID|1||PAT400|||WILSON^TOM\r"
        "ORC|SC|SPS400||ACC400||CM\r"
        "OBR|1|SPS400||CT\r";
    auto msg_cm = mllp::mllp_message::from_string(orm_cm);
    auto result_cm = client.send(msg_cm);
    INTEGRATION_TEST_ASSERT(result_cm.has_value(), "CM update should succeed");

    auto item_cm = pacs.get_worklist_item("ACC400");
    INTEGRATION_TEST_ASSERT(item_cm->status == "COMPLETED",
                            "Status should be COMPLETED after N-SET COMPLETED");

    // Verify update count
    INTEGRATION_TEST_ASSERT(pacs.updates_received() == 2,
                            "PACS should receive 2 updates");

    client.disconnect();
    pacs.stop();
    return true;
}

/**
 * @brief Test multiple concurrent worklist updates
 *
 * Verifies that multiple updates can be processed concurrently.
 */
bool test_worklist_concurrent_updates() {
    uint16_t port = integration_test_fixture::generate_test_port();

    mock_pacs_server::config pacs_config;
    pacs_config.port = port;
    mock_pacs_server pacs(pacs_config);

    // Add multiple items
    const int item_count = 5;
    for (int i = 0; i < item_count; ++i) {
        mock_pacs_server::worklist_item item;
        item.accession_number = "ACC50" + std::to_string(i);
        item.patient_id = "PAT50" + std::to_string(i);
        item.patient_name = "PATIENT^" + std::to_string(i);
        item.status = "SCHEDULED";
        pacs.add_worklist_item(item);
    }

    INTEGRATION_TEST_ASSERT(pacs.start(), "Failed to start mock PACS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&pacs]() { return pacs.is_running(); },
            std::chrono::milliseconds{1000}),
        "PACS server should start");

    // Send updates concurrently
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < item_count; ++i) {
        threads.emplace_back([&, i]() {
            mllp::mllp_client_config client_config;
            client_config.host = "localhost";
            client_config.port = port;
            mllp::mllp_client client(client_config);

            if (!client.connect().has_value()) {
                return;
            }

            std::string orm_msg =
                "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115120000||ORM^O01|MSG50" +
                std::to_string(i) + "|P|2.4\r" + "PID|1||PAT50" +
                std::to_string(i) + "|||PATIENT^" + std::to_string(i) + "\r" +
                "ORC|SC|SPS50" + std::to_string(i) + "||ACC50" +
                std::to_string(i) + "||IP\r" + "OBR|1|SPS50" +
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

    INTEGRATION_TEST_ASSERT(success_count == item_count,
                            "All updates should succeed");
    INTEGRATION_TEST_ASSERT(
        pacs.updates_received() >= static_cast<uint32_t>(item_count),
        "PACS should receive all updates");

    // Verify all items are updated
    for (int i = 0; i < item_count; ++i) {
        auto item = pacs.get_worklist_item("ACC50" + std::to_string(i));
        INTEGRATION_TEST_ASSERT(item.has_value(),
                                "Item ACC50" + std::to_string(i) + " should exist");
        INTEGRATION_TEST_ASSERT(item->status == "IN_PROGRESS",
                                "Item ACC50" + std::to_string(i) +
                                    " should be IN_PROGRESS");
    }

    pacs.stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_pacs_worklist_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== PACS Worklist Integration Tests ===" << std::endl;
    std::cout << "Testing Issue #161: PACS Integration\n" << std::endl;

    std::cout << "\n--- Worklist Query Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_worklist_query_basic);
    RUN_INTEGRATION_TEST(test_worklist_query_multiple_items);

    std::cout << "\n--- Worklist Update Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_worklist_update_in_progress);
    RUN_INTEGRATION_TEST(test_worklist_update_completed);
    RUN_INTEGRATION_TEST(test_worklist_update_cancelled);

    std::cout << "\n--- Complete Workflow Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_worklist_complete_lifecycle);
    RUN_INTEGRATION_TEST(test_worklist_concurrent_updates);

    std::cout << "\n=== PACS Worklist Test Summary ===" << std::endl;
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
    return pacs::bridge::integration::test::run_all_pacs_worklist_tests();
}
