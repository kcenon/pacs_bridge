/**
 * @file integration_test_base.h
 * @brief Base infrastructure for Phase 2 integration tests
 *
 * Provides common utilities, test fixtures, and mock components for
 * end-to-end integration testing of MPPS and bidirectional messaging.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/29
 */

#ifndef PACS_BRIDGE_TESTS_INTEGRATION_TEST_BASE_H
#define PACS_BRIDGE_TESTS_INTEGRATION_TEST_BASE_H

#include "pacs/bridge/mllp/mllp_client.h"
#include "pacs/bridge/mllp/mllp_server.h"
#include "pacs/bridge/mllp/mllp_types.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/router/message_router.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::integration::test {

// =============================================================================
// Test Macros
// =============================================================================

#define INTEGRATION_TEST_ASSERT(condition, message)                            \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_INTEGRATION_TEST(test_func)                                        \
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
// MPPS Status Codes
// =============================================================================

/**
 * @brief MPPS procedure status codes for HL7 ORM messages
 */
enum class mpps_status {
    in_progress,   ///< IP - Procedure in progress (N-CREATE)
    completed,     ///< CM - Procedure completed (N-SET COMPLETED)
    discontinued   ///< DC - Procedure discontinued (N-SET DISCONTINUED)
};

inline std::string_view to_hl7_status(mpps_status status) {
    switch (status) {
        case mpps_status::in_progress:
            return "IP";
        case mpps_status::completed:
            return "CM";
        case mpps_status::discontinued:
            return "DC";
        default:
            return "IP";
    }
}

inline std::string_view to_string(mpps_status status) {
    switch (status) {
        case mpps_status::in_progress:
            return "IN_PROGRESS";
        case mpps_status::completed:
            return "COMPLETED";
        case mpps_status::discontinued:
            return "DISCONTINUED";
        default:
            return "UNKNOWN";
    }
}

// =============================================================================
// Message Queue for Persistence Testing
// =============================================================================

/**
 * @brief Simple persistent message queue for testing queue recovery scenarios
 *
 * Stores messages to a temporary file and supports recovery after simulated
 * failures. Used to test message persistence and redelivery functionality.
 */
class test_message_queue {
public:
    explicit test_message_queue(const std::filesystem::path& storage_path)
        : storage_path_(storage_path), running_(false) {
        load_from_disk();
    }

    ~test_message_queue() {
        stop();
    }

    void start() {
        running_ = true;
    }

    void stop() {
        running_ = false;
    }

    bool is_running() const {
        return running_;
    }

    void enqueue(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(message);
        save_to_disk();
    }

    std::optional<std::string> dequeue() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        auto msg = queue_.front();
        queue_.pop_front();
        save_to_disk();
        return msg;
    }

    std::optional<std::string> peek() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        return queue_.front();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        return size() == 0;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        save_to_disk();
    }

    /**
     * @brief Simulate crash and recovery by reloading from disk
     */
    void simulate_recovery() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        load_from_disk_unlocked();
    }

private:
    void save_to_disk() {
        std::ofstream file(storage_path_, std::ios::binary | std::ios::trunc);
        if (!file) {
            return;
        }

        // Write queue size
        size_t count = queue_.size();
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));

        // Write each message
        for (const auto& msg : queue_) {
            size_t len = msg.size();
            file.write(reinterpret_cast<const char*>(&len), sizeof(len));
            file.write(msg.data(), static_cast<std::streamsize>(len));
        }
    }

    void load_from_disk() {
        std::lock_guard<std::mutex> lock(mutex_);
        load_from_disk_unlocked();
    }

    void load_from_disk_unlocked() {
        if (!std::filesystem::exists(storage_path_)) {
            return;
        }

        std::ifstream file(storage_path_, std::ios::binary);
        if (!file) {
            return;
        }

        // Read queue size
        size_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));

        // Read each message
        for (size_t i = 0; i < count && file.good(); ++i) {
            size_t len = 0;
            file.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (len > 0 && len < 10 * 1024 * 1024) {  // Max 10MB
                std::string msg(len, '\0');
                file.read(msg.data(), static_cast<std::streamsize>(len));
                queue_.push_back(std::move(msg));
            }
        }
    }

    std::filesystem::path storage_path_;
    std::deque<std::string> queue_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_;
};

// =============================================================================
// Mock RIS Server for Integration Testing
// =============================================================================

/**
 * @brief Mock RIS (Radiology Information System) server for integration tests
 *
 * Simulates a RIS endpoint that receives HL7 messages via MLLP and responds
 * with ACK messages. Supports configurable availability for failover testing.
 */
