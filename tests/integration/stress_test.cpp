/**
 * @file stress_test.cpp
 * @brief Stress and load tests for PACS Bridge integration
 *
 * Tests the system behavior under high message volumes and concurrent
 * operations. Verifies throughput, latency, and stability requirements.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/29
 */

#include "integration_test_base.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>

namespace pacs::bridge::integration::test {

// =============================================================================
// Stress Test Configuration
// =============================================================================

struct stress_test_config {
    uint32_t message_count{100};
    uint32_t concurrent_senders{4};
    std::chrono::milliseconds max_duration{30000};  // 30 seconds max
    std::chrono::milliseconds target_latency{100};  // Target p95 latency
    double min_success_rate{0.99};                  // 99% success rate
};

struct stress_test_result {
    uint32_t messages_sent{0};
    uint32_t messages_received{0};
    uint32_t messages_failed{0};
    std::chrono::milliseconds total_duration{0};
    std::chrono::microseconds min_latency{0};
    std::chrono::microseconds max_latency{0};
    std::chrono::microseconds avg_latency{0};
    std::chrono::microseconds p95_latency{0};
    std::chrono::microseconds p99_latency{0};
    double throughput_mps{0};  // Messages per second
    double success_rate{0};

    void print_summary() const {
        std::cout << "\n  Stress Test Results:" << std::endl;
        std::cout << "    Messages Sent:     " << messages_sent << std::endl;
        std::cout << "    Messages Received: " << messages_received << std::endl;
        std::cout << "    Messages Failed:   " << messages_failed << std::endl;
        std::cout << "    Total Duration:    " << total_duration.count() << "ms"
                  << std::endl;
        std::cout << "    Throughput:        " << throughput_mps << " msg/sec"
                  << std::endl;
        std::cout << "    Success Rate:      " << (success_rate * 100) << "%"
                  << std::endl;
        std::cout << "    Latency (min):     " << min_latency.count() << "us"
                  << std::endl;
        std::cout << "    Latency (max):     " << max_latency.count() << "us"
                  << std::endl;
        std::cout << "    Latency (avg):     " << avg_latency.count() << "us"
                  << std::endl;
        std::cout << "    Latency (p95):     " << p95_latency.count() << "us"
                  << std::endl;
        std::cout << "    Latency (p99):     " << p99_latency.count() << "us"
                  << std::endl;
    }
};

// =============================================================================
// Stress Test Runner
// =============================================================================

class stress_test_runner {
public:
    stress_test_runner(uint16_t ris_port, const stress_test_config& config)
        : ris_port_(ris_port), config_(config) {}

    stress_test_result run() {
        stress_test_result result;
        std::vector<std::chrono::microseconds> latencies;
        latencies.reserve(config_.message_count);

        std::mutex latencies_mutex;
        std::atomic<uint32_t> sent_count{0};
        std::atomic<uint32_t> success_count{0};
        std::atomic<uint32_t> fail_count{0};

        auto start_time = std::chrono::high_resolution_clock::now();

        // Calculate messages per sender
        uint32_t msgs_per_sender = config_.message_count /
                                   config_.concurrent_senders;
        uint32_t remainder = config_.message_count % config_.concurrent_senders;

        // Launch concurrent sender threads
        std::vector<std::future<void>> futures;
        for (uint32_t i = 0; i < config_.concurrent_senders; ++i) {
            uint32_t count = msgs_per_sender + (i < remainder ? 1 : 0);
            futures.push_back(std::async(std::launch::async, [&, count, i]() {
                send_messages(count, i, sent_count, success_count, fail_count,
                              latencies, latencies_mutex);
            }));
        }

        // Wait for all senders
        for (auto& f : futures) {
            f.wait();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);

        // Calculate statistics
        result.messages_sent = sent_count;
        result.messages_received = success_count;
        result.messages_failed = fail_count;
        result.total_duration = duration;

        if (sent_count > 0) {
            result.success_rate =
                static_cast<double>(success_count) / sent_count;
            result.throughput_mps =
                static_cast<double>(sent_count) /
                (duration.count() > 0 ? duration.count() / 1000.0 : 1.0);
        }

        // Calculate latency percentiles
        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());

