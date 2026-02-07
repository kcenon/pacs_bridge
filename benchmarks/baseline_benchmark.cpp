/**
 * @file baseline_benchmark.cpp
 * @brief Baseline comparison benchmarks for adapter overhead measurement
 *
 * Compares adapter abstraction overhead against direct implementation:
 * - Database: direct sqlite3_exec() vs. database_adapter->execute()
 * - Thread: std::async vs. simple_executor->execute()
 * - MPPS: std::unordered_map vs. stub_mpps_adapter
 * - MWL: std::unordered_map + filter vs. memory_mwl_adapter
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/322
 */

// Note: This file uses pacs_adapter.h only (for MPPS/storage baseline).
// mwl_adapter.h is NOT included here to avoid ODR conflict (both headers
// define class mwl_adapter in the same namespace).
#include "pacs/bridge/integration/database_adapter.h"
#ifndef PACS_BRIDGE_STANDALONE_BUILD
#include "pacs/bridge/integration/executor_adapter.h"
#endif
#include "pacs/bridge/integration/pacs_adapter.h"
#include "pacs/bridge/performance/benchmark_runner.h"
#include "pacs/bridge/performance/performance_types.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::benchmark::baseline {

// =============================================================================
// Test Utilities
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
// Comparison Result
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

// =============================================================================
// Database Baseline
// =============================================================================

/**
 * @brief Compare direct database operations vs adapter
 *
 * Direct: create adapter, acquire connection, execute SQL directly
 * Adapter: create adapter, acquire connection, execute via connection API
 *
 * Both use the same underlying SQLite engine. The overhead measured is
 * the adapter's connection pooling and error wrapping layers.
 */
bool test_baseline_database() {
    using namespace performance;

    const size_t warmup = 100;
    const size_t iterations = 5000;

    // Setup: create two separate databases
    auto db_direct = integration::create_database_adapter(
        {.database_path = ":memory:"});
    auto db_adapter = integration::create_database_adapter(
        {.database_path = ":memory:"});
    TEST_ASSERT(db_direct && db_adapter, "Both databases should be created");

    auto conn_d = db_direct->acquire_connection();
    auto conn_a = db_adapter->acquire_connection();
    TEST_ASSERT(conn_d.has_value() && conn_a.has_value(),
                "Both connections should be acquired");

    (void)conn_d.value()->execute(
        "CREATE TABLE bench(id INTEGER PRIMARY KEY, val TEXT)");
    (void)conn_a.value()->execute(
        "CREATE TABLE bench(id INTEGER PRIMARY KEY, val TEXT)");

    // Direct: minimal execute path
    auto direct_avg = benchmark_with_warmup(
        [&, idx = size_t{0}]() mutable {
            auto id = idx++;
            std::string sql = "INSERT OR REPLACE INTO bench VALUES(" +
                              std::to_string(id % iterations) + ", 'v" +
                              std::to_string(id) + "')";
            (void)conn_d.value()->execute(sql);
        },
        warmup, iterations);

    // Adapter: acquire/release + execute
    auto adapter_avg = benchmark_with_warmup(
        [&, idx = size_t{0}]() mutable {
            auto c = db_adapter->acquire_connection();
            if (c.has_value()) {
                auto id = idx++;
                std::string sql = "INSERT OR REPLACE INTO bench VALUES(" +
                                  std::to_string(id % iterations) + ", 'v" +
                                  std::to_string(id) + "')";
                (void)c.value()->execute(sql);
                db_adapter->release_connection(c.value());
            }
        },
        warmup, iterations);

    db_direct->release_connection(conn_d.value());
    db_adapter->release_connection(conn_a.value());

    print_comparison_header("Database Baseline Comparison");
    comparison_result r{
        "INSERT (execute)",
        static_cast<double>(direct_avg.count()),
        static_cast<double>(adapter_avg.count())};
    r.print();

    std::cout << "\n    Note: Adapter path includes pool acquire/release"
              << std::endl;
    return true;
}

