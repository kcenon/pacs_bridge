/**
 * @file benchmark_runner.cpp
 * @brief Implementation of performance benchmark suite
 */

#include "pacs/bridge/performance/benchmark_runner.h"
#include "pacs/bridge/performance/object_pool.h"
#include "pacs/bridge/performance/thread_pool_manager.h"
#include "pacs/bridge/performance/zero_copy_parser.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

namespace pacs::bridge::performance {

// =============================================================================
// Benchmark Suite Result Implementation
// =============================================================================

std::string benchmark_suite_result::to_text() const {
    std::ostringstream ss;

    ss << "======================================\n";
    ss << "PACS Bridge Performance Benchmark Suite\n";
    ss << "======================================\n\n";

    ss << "Suite: " << name << "\n";
    ss << "Duration: " << total_duration.count() << " ms\n";
    ss << "Status: " << (passed ? "PASSED" : "FAILED") << "\n\n";

    ss << "Summary:\n";
    ss << "  Total benchmarks: " << summary.total_benchmarks << "\n";
    ss << "  Passed: " << summary.passed_benchmarks << "\n";
    ss << "  Failed: " << summary.failed_benchmarks << "\n";
    ss << "  Avg throughput: " << std::fixed << std::setprecision(2)
       << summary.avg_throughput << " msg/s\n";
    ss << "  Avg latency: " << std::fixed << std::setprecision(2)
       << summary.avg_latency_us << " us\n\n";

    ss << "Individual Results:\n";
    ss << "----------------------------------------\n";

    for (const auto& result : benchmarks) {
        ss << "\nBenchmark: ";
        switch (result.type) {
            case benchmark_type::parsing:
                ss << "Parsing";
                break;
            case benchmark_type::throughput:
                ss << "Throughput";
                break;
            case benchmark_type::latency:
                ss << "Latency";
                break;
            case benchmark_type::memory:
                ss << "Memory";
                break;
            case benchmark_type::concurrent:
                ss << "Concurrent";
                break;
            case benchmark_type::pool_efficiency:
                ss << "Pool Efficiency";
                break;
            case benchmark_type::thread_scaling:
                ss << "Thread Scaling";
                break;
        }
        ss << "\n";

        ss << "  Status: " << (result.passed() ? "PASSED" : "FAILED") << "\n";
        ss << "  Throughput: " << std::fixed << std::setprecision(2)
           << result.throughput << " msg/s\n";
        ss << "  Latency (avg): " << std::fixed << std::setprecision(2)
           << result.avg_latency_us << " us\n";
        ss << "  Latency (P50): " << std::fixed << std::setprecision(2)
           << result.p50_latency_us << " us\n";
        ss << "  Latency (P95): " << std::fixed << std::setprecision(2)
           << result.p95_latency_us << " us\n";
        ss << "  Latency (P99): " << std::fixed << std::setprecision(2)
           << result.p99_latency_us << " us\n";
        ss << "  Messages: " << result.total_messages << "\n";
        ss << "  Errors: " << result.total_errors << "\n";
        ss << "  Peak memory: " << (result.peak_memory_bytes / 1024 / 1024)
           << " MB\n";
        ss << "  Duration: " << result.actual_duration.count() << " ms\n";
    }

    return ss.str();
}

std::string benchmark_suite_result::to_json() const {
    std::ostringstream ss;

    ss << "{\n";
    ss << "  \"name\": \"" << name << "\",\n";
    ss << "  \"duration_ms\": " << total_duration.count() << ",\n";
    ss << "  \"passed\": " << (passed ? "true" : "false") << ",\n";
    ss << "  \"summary\": {\n";
    ss << "    \"total_benchmarks\": " << summary.total_benchmarks << ",\n";
    ss << "    \"passed_benchmarks\": " << summary.passed_benchmarks << ",\n";
    ss << "    \"failed_benchmarks\": " << summary.failed_benchmarks << ",\n";
    ss << "    \"avg_throughput\": " << summary.avg_throughput << ",\n";
    ss << "    \"avg_latency_us\": " << summary.avg_latency_us << "\n";
    ss << "  },\n";
    ss << "  \"benchmarks\": [\n";

    for (size_t i = 0; i < benchmarks.size(); ++i) {
        const auto& result = benchmarks[i];
        ss << "    {\n";
        ss << "      \"type\": " << static_cast<int>(result.type) << ",\n";
        ss << "      \"passed\": " << (result.passed() ? "true" : "false")
           << ",\n";
        ss << "      \"throughput\": " << result.throughput << ",\n";
        ss << "      \"avg_latency_us\": " << result.avg_latency_us << ",\n";
        ss << "      \"p50_latency_us\": " << result.p50_latency_us << ",\n";
        ss << "      \"p95_latency_us\": " << result.p95_latency_us << ",\n";
        ss << "      \"p99_latency_us\": " << result.p99_latency_us << ",\n";
        ss << "      \"total_messages\": " << result.total_messages << ",\n";
        ss << "      \"total_errors\": " << result.total_errors << ",\n";
        ss << "      \"peak_memory_bytes\": " << result.peak_memory_bytes << ",\n";
        ss << "      \"duration_ms\": " << result.actual_duration.count() << "\n";
        ss << "    }";
        if (i < benchmarks.size() - 1) ss << ",";
        ss << "\n";
    }

    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

std::string benchmark_suite_result::to_markdown() const {
    std::ostringstream ss;

    ss << "# PACS Bridge Performance Benchmark Suite\n\n";
    ss << "**Suite:** " << name << "\n";
    ss << "**Duration:** " << total_duration.count() << " ms\n";
    ss << "**Status:** " << (passed ? "✅ PASSED" : "❌ FAILED") << "\n\n";

    ss << "## Summary\n\n";
    ss << "| Metric | Value |\n";
    ss << "|--------|-------|\n";
    ss << "| Total benchmarks | " << summary.total_benchmarks << " |\n";
    ss << "| Passed | " << summary.passed_benchmarks << " |\n";
    ss << "| Failed | " << summary.failed_benchmarks << " |\n";
    ss << "| Avg throughput | " << std::fixed << std::setprecision(2)
       << summary.avg_throughput << " msg/s |\n";
    ss << "| Avg latency | " << std::fixed << std::setprecision(2)
       << summary.avg_latency_us << " µs |\n\n";

    ss << "## Individual Results\n\n";
    ss << "| Benchmark | Status | Throughput | P50 | P95 | P99 | Messages "
          "| Errors |\n";
    ss << "|-----------|--------|------------|-----|-----|-----|----------|--------"
          "|\n";

    for (const auto& result : benchmarks) {
        std::string type_name;
        switch (result.type) {
            case benchmark_type::parsing:
                type_name = "Parsing";
                break;
            case benchmark_type::throughput:
                type_name = "Throughput";
                break;
            case benchmark_type::latency:
                type_name = "Latency";
                break;
            case benchmark_type::memory:
                type_name = "Memory";
                break;
            case benchmark_type::concurrent:
                type_name = "Concurrent";
                break;
            case benchmark_type::pool_efficiency:
                type_name = "Pool Eff";
                break;
            case benchmark_type::thread_scaling:
                type_name = "Thread Scale";
                break;
        }

        ss << "| " << type_name << " | " << (result.passed() ? "✅" : "❌")
           << " | " << std::fixed << std::setprecision(0) << result.throughput
           << " | " << std::setprecision(0) << result.p50_latency_us << "µs | "
           << result.p95_latency_us << "µs | " << result.p99_latency_us
           << "µs | " << result.total_messages << " | " << result.total_errors
           << " |\n";
    }

    return ss.str();
}

// =============================================================================
// Benchmark Runner Implementation
// =============================================================================

struct benchmark_runner::impl {
    benchmark_config config;
    std::optional<benchmark_result> last_result;
    std::optional<benchmark_suite_result> last_suite;
    std::vector<std::pair<std::string, custom_benchmark_fn>> custom_benchmarks;
    progress_callback progress_cb;
    std::atomic<bool> cancelled{false};

    // Sample HL7 message for benchmarking
    static const std::string& get_sample_message() {
        static const std::string sample =
            "MSH|^~\\&|SENDING_APP|SENDING_FAC|RECEIVING_APP|RECEIVING_FAC|"
            "20240115120000||ORM^O01|MSG00001|P|2.5\r"
            "PID|1|12345|67890^^^MRN||DOE^JOHN^A||19800101|M|||123 MAIN ST^^"
            "CITY^ST^12345||(555)555-1234\r"
            "PV1|1|O|CLINIC|||||||||||||||V123456\r"
            "ORC|NW|ORDER123|PLACER456||SC||^^^20240115120000||20240115120000|"
            "ORDERER^NAME\r"
            "OBR|1|ORDER123|FILLER789|12345^CHEST XRAY^LOCAL|||20240115120000||"
            "|||||ORDERING^PHYSICIAN||||||||||^^^^^RT\r";
        return sample;
    }

    benchmark_result run_parsing_benchmark() {
        benchmark_result result;
        result.type = benchmark_type::parsing;

        auto start = std::chrono::steady_clock::now();
        std::vector<uint64_t> latencies;
        latencies.reserve(config.iterations * 10000);

        // Warmup
        for (size_t i = 0; i < 1000 && !cancelled.load(); ++i) {
            auto parser =
                zero_copy_parser::parse(get_sample_message(), zero_copy_config{});
        }

        // Measure
        uint64_t total_messages = 0;
        uint64_t errors = 0;

        auto deadline =
            std::chrono::steady_clock::now() + config.duration;

        while (std::chrono::steady_clock::now() < deadline && !cancelled.load()) {
            auto op_start = std::chrono::steady_clock::now();

            auto parser = zero_copy_parser::parse(get_sample_message());
            if (parser && parser->valid()) {
                // Access some fields
                auto msg_type = parser->message_type();
                auto msg_id = parser->message_control_id();
                (void)msg_type;
                (void)msg_id;
                ++total_messages;
            } else {
                ++errors;
            }

            auto op_end = std::chrono::steady_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               op_end - op_start)
                               .count();
            latencies.push_back(static_cast<uint64_t>(latency));
        }

        auto end = std::chrono::steady_clock::now();
        result.actual_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        result.total_messages = total_messages;
        result.total_errors = errors;

        // Calculate statistics
        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());

            double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
            result.avg_latency_us = sum / latencies.size() / 1000.0;

            result.min_latency_us =
                static_cast<double>(latencies.front()) / 1000.0;
            result.max_latency_us =
                static_cast<double>(latencies.back()) / 1000.0;

            size_t p50_idx = latencies.size() * 50 / 100;
            size_t p95_idx = latencies.size() * 95 / 100;
            size_t p99_idx = latencies.size() * 99 / 100;

            result.p50_latency_us =
                static_cast<double>(latencies[p50_idx]) / 1000.0;
            result.p95_latency_us =
                static_cast<double>(latencies[p95_idx]) / 1000.0;
            result.p99_latency_us =
                static_cast<double>(latencies[p99_idx]) / 1000.0;
        }