            result.min_latency = latencies.front();
            result.max_latency = latencies.back();

            auto sum = std::accumulate(
                latencies.begin(), latencies.end(),
                std::chrono::microseconds{0},
                [](auto a, auto b) { return a + b; });
            result.avg_latency = sum / latencies.size();

            size_t p95_idx = static_cast<size_t>(latencies.size() * 0.95);
            size_t p99_idx = static_cast<size_t>(latencies.size() * 0.99);
            result.p95_latency = latencies[std::min(p95_idx, latencies.size() - 1)];
            result.p99_latency = latencies[std::min(p99_idx, latencies.size() - 1)];
        }

        return result;
    }

private:
    void send_messages(
        uint32_t count,
        uint32_t sender_id,
        std::atomic<uint32_t>& sent,
        std::atomic<uint32_t>& success,
        std::atomic<uint32_t>& fail,
        std::vector<std::chrono::microseconds>& latencies,
        std::mutex& latencies_mutex) {

        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = ris_port_;
        client_config.connect_timeout = std::chrono::milliseconds{5000};
        client_config.keep_alive = true;

        mllp::mllp_client client(client_config);

        auto connect_result = client.connect();
        if (!connect_result.has_value()) {
            fail += count;
            sent += count;
            return;
        }

        for (uint32_t i = 0; i < count; ++i) {
            std::string msg = generate_message(sender_id, i);
            auto mllp_msg = mllp::mllp_message::from_string(msg);

            auto send_start = std::chrono::high_resolution_clock::now();
            auto send_result = client.send(mllp_msg);
            auto send_end = std::chrono::high_resolution_clock::now();

            auto latency =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    send_end - send_start);

            sent++;

            if (send_result.has_value()) {
                success++;
                std::lock_guard<std::mutex> lock(latencies_mutex);
                latencies.push_back(latency);
            } else {
                fail++;
            }
        }

        client.disconnect();
    }

    std::string generate_message(uint32_t sender_id, uint32_t msg_id) {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp[15];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                      std::localtime(&time_t_now));

        std::ostringstream oss;
        oss << "MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|" << timestamp
            << "||ORM^O01|MSG" << sender_id << "_" << msg_id << "|P|2.4\r"
            << "PID|1||PAT" << (1000 + sender_id * 100 + msg_id)
            << "|||DOE^JOHN\r"
            << "ORC|NW|ORD" << (2000 + sender_id * 100 + msg_id) << "||ACC"
            << (3000 + sender_id * 100 + msg_id) << "||SC\r";

        return oss.str();
    }

    uint16_t ris_port_;
    stress_test_config config_;
};

// =============================================================================
// Basic Stress Tests
// =============================================================================

/**
 * @brief Test sequential message delivery under moderate load
 *
 * Sends 100 messages sequentially and verifies all are delivered.
 */
bool test_stress_sequential_moderate() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    stress_test_config config;
    config.message_count = 100;
    config.concurrent_senders = 1;
    config.min_success_rate = 0.99;

    stress_test_runner runner(ris_port, config);
    auto result = runner.run();

    result.print_summary();

    // Wait for server to process all messages
    integration_test_fixture::wait_for(
        [&ris, &config]() {
            return ris.messages_received() >= config.message_count * 0.99;
        },
        std::chrono::milliseconds{5000});

    INTEGRATION_TEST_ASSERT(result.success_rate >= config.min_success_rate,
                            "Success rate too low");
    INTEGRATION_TEST_ASSERT(result.messages_received >= 99,
                            "Should receive at least 99 messages");

    ris.stop();
    return true;
}

/**
 * @brief Test concurrent message delivery with multiple senders
 *
 * Uses 4 concurrent senders to stress test parallel processing.
 */
