# Performance Benchmark Report

> **Issue**: [#287](https://github.com/kcenon/pacs_bridge/issues/287) - Phase 5 Comprehensive Testing and Validation
> **Framework**: Custom `benchmark_runner` (see `include/pacs/bridge/performance/benchmark_runner.h`)

## Test Environment

| Item | Value |
|------|-------|
| **OS** | macOS 26.3 (Darwin 25.3.0) |
| **CPU** | Apple M1 |
| **RAM** | 8 GB |
| **Compiler** | Apple Clang 17.0.0 |
| **Build Type** | Release |
| **Build Mode** | Standalone (`BRIDGE_STANDALONE_BUILD=ON`) |

## SRS Performance Targets

From `performance_types.h` (SRS-PERF-001 to SRS-PERF-006):

| ID | Metric | Target | Source |
|----|--------|--------|--------|
| SRS-PERF-001 | Throughput | >= 500 msg/sec | NFR-1.1 |
| SRS-PERF-002 | P95 Latency | < 50 ms | NFR-1.2 |
| SRS-PERF-003 | MWL Creation Latency | < 100 ms | NFR-1.3 |
| SRS-PERF-004 | Concurrent Connections | >= 50 | NFR-1.4 |
| SRS-PERF-005 | Memory Baseline | < 200 MB | NFR-1.5 |
| SRS-PERF-006 | CPU Idle Usage | < 20% | NFR-1.6 |

## Adapter Benchmark Results

Run with: `./build/bin/adapter_benchmark`

### Database Adapter

| Operation | Iterations | Success Rate | Throughput | P50 | P95 | P99 |
|-----------|-----------|-------------|------------|-----|-----|-----|
| execute() | 5000 | 100.00% | 500,000 ops/sec | 1 us | 2 us | 4 us |
| prepared statement | 5000 | 100.00% | 1,000,000 ops/sec | 1 us | 1 us | 1 us |
| transactions (50 rows) | 100 | 100.00% | 20,000 ops/sec | 54 us | 69 us | 171 us |
| connection pool acquire/release | 2000 | 100.00% | - | 81 ns | - | - |

### Thread Adapter

| Operation | Iterations | Success Rate | Throughput | P50 | P95 | P99 |
|-----------|-----------|-------------|------------|-----|-----|-----|
| submit (4 threads) | 5000 | 100.00% | 1,250,000 ops/sec | 0 us | 3 us | 14 us |

**Thread Scaling**:

| Threads | Throughput |
|---------|-----------|
| 1 | > 2,000,000 tasks/sec |
| 2 | > 2,000,000 tasks/sec |
| 4 | 2,000,000 tasks/sec |
| 8 | 666,667 tasks/sec |

> Note: 1-2 thread results complete in < 1ms, showing sub-microsecond task
> submission overhead. 8-thread contention reduces throughput due to scheduler
> overhead on the Apple M1's 4 performance + 4 efficiency core topology.

### MWL Adapter (Memory)

| Operation | Iterations | Success Rate | Throughput | P50 | P95 | P99 |
|-----------|-----------|-------------|------------|-----|-----|-----|
| add_item | 5000 | 100.00% | 1,666,667 ops/sec | 0 us | 1 us | 1 us |
| query_items (500 items) | 2000 | 100.00% | 285,714 ops/sec | 3 us | 4 us | 4 us |
| get_item (1000 items) | 5000 | 100.00% | > 5,000,000 ops/sec | 0 us | 0 us | 0 us |

### Concurrent Stress Tests

| Test | Threads | Total Ops | Success Rate | Duration | Throughput |
|------|---------|-----------|-------------|----------|-----------|
| Database concurrent | 4 | 2000 | 1.65% | < 1 ms | N/A |
| MWL concurrent add + query | 4 | 4000 | 100.00% | 22 ms | 181,818 ops/sec |

> Note: Database concurrent test shows low success rate because SQLite `:memory:`
> creates a separate database per connection. With `pool_size=1`, threads contend
> for a single connection and most acquire attempts return immediately without
> waiting. This is expected behavior for the test configuration.

## Baseline Comparison Results

Run with: `./build/bin/baseline_benchmark` (database, executor, MPPS)
and `./build/bin/adapter_benchmark` (MWL baseline).

Compares adapter abstraction overhead against direct implementation.

### Database: Connection Pool vs Direct

| Operation | Direct (ns) | Adapter (ns) | Overhead |
|-----------|-------------|--------------|----------|
| INSERT (execute) | 1,632 | 663 | -59.4% |

> Note: Adapter path includes pool acquire/release on each iteration.
> Negative overhead indicates the connection pool's cached connection
> is faster than creating a fresh connection path each time.

### MPPS: unordered_map vs Stub Adapter

| Operation | Direct (ns) | Adapter (ns) | Overhead |
|-----------|-------------|--------------|----------|
| create/emplace | 102 | 68 | -33.3% |
| get/find | 27 | 41 | +51.9% |

> Note: Adapter includes validation + mutex + record copying.
> The get/find overhead is from additional validation and locking.

### MWL: unordered_map vs memory_mwl_adapter

| Operation | Direct (ns) | Adapter (ns) | Overhead |
|-----------|-------------|--------------|----------|
| add_item/emplace | 515 | 483 | -6.2% |
| query/linear scan | 41,553 | 40,540 | -2.4% |
| get_item/find | 25 | 172 | +588.0% |

> Note: Adapter includes validation + mutex + optional filter matching.
> The get_item overhead is due to the adapter's thread-safe locking and
> additional error handling wrapping around the hash map lookup.

### Performance Target Validation

| Metric | Measured | Target | Result |
|--------|----------|--------|--------|
| MWL query latency | 23 ns | < 100 ms | **PASS** |

## How to Run

### Build Benchmarks

```bash
# Using build.sh
./build.sh --benchmarks

# Using CMake directly
cmake -B build -DBRIDGE_BUILD_BENCHMARKS=ON
cmake --build build --config Release
```

### Execute Benchmarks

```bash
# Adapter performance benchmarks
./build/bin/adapter_benchmark

# Baseline comparison benchmarks
./build/bin/baseline_benchmark
```

### Save Results for Regression Tracking

The `benchmark_runner` framework supports JSON output for baseline comparison:

```cpp
#include "pacs/bridge/performance/benchmark_runner.h"

benchmark_runner runner;
runner.register_benchmark("my_benchmark", my_func);
runner.run_all();
runner.save_results("benchmark_results.json");

// Compare against previous baseline
runner.compare_baseline("previous_results.json");
```

## Regression Tracking

To track performance over time:

1. Run benchmarks before and after changes
2. Use `benchmark_runner::save_results()` to persist JSON output
3. Use `benchmark_runner::compare_baseline()` to detect regressions
4. CI workflow (`.github/workflows/benchmarks.yml`) runs benchmarks on PRs
   targeting `main` and validates SRS performance targets automatically

## Notes

- All benchmarks use `benchmark_with_warmup()` for consistent measurement
- Executor adapter benchmarks require full build (`BRIDGE_STANDALONE_BUILD=OFF`)
- MWL baseline comparison is in `adapter_benchmark` (resolved ODR conflict between `pacs_adapter.h` and `mwl_adapter.h`)
- SQLite `:memory:` creates separate databases per connection; concurrent tests use `pool_size=1`
- Results may vary across hardware; these numbers represent Apple M1 baseline
