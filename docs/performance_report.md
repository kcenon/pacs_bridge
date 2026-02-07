# Performance Benchmark Report

> **Issue**: [#322](https://github.com/kcenon/pacs_bridge/issues/322) - Phase 5d Performance Benchmarks
> **Framework**: Custom `benchmark_runner` (see `include/pacs/bridge/performance/benchmark_runner.h`)

## Test Environment

| Item | Value |
|------|-------|
| **OS** | _(fill in: e.g., Ubuntu 24.04, macOS 15)_ |
| **CPU** | _(fill in: e.g., Apple M2, Intel i7-13700)_ |
| **RAM** | _(fill in: e.g., 16 GB)_ |
| **Compiler** | _(fill in: e.g., Clang 18.1, GCC 14.2)_ |
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
| execute() | 5000 | __%% | __ ops/sec | __ us | __ us | __ us |
| prepared statement | 5000 | __%% | __ ops/sec | __ us | __ us | __ us |
| transactions (50 rows) | 100 | __%% | __ ops/sec | __ us | __ us | __ us |
| connection pool acquire/release | 2000 | __%% | - | __ ns | __ ns | __ ns |

### Thread Adapter

| Operation | Iterations | Success Rate | Throughput | P50 | P95 | P99 |
|-----------|-----------|-------------|------------|-----|-----|-----|
| submit (4 threads) | 5000 | __%% | __ ops/sec | __ us | __ us | __ us |

**Thread Scaling**:

| Threads | Throughput |
|---------|-----------|
| 1 | __ tasks/sec |
| 2 | __ tasks/sec |
| 4 | __ tasks/sec |
| 8 | __ tasks/sec |

### MWL Adapter (Memory)

| Operation | Iterations | Success Rate | Throughput | P50 | P95 | P99 |
|-----------|-----------|-------------|------------|-----|-----|-----|
| add_item | 5000 | __%% | __ ops/sec | __ us | __ us | __ us |
| query_items (500 items) | 2000 | __%% | __ ops/sec | __ us | __ us | __ us |
| get_item (1000 items) | 5000 | __%% | __ ops/sec | __ us | __ us | __ us |

### Concurrent Stress Tests

| Test | Threads | Total Ops | Success Rate | Duration | Throughput |
|------|---------|-----------|-------------|----------|-----------|
| Database concurrent | 4 | 2000 | __%% | __ ms | __ ops/sec |
| MWL concurrent add + query | 4 | 4000 | __%% | __ ms | __ ops/sec |

## Baseline Comparison Results

Run with: `./build/bin/baseline_benchmark`

Compares adapter abstraction overhead against direct implementation.

### Database: Connection Pool vs Direct

| Operation | Direct (ns) | Adapter (ns) | Overhead |
|-----------|-------------|--------------|----------|
| INSERT (execute) | __ | __ | __% |

> Note: Adapter path includes pool acquire/release on each iteration.

### MPPS: unordered_map vs Stub Adapter

| Operation | Direct (ns) | Adapter (ns) | Overhead |
|-----------|-------------|--------------|----------|
| create/emplace | __ | __ | __% |
| get/find | __ | __ | __% |

> Note: Adapter includes validation + mutex + record copying.

### Performance Target Validation

| Metric | Measured | Target | Result |
|--------|----------|--------|--------|
| MWL query latency | __ ns | < 100 ms | PASS/FAIL |

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
4. Integrate into CI with threshold-based pass/fail gates

## Notes

- All benchmarks use `benchmark_with_warmup()` for consistent measurement
- Executor adapter benchmarks require full build (`BRIDGE_STANDALONE_BUILD=OFF`)
- MWL baseline comparison is excluded due to ODR conflict between `pacs_adapter.h` and `mwl_adapter.h`
- SQLite `:memory:` creates separate databases per connection; concurrent tests use `pool_size=1`