class mock_ris_server {
public:
    struct config {
        uint16_t port = 12800;
        bool auto_ack = true;
        std::chrono::milliseconds response_delay{0};
        bool simulate_failure = false;
    };

    explicit mock_ris_server(const config& cfg)
        : config_(cfg), running_(false), messages_received_(0) {}

    ~mock_ris_server() {
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

    void set_available(bool available) {
        config_.simulate_failure = !available;
    }

    bool is_available() const {
        return !config_.simulate_failure;
    }

    uint32_t messages_received() const {
        return messages_received_;
    }

    const std::vector<std::string>& received_messages() const {
        return received_messages_;
    }

    void clear_received() {
        messages_received_ = 0;
        received_messages_.clear();
    }

    void set_response_delay(std::chrono::milliseconds delay) {
        config_.response_delay = delay;
    }

    uint16_t port() const {
        return config_.port;
    }

private:
    std::optional<mllp::mllp_message> handle_message(
        const mllp::mllp_message& msg) {
        // Simulate failure
        if (config_.simulate_failure) {
            return std::nullopt;
        }

        // Apply response delay
        if (config_.response_delay.count() > 0) {
            std::this_thread::sleep_for(config_.response_delay);
        }

        // Store received message
        {
            std::lock_guard<std::mutex> lock(mutex_);
            received_messages_.push_back(msg.to_string());
            messages_received_++;
        }

        // Generate ACK if auto_ack is enabled
        if (config_.auto_ack) {
            return generate_ack(msg);
        }

        return std::nullopt;
    }

    mllp::mllp_message generate_ack(const mllp::mllp_message& original) {
        // Parse original to extract message control ID
        hl7::hl7_parser parser;
        auto parse_result = parser.parse(original.to_string());

        std::string msg_control_id = "0";
        std::string sending_app = "TEST";
        std::string sending_facility = "FACILITY";

        if (parse_result.is_ok()) {
            msg_control_id = parse_result.value().get_value("MSH.10");
            sending_app = parse_result.value().get_value("MSH.3");
            sending_facility = parse_result.value().get_value("MSH.4");
        }

        // Build ACK message
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp[15];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                      std::localtime(&time_t_now));

        std::string ack =
            "MSH|^~\\&|RIS|RADIOLOGY|" + sending_app + "|" + sending_facility +
            "|" + timestamp + "||ACK|ACK" + msg_control_id + "|P|2.4\r" +
            "MSA|AA|" + msg_control_id + "\r";

        return mllp::mllp_message::from_string(ack);
    }

    config config_;
    std::unique_ptr<mllp::mllp_server> server_;
    std::atomic<bool> running_;
    std::atomic<uint32_t> messages_received_;
    std::vector<std::string> received_messages_;
    std::mutex mutex_;
};

// =============================================================================
// MPPS Bridge Simulator
// =============================================================================

/**
 * @brief Simulates MPPS bridge behavior for integration testing
 *
 * Converts simulated DICOM MPPS N-CREATE/N-SET operations into HL7 ORM^O01
 * messages and routes them to configured RIS endpoints.
 */
class mpps_bridge_simulator {
public:
    struct mpps_event {
        std::string sop_instance_uid;
        std::string patient_id;
        std::string patient_name;
        std::string accession_number;
        std::string scheduled_procedure_id;
        std::string modality;
        mpps_status status;
        std::chrono::system_clock::time_point timestamp;
    };

    explicit mpps_bridge_simulator(uint16_t ris_port)
        : ris_port_(ris_port), messages_sent_(0) {}

    /**
     * @brief Process MPPS N-CREATE event (procedure started)
     */
    bool process_n_create(const mpps_event& event) {
        return send_status_update(event, mpps_status::in_progress);
    }

    /**
     * @brief Process MPPS N-SET COMPLETED event (procedure finished)
     */
    bool process_n_set_completed(const mpps_event& event) {
        return send_status_update(event, mpps_status::completed);
    }

    /**
     * @brief Process MPPS N-SET DISCONTINUED event (procedure cancelled)
     */
    bool process_n_set_discontinued(const mpps_event& event) {
        return send_status_update(event, mpps_status::discontinued);
    }

    uint32_t messages_sent() const {
        return messages_sent_;
    }

    void set_primary_ris_port(uint16_t port) {
        ris_port_ = port;
    }

    void set_backup_ris_port(uint16_t port) {
        backup_ris_port_ = port;
    }

