/**
 * @file adapter_benchmark.cpp
 * @brief Performance benchmarks for pacs_bridge adapter implementations
 *
 * Measures throughput, latency, and scalability of each adapter:
 * - Database adapter (SQLite in-memory)
 * - Thread adapter (worker pool)
 * - Executor adapter (simple_executor)
 * - PACS adapter (stub MPPS/MWL/Storage)
 * - MWL adapter (memory_mwl_adapter)
 * - Concurrent stress (multi-threaded adapter access)
 *
 * Uses the same custom benchmark framework pattern as benchmark_suite_test.cpp.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/322
 */

// Note: mwl_adapter.h and pacs_adapter.h both define class mwl_adapter in the
// same namespace, causing ODR violation if included together. This file uses
// mwl_adapter.h for standalone memory MWL adapter tests. PACS adapter tests
// (MPPS, storage) are in baseline_benchmark.cpp which uses pacs_adapter.h.
#include "pacs/bridge/integration/database_adapter.h"
#ifndef PACS_BRIDGE_STANDALONE_BUILD
#include "pacs/bridge/integration/executor_adapter.h"
#endif
#include "pacs/bridge/integration/mwl_adapter.h"
#include "pacs/bridge/integration/thread_adapter.h"
#include "pacs/bridge/performance/benchmark_runner.h"
#include "pacs/bridge/performance/performance_types.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::benchmark::adapter {

// =============================================================================
// Test Utilities (same pattern as benchmark_suite_test.cpp)
// =============================================================================

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"   \
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
            auto duration =                                                    \
                std::chrono::duration_cast<std::chrono::milliseconds>(         \
                    end - start);                                              \
            std::cout << "  PASSED (" << duration.count() << "ms)"            \
                      << std::endl;                                            \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "  FAILED" << std::endl;                              \
            failed++;                                                          \
        }                                                                      \
    } while (0)

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
                   ? (static_cast<double>(successful_operations) /
                      total_operations) *
                         100.0
                   : 0.0;
    }

    double throughput_per_second() const {
        auto total_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(total_time)
                .count();
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

    void record_operation(std::chrono::microseconds latency, bool success) {
        total_operations++;
        latencies.push_back(latency);
        if (latency < min_latency) min_latency = latency;
        if (latency > max_latency) max_latency = latency;
        if (success) {
            successful_operations++;
        } else {
            failed_operations++;
        }
    }

    void print_summary(const std::string& test_name) const {
        std::cout << "\n  " << test_name << " Results:" << std::endl;
        std::cout << "    Total Operations:    " << total_operations
                  << std::endl;
        std::cout << "    Successful:          " << successful_operations
                  << std::endl;
        std::cout << "    Failed:              " << failed_operations
                  << std::endl;
        std::cout << "    Success Rate:        " << std::fixed
                  << std::setprecision(2) << success_rate() << "%"
                  << std::endl;
        std::cout << "    Throughput:          " << std::fixed
                  << std::setprecision(2) << throughput_per_second()
                  << " ops/sec" << std::endl;
        std::cout << "    Avg Latency:         " << avg_latency().count()
                  << " us" << std::endl;
        std::cout << "    Min Latency:         " << min_latency.count()
                  << " us" << std::endl;
        std::cout << "    Max Latency:         " << max_latency.count()
                  << " us" << std::endl;
        std::cout << "    P50 Latency:         " << percentile(50).count()
                  << " us" << std::endl;
        std::cout << "    P95 Latency:         " << percentile(95).count()
                  << " us" << std::endl;
        std::cout << "    P99 Latency:         " << percentile(99).count()
                  << " us" << std::endl;
    }
};

// =============================================================================
// Helper: Generate Test Data
// =============================================================================

