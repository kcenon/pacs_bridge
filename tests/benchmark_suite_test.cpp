/**
 * @file benchmark_suite_test.cpp
 * @brief Performance benchmarks and throughput measurement tests
 *
 * Comprehensive benchmarks for:
 * - Message processing performance
 * - Concurrent connection handling
 * - Throughput measurement under various loads
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/163
 */

#include "pacs/bridge/mllp/mllp_client.h"
#include "pacs/bridge/mllp/mllp_server.h"
#include "pacs/bridge/mllp/mllp_types.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::benchmark::test {

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

#define RUN_TEST(test_func)                                                    \
    do {                                                                       \
        std::cout << "Running " << #test_func << "..." << std::endl;           \
        auto start = std::chrono::high_resolution_clock::now();                \
        if (test_func()) {                                                     \
            auto end = std::chrono::high_resolution_clock::now();              \
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( \
                end - start);                                                  \
            std::cout << "  PASSED (" << duration.count() << "ms)"             \
                      << std::endl;                                            \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "  FAILED" << std::endl;                              \
            failed++;                                                          \
        }                                                                      \
    } while (0)

/**
 * @brief Wait until a condition is met or timeout occurs
 */
template <typename Predicate>
bool wait_for(Predicate condition, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

/**
 * @brief Generate unique port number for test isolation
 */
static uint16_t generate_test_port() {
    static std::atomic<uint16_t> port_counter{14000};
    return port_counter.fetch_add(1);
}

// =============================================================================
// Sample HL7 Messages
// =============================================================================

static const std::string SAMPLE_ORM =
    "MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|20240115120000||ORM^O01|MSG00001|P|2.4\r"
    "PID|1||12345^^^MRN||DOE^JOHN^A||19800101|M|||123 MAIN ST^^CITY^ST^12345\r"
    "PV1|1|O|RADIOLOGY|||||||||||||||V123456\r"
    "ORC|NW|ORDER123|PLACER456||SC||^^^20240115120000||20240115120000|SMITH^JOHN\r"
    "OBR|1|ORDER123|FILLER789|12345^CHEST XRAY^LOCAL|||20240115120000|||||||ORDERING^PHYSICIAN\r";

static const std::string SAMPLE_ADT =
    "MSH|^~\\&|ADT|HOSPITAL|PACS|RADIOLOGY|20240115120000||ADT^A01|MSG00002|P|2.4\r"
    "EVN|A01|20240115120000\r"
    "PID|1||12345^^^MRN||DOE^JANE^B||19900515|F|||456 OAK AVE^^TOWN^ST^67890\r"
    "PV1|1|I|ICU|||||||||||||||INP123456\r";

static const std::string SAMPLE_ORU =
    "MSH|^~\\&|RIS|RADIOLOGY|EMR|HOSPITAL|20240115130000||ORU^R01|MSG00003|P|2.4\r"
    "PID|1||12345^^^MRN||DOE^JOHN^A||19800101|M\r"
    "OBR|1|ORDER123|FILLER789|12345^CHEST XRAY|||20240115120000|||F\r"
    "OBX|1|TX|FINDINGS||Normal chest X-ray. No acute cardiopulmonary abnormality.||||||F\r";

// =============================================================================
// Benchmark Statistics
// =============================================================================

struct benchmark_stats {
    uint64_t total_operations{0};
    uint64_t successful_operations{0};
    uint64_t failed_operations{0};
    std::chrono::microseconds total_time{0};
    std::chrono::microseconds min_latency{std::chrono::microseconds::max()};
    std::chrono::microseconds max_latency{0};
    std::vector<std::chrono::microseconds> latencies;

    double success_rate() const {
        return total_operations > 0
                   ? (static_cast<double>(successful_operations) / total_operations) * 100.0
                   : 0.0;
    }

    double throughput_per_second() const {
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
        return total_ms > 0
                   ? (static_cast<double>(total_operations) / total_ms) * 1000.0
                   : 0.0;
    }

    std::chrono::microseconds avg_latency() const {
        return total_operations > 0
                   ? total_time / static_cast<long>(total_operations)
                   : std::chrono::microseconds{0};
    }

    std::chrono::microseconds percentile(double p) const {
        if (latencies.empty()) {
            return std::chrono::microseconds{0};
        }
        auto sorted = latencies;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * p / 100.0);
        return sorted[std::min(idx, sorted.size() - 1)];
    }

    void print_summary(const std::string& test_name) const {
        std::cout << "\n  " << test_name << " Results:" << std::endl;
        std::cout << "    Total Operations:    " << total_operations << std::endl;
        std::cout << "    Successful:          " << successful_operations << std::endl;
        std::cout << "    Failed:              " << failed_operations << std::endl;
        std::cout << "    Success Rate:        " << std::fixed << std::setprecision(2)
                  << success_rate() << "%" << std::endl;
        std::cout << "    Throughput:          " << std::fixed << std::setprecision(2)
                  << throughput_per_second() << " ops/sec" << std::endl;
        std::cout << "    Avg Latency:         " << avg_latency().count() << " us" << std::endl;
        std::cout << "    Min Latency:         " << min_latency.count() << " us" << std::endl;
        std::cout << "    Max Latency:         " << max_latency.count() << " us" << std::endl;
        std::cout << "    P50 Latency:         " << percentile(50).count() << " us" << std::endl;
        std::cout << "    P95 Latency:         " << percentile(95).count() << " us" << std::endl;
        std::cout << "    P99 Latency:         " << percentile(99).count() << " us" << std::endl;
    }
};

