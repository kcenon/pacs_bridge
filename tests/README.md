# PACS Bridge Tests

This directory contains all tests for the PACS Bridge project. Tests are organized by functionality and phase.

## Test Summary

| Category | Test Count | Description |
|----------|------------|-------------|
| **HL7 Protocol** | 18 | Message parsing, encoding, handlers |
| **Mapping** | 3 | HL7-DICOM translation |
| **Router** | 4 | Message routing, queue management |
| **Transport** | 1 | MLLP protocol |
| **Configuration** | 3 | Config loading, hot-reload |
| **Security** | 3 | TLS, OAuth2, audit |
| **Monitoring** | 5 | Health checks, metrics, tracing |
| **Performance** | 5 | Benchmarks, stress tests |
| **PACS Adapter** | 2 | MWL, MPPS integration |
| **Integration** | 12 | End-to-end workflows |
| **FHIR** | 6 | FHIR R4 gateway (Phase 3) |
| **EMR** | 7 | EMR client (Phase 5) |
| **Messaging** | 2 | Event bus, patterns |
| **Total** | **70** | - |

## Test Categories

### HL7 Protocol Tests

| Test File | Description | Framework |
|-----------|-------------|-----------|
| `hl7_test` | Core HL7 parsing and building | GTest |
| `hl7_extended_test` | Extended parsing, encoding, ACK | GTest |
| `hl7_encoding_iso_conversion_test` | ISO-8859-1/UTF-8 conversion | GTest |
| `hl7_validation_edge_cases_test` | Boundary conditions, validation | GTest |
| `hl7_malformed_segment_recovery_test` | Error recovery, partial parsing | GTest |
| `oru_generator_test` | ORU message generation | GTest |
| `adt_handler_test` | ADT message handling | Bridge |
| `orm_handler_test` | ORM message handling | Bridge |
| `siu_handler_test` | SIU scheduling messages | GTest |
| `mdm_handler_test` | MDM document management | GTest |
| `bar_handler_test` | BAR billing records | GTest |
| `rde_handler_test` | RDE pharmacy orders | GTest |
| `qry_handler_test` | QRY query messages | GTest |
| `mfn_handler_test` | MFN master files | GTest |
| `pex_handler_test` | PEX adverse events | GTest |

### Monitoring Tests

| Test File | Description | Framework |
|-----------|-------------|-----------|
| `health_check_test` | Health endpoint | Bridge |
| `bridge_metrics_test` | Metrics collection | GTest |
| `monitoring_metrics_completeness_test` | Counter/gauge/histogram validation | GTest |
| `trace_manager_test` | Distributed tracing | GTest |
| `monitoring_trace_validation_test` | W3C traceparent format | GTest |
| `prometheus_endpoint_test` | Prometheus /metrics endpoint | Bridge |

### Performance Tests

| Test File | Description | Framework |
|-----------|-------------|-----------|
| `performance_test` | General performance | Bridge |
| `load_test` | Load testing framework | Bridge |
| `stress_high_volume_message_test` | High volume parsing (10k+ messages) | GTest |
| `concurrency_thread_safety_test` | Thread safety, race conditions | GTest |
| `benchmark_suite_test` | Comprehensive benchmarks | Bridge |
| `memory_safety_test` | Memory leak detection | Bridge |

### Integration Tests (`integration/`)

| Test File | Description | Timeout |
|-----------|-------------|---------|
| `mpps_integration_test` | MPPS N-CREATE/N-SET flows | 300s |
| `queue_persistence_test` | Message recovery | 300s |
| `failover_test` | RIS failover | 300s |
| `multi_pacs_failover_test` | Multi-PACS routing strategies | 300s |
| `backwards_compatibility_test` | HL7 v2.3-v2.5 compatibility | 300s |
| `disaster_recovery_test` | Network failures, retry logic | 600s |
| `stress_test` | High volume integration | 600s |
| `mllp_connection_test` | Connection lifecycle | 300s |
| `pacs_worklist_test` | Worklist updates | 300s |
| `e2e_scenario_test` | Complete workflows | 600s |
| `monitoring_integration_test` | Metrics integration | 180s |

## Running Tests

### Run All Tests

```bash
# Build with tests enabled
cmake -B build -DBRIDGE_BUILD_TESTS=ON
cmake --build build

# Run all tests
ctest --test-dir build

# Run with verbose output
ctest --test-dir build --output-on-failure
```

### Run Tests by Label

```bash
# Run only HL7 tests
ctest --test-dir build -L hl7

# Run only integration tests
ctest --test-dir build -L integration

# Run Phase 1 tests
ctest --test-dir build -L phase1

# Run performance tests
ctest --test-dir build -L performance
```

### Run Specific Test

```bash
# Run a specific test executable
./build/tests/hl7_test

# Run specific test case
./build/tests/hl7_test --gtest_filter="*ParseADT*"
```

## Code Coverage

```bash
# Build with coverage
cmake -B build -DBRIDGE_BUILD_TESTS=ON -DBRIDGE_ENABLE_COVERAGE=ON
cmake --build build

# Run tests
ctest --test-dir build

# Generate coverage report
lcov --capture --directory build --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/tests/*' '*/build/_deps/*' --output-file coverage.info
lcov --list coverage.info
```

### Coverage Targets

| Metric | Target | Status |
|--------|--------|--------|
| Line Coverage | 80%+ | In Progress |
| Branch Coverage | 70%+ | In Progress |
| Test Files | 70+ | **Achieved** |

## Test Frameworks

### Google Test (GTest)

Most unit tests use Google Test framework:
- Automatic test discovery via `gtest_discover_tests()`
- Rich assertion macros (`EXPECT_EQ`, `ASSERT_TRUE`, etc.)
- Mock support via gmock
- Parameterized tests

### Bridge Framework

Custom test framework for tests that don't use GTest:
- Registered via `add_test()` directly
- Used for legacy tests and integration tests

## Adding New Tests

### Unit Test (GTest)

1. Create `tests/your_test.cpp`
2. Add to `tests/CMakeLists.txt`:
   ```cmake
   add_gtest_test(your_test "unit;your_category")
   ```

### Integration Test

1. Create `tests/integration/your_test.cpp`
2. Add to `tests/CMakeLists.txt`:
   ```cmake
   add_integration_test(your_test "integration;your_category" 300)
   ```

## Labels Reference

| Label | Description |
|-------|-------------|
| `unit` | Unit tests |
| `integration` | Integration tests |
| `hl7` | HL7 message handling |
| `fhir` | FHIR resources |
| `mapping` | HL7-DICOM mapping |
| `monitoring` | Metrics, tracing |
| `performance` | Performance tests |
| `stress` | Stress/load tests |
| `phase1` | Phase 1 tests |
| `phase2` | Phase 2 tests |
| `phase3` | Phase 3 tests (FHIR) |
| `phase4` | Phase 4 tests (Monitoring) |
| `phase5` | Phase 5 tests (EMR) |

## Related Issues

- Issue #145 - Test Coverage Expansion (this work)
- Issue #159 - HL7 Extended Tests
- Issue #160 - Mapper Extended Tests
- Issue #161 - Integration Tests
- Issue #162 - Disaster Recovery Tests
- Issue #163 - Benchmark and Memory Tests