static mapping::mwl_item make_test_mwl(int index) {
    mapping::mwl_item item;
    item.patient.patient_id = "PAT" + std::to_string(index);
    item.patient.patient_name = "TEST^PATIENT^" + std::to_string(index);
    item.patient.patient_birth_date = "19800101";
    item.patient.patient_sex = "M";
    item.imaging_service_request.accession_number =
        "ACC" + std::to_string(index);
    item.imaging_service_request.requesting_physician = "DR^SMITH";
    item.requested_procedure.requested_procedure_id =
        "REQ" + std::to_string(index);
    item.requested_procedure.requested_procedure_description =
        "CT Head Without Contrast";
    item.requested_procedure.study_instance_uid =
        "1.2.840.113619.2.55.3.888." + std::to_string(index);
    item.requested_procedure.referring_physician_name = "DR^SMITH";

    mapping::dicom_scheduled_procedure_step sps;
    sps.scheduled_station_ae_title = "CT_SCANNER_1";
    sps.scheduled_start_date = "20240115";
    sps.scheduled_start_time = "120000";
    sps.modality = "CT";
    sps.scheduled_step_id = "SPS" + std::to_string(index);
    sps.scheduled_step_description = "CT Head";
    item.scheduled_steps.push_back(std::move(sps));

    return item;
}

// =============================================================================
// Database Adapter Benchmarks
// =============================================================================

/**
 * @brief Benchmark database execute() with simple queries
 */
bool test_database_execute() {
    auto db = integration::create_database_adapter(
        {.database_path = ":memory:"});
    TEST_ASSERT(db != nullptr, "Database adapter should be created");

    auto conn_result = db->acquire_connection();
    TEST_ASSERT(conn_result.has_value(), "Should acquire connection");
    auto& conn = *conn_result.value();

    // Create test table
    auto schema = conn.execute(
        "CREATE TABLE IF NOT EXISTS bench_test "
        "(id INTEGER PRIMARY KEY, name TEXT, value REAL)");
    TEST_ASSERT(schema.has_value(), "Schema creation should succeed");

    const int iterations = 5000;
    benchmark_stats stats;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto op_start = std::chrono::high_resolution_clock::now();
        std::string sql = "INSERT INTO bench_test VALUES(" +
                          std::to_string(i) + ", 'name" + std::to_string(i) +
                          "', " + std::to_string(i * 1.5) + ")";
        auto result = conn.execute(sql);
        auto op_end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);
        stats.record_operation(latency, result.has_value());
    }
    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("Database execute() Benchmark");

    db->release_connection(conn_result.value());

    TEST_ASSERT(stats.success_rate() >= 99.0,
                "Execute success rate should be >= 99%");
    TEST_ASSERT(stats.throughput_per_second() > 100,
                "Should execute > 100 inserts/sec");
    return true;
}

/**
 * @brief Benchmark database prepared statements with bind
 */
bool test_database_prepared_statement() {
    auto db = integration::create_database_adapter(
        {.database_path = ":memory:"});
    TEST_ASSERT(db != nullptr, "Database adapter should be created");

    auto conn_result = db->acquire_connection();
    TEST_ASSERT(conn_result.has_value(), "Should acquire connection");
    auto& conn = *conn_result.value();

    (void)conn.execute(
        "CREATE TABLE IF NOT EXISTS bench_prep "
        "(id INTEGER PRIMARY KEY, name TEXT, value REAL)");

    auto stmt_result =
        conn.prepare("INSERT INTO bench_prep VALUES(?, ?, ?)");
    TEST_ASSERT(stmt_result.has_value(), "Prepare should succeed");
    auto& stmt = *stmt_result.value();

    const int iterations = 5000;
    benchmark_stats stats;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto op_start = std::chrono::high_resolution_clock::now();
        (void)stmt.bind_int64(1, i);
        (void)stmt.bind_string(2, "name" + std::to_string(i));
        (void)stmt.bind_double(3, i * 1.5);
        auto result = stmt.execute();
        (void)stmt.reset();
        (void)stmt.clear_bindings();
        auto op_end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);
        stats.record_operation(latency, result.has_value());
    }
    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("Database prepared statement Benchmark");

    db->release_connection(conn_result.value());

    TEST_ASSERT(stats.success_rate() >= 99.0,
                "Prepared statement success rate should be >= 99%");
    TEST_ASSERT(stats.throughput_per_second() > 100,
                "Should execute > 100 prepared inserts/sec");
    return true;
}