// =============================================================================
// Mock Server for Benchmarking
// =============================================================================

class benchmark_server {
public:
    explicit benchmark_server(uint16_t port)
        : port_(port), running_(false), messages_received_(0) {}

    ~benchmark_server() {
        stop();
    }

    bool start() {
        if (running_) {
            return false;
        }

        mllp::mllp_server_config config;
        config.port = port_;

        server_ = std::make_unique<mllp::mllp_server>(config);

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

    uint64_t messages_received() const {
        return messages_received_.load();
    }

    uint16_t port() const {
        return port_;
    }

private:
    std::optional<mllp::mllp_message> handle_message(const mllp::mllp_message& msg) {
        messages_received_++;

        // Parse message to extract control ID
        hl7::hl7_parser parser;
        auto parse_result = parser.parse(msg.to_string());

        std::string msg_control_id = "0";
        if (parse_result.is_ok()) {
            msg_control_id = parse_result.value().get_value("MSH.10");
        }

        // Generate ACK
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp[15];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                      std::localtime(&time_t_now));

        std::string ack =
            "MSH|^~\\&|RIS|HOSPITAL|PACS|RADIOLOGY|" + std::string(timestamp) +
            "||ACK|ACK" + msg_control_id + "|P|2.4\r" +
            "MSA|AA|" + msg_control_id + "\r";

        return mllp::mllp_message::from_string(ack);
    }

    uint16_t port_;
    std::unique_ptr<mllp::mllp_server> server_;
    std::atomic<bool> running_;
    std::atomic<uint64_t> messages_received_;
};

// =============================================================================
// Message Processing Benchmarks
// =============================================================================

/**
 * @brief Benchmark HL7 message parsing performance
 *
 * Measures parsing throughput for different message types.
 */