bool test_stress_concurrent_senders() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    stress_test_config config;
    config.message_count = 200;
    config.concurrent_senders = 4;
    config.min_success_rate = 0.95;

    stress_test_runner runner(ris_port, config);
    auto result = runner.run();

    result.print_summary();

    integration_test_fixture::wait_for(
        [&ris, &config]() {
            return ris.messages_received() >= config.message_count * 0.90;
        },
        std::chrono::milliseconds{10000});

    INTEGRATION_TEST_ASSERT(result.success_rate >= config.min_success_rate,
                            "Success rate too low");
    INTEGRATION_TEST_ASSERT(result.throughput_mps > 10,
                            "Throughput should be > 10 msg/sec");

    ris.stop();
    return true;
}

/**
 * @brief Test high volume message burst
 *
 * Sends a burst of 500 messages to test system stability.
 */
bool test_stress_high_volume_burst() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    stress_test_config config;
    config.message_count = 500;
    config.concurrent_senders = 8;
    config.max_duration = std::chrono::milliseconds{60000};
    config.min_success_rate = 0.90;

    stress_test_runner runner(ris_port, config);
    auto result = runner.run();

    result.print_summary();

    INTEGRATION_TEST_ASSERT(result.messages_sent == config.message_count,
                            "Should attempt all messages");
    INTEGRATION_TEST_ASSERT(result.success_rate >= config.min_success_rate,
                            "Success rate too low for high volume");
    INTEGRATION_TEST_ASSERT(
        result.total_duration < config.max_duration,
        "Should complete within time limit");

    ris.stop();
    return true;
}

// =============================================================================
// Latency Tests
// =============================================================================

/**
 * @brief Test message latency under normal conditions
 *
 * Verifies that p95 latency stays within acceptable bounds.
 */
bool test_stress_latency_normal() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    stress_test_config config;
    config.message_count = 100;
    config.concurrent_senders = 1;
    config.target_latency = std::chrono::milliseconds{100};  // 100ms target

    stress_test_runner runner(ris_port, config);
    auto result = runner.run();

    result.print_summary();

    // p95 latency should be under 100ms (100000 microseconds)
    INTEGRATION_TEST_ASSERT(
        result.p95_latency.count() < 100000,
        "P95 latency should be under 100ms");

    ris.stop();
    return true;
}

/**
 * @brief Test latency with slow RIS response
 *
 * Verifies that latency metrics accurately reflect slow server responses.
 */
bool test_stress_latency_slow_server() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;
    ris_config.response_delay = std::chrono::milliseconds{50};

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    stress_test_config config;
    config.message_count = 50;
    config.concurrent_senders = 1;

    stress_test_runner runner(ris_port, config);
    auto result = runner.run();

    result.print_summary();

    // Average latency should reflect the server delay
    INTEGRATION_TEST_ASSERT(
        result.avg_latency.count() >= 40000,  // At least 40ms
        "Average latency should reflect server delay");

    ris.stop();
    return true;
}

// =============================================================================
// Stability Tests
// =============================================================================

/**
 * @brief Test sustained load over time
 *
 * Sends messages at a steady rate for an extended period.
 */
bool test_stress_sustained_load() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // Sustained test: 10 batches of 50 messages each with delays
    uint32_t total_sent = 0;
    uint32_t total_success = 0;
    auto test_start = std::chrono::steady_clock::now();

    for (int batch = 0; batch < 10; ++batch) {
        stress_test_config config;
        config.message_count = 50;
        config.concurrent_senders = 2;

        stress_test_runner runner(ris_port, config);
        auto result = runner.run();

        total_sent += result.messages_sent;
        total_success += result.messages_received;

        // Brief pause between batches
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    auto test_end = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(
        test_end - test_start);

    double overall_success_rate =
        static_cast<double>(total_success) / total_sent;

    std::cout << "\n  Sustained Load Results:" << std::endl;
    std::cout << "    Total Sent:     " << total_sent << std::endl;
    std::cout << "    Total Success:  " << total_success << std::endl;
    std::cout << "    Total Duration: " << total_duration.count() << "s"
              << std::endl;
    std::cout << "    Success Rate:   " << (overall_success_rate * 100) << "%"
              << std::endl;

    INTEGRATION_TEST_ASSERT(overall_success_rate >= 0.95,
                            "Sustained load success rate should be >= 95%");
    INTEGRATION_TEST_ASSERT(total_duration.count() < 60,
                            "Sustained test should complete within 60 seconds");

    ris.stop();
    return true;
}