/**
 * @brief Benchmark database transaction batching
 */
bool test_database_transactions() {
    auto db = integration::create_database_adapter(
        {.database_path = ":memory:"});
    TEST_ASSERT(db != nullptr, "Database adapter should be created");

    auto conn_result = db->acquire_connection();
    TEST_ASSERT(conn_result.has_value(), "Should acquire connection");
    auto& conn = *conn_result.value();

    (void)conn.execute(
        "CREATE TABLE IF NOT EXISTS bench_txn "
        "(id INTEGER PRIMARY KEY, data TEXT)");

    const int batches = 100;
    const int rows_per_batch = 50;
    benchmark_stats stats;

    auto start = std::chrono::high_resolution_clock::now();
    for (int b = 0; b < batches; ++b) {
        auto op_start = std::chrono::high_resolution_clock::now();

        auto txn = conn.begin_transaction();
        bool ok = txn.has_value();
        for (int r = 0; r < rows_per_batch && ok; ++r) {
            int id = b * rows_per_batch + r;
            std::string sql = "INSERT INTO bench_txn VALUES(" +
                              std::to_string(id) + ", 'data" +
                              std::to_string(id) + "')";
            auto res = conn.execute(sql);
            ok = res.has_value();
        }
        if (ok) {
            ok = conn.commit().has_value();
        } else {
            (void)conn.rollback();
        }

        auto op_end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);
        stats.record_operation(latency, ok);
    }
    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("Database transaction Benchmark (" +
                        std::to_string(rows_per_batch) + " rows/batch)");

    db->release_connection(conn_result.value());

    TEST_ASSERT(stats.success_rate() >= 99.0,
                "Transaction success rate should be >= 99%");
    return true;
}

/**
 * @brief Benchmark connection pool acquire/release cycle
 */
bool test_database_connection_pool() {
    auto db = integration::create_database_adapter(
        {.database_path = ":memory:", .pool_size = 5});
    TEST_ASSERT(db != nullptr, "Database adapter should be created");

    const int iterations = 2000;
    benchmark_stats stats;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto op_start = std::chrono::high_resolution_clock::now();
        auto conn = db->acquire_connection();
        bool ok = conn.has_value();
        if (ok) {
            db->release_connection(conn.value());
        }
        auto op_end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);
        stats.record_operation(latency, ok);
    }
    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("Database connection pool acquire/release Benchmark");

    // Also show nanosecond-level timing since this operation is sub-microsecond
    auto total_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    if (total_ns.count() > 0) {
        double ns_per_op =
            static_cast<double>(total_ns.count()) / iterations;
        std::cout << "    Avg (ns):            " << std::fixed
                  << std::setprecision(0) << ns_per_op << " ns" << std::endl;
    }

    TEST_ASSERT(stats.success_rate() >= 99.0,
                "Pool acquire/release success rate should be >= 99%");
    return true;
}

// =============================================================================
// Thread Adapter Benchmarks
// =============================================================================

/**
 * @brief Benchmark thread adapter task submission and completion
 */