        // Calculate throughput
        double duration_sec =
            static_cast<double>(result.actual_duration.count()) / 1000.0;
        if (duration_sec > 0) {
            result.throughput =
                static_cast<double>(total_messages) / duration_sec;
        }

        // Check if targets met
        result.targets_met =
            result.throughput >= performance_targets::MIN_THROUGHPUT_MSG_PER_SEC &&
            result.p95_latency_us <=
                performance_targets::MAX_P95_LATENCY.count() * 1000;

        return result;
    }

    benchmark_result run_throughput_benchmark() {
        benchmark_result result;
        result.type = benchmark_type::throughput;

        auto start = std::chrono::steady_clock::now();
        std::atomic<uint64_t> total_messages{0};
        std::atomic<uint64_t> errors{0};

        auto deadline = std::chrono::steady_clock::now() + config.duration;

        // Multi-threaded throughput test
        std::vector<std::thread> threads;
        threads.reserve(config.concurrency);

        for (size_t t = 0; t < config.concurrency; ++t) {
            threads.emplace_back([&, deadline]() {
                while (std::chrono::steady_clock::now() < deadline &&
                       !cancelled.load()) {
                    auto parser = zero_copy_parser::parse(get_sample_message());
                    if (parser && parser->valid()) {
                        total_messages.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        errors.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        auto end = std::chrono::steady_clock::now();
        result.actual_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        result.total_messages = total_messages.load();
        result.total_errors = errors.load();

        double duration_sec =
            static_cast<double>(result.actual_duration.count()) / 1000.0;
        if (duration_sec > 0) {
            result.throughput =
                static_cast<double>(result.total_messages) / duration_sec;
        }

        result.targets_met =
            result.throughput >= performance_targets::MIN_THROUGHPUT_MSG_PER_SEC;

        return result;
    }

    benchmark_result run_memory_benchmark() {
        benchmark_result result;
        result.type = benchmark_type::memory;

        auto start = std::chrono::steady_clock::now();

        // Create buffer pool and measure efficiency
        memory_config mem_config;
        mem_config.message_buffer_pool_size = 128;

        message_buffer_pool pool(mem_config);

        std::vector<scoped_buffer> buffers;
        buffers.reserve(1000);

        uint64_t acquisitions = 0;
        uint64_t errors = 0;

        auto deadline = std::chrono::steady_clock::now() + config.duration;

        while (std::chrono::steady_clock::now() < deadline && !cancelled.load()) {
            // Acquire buffer
            auto buf_result = pool.acquire(1024);
            if (buf_result) {
                scoped_buffer sbuf(pool, std::move(*buf_result));
                // Use buffer briefly
                if (sbuf.data()) {
                    sbuf.set_size(512);
                }
                ++acquisitions;
                // Buffer released when sbuf goes out of scope
            } else {
                ++errors;
            }
        }

        auto end = std::chrono::steady_clock::now();
        result.actual_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        result.total_messages = acquisitions;
        result.total_errors = errors;
        result.peak_memory_bytes = pool.memory_usage();

        double duration_sec =
            static_cast<double>(result.actual_duration.count()) / 1000.0;
        if (duration_sec > 0) {
            result.throughput =
                static_cast<double>(acquisitions) / duration_sec;
        }

        const auto& stats = pool.statistics();
        result.targets_met =
            stats.hit_rate() >= 90.0 &&
            result.peak_memory_bytes <=
                performance_targets::MAX_MEMORY_BASELINE_MB * 1024 * 1024;

        return result;
    }

    benchmark_result run_pool_efficiency_benchmark() {
        benchmark_result result;
        result.type = benchmark_type::pool_efficiency;

        auto start = std::chrono::steady_clock::now();

        memory_config mem_config;
        mem_config.message_buffer_pool_size = 64;

        message_buffer_pool pool(mem_config);

        uint64_t total_ops = 0;

        auto deadline = std::chrono::steady_clock::now() + config.duration;

        // Mixed acquire/release pattern
        std::vector<message_buffer_pool::buffer_handle> active;
        active.reserve(100);

        while (std::chrono::steady_clock::now() < deadline && !cancelled.load()) {
            // 70% chance acquire, 30% chance release
            if (active.size() < 50 || (std::rand() % 100) < 70) {
                auto buf = pool.acquire(512);
                if (buf) {
                    active.push_back(std::move(*buf));
                    ++total_ops;
                }
            } else if (!active.empty()) {
                pool.release(active.back());
                active.pop_back();
                ++total_ops;
            }
        }

        // Cleanup
        for (auto& buf : active) {
            pool.release(buf);
        }

        auto end = std::chrono::steady_clock::now();
        result.actual_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        result.total_messages = total_ops;
        result.peak_memory_bytes = pool.memory_usage();

        const auto& stats = pool.statistics();

        // Use hit rate as a proxy for latency (high hit rate = low latency)
        result.avg_latency_us = (100.0 - stats.hit_rate()) * 10;  // Rough estimate

        double duration_sec =
            static_cast<double>(result.actual_duration.count()) / 1000.0;
        if (duration_sec > 0) {
            result.throughput = static_cast<double>(total_ops) / duration_sec;
        }

        result.targets_met = stats.hit_rate() >= 80.0;

        return result;
    }

    benchmark_result run_thread_scaling_benchmark() {
        benchmark_result result;
        result.type = benchmark_type::thread_scaling;

        auto start = std::chrono::steady_clock::now();

        thread_pool_config pool_config;
        pool_config.min_threads = config.concurrency;
        pool_config.max_threads = config.concurrency;

        thread_pool_manager pool(pool_config);
        auto start_result = pool.start();
        if (!start_result) {
            result.total_errors = 1;
            return result;
        }

        std::atomic<uint64_t> completed{0};
        size_t submitted = 0;

        auto deadline = std::chrono::steady_clock::now() + config.duration;

        while (std::chrono::steady_clock::now() < deadline && !cancelled.load()) {
            uint64_t before = completed.load(std::memory_order_relaxed);

            // Submit tasks
            for (size_t i = 0; i < 100; ++i) {
                pool.post(
                    [&completed]() {
                        // Simulate work
                        auto parser = zero_copy_parser::parse(get_sample_message());
                        (void)parser;
                        completed.fetch_add(1, std::memory_order_relaxed);
                    },
                    task_priority::normal);
                ++submitted;
            }

            // Wait for at least some tasks to complete before submitting more
            auto wait_deadline = std::chrono::steady_clock::now() +
                                 std::chrono::milliseconds(100);
            while (completed.load(std::memory_order_relaxed) < before + 50 &&
                   std::chrono::steady_clock::now() < wait_deadline &&
                   !cancelled.load()) {
                std::this_thread::yield();
            }
        }

        // Wait for completion
        pool.stop(true, std::chrono::seconds{10});

        auto end = std::chrono::steady_clock::now();
        result.actual_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        result.total_messages = completed.load();

        const auto& stats = pool.statistics();

        double duration_sec =
            static_cast<double>(result.actual_duration.count()) / 1000.0;
        if (duration_sec > 0) {
            result.throughput =
                static_cast<double>(result.total_messages) / duration_sec;
        }

        result.targets_met =
            result.throughput >= performance_targets::MIN_THROUGHPUT_MSG_PER_SEC;

        return result;
    }
};

benchmark_runner::benchmark_runner(const benchmark_config& config)
    : impl_(std::make_unique<impl>()) {
    impl_->config = config;
}

benchmark_runner::~benchmark_runner() = default;

benchmark_runner::benchmark_runner(benchmark_runner&&) noexcept = default;
benchmark_runner& benchmark_runner::operator=(benchmark_runner&&) noexcept =
    default;

void benchmark_runner::set_duration(std::chrono::seconds duration) {
    impl_->config.duration = duration;
}

void benchmark_runner::set_warmup_duration(std::chrono::seconds warmup) {
    impl_->config.warmup = warmup;
}

void benchmark_runner::set_iterations(size_t iterations) {
    impl_->config.iterations = iterations;
}

void benchmark_runner::set_concurrency(size_t threads) {
    impl_->config.concurrency = threads;
}

void benchmark_runner::set_message_size(size_t bytes) {
    impl_->config.message_size = bytes;
}

void benchmark_runner::set_target_rate(size_t messages_per_second) {
    impl_->config.target_rate = messages_per_second;
}

void benchmark_runner::set_detailed_timing(bool enabled) {
    impl_->config.detailed_timing = enabled;
}

void benchmark_runner::set_progress_callback(progress_callback callback) {
    impl_->progress_cb = std::move(callback);
}

const benchmark_config& benchmark_runner::config() const noexcept {
    return impl_->config;
}

std::expected<benchmark_suite_result, performance_error>
benchmark_runner::run_all() {
    impl_->cancelled.store(false);

    auto suite_start = std::chrono::steady_clock::now();

    benchmark_suite_result suite;
    suite.name = "PACS Bridge Performance Suite";

    // Run all benchmark types
    std::vector<benchmark_type> types = {benchmark_type::parsing,
                                          benchmark_type::throughput,
                                          benchmark_type::memory,
                                          benchmark_type::pool_efficiency,
                                          benchmark_type::thread_scaling};

    for (auto type : types) {
        if (impl_->cancelled.load()) break;

        auto result = run_benchmark(type);
        if (result) {
            suite.benchmarks.push_back(*result);
        }
    }

    // Run custom benchmarks
    auto custom = run_custom_benchmarks();
    if (custom) {
        for (auto& r : *custom) {
            suite.benchmarks.push_back(r);
        }
    }

    auto suite_end = std::chrono::steady_clock::now();
    suite.total_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(suite_end -
                                                              suite_start);

    // Calculate summary
    suite.summary.total_benchmarks = suite.benchmarks.size();
    suite.summary.passed_benchmarks = 0;
    suite.summary.failed_benchmarks = 0;
    double total_throughput = 0;
    double total_latency = 0;

    for (const auto& b : suite.benchmarks) {
        if (b.passed()) {
            ++suite.summary.passed_benchmarks;
        } else {
            ++suite.summary.failed_benchmarks;
        }
        total_throughput += b.throughput;
        total_latency += b.avg_latency_us;
    }

    if (!suite.benchmarks.empty()) {
        suite.summary.avg_throughput =
            total_throughput / suite.benchmarks.size();
        suite.summary.avg_latency_us = total_latency / suite.benchmarks.size();
    }

    suite.passed = suite.summary.failed_benchmarks == 0;

    impl_->last_suite = suite;
    return suite;
}

std::expected<benchmark_result, performance_error>
benchmark_runner::run_benchmark(benchmark_type type) {
    impl_->cancelled.store(false);

    benchmark_result result;

    switch (type) {
        case benchmark_type::parsing:
            result = impl_->run_parsing_benchmark();
            break;
        case benchmark_type::throughput:
            result = impl_->run_throughput_benchmark();
            break;
        case benchmark_type::latency:
            result = impl_->run_parsing_benchmark();  // Latency from parsing
            result.type = benchmark_type::latency;
            break;
        case benchmark_type::memory:
            result = impl_->run_memory_benchmark();
            break;
        case benchmark_type::concurrent:
            result = impl_->run_throughput_benchmark();
            result.type = benchmark_type::concurrent;
            break;
        case benchmark_type::pool_efficiency:
            result = impl_->run_pool_efficiency_benchmark();
            break;
        case benchmark_type::thread_scaling:
            result = impl_->run_thread_scaling_benchmark();
            break;
    }

    impl_->last_result = result;
    return result;
}

std::expected<benchmark_result, performance_error>
benchmark_runner::run_parsing_benchmark() {
    return run_benchmark(benchmark_type::parsing);
}

std::expected<benchmark_result, performance_error>
benchmark_runner::run_throughput_benchmark() {
    return run_benchmark(benchmark_type::throughput);
}

std::expected<benchmark_result, performance_error>
benchmark_runner::run_latency_benchmark() {
    return run_benchmark(benchmark_type::latency);
}

std::expected<benchmark_result, performance_error>
benchmark_runner::run_memory_benchmark() {
    return run_benchmark(benchmark_type::memory);
}

std::expected<benchmark_result, performance_error>
benchmark_runner::run_concurrent_benchmark() {
    return run_benchmark(benchmark_type::concurrent);
}

std::expected<benchmark_result, performance_error>
benchmark_runner::run_pool_efficiency_benchmark() {
    return run_benchmark(benchmark_type::pool_efficiency);
}

std::expected<benchmark_result, performance_error>
benchmark_runner::run_thread_scaling_benchmark() {
    return run_benchmark(benchmark_type::thread_scaling);
}

void benchmark_runner::register_benchmark(std::string name,
                                          custom_benchmark_fn benchmark) {
    impl_->custom_benchmarks.emplace_back(std::move(name), std::move(benchmark));
}

std::expected<std::vector<benchmark_result>, performance_error>
benchmark_runner::run_custom_benchmarks() {
    std::vector<benchmark_result> results;
    results.reserve(impl_->custom_benchmarks.size());

    for (const auto& [name, fn] : impl_->custom_benchmarks) {
        if (impl_->cancelled.load()) break;

        try {
            results.push_back(fn());
        } catch (...) {
            benchmark_result error_result;
            error_result.total_errors = 1;
            results.push_back(error_result);
        }
    }

    return results;
}

std::optional<benchmark_result> benchmark_runner::last_result() const {
    return impl_->last_result;
}

std::optional<benchmark_suite_result> benchmark_runner::last_suite_result() const {
    return impl_->last_suite;
}

std::string benchmark_runner::compare_baseline(
    const benchmark_suite_result& baseline) const {
    if (!impl_->last_suite) {
        return "No current results to compare";
    }

    std::ostringstream ss;
    ss << "# Baseline Comparison\n\n";

    const auto& current = *impl_->last_suite;

    ss << "| Benchmark | Baseline | Current | Delta |\n";
    ss << "|-----------|----------|---------|-------|\n";

    for (size_t i = 0;
         i < std::min(baseline.benchmarks.size(), current.benchmarks.size());
         ++i) {
        const auto& b = baseline.benchmarks[i];
        const auto& c = current.benchmarks[i];

        double delta =
            ((c.throughput - b.throughput) / b.throughput) * 100.0;

        ss << "| " << static_cast<int>(b.type) << " | " << std::fixed
           << std::setprecision(0) << b.throughput << " | " << c.throughput
           << " | " << std::showpos << std::setprecision(1) << delta
           << "% |\n";
    }

    return ss.str();
}

std::expected<void, performance_error>
benchmark_runner::save_results(const std::string& path,
                               const std::string& format) {
    if (!impl_->last_suite) {
        return std::unexpected(performance_error::not_initialized);
    }

    std::ofstream file(path);
    if (!file) {
        return std::unexpected(performance_error::allocation_failed);
    }

    if (format == "json") {
        file << impl_->last_suite->to_json();
    } else if (format == "markdown") {
        file << impl_->last_suite->to_markdown();
    } else {
        file << impl_->last_suite->to_text();
    }

    return {};
}

std::expected<benchmark_suite_result, performance_error>
benchmark_runner::load_baseline(const std::string& path) {
    // Would parse JSON file
    return std::unexpected(performance_error::not_initialized);
}

void benchmark_runner::cancel() {
    impl_->cancelled.store(true);
}

bool benchmark_runner::is_cancelled() const noexcept {
    return impl_->cancelled.load();
}

}  // namespace pacs::bridge::performance
