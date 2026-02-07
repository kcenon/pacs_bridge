# Integration Module Guide

> **Module**: `pacs::bridge::integration`
> **Headers**: `include/pacs/bridge/integration/`
> **Issue**: [#283](https://github.com/kcenon/pacs_bridge/issues/283), [#274](https://github.com/kcenon/pacs_bridge/issues/274)

## Overview

The Integration Module provides adapter interfaces that abstract external system dependencies (database, threading, PACS server) behind testable, swappable interfaces. Each adapter supports two build modes:

| Mode | CMake Flag | Description |
|------|-----------|-------------|
| **Standalone** | `BRIDGE_STANDALONE_BUILD=ON` | In-memory/stub implementations, no external deps |
| **Full Integration** | `BRIDGE_STANDALONE_BUILD=OFF` | Connects to kcenon ecosystem (database_system, thread_system, pacs_system) |

## Quick Start

### Database Adapter

```cpp
#include "pacs/bridge/integration/database_adapter.h"
using namespace pacs::bridge::integration;

// Create in-memory database (standalone)
auto db = create_database_adapter({.database_path = ":memory:"});

// Acquire connection and execute SQL
auto conn = db->acquire_connection();
if (conn.has_value()) {
    auto result = conn.value()->execute("CREATE TABLE test(id INTEGER PRIMARY KEY)");
    db->release_connection(conn.value());
}
```

### Thread Adapter

```cpp
#include "pacs/bridge/integration/thread_adapter.h"
using namespace pacs::bridge::integration;

auto pool = create_thread_adapter();
pool->initialize({.name = "my_pool", .min_threads = 2, .max_threads = 8});

auto future = pool->submit([]() { return 42; }, task_priority::normal);
int result = future.get();  // 42

pool->shutdown(true);
```

### PACS Adapter (MPPS / MWL / Storage)

```cpp
#include "pacs/bridge/integration/pacs_adapter.h"
using namespace pacs::bridge::integration;

auto pacs = create_pacs_adapter({});
auto mpps = pacs->get_mpps_adapter();
auto mwl  = pacs->get_mwl_adapter();

// Create MPPS record
mpps_record record;
record.sop_instance_uid = "1.2.840.113619.2.1";
record.patient_id = "PAT001";
record.status = "IN PROGRESS";
record.performed_station_ae_title = "CT1";
record.start_datetime = std::chrono::system_clock::now();
auto result = mpps->create_mpps(record);
```

### MWL Adapter (Standalone)

```cpp
#include "pacs/bridge/integration/mwl_adapter.h"
using namespace pacs::bridge::integration;

auto mwl = create_mwl_adapter("");  // In-memory storage

mapping::mwl_item item;
item.imaging_service_request.accession_number = "ACC001";
item.patient.patient_id = "PAT001";
auto result = mwl->add_item(item);

// Query with filter
mwl_query_filter filter;
filter.patient_id = "PAT001";
auto items = mwl->query_items(filter);
```

### Executor Adapter (Full Build Only)

```cpp
// Requires BRIDGE_STANDALONE_BUILD=OFF
#include "pacs/bridge/integration/executor_adapter.h"
using namespace pacs::bridge::integration;

auto executor = std::make_shared<simple_executor>(4);  // 4 threads
auto job = std::make_unique<lambda_job>([]() { /* work */ }, "my_task");
auto result = executor->execute(std::move(job));
if (result.is_ok()) {
    result.value().wait();
}
executor->shutdown(true);
```

## Adapter Reference

### database_adapter

| Item | Detail |
|------|--------|
| **Header** | `pacs/bridge/integration/database_adapter.h` |
| **Factory** | `create_database_adapter(database_config)` |
| **Thread Safety** | Connection pool is thread-safe; individual connections are not |
| **Error Type** | `database_error` (range: -800 to -849) |

**Configuration** (`database_config`):

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `database_path` | `string` | `""` | SQLite file path (`:memory:` for in-memory) |
| `pool_size` | `size_t` | `5` | Connection pool size |
| `connection_timeout` | `seconds` | `30` | Connection acquisition timeout |
| `query_timeout` | `seconds` | `60` | Query execution timeout |
| `enable_wal` | `bool` | `true` | Enable WAL mode (SQLite) |
| `busy_timeout_ms` | `int` | `5000` | Busy retry timeout (SQLite) |

**Key Operations**:
- `acquire_connection()` / `release_connection()` - Pool management
- `connection->execute(sql)` - Direct SQL execution
- `connection->prepare(sql)` - Prepared statements with `bind_*()` methods
- `connection->begin_transaction()` / `commit()` / `rollback()` - Transactions
- `transaction_guard::begin(conn)` - RAII transaction scope
- `connection_scope::acquire(adapter)` - RAII connection scope

### thread_adapter

| Item | Detail |
|------|--------|
| **Header** | `pacs/bridge/integration/thread_adapter.h` |
| **Factory** | `create_thread_adapter()` |
| **Thread Safety** | Fully thread-safe |
| **Return** | `std::unique_ptr<thread_adapter>` |

**Configuration** (`worker_pool_config`):

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | `string` | `"worker_pool"` | Pool name for debugging |
| `min_threads` | `size_t` | `2` | Minimum thread count |
| `max_threads` | `size_t` | `8` | Maximum thread count |
| `idle_timeout` | `seconds` | `60` | Thread idle timeout |
| `queue_size` | `size_t` | `1000` | Task queue capacity |

**Key Operations**:
- `initialize(config)` - Start the pool
- `submit(callable, priority)` - Submit task, returns `std::future<T>`
- `shutdown(wait)` - Stop pool, optionally waiting for queued tasks
- `queue_size()` / `active_threads()` / `is_running()` - Status queries

**Priority Levels**: `task_priority::low`, `normal`, `high`, `critical`

### pacs_adapter (MPPS + MWL + Storage)

| Item | Detail |
|------|--------|
| **Header** | `pacs/bridge/integration/pacs_adapter.h` |
| **Factory** | `create_pacs_adapter(pacs_config)` |
| **Thread Safety** | Sub-adapters are individually thread-safe |
| **Error Type** | `pacs_error` (range: -850 to -899) |

**Sub-adapters**:
- `get_mpps_adapter()` → `mpps_adapter` (N-CREATE, N-SET, query, get)
- `get_mwl_adapter()` → `mwl_adapter` (query, get_item)
- `get_storage_adapter()` → `storage_adapter` (store, retrieve, exists)

> **Note**: The `mwl_adapter` class in `pacs_adapter.h` and `mwl_adapter.h` are
> different types in the same namespace. Do NOT include both headers in the same
> translation unit (ODR violation).

### mwl_adapter (Standalone)

| Item | Detail |
|------|--------|
| **Header** | `pacs/bridge/integration/mwl_adapter.h` |
| **Factory** | `create_mwl_adapter(database_path)` |
| **Thread Safety** | Fully thread-safe |
| **Error Type** | `mwl_adapter_error` (range: -870 to -879) |
| **Data Type** | `mapping::mwl_item` (from `hl7_dicom_mapper.h`) |

**Key Operations**:
- `add_item(item)` - Add MWL entry
- `update_item(accession, item)` - Update by accession number
- `delete_item(accession)` - Delete by accession number
- `query_items(filter)` - Query with `mwl_query_filter`
- `get_item(accession)` - Get single entry
- `exists(accession)` - Check existence
- `delete_items_before(date)` - Cleanup old entries

### executor_adapter (Full Build Only)

| Item | Detail |
|------|--------|
| **Header** | `pacs/bridge/integration/executor_adapter.h` |
| **Requires** | `BRIDGE_STANDALONE_BUILD=OFF` |
| **Thread Safety** | Fully thread-safe |

**Key Classes**:
- `simple_executor(num_threads)` - Thread pool executor
- `lambda_job(fn, name)` - Wraps a callable as a job
- `thread_pool_executor_adapter` - Adapter over kcenon executor_interface

## Build Configuration

| Flag | Default | Effect |
|------|---------|--------|
| `BRIDGE_STANDALONE_BUILD` | `ON` | Stub/memory adapters only |
| `BRIDGE_BUILD_PACS_INTEGRATION` | `ON` | pacs_system-backed adapters |
| `BRIDGE_BUILD_BENCHMARKS` | `OFF` | Build performance benchmarks |

```bash
# Standalone build (default)
cmake -B build
cmake --build build

# Full integration build
cmake -B build -DBRIDGE_STANDALONE_BUILD=OFF
cmake --build build

# With benchmarks
cmake -B build -DBRIDGE_BUILD_BENCHMARKS=ON
cmake --build build
```

## Testing with Stub/Memory Adapters

All adapters provide in-process implementations suitable for unit testing:

```cpp
// Database: in-memory SQLite
auto db = create_database_adapter({.database_path = ":memory:"});

// PACS: stub implementation (stores in std::unordered_map)
auto pacs = create_pacs_adapter({});

// MWL: memory adapter
auto mwl = create_mwl_adapter("");

// Thread: simple thread pool (no external deps)
auto pool = create_thread_adapter();
pool->initialize({.min_threads = 1, .max_threads = 2});
```

These implementations are used throughout the test suite and benchmarks.

## Performance

Adapter overhead has been measured against direct implementations:

- **Database**: Connection pool acquire/release adds measurable overhead for sub-microsecond operations
- **MPPS**: ~38% overhead for get operations (validation + mutex + record copy)
- **MWL**: Sub-microsecond operations for in-memory adapter
- **Thread**: Thread pool amortizes thread creation cost vs `std::async`

SRS performance targets (from `performance_types.h`):

| Target | Requirement | Source |
|--------|-------------|--------|
| Throughput | >= 500 msg/sec | NFR-1.1 |
| P95 Latency | < 50 ms | NFR-1.2 |
| MWL Latency | < 100 ms | NFR-1.3 |
| Concurrent Connections | >= 50 | NFR-1.4 |
| Memory Baseline | < 200 MB | NFR-1.5 |

See [performance_report.md](performance_report.md) for benchmark results and methodology.