bool test_thread_adapter_submit() {
    auto adapter = integration::create_thread_adapter();
    TEST_ASSERT(adapter != nullptr, "Thread adapter should be created");

    integration::worker_pool_config config;
    config.name = "bench_pool";
    config.min_threads = 4;
    config.max_threads = 8;
    TEST_ASSERT(adapter->initialize(config), "Should initialize");

    const int iterations = 5000;
    benchmark_stats stats;
    std::atomic<int> completed{0};

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<void>> futures;
    futures.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto op_start = std::chrono::high_resolution_clock::now();
        auto future = adapter->submit([&completed]() { completed++; },
                                      integration::task_priority::normal);
        auto op_end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);
        stats.record_operation(latency, future.valid());

        if (future.valid()) {
            futures.push_back(std::move(future));
        }
    }

    // Wait for all tasks
    for (auto& f : futures) {
        f.wait();
    }
    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("Thread adapter submit Benchmark");
    std::cout << "    Tasks completed:     " << completed.load() << "/"
              << iterations << std::endl;

    adapter->shutdown(true);

    TEST_ASSERT(stats.success_rate() >= 99.0,
                "Submit success rate should be >= 99%");
    TEST_ASSERT(completed.load() == iterations,
                "All tasks should have completed");
    return true;
}

/**
 * @brief Benchmark thread adapter scaling across different thread counts
 */
bool test_thread_adapter_scaling() {
    std::vector<size_t> thread_counts = {1, 2, 4, 8};
    const int tasks_per_test = 2000;

    std::cout << "\n  Thread Scaling Results:" << std::endl;

    for (size_t tc : thread_counts) {
        auto adapter = integration::create_thread_adapter();
        TEST_ASSERT(adapter != nullptr, "Thread adapter should be created");

        integration::worker_pool_config config;
        config.name = "scale_pool";
        config.min_threads = tc;
        config.max_threads = tc;
        TEST_ASSERT(adapter->initialize(config), "Should initialize");

        std::atomic<int> completed{0};
        std::vector<std::future<void>> futures;
        futures.reserve(tasks_per_test);

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < tasks_per_test; ++i) {
            auto future = adapter->submit(
                [&completed]() {
                    // Simulate light work
                    volatile int sum = 0;
                    for (int j = 0; j < 100; ++j) sum += j;
                    (void)sum;
                    completed++;
                },
                integration::task_priority::normal);
            if (future.valid()) {
                futures.push_back(std::move(future));
            }
        }

        for (auto& f : futures) {
            f.wait();
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        double throughput =
            static_cast<double>(tasks_per_test) / (duration.count() / 1000.0);

        std::cout << "    " << tc
                  << " threads: " << std::fixed << std::setprecision(0)
                  << throughput << " tasks/sec (" << duration.count() << "ms)"
                  << std::endl;

        adapter->shutdown(true);
    }

    return true;
}

// =============================================================================
// Executor Adapter Benchmarks (requires kcenon common_system)
// =============================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD
/**
 * @brief Benchmark simple_executor task execution throughput
 */
bool test_executor_throughput() {
    auto executor = std::make_shared<integration::simple_executor>(4);
    TEST_ASSERT(executor->is_running(), "Executor should be running");

    const int iterations = 5000;
    benchmark_stats stats;
    std::atomic<int> completed{0};
    std::vector<std::future<void>> futures;
    futures.reserve(iterations);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto op_start = std::chrono::high_resolution_clock::now();
        auto job = std::make_unique<integration::lambda_job>(
            [&completed]() { completed++; }, "bench_job_" + std::to_string(i));
        auto result = executor->execute(std::move(job));
        auto op_end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);
        bool ok = result.is_ok();
        stats.record_operation(latency, ok);

        if (ok) {
            futures.push_back(std::move(result.value()));
        }
    }

    for (auto& f : futures) {
        f.wait();
    }
    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("Executor throughput Benchmark");
    std::cout << "    Tasks completed:     " << completed.load() << "/"
              << iterations << std::endl;

    executor->shutdown(true);

    TEST_ASSERT(stats.success_rate() >= 99.0,
                "Execute success rate should be >= 99%");
    TEST_ASSERT(completed.load() == iterations,
                "All tasks should have completed");
    return true;
}
#endif  // !PACS_BRIDGE_STANDALONE_BUILD