bool test_benchmark_message_parsing() {
    const int iterations = 5000;
    hl7::hl7_parser parser;

    std::vector<std::string> messages = {SAMPLE_ORM, SAMPLE_ADT, SAMPLE_ORU};
    benchmark_stats stats;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        for (const auto& msg : messages) {
            auto op_start = std::chrono::high_resolution_clock::now();
            auto result = parser.parse(msg);
            auto op_end = std::chrono::high_resolution_clock::now();

            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                op_end - op_start);

            stats.total_operations++;
            stats.latencies.push_back(latency);

            if (latency < stats.min_latency) {
                stats.min_latency = latency;
            }
            if (latency > stats.max_latency) {
                stats.max_latency = latency;
            }

            if (result.is_ok()) {
                stats.successful_operations++;
            } else {
                stats.failed_operations++;
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("Message Parsing Benchmark");

    TEST_ASSERT(stats.success_rate() >= 99.0, "Parse success rate should be >= 99%");
    TEST_ASSERT(stats.throughput_per_second() > 1000, "Should parse > 1000 messages/sec");
    TEST_ASSERT(stats.percentile(95).count() < 1000, "P95 latency should be < 1ms");

    return true;
}

/**
 * @brief Benchmark HL7 message building performance
 *
 * Measures message construction throughput using HL7 builder.
 */
bool test_benchmark_message_building() {
    const int iterations = 5000;
    benchmark_stats stats;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        auto op_start = std::chrono::high_resolution_clock::now();

        auto result = hl7::hl7_builder::create()
            .message_type("ORM", "O01")
            .sending_app("PACS")
            .sending_facility("RADIOLOGY")
            .receiving_app("RIS")
            .receiving_facility("HOSPITAL")
            .control_id("MSG" + std::to_string(i))
            .build();

        auto op_end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);

        stats.total_operations++;
        stats.latencies.push_back(latency);

        if (latency < stats.min_latency) {
            stats.min_latency = latency;
        }
        if (latency > stats.max_latency) {
            stats.max_latency = latency;
        }

        if (result.is_ok()) {
            stats.successful_operations++;
        } else {
            stats.failed_operations++;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("Message Building Benchmark");

    TEST_ASSERT(stats.success_rate() >= 99.0, "Build success rate should be >= 99%");
    TEST_ASSERT(stats.throughput_per_second() > 5000, "Should build > 5000 messages/sec");

    return true;
}

/**
 * @brief Benchmark end-to-end message processing
 *
 * Measures complete parse -> modify -> serialize cycle.
 */
bool test_benchmark_roundtrip_processing() {
    const int iterations = 2000;
    hl7::hl7_parser parser;
    benchmark_stats stats;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        auto op_start = std::chrono::high_resolution_clock::now();

        // Parse
        auto parse_result = parser.parse(SAMPLE_ORM);
        if (!parse_result.is_ok()) {
            stats.failed_operations++;
            stats.total_operations++;
            continue;
        }

        // Modify (simulate processing)
        auto& parsed = parse_result.value();
        std::string msg_id(parsed.get_value("MSH.10"));
        std::string patient_id(parsed.get_value("PID.3"));

        // Build response
        auto ack_result = hl7::hl7_builder::create()
            .message_type("ACK", "")
            .sending_app("RIS")
            .sending_facility("HOSPITAL")
            .receiving_app("PACS")
            .receiving_facility("RADIOLOGY")
            .control_id("ACK" + msg_id)
            .build();

        auto op_end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);

        stats.total_operations++;
        stats.latencies.push_back(latency);

        if (latency < stats.min_latency) {
            stats.min_latency = latency;
        }
        if (latency > stats.max_latency) {
            stats.max_latency = latency;
        }

        if (ack_result.is_ok()) {
            stats.successful_operations++;
        } else {
            stats.failed_operations++;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("Roundtrip Processing Benchmark");

    TEST_ASSERT(stats.success_rate() >= 99.0, "Roundtrip success rate should be >= 99%");
    TEST_ASSERT(stats.throughput_per_second() > 500, "Should process > 500 roundtrips/sec");

    return true;
}

// =============================================================================
// Concurrent Connection Handling Tests
// =============================================================================

/**
 * @brief Test concurrent client connections to a single server
 *
 * Verifies server stability under multiple simultaneous connections.
 */
bool test_concurrent_connections_basic() {
    uint16_t port = generate_test_port();
    benchmark_server server(port);

    TEST_ASSERT(server.start(), "Server should start");
    TEST_ASSERT(
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{2000}),
        "Server should be running");

    const int num_clients = 10;
    const int messages_per_client = 50;
    std::atomic<uint32_t> successful{0};
    std::atomic<uint32_t> failed{0};
    std::vector<std::future<void>> futures;

    auto start = std::chrono::high_resolution_clock::now();

    for (int c = 0; c < num_clients; ++c) {
        futures.push_back(std::async(std::launch::async, [&, c]() {
            mllp::mllp_client_config config;
            config.host = "localhost";
            config.port = port;
            config.connect_timeout = std::chrono::milliseconds{5000};

            mllp::mllp_client client(config);

            auto connect_result = client.connect();
            if (!connect_result.has_value()) {
                failed += messages_per_client;
                return;
            }

            for (int m = 0; m < messages_per_client; ++m) {
                std::string msg_id = "MSG_" + std::to_string(c) + "_" + std::to_string(m);
                std::string message =
                    "MSH|^~\\&|PACS|RAD|RIS|HOSP|20240115||ORM^O01|" + msg_id + "|P|2.4\r"
                    "PID|1||PAT" + std::to_string(c * 100 + m) + "|||DOE^JOHN\r";

                auto mllp_msg = mllp::mllp_message::from_string(message);
                auto send_result = client.send(mllp_msg);

                if (send_result.has_value()) {
                    successful++;
                } else {
                    failed++;
                }
            }

            client.disconnect();
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    server.stop();

    uint32_t total = num_clients * messages_per_client;
    double success_rate = (static_cast<double>(successful) / total) * 100.0;
    double throughput = static_cast<double>(total) / (duration.count() / 1000.0);

    std::cout << "\n  Concurrent Connections Basic Results:" << std::endl;
    std::cout << "    Clients:         " << num_clients << std::endl;
    std::cout << "    Messages/Client: " << messages_per_client << std::endl;
    std::cout << "    Total Messages:  " << total << std::endl;
    std::cout << "    Successful:      " << successful << std::endl;
    std::cout << "    Failed:          " << failed << std::endl;
    std::cout << "    Success Rate:    " << std::fixed << std::setprecision(2)
              << success_rate << "%" << std::endl;
    std::cout << "    Duration:        " << duration.count() << "ms" << std::endl;
    std::cout << "    Throughput:      " << std::fixed << std::setprecision(2)
              << throughput << " msg/sec" << std::endl;

    TEST_ASSERT(success_rate >= 95.0, "Success rate should be >= 95%");
    TEST_ASSERT(throughput > 50, "Throughput should be > 50 msg/sec");

    return true;
}

/**
 * @brief Test high concurrency stress scenario
 *
 * Pushes the system with many concurrent connections.
 */
bool test_concurrent_connections_stress() {
    uint16_t port = generate_test_port();
    benchmark_server server(port);

    TEST_ASSERT(server.start(), "Server should start");
    TEST_ASSERT(
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{2000}),
        "Server should be running");

    const int num_clients = 25;
    const int messages_per_client = 20;
    std::atomic<uint32_t> successful{0};
    std::atomic<uint32_t> failed{0};
    std::atomic<uint32_t> connection_failures{0};
    std::vector<std::future<void>> futures;

    auto start = std::chrono::high_resolution_clock::now();

    for (int c = 0; c < num_clients; ++c) {
        futures.push_back(std::async(std::launch::async, [&, c]() {
            mllp::mllp_client_config config;
            config.host = "localhost";
            config.port = port;
            config.connect_timeout = std::chrono::milliseconds{10000};
            config.keep_alive = true;

            mllp::mllp_client client(config);

            auto connect_result = client.connect();
            if (!connect_result.has_value()) {
                connection_failures++;
                failed += messages_per_client;
                return;
            }

            for (int m = 0; m < messages_per_client; ++m) {
                std::string msg_id = "STRESS_" + std::to_string(c) + "_" + std::to_string(m);
                std::string message =
                    "MSH|^~\\&|PACS|RAD|RIS|HOSP|20240115||ORM^O01|" + msg_id + "|P|2.4\r"
                    "PID|1||STRESS" + std::to_string(c * 100 + m) + "|||STRESS^TEST\r";

                auto mllp_msg = mllp::mllp_message::from_string(message);
                auto send_result = client.send(mllp_msg);

                if (send_result.has_value()) {
                    successful++;
                } else {
                    failed++;
                }
            }

            client.disconnect();
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    server.stop();

    uint32_t total = num_clients * messages_per_client;
    double success_rate = (static_cast<double>(successful) / total) * 100.0;

    std::cout << "\n  Concurrent Connections Stress Results:" << std::endl;
    std::cout << "    Clients:             " << num_clients << std::endl;
    std::cout << "    Connection Failures: " << connection_failures << std::endl;
    std::cout << "    Total Messages:      " << total << std::endl;
    std::cout << "    Successful:          " << successful << std::endl;
    std::cout << "    Success Rate:        " << std::fixed << std::setprecision(2)
              << success_rate << "%" << std::endl;
    std::cout << "    Duration:            " << duration.count() << "ms" << std::endl;

    // Stress test has lower success threshold
    TEST_ASSERT(success_rate >= 85.0, "Success rate should be >= 85% under stress");

    return true;
}

/**
 * @brief Test connection pool behavior with reuse
 *
 * Verifies efficient connection handling with keep-alive.
 */
bool test_concurrent_connection_reuse() {
    uint16_t port = generate_test_port();
    benchmark_server server(port);

    TEST_ASSERT(server.start(), "Server should start");
    TEST_ASSERT(
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{2000}),
        "Server should be running");

    const int num_clients = 5;
    const int rounds = 10;
    const int messages_per_round = 20;
    std::atomic<uint32_t> successful{0};
    std::atomic<uint32_t> failed{0};
    std::vector<std::future<void>> futures;

    auto start = std::chrono::high_resolution_clock::now();

    for (int c = 0; c < num_clients; ++c) {
        futures.push_back(std::async(std::launch::async, [&, c]() {
            mllp::mllp_client_config config;
            config.host = "localhost";
            config.port = port;
            config.connect_timeout = std::chrono::milliseconds{5000};
            config.keep_alive = true;

            mllp::mllp_client client(config);

            for (int r = 0; r < rounds; ++r) {
                // Connect once per round
                auto connect_result = client.connect();
                if (!connect_result.has_value()) {
                    failed += messages_per_round;
                    continue;
                }

                // Send multiple messages on same connection
                for (int m = 0; m < messages_per_round; ++m) {
                    std::string msg_id = "REUSE_" + std::to_string(c) + "_" +
                                         std::to_string(r) + "_" + std::to_string(m);
                    std::string message =
                        "MSH|^~\\&|PACS|RAD|RIS|HOSP|20240115||ORM^O01|" + msg_id + "|P|2.4\r"
                        "PID|1||REUSE" + std::to_string(c * 1000 + r * 100 + m) + "|||TEST^REUSE\r";

                    auto mllp_msg = mllp::mllp_message::from_string(message);
                    auto send_result = client.send(mllp_msg);

                    if (send_result.has_value()) {
                        successful++;
                    } else {
                        failed++;
                    }
                }

                client.disconnect();
            }
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    server.stop();

    uint32_t total = num_clients * rounds * messages_per_round;
    double success_rate = (static_cast<double>(successful) / total) * 100.0;
    double throughput = static_cast<double>(total) / (duration.count() / 1000.0);

    std::cout << "\n  Connection Reuse Results:" << std::endl;
    std::cout << "    Clients:         " << num_clients << std::endl;
    std::cout << "    Rounds:          " << rounds << std::endl;
    std::cout << "    Total Messages:  " << total << std::endl;
    std::cout << "    Successful:      " << successful << std::endl;
    std::cout << "    Success Rate:    " << std::fixed << std::setprecision(2)
              << success_rate << "%" << std::endl;
    std::cout << "    Throughput:      " << std::fixed << std::setprecision(2)
              << throughput << " msg/sec" << std::endl;

    TEST_ASSERT(success_rate >= 95.0, "Success rate should be >= 95%");

    return true;
}

// =============================================================================
// Throughput Measurement Tests
// =============================================================================

/**
 * @brief Measure sustained throughput over time
 *
 * Tests system performance under continuous load.
 */
bool test_throughput_sustained() {
    uint16_t port = generate_test_port();
    benchmark_server server(port);

    TEST_ASSERT(server.start(), "Server should start");
    TEST_ASSERT(
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{2000}),
        "Server should be running");

    const auto test_duration = std::chrono::seconds{5};
    const int num_senders = 4;
    std::atomic<uint64_t> total_sent{0};
    std::atomic<uint64_t> total_received{0};
    std::atomic<bool> stop_flag{false};
    std::vector<std::future<void>> futures;

    auto start = std::chrono::steady_clock::now();

    for (int s = 0; s < num_senders; ++s) {
        futures.push_back(std::async(std::launch::async, [&, s]() {
            mllp::mllp_client_config config;
            config.host = "localhost";
            config.port = port;
            config.connect_timeout = std::chrono::milliseconds{5000};
            config.keep_alive = true;

            mllp::mllp_client client(config);

            auto connect_result = client.connect();
            if (!connect_result.has_value()) {
                return;
            }

            uint32_t msg_counter = 0;
            while (!stop_flag) {
                std::string msg_id = "SUSTAINED_" + std::to_string(s) + "_" +
                                     std::to_string(msg_counter++);
                std::string message =
                    "MSH|^~\\&|PACS|RAD|RIS|HOSP|20240115||ORM^O01|" + msg_id + "|P|2.4\r"
                    "PID|1||SUST" + std::to_string(static_cast<uint32_t>(s) * 10000U + msg_counter) + "|||TEST^SUST\r";

                auto mllp_msg = mllp::mllp_message::from_string(message);
                auto send_result = client.send(mllp_msg);

                total_sent++;
                if (send_result.has_value()) {
                    total_received++;
                }
            }

            client.disconnect();
        }));
    }

    // Run for specified duration
    std::this_thread::sleep_for(test_duration);
    stop_flag = true;

    for (auto& f : futures) {
        f.wait();
    }

    auto end = std::chrono::steady_clock::now();
    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    server.stop();

    double throughput = static_cast<double>(total_sent) / (actual_duration.count() / 1000.0);
    double success_rate = (static_cast<double>(total_received) / total_sent) * 100.0;

    std::cout << "\n  Sustained Throughput Results:" << std::endl;
    std::cout << "    Test Duration:   " << actual_duration.count() << "ms" << std::endl;
    std::cout << "    Senders:         " << num_senders << std::endl;
    std::cout << "    Total Sent:      " << total_sent << std::endl;
    std::cout << "    Total Received:  " << total_received << std::endl;
    std::cout << "    Success Rate:    " << std::fixed << std::setprecision(2)
              << success_rate << "%" << std::endl;
    std::cout << "    Throughput:      " << std::fixed << std::setprecision(2)
              << throughput << " msg/sec" << std::endl;

    TEST_ASSERT(success_rate >= 90.0, "Success rate should be >= 90%");
    TEST_ASSERT(throughput > 100, "Sustained throughput should be > 100 msg/sec");

    return true;
}

/**
 * @brief Measure peak throughput capacity
 *
 * Tests maximum message rate the system can handle.
 */
bool test_throughput_peak() {
    uint16_t port = generate_test_port();
    benchmark_server server(port);

    TEST_ASSERT(server.start(), "Server should start");
    TEST_ASSERT(
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{2000}),
        "Server should be running");

    const int total_messages = 1000;
    const int num_senders = 8;
    const int messages_per_sender = total_messages / num_senders;

    std::atomic<uint64_t> successful{0};
    std::vector<std::future<void>> futures;

    auto start = std::chrono::high_resolution_clock::now();

    for (int s = 0; s < num_senders; ++s) {
        futures.push_back(std::async(std::launch::async, [&, s]() {
            mllp::mllp_client_config config;
            config.host = "localhost";
            config.port = port;
            config.connect_timeout = std::chrono::milliseconds{10000};
            config.keep_alive = true;

            mllp::mllp_client client(config);

            auto connect_result = client.connect();
            if (!connect_result.has_value()) {
                return;
            }

            for (int m = 0; m < messages_per_sender; ++m) {
                std::string msg_id = "PEAK_" + std::to_string(s) + "_" + std::to_string(m);
                std::string message =
                    "MSH|^~\\&|PACS|RAD|RIS|HOSP|20240115||ORM^O01|" + msg_id + "|P|2.4\r"
                    "PID|1||PEAK" + std::to_string(s * 1000 + m) + "|||TEST^PEAK\r";

                auto mllp_msg = mllp::mllp_message::from_string(message);
                auto send_result = client.send(mllp_msg);

                if (send_result.has_value()) {
                    successful++;
                }
            }

            client.disconnect();
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    server.stop();

    double throughput = static_cast<double>(total_messages) / (duration.count() / 1000.0);
    double success_rate = (static_cast<double>(successful) / total_messages) * 100.0;

    std::cout << "\n  Peak Throughput Results:" << std::endl;
    std::cout << "    Total Messages:  " << total_messages << std::endl;
    std::cout << "    Senders:         " << num_senders << std::endl;
    std::cout << "    Successful:      " << successful << std::endl;
    std::cout << "    Success Rate:    " << std::fixed << std::setprecision(2)
              << success_rate << "%" << std::endl;
    std::cout << "    Duration:        " << duration.count() << "ms" << std::endl;
    std::cout << "    Peak Throughput: " << std::fixed << std::setprecision(2)
              << throughput << " msg/sec" << std::endl;

    TEST_ASSERT(success_rate >= 85.0, "Success rate should be >= 85%");

    return true;
}

/**
 * @brief Measure throughput with varying message sizes
 *
 * Tests performance impact of message size.
 */
bool test_throughput_varying_sizes() {
    uint16_t port = generate_test_port();
    benchmark_server server(port);

    TEST_ASSERT(server.start(), "Server should start");
    TEST_ASSERT(
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{2000}),
        "Server should be running");

    // Different message sizes
    std::vector<std::pair<std::string, std::string>> size_tests = {
        {"Small (200B)", SAMPLE_ADT},
        {"Medium (500B)", SAMPLE_ORM},
        {"Large (1KB)",
         SAMPLE_ORM +
         "NTE|1||This is additional notes to increase message size\r"
         "NTE|2||More notes to make the message larger for testing purposes\r"
         "NTE|3||Even more notes to reach approximately 1KB message size here\r"
         "NTE|4||Final notes segment to complete the large message test case\r"}
    };

    mllp::mllp_client_config config;
    config.host = "localhost";
    config.port = port;
    config.connect_timeout = std::chrono::milliseconds{5000};
    config.keep_alive = true;

    mllp::mllp_client client(config);
    TEST_ASSERT(client.connect().has_value(), "Client should connect");

    std::cout << "\n  Throughput by Message Size:" << std::endl;

    for (const auto& [size_name, message] : size_tests) {
        const int iterations = 500;
        uint32_t successful = 0;

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i) {
            auto mllp_msg = mllp::mllp_message::from_string(message);
            auto send_result = client.send(mllp_msg);
            if (send_result.has_value()) {
                successful++;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        double throughput = static_cast<double>(iterations) / (duration.count() / 1000.0);
        double success_rate = (static_cast<double>(successful) / iterations) * 100.0;

        std::cout << "    " << size_name << ": " << std::fixed << std::setprecision(2)
                  << throughput << " msg/sec (success: " << success_rate << "%)"
                  << std::endl;
    }

    client.disconnect();
    server.stop();

    return true;
}

}  // namespace pacs::bridge::benchmark::test

// =============================================================================
// Main
// =============================================================================

int main() {
    using namespace pacs::bridge::benchmark::test;

    std::cout << "==================================" << std::endl;
    std::cout << "PACS Bridge Benchmark Suite Tests" << std::endl;
    std::cout << "Issue #163: Performance Benchmarks" << std::endl;
    std::cout << "==================================" << std::endl;

    int passed = 0;
    int failed = 0;

    // Message Processing Benchmarks
    std::cout << "\n--- Message Processing Benchmarks ---" << std::endl;
    RUN_TEST(test_benchmark_message_parsing);
    RUN_TEST(test_benchmark_message_building);
    RUN_TEST(test_benchmark_roundtrip_processing);

    // Concurrent Connection Handling Tests
    std::cout << "\n--- Concurrent Connection Handling ---" << std::endl;
    RUN_TEST(test_concurrent_connections_basic);
    RUN_TEST(test_concurrent_connections_stress);
    RUN_TEST(test_concurrent_connection_reuse);

    // Throughput Measurement Tests
    std::cout << "\n--- Throughput Measurement ---" << std::endl;
    RUN_TEST(test_throughput_sustained);
    RUN_TEST(test_throughput_peak);
    RUN_TEST(test_throughput_varying_sizes);

    // Summary
    std::cout << "\n==================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << std::endl;
    std::cout << "==================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
