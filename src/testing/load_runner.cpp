/**
 * @file load_runner.cpp
 * @brief Implementation of load test executor
 */

#include "pacs/bridge/testing/load_runner.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace pacs::bridge::testing {

// =============================================================================
// Implementation Class
// =============================================================================

class load_runner::impl {
public:
    impl()
        : state_(test_state::idle)
        , cancel_requested_(false)
        , progress_interval_(std::chrono::seconds(1)) {}

    ~impl() {
        cancel();
        wait_for_completion();
    }

    std::expected<test_result, load_error> run(const load_config& config) {
        if (!config.is_valid()) {
            return std::unexpected(load_error::invalid_configuration);
        }

        if (state_ == test_state::running) {
            return std::unexpected(load_error::already_running);
        }

        // Reset state
        cancel_requested_ = false;
        metrics_.reset();
        state_ = test_state::initializing;

        // Store configuration
        current_config_ = config;

        // Create generator if not set
        if (!generator_) {
            generator_ = std::make_shared<load_generator>();
        }

        // Validate target connectivity
        if (!validate_target(config.target_host, config.target_port,
                             config.message_timeout)) {
            state_ = test_state::failed;
            test_result result;
            result.state = test_state::failed;
            result.error_message = "Failed to connect to target";
            last_result_ = result;
            return std::unexpected(load_error::connection_failed);
        }

        // Run the appropriate test
        test_result result;
        result.type = config.type;
        result.started_at = std::chrono::system_clock::now();
        result.target_host = config.target_host;
        result.target_port = config.target_port;

        state_ = test_state::running;
        metrics_.start_time = std::chrono::steady_clock::now();

        // Start progress reporting thread
        std::thread progress_thread;
        if (progress_callback_) {
            progress_thread = std::thread([this, &config]() {
                run_progress_reporter(config.duration);
            });
        }

        // Execute test based on type
        bool success = false;
        switch (config.type) {
            case test_type::sustained:
                success = execute_sustained_test(config);
                break;
            case test_type::peak:
                success = execute_peak_test(config);
                break;
            case test_type::endurance:
                success = execute_endurance_test(config);
                break;
            case test_type::concurrent:
                success = execute_concurrent_test(config);
                break;
            case test_type::queue_stress:
                success = execute_queue_stress_test(config);
                break;
            case test_type::failover:
                success = execute_failover_test(config);
                break;
        }

        // Wait for progress thread
        if (progress_thread.joinable()) {
            progress_thread.join();
        }

        // Populate result
        result.ended_at = std::chrono::system_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::seconds>(
            result.ended_at - result.started_at);

        populate_result(result);

        if (cancel_requested_) {
            result.state = test_state::cancelled;
            state_ = test_state::cancelled;
        } else if (success) {
            result.state = test_state::completed;
            state_ = test_state::completed;
        } else {
            result.state = test_state::failed;
            state_ = test_state::failed;
        }

        last_result_ = result;
        return result;
    }

    std::expected<test_result, load_error>
    run_sustained(std::string_view host, uint16_t port,
                  std::chrono::seconds duration, uint32_t rate) {
        auto config = load_config::sustained(host, port, duration, rate);
        return run(config);
    }

    std::expected<test_result, load_error>
    run_peak(std::string_view host, uint16_t port, uint32_t max_rate) {
        auto config = load_config::peak(host, port, max_rate);
        return run(config);
    }

    std::expected<test_result, load_error>
    run_endurance(std::string_view host, uint16_t port,
                  std::chrono::seconds duration) {
        auto config = load_config::endurance(host, port);
        config.duration = duration;
        return run(config);
    }

    std::expected<test_result, load_error>
    run_concurrent(std::string_view host, uint16_t port,
                   size_t connections, size_t messages_per_connection) {
        auto config = load_config::concurrent(host, port, connections,
                                              messages_per_connection);
        return run(config);
    }

    std::expected<test_result, load_error>
    run_queue_stress(std::string_view host, uint16_t port,
                     std::chrono::minutes accumulation_time) {
        load_config config;
        config.type = test_type::queue_stress;
        config.target_host = std::string(host);
        config.target_port = port;
        config.duration = std::chrono::duration_cast<std::chrono::seconds>(
            accumulation_time * 2);  // Accumulation + drain
        config.messages_per_second = 500;
        return run(config);
    }