// =============================================================================
// MWL Adapter (Memory) Benchmarks
// =============================================================================

/**
 * @brief Benchmark memory MWL adapter add_item throughput
 */
bool test_mwl_adapter_add() {
    auto mwl = integration::create_mwl_adapter("");
    TEST_ASSERT(mwl != nullptr, "MWL adapter should be created");

    const int iterations = 5000;
    benchmark_stats stats;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto item = make_test_mwl(i);
        auto op_start = std::chrono::high_resolution_clock::now();
        auto result = mwl->add_item(item);
        auto op_end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);
        stats.record_operation(latency, result.has_value());
    }
    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("MWL add_item Benchmark");

    TEST_ASSERT(stats.success_rate() >= 99.0,
                "MWL add success rate should be >= 99%");
    TEST_ASSERT(stats.throughput_per_second() > 1000,
                "Should add > 1000 items/sec");
    return true;
}

/**
 * @brief Benchmark memory MWL adapter query_items with filters
 */
bool test_mwl_adapter_query() {
    auto mwl = integration::create_mwl_adapter("");
    TEST_ASSERT(mwl != nullptr, "MWL adapter should be created");

    // Populate data
    for (int i = 0; i < 500; ++i) {
        auto item = make_test_mwl(i);
        (void)mwl->add_item(item);
    }

    const int iterations = 2000;
    benchmark_stats stats;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        integration::mwl_query_filter filter;
        filter.patient_id = "PAT" + std::to_string(i % 500);

        auto op_start = std::chrono::high_resolution_clock::now();
        auto result = mwl->query_items(filter);
        auto op_end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);
        stats.record_operation(latency, result.has_value());
    }
    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("MWL query_items Benchmark (500 items, filtered)");

    TEST_ASSERT(stats.success_rate() >= 99.0,
                "MWL query success rate should be >= 99%");
    return true;
}

/**
 * @brief Benchmark memory MWL adapter get_item by accession number
 */
bool test_mwl_adapter_get() {
    auto mwl = integration::create_mwl_adapter("");
    TEST_ASSERT(mwl != nullptr, "MWL adapter should be created");

    // Populate data
    for (int i = 0; i < 1000; ++i) {
        auto item = make_test_mwl(i);
        (void)mwl->add_item(item);
    }

    const int iterations = 5000;
    benchmark_stats stats;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        std::string acc = "ACC" + std::to_string(i % 1000);

        auto op_start = std::chrono::high_resolution_clock::now();
        auto result = mwl->get_item(acc);
        auto op_end = std::chrono::high_resolution_clock::now();

        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            op_end - op_start);
        stats.record_operation(latency, result.has_value());
    }
    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    stats.print_summary("MWL get_item Benchmark (1000 items)");

    TEST_ASSERT(stats.success_rate() >= 99.0,
                "MWL get success rate should be >= 99%");
    // Note: operation may be sub-microsecond, so throughput_per_second()
    // can report 0 when total_time rounds down to 0us. Check via nanoseconds.
    auto total_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    TEST_ASSERT(total_ns.count() > 0, "Should complete in finite time");
    return true;
}

// =============================================================================
// Concurrent Adapter Stress Benchmarks
// =============================================================================

/**
 * @brief Stress test database adapter from multiple threads
 */