    void enable_failover(bool enable) {
        failover_enabled_ = enable;
    }

private:
    bool send_status_update(const mpps_event& event, mpps_status status) {
        auto orm_msg = build_orm_message(event, status);

        // Try primary RIS
        if (send_to_ris(ris_port_, orm_msg)) {
            messages_sent_++;
            return true;
        }

        // Try backup if failover is enabled
        if (failover_enabled_ && backup_ris_port_ > 0) {
            if (send_to_ris(backup_ris_port_, orm_msg)) {
                messages_sent_++;
                return true;
            }
        }

        return false;
    }

    bool send_to_ris(uint16_t port, const std::string& message) {
        mllp::mllp_client_config config;
        config.host = "localhost";
        config.port = port;
        config.connect_timeout = std::chrono::milliseconds{1000};

        mllp::mllp_client client(config);

        auto connect_result = client.connect();
        if (!connect_result.has_value()) {
            return false;
        }

        auto msg = mllp::mllp_message::from_string(message);
        auto send_result = client.send(msg);

        client.disconnect();

        return send_result.has_value();
    }

    std::string build_orm_message(const mpps_event& event, mpps_status status) {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp[15];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                      std::localtime(&time_t_now));

        std::string status_code(to_hl7_status(status));

        std::ostringstream oss;
        // Build HL7 ORM^O01 message
        // ORC segment: ORC-1=Order Control, ORC-2=Placer Order, ORC-3=Filler Order,
        //              ORC-4=Placer Group, ORC-5=Order Status (IP/CM/CA/DC)
        oss << "MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|" << timestamp
            << "||ORM^O01|MSG" << messages_sent_ << "|P|2.4\r"
            << "PID|1||" << event.patient_id << "|||" << event.patient_name
            << "\r"
            << "ORC|SC|" << event.scheduled_procedure_id << "|"
            << event.accession_number << "||" << status_code << "\r"
            << "OBR|1|" << event.scheduled_procedure_id << "||"
            << event.modality << "|||||||||||||||" << event.accession_number
            << "\r";

        return oss.str();
    }

    uint16_t ris_port_;
    uint16_t backup_ris_port_ = 0;
    bool failover_enabled_ = false;
    std::atomic<uint32_t> messages_sent_;
};

// =============================================================================
// Test Fixture for Integration Tests
// =============================================================================

/**
 * @brief Base fixture providing common setup/teardown for integration tests
 */
class integration_test_fixture {
public:
    integration_test_fixture() = default;
    virtual ~integration_test_fixture() = default;

    /**
     * @brief Generate unique port number for test isolation
     */
    static uint16_t generate_test_port() {
        static std::atomic<uint16_t> port_counter{12800};
        return port_counter.fetch_add(1);
    }

    /**
     * @brief Generate unique temporary file path
     */
    static std::filesystem::path generate_temp_path() {
        static std::atomic<int> counter{0};
        auto temp_dir = std::filesystem::temp_directory_path();
        return temp_dir / ("pacs_bridge_test_" + std::to_string(counter++) +
                           ".dat");
    }

    /**
     * @brief Wait with timeout for a condition using yield-based polling
     *
     * This provides more responsive waiting compared to sleep_for-based polling,
     * allowing the condition to be checked more frequently while still being
     * cooperative with other threads.
     */
    template <typename Predicate>
    static bool wait_for(Predicate pred, std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!pred()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }

    /**
     * @brief Clean up temporary test files
     */
    static void cleanup_temp_file(const std::filesystem::path& path) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// =============================================================================
// Test Data Generators
// =============================================================================

/**
 * @brief Generate sample MPPS events for testing
 */
class mpps_event_generator {
public:
    static mpps_bridge_simulator::mpps_event create_sample_event() {
        static std::atomic<int> counter{0};
        int id = counter++;

        mpps_bridge_simulator::mpps_event event;
        event.sop_instance_uid = "1.2.3.4.5.6.7." + std::to_string(id);
        event.patient_id = "PAT" + std::to_string(1000 + id);
        event.patient_name = "DOE^JOHN^" + std::to_string(id);
        event.accession_number = "ACC" + std::to_string(2000 + id);
        event.scheduled_procedure_id = "SPS" + std::to_string(3000 + id);
        event.modality = "CT";
        event.status = mpps_status::in_progress;
        event.timestamp = std::chrono::system_clock::now();

        return event;
    }