// =============================================================================
// Thread/Executor Baseline (requires kcenon common_system)
// =============================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD
/**
 * @brief Compare std::async vs simple_executor
 *
 * Direct: std::async(std::launch::async, fn)
 * Adapter: simple_executor->execute(lambda_job)
 */
bool test_baseline_executor() {
    using namespace performance;

    const size_t warmup = 50;
    const size_t iterations = 2000;
    std::atomic<int> counter_d{0};
    std::atomic<int> counter_a{0};

    // Direct: std::async
    auto direct_avg = benchmark_with_warmup(
        [&]() {
            auto fut = std::async(std::launch::async,
                                  [&counter_d]() { counter_d++; });
            fut.wait();
        },
        warmup, iterations);

    // Adapter: simple_executor
    auto executor = std::make_shared<integration::simple_executor>(4);
    auto adapter_avg = benchmark_with_warmup(
        [&]() {
            auto job = std::make_unique<integration::lambda_job>(
                [&counter_a]() { counter_a++; }, "bench");
            auto result = executor->execute(std::move(job));
            if (result.is_ok()) {
                result.value().wait();
            }
        },
        warmup, iterations);
    executor->shutdown(true);

    print_comparison_header("Executor Baseline Comparison");
    comparison_result r{
        "submit + wait",
        static_cast<double>(direct_avg.count()),
        static_cast<double>(adapter_avg.count())};
    r.print();

    std::cout << "\n    Note: Executor reuses thread pool; std::async creates "
                 "threads on demand"
              << std::endl;
    return true;
}
#endif  // !PACS_BRIDGE_STANDALONE_BUILD

// =============================================================================
// MPPS Baseline
// =============================================================================

/**
 * @brief Compare direct unordered_map vs stub MPPS adapter
 *
 * Direct: unordered_map::emplace() / find()
 * Adapter: mpps_adapter->create_mpps() / get_mpps()
 */
bool test_baseline_mpps() {
    using namespace performance;

    const size_t warmup = 100;
    const size_t iterations = 5000;

    // Direct: unordered_map
    struct simple_mpps {
        std::string uid;
        std::string patient_id;
        std::string status;
    };
    std::unordered_map<std::string, simple_mpps> direct_map;
    std::mutex direct_mu;

    auto direct_create_avg = benchmark_with_warmup(
        [&, idx = size_t{0}]() mutable {
            std::string uid =
                "1.2.840.999." + std::to_string(idx++);
            std::lock_guard lock(direct_mu);
            direct_map.emplace(uid,
                               simple_mpps{uid, "PAT" + std::to_string(idx),
                                           "IN PROGRESS"});
        },
        warmup, iterations);

    auto direct_get_avg = benchmark_with_warmup(
        [&, idx = size_t{0}]() mutable {
            std::string uid =
                "1.2.840.999." + std::to_string(idx++ % iterations);
            std::lock_guard lock(direct_mu);
            (void)direct_map.find(uid);
        },
        warmup, iterations);

    // Adapter: stub MPPS
    auto pacs = integration::create_pacs_adapter({});
    auto mpps = pacs->get_mpps_adapter();

    auto adapter_create_avg = benchmark_with_warmup(
        [&, idx = size_t{0}]() mutable {
            integration::mpps_record record;
            record.sop_instance_uid =
                "1.2.840.888." + std::to_string(idx++);
            record.patient_id = "PAT" + std::to_string(idx);
            record.status = "IN PROGRESS";
            record.performed_station_ae_title = "CT1";
            record.start_datetime = std::chrono::system_clock::now();
            (void)mpps->create_mpps(record);
        },
        warmup, iterations);

    auto adapter_get_avg = benchmark_with_warmup(
        [&, idx = size_t{0}]() mutable {
            (void)mpps->get_mpps(
                "1.2.840.888." + std::to_string(idx++ % iterations));
        },
        warmup, iterations);

    print_comparison_header("MPPS Baseline Comparison");
    comparison_result r1{
        "create/emplace",
        static_cast<double>(direct_create_avg.count()),
        static_cast<double>(adapter_create_avg.count())};
    r1.print();

    comparison_result r2{
        "get/find",
        static_cast<double>(direct_get_avg.count()),
        static_cast<double>(adapter_get_avg.count())};
    r2.print();

    std::cout
        << "\n    Note: Adapter includes validation + mutex + record copying"
        << std::endl;
    return true;
}