bool test_concurrent_database() {
    // Use pool_size=1: SQLite :memory: creates a separate database per
    // connection, so all threads must share one connection to see the same
    // tables.
    auto db = integration::create_database_adapter(
        {.database_path = ":memory:", .pool_size = 1});
    TEST_ASSERT(db != nullptr, "Database adapter should be created");

    // Create table
    auto conn = db->acquire_connection();
    TEST_ASSERT(conn.has_value(), "Should acquire connection");
    (void)conn.value()->execute(
        "CREATE TABLE IF NOT EXISTS bench_concurrent "
        "(id INTEGER PRIMARY KEY, thread_id INTEGER, data TEXT)");
    db->release_connection(conn.value());

    const int num_threads = 4;
    const int ops_per_thread = 500;
    std::atomic<int> successful{0};
    std::atomic<int> failed{0};
    std::atomic<int> pool_waits{0};

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<void>> futures;

    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [&, t]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                auto c = db->acquire_connection();
                if (!c.has_value()) {
                    pool_waits++;
                    failed++;
                    continue;
                }
                int id = t * ops_per_thread + i;
                std::string sql =
                    "INSERT OR REPLACE INTO bench_concurrent VALUES(" +
                    std::to_string(id) + ", " + std::to_string(t) +
                    ", 'data" + std::to_string(id) + "')";
                auto result = c.value()->execute(sql);
                if (result.has_value()) {
                    successful++;
                } else {
                    failed++;
                }
                db->release_connection(c.value());
            }
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    int total = num_threads * ops_per_thread;
    double throughput =
        duration.count() > 0
            ? static_cast<double>(total) / (duration.count() / 1000.0)
            : 0.0;
    double success_rate =
        (static_cast<double>(successful) / total) * 100.0;

    std::cout << "\n  Concurrent Database Results:" << std::endl;
    std::cout << "    Threads:         " << num_threads << std::endl;
    std::cout << "    Ops/Thread:      " << ops_per_thread << std::endl;
    std::cout << "    Successful:      " << successful << "/" << total
              << std::endl;
    std::cout << "    Pool waits:      " << pool_waits << std::endl;
    std::cout << "    Success Rate:    " << std::fixed << std::setprecision(2)
              << success_rate << "%" << std::endl;
    std::cout << "    Duration:        " << duration.count() << "ms"
              << std::endl;
    std::cout << "    Throughput:      " << std::fixed << std::setprecision(0)
              << throughput << " ops/sec" << std::endl;

    TEST_ASSERT(successful > 0, "At least some operations should succeed");
    return true;
}

/**
 * @brief Stress test MWL adapter with concurrent add + query from multiple threads
 */
bool test_concurrent_mwl() {
    auto mwl = integration::create_mwl_adapter("");
    TEST_ASSERT(mwl != nullptr, "MWL adapter should be created");

    const int num_threads = 4;
    const int ops_per_thread = 500;
    std::atomic<int> add_success{0};
    std::atomic<int> query_success{0};

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<void>> futures;

    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [&, t]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                int idx = t * ops_per_thread + i;

                // MWL add
                auto item = make_test_mwl(idx);
                if (mwl->add_item(item).has_value()) {
                    add_success++;
                }

                // MWL query
                integration::mwl_query_filter filter;
                filter.patient_id = "PAT" + std::to_string(idx);
                auto result = mwl->query_items(filter);
                if (result.has_value()) {
                    query_success++;
                }
            }
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    int total = num_threads * ops_per_thread;
    double throughput =
        static_cast<double>(total * 2) / (duration.count() / 1000.0);

    std::cout << "\n  Concurrent MWL add + query Results:" << std::endl;
    std::cout << "    Threads:         " << num_threads << std::endl;
    std::cout << "    Add Successful:  " << add_success << "/" << total
              << std::endl;
    std::cout << "    Query Successful:" << query_success << "/" << total
              << std::endl;
    std::cout << "    Duration:        " << duration.count() << "ms"
              << std::endl;
    std::cout << "    Combined Throughput: " << std::fixed
              << std::setprecision(0) << throughput << " ops/sec" << std::endl;

    TEST_ASSERT(add_success.load() >= total * 0.9,
                "MWL concurrent add success rate should be >= 90%");
    return true;
}

// =============================================================================
// MWL Baseline Comparison (Direct vs Adapter)
//
// This baseline comparison lives here (not in baseline_benchmark.cpp) because
// mwl_adapter.h and pacs_adapter.h both define class mwl_adapter in the same
// namespace, causing an ODR violation when included together. Since this file
// already includes mwl_adapter.h, the MWL baseline comparison is placed here.
// =============================================================================