    static std::vector<mpps_bridge_simulator::mpps_event> create_batch(
        size_t count) {
        std::vector<mpps_bridge_simulator::mpps_event> events;
        events.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            events.push_back(create_sample_event());
        }
        return events;
    }
};

// =============================================================================
// Outbound Queue Simulator for Reliable Delivery Testing
// =============================================================================

/**
 * @brief Simulates outbound message queue with retry logic for integration tests
 *
 * This class provides a complete simulation of the outbound delivery queue
 * functionality, including:
 * - Persistent message storage via test_message_queue
 * - Background delivery thread with retry logic
 * - Automatic redelivery when destination becomes available
 *
 * Used for testing Workflow 2 scenarios (reliable delivery + recovery).
 */
class outbound_queue_simulator {
public:
    struct config {
        std::filesystem::path storage_path;
        uint16_t ris_port = 12800;
        std::chrono::milliseconds retry_interval{500};
        size_t max_retries = 10;
    };

    explicit outbound_queue_simulator(const config& cfg)
        : config_(cfg),
          queue_(cfg.storage_path),
          running_(false),
          delivery_attempts_(0),
          delivered_count_(0) {}

    ~outbound_queue_simulator() {
        stop();
    }

    /**
     * @brief Start the background delivery thread
     */
    void start() {
        if (running_) {
            return;
        }
        running_ = true;
        delivery_thread_ = std::thread(&outbound_queue_simulator::delivery_loop, this);
    }

    /**
     * @brief Stop the delivery thread
     */
    void stop() {
        running_ = false;
        cv_.notify_all();
        if (delivery_thread_.joinable()) {
            delivery_thread_.join();
        }
    }

    /**
     * @brief Check if the simulator is running
     */
    bool is_running() const {
        return running_;
    }

    /**
     * @brief Enqueue a message for delivery
     */
    void enqueue(const std::string& message) {
        queue_.enqueue(message);
        cv_.notify_one();
    }

    /**
     * @brief Simulate system restart by stopping and reloading queue from disk
     */
    void simulate_restart() {
        stop();
        queue_.simulate_recovery();
        start();
    }

    /**
     * @brief Get current queue size (pending messages)
     */
    size_t queue_size() const {
        return queue_.size();
    }

    /**
     * @brief Check if queue is empty
     */
    bool queue_empty() const {
        return queue_.empty();
    }

    /**
     * @brief Get count of successfully delivered messages
     */
    size_t delivered_count() const {
        return delivered_count_;
    }

    /**
     * @brief Get count of delivery attempts (for queue_persistence_test compatibility)
     */
    uint32_t delivery_attempts() const {
        return delivery_attempts_;
    }

    /**
     * @brief Alias for delivered_count (for queue_persistence_test compatibility)
     */
    uint32_t successful_deliveries() const {
        return static_cast<uint32_t>(delivered_count_);
    }

    /**
     * @brief Reset delivery counters
     */
    void reset_counters() {
        delivery_attempts_ = 0;
        delivered_count_ = 0;
    }

    /**
     * @brief Access the underlying test_message_queue for recovery testing
     */
    test_message_queue& underlying_queue() {
        return queue_;
    }

private:
    void delivery_loop() {
        while (running_) {
            auto msg_opt = queue_.peek();
            if (!msg_opt.has_value()) {
                // Wait for new messages
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, config_.retry_interval, [this]() {
                    return !running_ || queue_.size() > 0;
                });
                continue;
            }

            // Try to deliver
            delivery_attempts_++;
            if (try_deliver(msg_opt.value())) {
                // Success - remove from queue
                queue_.dequeue();
                delivered_count_++;
            } else {
                // Failed - wait before retry
                std::this_thread::sleep_for(config_.retry_interval);
            }
        }
    }

    bool try_deliver(const std::string& message) {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = config_.ris_port;
        client_config.connect_timeout = std::chrono::milliseconds{500};

        mllp::mllp_client client(client_config);

        auto connect_result = client.connect();
        if (!connect_result.has_value()) {
            return false;
        }

        auto msg = mllp::mllp_message::from_string(message);
        auto send_result = client.send(msg);

        client.disconnect();

        return send_result.has_value();
    }

    config config_;
    test_message_queue queue_;
    std::atomic<bool> running_;
    std::atomic<uint32_t> delivery_attempts_;
    std::atomic<size_t> delivered_count_;
    std::thread delivery_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

}  // namespace pacs::bridge::integration::test

#endif  // PACS_BRIDGE_TESTS_INTEGRATION_TEST_BASE_H