// Note: MWL baseline comparison is in adapter_benchmark.cpp (test_baseline_mwl)
// because mwl_adapter.h and pacs_adapter.h both define class mwl_adapter in
// the same namespace, causing an ODR violation if included together.

// =============================================================================
// Performance Targets Validation
// =============================================================================

/**
 * @brief Validate adapter operations against SRS performance targets
 */
bool test_performance_targets() {
    using namespace performance;

    std::cout << "\n  SRS Performance Targets:" << std::endl;
    std::cout << "    MIN_THROUGHPUT:        "
              << performance_targets::MIN_THROUGHPUT_MSG_PER_SEC << " msg/sec"
              << std::endl;
    std::cout << "    MAX_P95_LATENCY:       "
              << performance_targets::MAX_P95_LATENCY.count() << " ms"
              << std::endl;
    std::cout << "    MAX_MWL_LATENCY:       "
              << performance_targets::MAX_MWL_LATENCY.count() << " ms"
              << std::endl;
    std::cout << "    MIN_CONCURRENT_CONNS:  "
              << performance_targets::MIN_CONCURRENT_CONNECTIONS << std::endl;
    std::cout << "    MAX_MEMORY_BASELINE:   "
              << performance_targets::MAX_MEMORY_BASELINE_MB << " MB"
              << std::endl;

    // Validate MWL latency target using PACS adapter's MWL sub-adapter
    auto pacs = integration::create_pacs_adapter({});
    auto mwl = pacs->get_mwl_adapter();
    TEST_ASSERT(mwl != nullptr, "MWL sub-adapter should be available");

    auto mwl_latency = benchmark_with_warmup(
        [&, idx = 0]() mutable {
            integration::mwl_query_params params;
            params.patient_id = "TGT" + std::to_string(idx++ % 100);
            params.modality = "CT";
            (void)mwl->query_mwl(params);
        },
        50, 1000);

    auto mwl_latency_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(mwl_latency);

    std::cout << "\n  Target Validation:" << std::endl;
    std::cout << "    MWL query latency: " << mwl_latency.count()
              << " ns (target < "
              << performance_targets::MAX_MWL_LATENCY.count() << " ms) -> "
              << (mwl_latency_ms.count() <
                          performance_targets::MAX_MWL_LATENCY.count()
                      ? "PASS"
                      : "FAIL")
              << std::endl;

    TEST_ASSERT(mwl_latency_ms.count() <
                    performance_targets::MAX_MWL_LATENCY.count(),
                "MWL query latency should meet SRS target");
    return true;
}

}  // namespace pacs::bridge::benchmark::baseline

// =============================================================================
// Main
// =============================================================================

int main() {
    using namespace pacs::bridge::benchmark::baseline;

    std::cout << "=============================================" << std::endl;
    std::cout << "PACS Bridge Baseline Comparison Benchmarks" << std::endl;
    std::cout << "Issue #287: Phase 5 Comprehensive Testing" << std::endl;
    std::cout << "=============================================" << std::endl;

    int passed = 0;
    int failed = 0;

    // Baseline Comparisons
    std::cout << "\n--- Baseline Comparisons ---" << std::endl;
    RUN_TEST(test_baseline_database);
#ifndef PACS_BRIDGE_STANDALONE_BUILD
    RUN_TEST(test_baseline_executor);
#else
    std::cout << "  (skipped test_baseline_executor: standalone build)"
              << std::endl;
#endif
    RUN_TEST(test_baseline_mpps);

    // Performance Target Validation
    std::cout << "\n--- SRS Performance Target Validation ---" << std::endl;
    RUN_TEST(test_performance_targets);

    // Summary
    std::cout << "\n=============================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << std::endl;
    std::cout << "=============================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