struct comparison_result {
    std::string label;
    double direct_ns;
    double adapter_ns;

    double overhead_percent() const {
        return direct_ns > 0
                   ? ((adapter_ns - direct_ns) / direct_ns) * 100.0
                   : 0.0;
    }

    void print() const {
        std::cout << "    " << std::left << std::setw(24) << label << " | "
                  << std::right << std::setw(10) << std::fixed
                  << std::setprecision(0) << direct_ns << " ns"
                  << " | " << std::setw(10) << adapter_ns << " ns"
                  << " | " << std::setw(8) << std::setprecision(1)
                  << overhead_percent() << "%" << std::endl;
    }
};

static void print_comparison_header(const std::string& section) {
    std::cout << "\n  " << section << ":" << std::endl;
    std::cout << "    " << std::left << std::setw(24) << "Operation"
              << " | " << std::right << std::setw(13) << "Direct"
              << " | " << std::setw(13) << "Adapter"
              << " | " << std::setw(9) << "Overhead" << std::endl;
    std::cout << "    " << std::string(24, '-') << "-+-" << std::string(13, '-')
              << "-+-" << std::string(13, '-') << "-+-"
              << std::string(9, '-') << std::endl;
}

/**
 * @brief Compare direct unordered_map operations vs memory_mwl_adapter
 *
 * Direct: unordered_map + manual linear scan for filtering
 * Adapter: memory_mwl_adapter (add_item / query_items / get_item)
 *
 * Measures the overhead of adapter abstraction for MWL CRUD operations.
 */
bool test_baseline_mwl() {
    using namespace performance;

    const size_t warmup = 100;
    const size_t iterations = 5000;
    const size_t data_size = 500;

    // ---- Setup: Direct implementation ----
    struct direct_entry {
        std::string accession;
        std::string patient_id;
        mapping::mwl_item item;
    };
    std::unordered_map<std::string, direct_entry> direct_map;
    std::mutex direct_mu;

    for (size_t i = 0; i < data_size; ++i) {
        auto item = make_test_mwl(static_cast<int>(i));
        std::string acc = item.imaging_service_request.accession_number;
        direct_map.emplace(
            acc, direct_entry{acc, item.patient.patient_id, item});
    }

    // ---- Setup: Adapter ----
    auto mwl = integration::create_mwl_adapter("");
    for (size_t i = 0; i < data_size; ++i) {
        (void)mwl->add_item(make_test_mwl(static_cast<int>(i)));
    }

    // ---- Benchmark: add_item ----
    auto direct_add_avg = benchmark_with_warmup(
        [&, idx = data_size]() mutable {
            auto item = make_test_mwl(static_cast<int>(idx));
            std::string acc = item.imaging_service_request.accession_number;
            std::lock_guard lock(direct_mu);
            direct_map.emplace(
                acc, direct_entry{acc, item.patient.patient_id, item});
            idx++;
        },
        warmup, iterations);

    auto adapter_add_avg = benchmark_with_warmup(
        [&, idx = data_size]() mutable {
            (void)mwl->add_item(make_test_mwl(static_cast<int>(idx++)));
        },
        warmup, iterations);

    // ---- Benchmark: query by patient_id ----
    auto direct_query_avg = benchmark_with_warmup(
        [&, idx = size_t{0}]() mutable {
            std::string pat_id = "PAT" + std::to_string(idx++ % data_size);
            std::lock_guard lock(direct_mu);
            std::vector<mapping::mwl_item> results;
            for (const auto& [k, v] : direct_map) {
                if (v.patient_id == pat_id) {
                    results.push_back(v.item);
                }
            }
            (void)results.size();
        },
        warmup, iterations);

    auto adapter_query_avg = benchmark_with_warmup(
        [&, idx = size_t{0}]() mutable {
            integration::mwl_query_filter filter;
            filter.patient_id = "PAT" + std::to_string(idx++ % data_size);
            (void)mwl->query_items(filter);
        },
        warmup, iterations);

    // ---- Benchmark: get by accession number ----
    auto direct_get_avg = benchmark_with_warmup(
        [&, idx = size_t{0}]() mutable {
            std::string acc = "ACC" + std::to_string(idx++ % data_size);
            std::lock_guard lock(direct_mu);
            (void)direct_map.find(acc);
        },
        warmup, iterations);

    auto adapter_get_avg = benchmark_with_warmup(
        [&, idx = size_t{0}]() mutable {
            (void)mwl->get_item(
                "ACC" + std::to_string(idx++ % data_size));
        },
        warmup, iterations);

    // ---- Print results ----
    print_comparison_header("MWL Baseline Comparison");

    comparison_result r1{
        "add_item/emplace",
        static_cast<double>(direct_add_avg.count()),
        static_cast<double>(adapter_add_avg.count())};
    r1.print();

    comparison_result r2{
        "query/linear scan",
        static_cast<double>(direct_query_avg.count()),
        static_cast<double>(adapter_query_avg.count())};
    r2.print();

    comparison_result r3{
        "get_item/find",
        static_cast<double>(direct_get_avg.count()),
        static_cast<double>(adapter_get_avg.count())};
    r3.print();

    std::cout
        << "\n    Note: Adapter includes validation + mutex + optional filter "
           "matching"
        << std::endl;
    return true;
}

}  // namespace pacs::bridge::benchmark::adapter