/**
 * @brief Test recovery after brief overload
 *
 * Verifies system recovers gracefully after being overloaded.
 */
bool test_stress_recovery_after_overload() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // Phase 1: Normal operation
    {
        stress_test_config config;
        config.message_count = 50;
        config.concurrent_senders = 2;

        stress_test_runner runner(ris_port, config);
        auto result = runner.run();

        INTEGRATION_TEST_ASSERT(result.success_rate >= 0.95,
                                "Normal operation should have high success rate");
    }

    // Phase 2: Overload (high concurrency)
    {
        stress_test_config config;
        config.message_count = 200;
        config.concurrent_senders = 16;  // Very high concurrency

        stress_test_runner runner(ris_port, config);
        auto result = runner.run();

        // Success rate may be lower during overload, but should still work
        std::cout << "\n  Overload phase success rate: "
                  << (result.success_rate * 100) << "%" << std::endl;
    }

    // Brief recovery period
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    // Phase 3: Back to normal - should recover
    {
        stress_test_config config;
        config.message_count = 50;
        config.concurrent_senders = 2;

        stress_test_runner runner(ris_port, config);
        auto result = runner.run();

        std::cout << "  Recovery phase success rate: "
                  << (result.success_rate * 100) << "%" << std::endl;

        INTEGRATION_TEST_ASSERT(
            result.success_rate >= 0.90,
            "System should recover after overload");
    }

    ris.stop();
    return true;
}

// =============================================================================
// MPPS-Specific Stress Tests
// =============================================================================

/**
 * @brief Test high volume MPPS events
 *
 * Simulates a busy radiology department with many concurrent procedures.
 */
bool test_stress_mpps_high_volume() {
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    mpps_bridge_simulator bridge(ris_port);

    auto start = std::chrono::steady_clock::now();
    uint32_t success_count = 0;
    uint32_t total_count = 100;

    // Simulate 100 procedures (each has N-CREATE and N-SET COMPLETED)
    for (uint32_t i = 0; i < total_count; ++i) {
        auto event = mpps_event_generator::create_sample_event();

        if (bridge.process_n_create(event)) {
            success_count++;
        }
        if (bridge.process_n_set_completed(event)) {
            success_count++;
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double success_rate =
        static_cast<double>(success_count) / (total_count * 2);
    double throughput =
        static_cast<double>(total_count * 2) / (duration.count() / 1000.0);

    std::cout << "\n  MPPS High Volume Results:" << std::endl;
    std::cout << "    Procedures:   " << total_count << std::endl;
    std::cout << "    Messages:     " << (total_count * 2) << std::endl;
    std::cout << "    Duration:     " << duration.count() << "ms" << std::endl;
    std::cout << "    Success Rate: " << (success_rate * 100) << "%"
              << std::endl;
    std::cout << "    Throughput:   " << throughput << " msg/sec" << std::endl;

    // Wait for RIS to receive all messages
    integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 180; },  // 90% of 200
        std::chrono::milliseconds{10000});

    INTEGRATION_TEST_ASSERT(success_rate >= 0.90,
                            "MPPS high volume success rate should be >= 90%");

    ris.stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_stress_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== Stress Integration Tests ===" << std::endl;
    std::cout << "Testing Phase 2: High Volume & Performance\n" << std::endl;

    std::cout << "\n--- Basic Stress Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_stress_sequential_moderate);
    RUN_INTEGRATION_TEST(test_stress_concurrent_senders);
    RUN_INTEGRATION_TEST(test_stress_high_volume_burst);

    std::cout << "\n--- Latency Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_stress_latency_normal);
    RUN_INTEGRATION_TEST(test_stress_latency_slow_server);

    std::cout << "\n--- Stability Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_stress_sustained_load);
    RUN_INTEGRATION_TEST(test_stress_recovery_after_overload);

    std::cout << "\n--- MPPS Stress Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_stress_mpps_high_volume);

    std::cout << "\n=== Stress Test Summary ===" << std::endl;
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
    return pacs::bridge::integration::test::run_all_stress_tests();
}