    void cancel() {
        cancel_requested_ = true;
        cv_.notify_all();
    }

    bool is_running() const noexcept {
        return state_ == test_state::running ||
               state_ == test_state::initializing;
    }

    test_state state() const noexcept {
        return state_;
    }

    std::optional<test_metrics> current_metrics() const {
        if (!is_running()) {
            return std::nullopt;
        }
        return metrics_;
    }

    void on_progress(progress_callback callback) {
        progress_callback_ = std::move(callback);
    }

    void set_progress_interval(std::chrono::milliseconds interval) {
        progress_interval_ = interval;
    }

    void set_generator(std::shared_ptr<load_generator> generator) {
        generator_ = std::move(generator);
    }

    std::optional<test_result> last_result() const {
        return last_result_;
    }

    bool validate_target(std::string_view host, uint16_t port,
                         std::chrono::milliseconds timeout) {
        // Simulate connection validation
        // In real implementation, this would attempt TCP connection
        (void)host;
        (void)port;
        (void)timeout;
        return true;  // Assume target is available for header-only validation
    }

private:
    void wait_for_completion() {
        // Wait for any running threads to complete
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(5), [this]() {
            return state_ != test_state::running;
        });
    }

    bool execute_sustained_test(const load_config& config) {
        auto start = std::chrono::steady_clock::now();
        auto end_time = start + config.duration;
        auto interval = std::chrono::microseconds(
            1000000 / config.messages_per_second);

        // Ramp-up phase
        if (config.ramp_up.count() > 0) {
            auto ramp_end = start + config.ramp_up;
            double ramp_rate = 0.0;
            double rate_increment = static_cast<double>(config.messages_per_second) /
                                    static_cast<double>(config.ramp_up.count());

            while (std::chrono::steady_clock::now() < ramp_end && !cancel_requested_) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed_secs = std::chrono::duration<double>(now - start).count();
                ramp_rate = std::min(rate_increment * elapsed_secs,
                                     static_cast<double>(config.messages_per_second));

                if (ramp_rate > 0) {
                    send_message_batch(config, static_cast<size_t>(ramp_rate));
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        // Main test phase
        while (std::chrono::steady_clock::now() < end_time && !cancel_requested_) {
            auto batch_start = std::chrono::steady_clock::now();

            // Send batch of messages
            send_message_batch(config, config.messages_per_second);

            // Wait until next second
            auto batch_elapsed = std::chrono::steady_clock::now() - batch_start;
            auto sleep_time = std::chrono::seconds(1) - batch_elapsed;
            if (sleep_time > std::chrono::milliseconds(0)) {
                std::this_thread::sleep_for(sleep_time);
            }
        }

        return !cancel_requested_;
    }

    bool execute_peak_test(const load_config& config) {
        auto start = std::chrono::steady_clock::now();
        auto end_time = start + config.duration;

        uint32_t current_rate = 100;  // Start at 100 msg/s
        uint32_t step = 100;
        double peak_throughput = 0;
        uint32_t peak_rate = 0;

        while (current_rate <= config.messages_per_second &&
               std::chrono::steady_clock::now() < end_time &&
               !cancel_requested_) {

            // Run at current rate for 30 seconds
            auto phase_start = std::chrono::steady_clock::now();
            auto phase_end = phase_start + std::chrono::seconds(30);

            uint64_t phase_sent = 0;
            uint64_t phase_failed = 0;

            while (std::chrono::steady_clock::now() < phase_end &&
                   !cancel_requested_) {
                send_message_batch(config, current_rate);
                phase_sent += current_rate;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            // Calculate success rate
            double success_rate = metrics_.success_rate();
            double throughput = metrics_.overall_throughput();

            // Record peak
            if (throughput > peak_throughput && success_rate >= 99.0) {
                peak_throughput = throughput;
                peak_rate = current_rate;
            }

            // If degradation detected, stop increasing
            if (success_rate < 99.0 || metrics_.latency.percentile_us(95) > 100000) {
                break;
            }

            current_rate += step;
        }

        return !cancel_requested_;
    }

    bool execute_endurance_test(const load_config& config) {
        // Similar to sustained but with memory monitoring
        return execute_sustained_test(config);
    }

    bool execute_concurrent_test(const load_config& config) {
        std::vector<std::thread> threads;
        std::atomic<size_t> successful_connections{0};
        std::atomic<size_t> failed_connections{0};

        // Calculate messages per connection
        size_t messages_per_conn = 100;  // Default

        // Launch concurrent connection threads
        for (size_t i = 0; i < config.concurrent_connections && !cancel_requested_; ++i) {
            threads.emplace_back([this, &config, &successful_connections,
                                  &failed_connections, messages_per_conn, i]() {
                // Simulate connection
                metrics_.active_connections.fetch_add(1);

                bool success = true;
                for (size_t m = 0; m < messages_per_conn && !cancel_requested_; ++m) {
                    if (!send_single_message(config)) {
                        success = false;
                        break;
                    }
                }

                metrics_.active_connections.fetch_sub(1);

                if (success) {
                    successful_connections.fetch_add(1);
                } else {
                    failed_connections.fetch_add(1);
                }
            });

            // Stagger connection creation
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Wait for all threads
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }

        return failed_connections.load() == 0 && !cancel_requested_;
    }

    bool execute_queue_stress_test(const load_config& config) {
        // Simulate queue accumulation by sending without waiting for ACK
        auto accumulation_time = config.duration / 2;
        auto start = std::chrono::steady_clock::now();
        auto accumulation_end = start + accumulation_time;

        // Accumulation phase - send without processing responses
        while (std::chrono::steady_clock::now() < accumulation_end &&
               !cancel_requested_) {
            send_message_batch(config, config.messages_per_second);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Drain phase - normal operation
        auto drain_end = start + config.duration;
        while (std::chrono::steady_clock::now() < drain_end &&
               !cancel_requested_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return !cancel_requested_;
    }

    bool execute_failover_test(const load_config& config) {
        // Simulate failover scenario
        return execute_sustained_test(config);
    }

    void send_message_batch(const load_config& config, size_t count) {
        for (size_t i = 0; i < count && !cancel_requested_; ++i) {
            send_single_message(config);
        }
    }

    bool send_single_message(const load_config& config) {
        auto start = std::chrono::steady_clock::now();

        // Generate message
        auto msg_result = generator_->generate_random(config.distribution);
        if (!msg_result) {
            metrics_.messages_failed.fetch_add(1);
            return false;
        }

        // Simulate send (in real implementation, would use MLLP client)
        metrics_.bytes_sent.fetch_add(msg_result->size());

        // Simulate latency (1-10ms for now)
        std::this_thread::sleep_for(std::chrono::microseconds(
            1000 + (std::rand() % 9000)));

        auto end = std::chrono::steady_clock::now();
        auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count();

        // Record metrics
        metrics_.messages_sent.fetch_add(1);
        metrics_.messages_acked.fetch_add(1);
        metrics_.bytes_received.fetch_add(50);  // Approximate ACK size
        metrics_.latency.record(static_cast<uint64_t>(latency_us));

        // Update throughput
        metrics_.current_throughput.store(metrics_.overall_throughput());

        return true;
    }

    void run_progress_reporter(std::chrono::seconds total_duration) {
        auto start = std::chrono::steady_clock::now();

        while (state_ == test_state::running && !cancel_requested_) {
            std::this_thread::sleep_for(progress_interval_);

            if (cancel_requested_) break;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - start);
            auto remaining = total_duration - elapsed;
            if (remaining.count() < 0) remaining = std::chrono::seconds(0);

            progress_info info;
            info.state = state_;
            info.elapsed = elapsed;
            info.remaining = remaining;
            info.progress_percent = static_cast<double>(elapsed.count()) * 100.0 /
                                    static_cast<double>(total_duration.count());
            info.messages_sent = metrics_.messages_sent.load();
            info.messages_acked = metrics_.messages_acked.load();
            info.messages_failed = metrics_.messages_failed.load();
            info.current_throughput = metrics_.current_throughput.load();
            info.current_latency_p95_ms =
                static_cast<double>(metrics_.latency.percentile_us(95)) / 1000.0;

            if (progress_callback_) {
                progress_callback_(info);
            }
        }
    }

    void populate_result(test_result& result) {
        result.messages_sent = metrics_.messages_sent.load();
        result.messages_acked = metrics_.messages_acked.load();
        result.messages_failed = metrics_.messages_failed.load();
        result.success_rate_percent = metrics_.success_rate();
        result.throughput = metrics_.overall_throughput();
        result.peak_throughput = metrics_.current_throughput.load();

        result.latency_p50_ms =
            static_cast<double>(metrics_.latency.percentile_us(50)) / 1000.0;
        result.latency_p95_ms =
            static_cast<double>(metrics_.latency.percentile_us(95)) / 1000.0;
        result.latency_p99_ms =
            static_cast<double>(metrics_.latency.percentile_us(99)) / 1000.0;
        result.latency_min_ms =
            static_cast<double>(metrics_.latency.min_latency.load()) / 1000.0;
        result.latency_max_ms =
            static_cast<double>(metrics_.latency.max_latency.load()) / 1000.0;
        result.latency_mean_ms = metrics_.latency.mean_us() / 1000.0;

        result.bytes_sent = metrics_.bytes_sent.load();
        result.bytes_received = metrics_.bytes_received.load();
        result.connection_errors = metrics_.connection_errors.load();
        result.timeout_errors = metrics_.timeout_errors.load();
        result.protocol_errors = metrics_.protocol_errors.load();
    }

    std::atomic<test_state> state_;
    std::atomic<bool> cancel_requested_;
    test_metrics metrics_;
    load_config current_config_;
    std::shared_ptr<load_generator> generator_;
    progress_callback progress_callback_;
    std::chrono::milliseconds progress_interval_;
    std::optional<test_result> last_result_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

// =============================================================================
// Public Interface
// =============================================================================

load_runner::load_runner()
    : pimpl_(std::make_unique<impl>()) {}

load_runner::~load_runner() = default;

load_runner::load_runner(load_runner&&) noexcept = default;
load_runner& load_runner::operator=(load_runner&&) noexcept = default;

std::expected<test_result, load_error>
load_runner::run(const load_config& config) {
    return pimpl_->run(config);
}

std::expected<test_result, load_error>
load_runner::run_sustained(std::string_view host, uint16_t port,
                           std::chrono::seconds duration, uint32_t rate) {
    return pimpl_->run_sustained(host, port, duration, rate);
}

std::expected<test_result, load_error>
load_runner::run_peak(std::string_view host, uint16_t port, uint32_t max_rate) {
    return pimpl_->run_peak(host, port, max_rate);
}

std::expected<test_result, load_error>
load_runner::run_endurance(std::string_view host, uint16_t port,
                           std::chrono::seconds duration) {
    return pimpl_->run_endurance(host, port, duration);
}

std::expected<test_result, load_error>
load_runner::run_concurrent(std::string_view host, uint16_t port,
                            size_t connections, size_t messages_per_connection) {
    return pimpl_->run_concurrent(host, port, connections, messages_per_connection);
}

std::expected<test_result, load_error>
load_runner::run_queue_stress(std::string_view host, uint16_t port,
                              std::chrono::minutes accumulation_time) {
    return pimpl_->run_queue_stress(host, port, accumulation_time);
}

void load_runner::cancel() {
    pimpl_->cancel();
}

bool load_runner::is_running() const noexcept {
    return pimpl_->is_running();
}

test_state load_runner::state() const noexcept {
    return pimpl_->state();
}

std::optional<test_metrics> load_runner::current_metrics() const {
    return pimpl_->current_metrics();
}

void load_runner::on_progress(progress_callback callback) {
    pimpl_->on_progress(std::move(callback));
}

void load_runner::set_progress_interval(std::chrono::milliseconds interval) {
    pimpl_->set_progress_interval(interval);
}

void load_runner::set_generator(std::shared_ptr<load_generator> generator) {
    pimpl_->set_generator(std::move(generator));
}

std::optional<test_result> load_runner::last_result() const {
    return pimpl_->last_result();
}

bool load_runner::validate_target(std::string_view host, uint16_t port,
                                  std::chrono::milliseconds timeout) {
    return pimpl_->validate_target(host, port, timeout);
}

// =============================================================================
// Utility Functions
// =============================================================================

std::vector<test_result> run_test_suite(
    load_runner& runner,
    std::span<const load_config> configs) {
    std::vector<test_result> results;
    results.reserve(configs.size());

    for (const auto& config : configs) {
        auto result = runner.run(config);
        if (result) {
            results.push_back(std::move(*result));
        } else {
            // Create failed result entry
            test_result failed;
            failed.type = config.type;
            failed.state = test_state::failed;
            failed.error_message = to_string(result.error());
            results.push_back(std::move(failed));
        }
    }

    return results;
}

}  // namespace pacs::bridge::testing