// =============================================================================
// Main
// =============================================================================

int main() {
    using namespace pacs::bridge::benchmark::adapter;

    std::cout << "=============================================" << std::endl;
    std::cout << "PACS Bridge Adapter Performance Benchmarks" << std::endl;
    std::cout << "Issue #287: Phase 5 Comprehensive Testing" << std::endl;
    std::cout << "=============================================" << std::endl;

    int passed = 0;
    int failed = 0;

    // Database Adapter Benchmarks
    std::cout << "\n--- Database Adapter Benchmarks ---" << std::endl;
    RUN_TEST(test_database_execute);
    RUN_TEST(test_database_prepared_statement);
    RUN_TEST(test_database_transactions);
    RUN_TEST(test_database_connection_pool);

    // Thread Adapter Benchmarks
    std::cout << "\n--- Thread Adapter Benchmarks ---" << std::endl;
    RUN_TEST(test_thread_adapter_submit);
    RUN_TEST(test_thread_adapter_scaling);

    // Executor Adapter Benchmarks (requires kcenon ecosystem)
#ifndef PACS_BRIDGE_STANDALONE_BUILD
    std::cout << "\n--- Executor Adapter Benchmarks ---" << std::endl;
    RUN_TEST(test_executor_throughput);
#else
    std::cout << "\n--- Executor Adapter Benchmarks (skipped: standalone build) ---" << std::endl;
#endif

    // MWL Adapter (Memory) Benchmarks
    std::cout << "\n--- MWL Adapter (Memory) Benchmarks ---" << std::endl;
    RUN_TEST(test_mwl_adapter_add);
    RUN_TEST(test_mwl_adapter_query);
    RUN_TEST(test_mwl_adapter_get);

    // Concurrent Stress Benchmarks
    std::cout << "\n--- Concurrent Adapter Stress Benchmarks ---" << std::endl;
    RUN_TEST(test_concurrent_database);
    RUN_TEST(test_concurrent_mwl);

    // MWL Baseline Comparison (resolves ODR conflict with pacs_adapter.h)
    std::cout << "\n--- MWL Baseline Comparison ---" << std::endl;
    RUN_TEST(test_baseline_mwl);

    // Summary
    std::cout << "\n=============================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << std::endl;
    std::cout << "=============================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
