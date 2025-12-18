# Module Descriptions

> **Version:** 0.1.0.1
> **Last Updated:** 2025-12-17

---

## Table of Contents

- [Module Overview](#module-overview)
- [Protocol Modules](#protocol-modules)
- [Transport Modules](#transport-modules)
- [Service Modules](#service-modules)
- [Infrastructure Modules](#infrastructure-modules)
- [Extension Points](#extension-points)

---

## Module Overview

PACS Bridge is organized into modular components for maintainability and extensibility.

### Module Structure

```
pacs_bridge/
├── include/pacs/bridge/
│   ├── protocol/hl7/         # HL7 message handling
│   ├── messaging/            # Messaging patterns (Pub/Sub, Pipeline)
│   ├── mllp/                 # MLLP transport
│   ├── fhir/                 # FHIR gateway
│   ├── emr/                  # EMR client integration
│   ├── mapping/              # Protocol translation
│   ├── router/               # Message routing
│   ├── workflow/             # Workflow orchestration
│   ├── pacs_adapter/         # PACS integration
│   ├── cache/                # Patient cache
│   ├── config/               # Configuration
│   ├── security/             # Security features
│   ├── monitoring/           # Health and metrics
│   ├── tracing/              # Distributed tracing
│   ├── testing/              # Test utilities
│   └── concepts/             # C++20 concepts
├── src/
│   └── [corresponding implementations]
└── tests/
    └── [module tests]
```

---

## Protocol Modules

### HL7 Protocol Module (`protocol/hl7/`)

**Purpose:** Parse, build, and validate HL7 v2.x messages.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `hl7_message` | Message container with segments |
| `hl7_parser` | Parse raw HL7 to structured message |
| `hl7_builder` | Build HL7 messages programmatically |
| `hl7_validator` | Validate message structure and content |
| `hl7_types` | Type definitions and enumerations |

**Key Headers:**

```cpp
#include <pacs/bridge/protocol/hl7/hl7_message.h>
#include <pacs/bridge/protocol/hl7/hl7_parser.h>
#include <pacs/bridge/protocol/hl7/hl7_builder.h>
```

**Usage Example:**

```cpp
// Parse an HL7 message
hl7_parser parser;
auto result = parser.parse(raw_message);
if (result) {
    auto& message = result.value();
    auto msh = message.get_segment("MSH");
    auto message_type = msh.get_field(9);  // "ADT^A01"
}

// Build an ACK message
hl7_builder builder;
auto ack = builder
    .set_message_type("ACK", "A01")
    .set_message_control_id("ACK001")
    .add_msa_segment("AA", original_message_id)
    .build();
```

### ADT Handler (`protocol/hl7/adt_handler.h`)

**Purpose:** Process ADT (Admission/Discharge/Transfer) messages.

**Supported Events:**

| Event | Description | Action |
|-------|-------------|--------|
| A01 | Patient Admit | Cache patient |
| A04 | Patient Registration | Cache patient |
| A08 | Patient Update | Update cache |
| A40 | Patient Merge | Update aliases |

**Usage:**

```cpp
adt_handler handler(patient_cache);
auto result = handler.handle(adt_message);
```

### ORM Handler (`protocol/hl7/orm_handler.h`)

**Purpose:** Process ORM (Order) messages.

**Supported Events:**

| Event | Description | Action |
|-------|-------------|--------|
| O01 | New Order | Create MWL entry |

**Usage:**

```cpp
orm_handler handler(hl7_dicom_mapper, pacs_adapter);
auto result = handler.handle(orm_message);
```

### SIU Handler (`protocol/hl7/siu_handler.h`)

**Purpose:** Process SIU (Scheduling) messages.

**Supported Events:**

| Event | Description | Action |
|-------|-------------|--------|
| S12 | Scheduled | Create worklist |
| S13 | Modified | Update worklist |
| S14 | Cancelled | Cancel worklist |

### Messaging Module (`messaging/`)

**Purpose:** Provide messaging patterns for decoupled, scalable HL7 message processing.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `hl7_message_bus` | Pub/Sub message distribution with topic-based routing |
| `hl7_publisher` | Convenient publisher wrapper |
| `hl7_subscriber` | Convenient subscriber wrapper with type-specific helpers |
| `hl7_pipeline` | Staged message processing with validators, transformers |
| `hl7_pipeline_builder` | Fluent API for building processing pipelines |
| `hl7_request_client` | Request/Reply pattern client |
| `hl7_request_server` | Request/Reply pattern server |
| `messaging_backend_factory` | Factory for backend selection |
| `event_subscription` | RAII subscription handle for Event Bus |
| `event_publisher` | Utility namespace for publishing HL7 events |
| `event_subscriber` | Utility namespace for subscribing to HL7 events |

**Key Headers:**

```cpp
#include <pacs/bridge/messaging/hl7_message_bus.h>     // Pub/Sub
#include <pacs/bridge/messaging/hl7_pipeline.h>        // Pipeline
#include <pacs/bridge/messaging/hl7_request_handler.h> // Request/Reply
#include <pacs/bridge/messaging/messaging_backend.h>   // Backend factory
#include <pacs/bridge/messaging/hl7_events.h>          // Event Bus integration
```

**Pub/Sub Pattern:**

```cpp
// Create and start message bus
hl7_message_bus bus;
bus.start();

// Subscribe to ADT messages
bus.subscribe(topics::HL7_ADT_ALL, [](const hl7_message& msg) {
    // Process ADT message
    return subscription_result::ok();
});

// Publish a message (auto-routes by type)
bus.publish(adt_message);
```

**Pipeline Pattern:**

```cpp
// Build a processing pipeline
auto pipeline = hl7_pipeline_builder::create("validation_pipeline")
    .add_validator([](const hl7_message& msg) {
        return msg.has_segment("MSH");
    })
    .add_transformer("enrich", [](const hl7_message& msg) {
        auto enriched = msg;
        enriched.set_value("ZPI.1", "PROCESSED");
        return enriched;
    })
    .with_statistics(true)
    .build();

auto result = pipeline.process(message);
```

**Request/Reply Pattern:**

```cpp
// Server side
hl7_request_server server(bus, "hl7.orders");
server.register_handler([](const hl7_message& request)
    -> std::expected<hl7_message, request_error> {
    // Process request and return response
    return ack_builder::generate_ack(request, ack_code::AA);
});
server.start();

// Client side
hl7_request_client client(bus, "hl7.orders");
auto result = client.request(order_message, std::chrono::seconds(30));
```

**Event Bus Integration (Issue #142):**

> **Note:** Event Bus integration requires building with `BRIDGE_STANDALONE_BUILD=OFF`.
> This feature depends on `common_system` which provides the Event Bus implementation.
> In standalone builds, the `hl7_events.h` header and related functionality are not available.

The HL7 Events system integrates with `common_system`'s Event Bus to provide
event-driven message processing. Events are published at each processing stage:

| Event Type | Stage | Description |
|------------|-------|-------------|
| `hl7_message_received_event` | Receive | Raw message received from MLLP |
| `hl7_ack_sent_event` | Receive | ACK/NAK sent to sender |
| `hl7_message_parsed_event` | Parse | Message successfully parsed |
| `hl7_message_validated_event` | Validate | Message passed validation |
| `hl7_message_routed_event` | Route | Message routed to destination |
| `hl7_to_dicom_mapped_event` | Transform | HL7 mapped to DICOM |
| `dicom_worklist_updated_event` | Transform | Worklist entry updated |
| `hl7_processing_error_event` | Error | Processing error occurred |

**Event Subscription:**

```cpp
// Subscribe to specific events
auto sub = event_subscriber::on_message_received(
    [](const hl7_message_received_event& event) {
        std::cout << "Received: " << event.message_type << std::endl;
    });

// Subscribe to all events for monitoring
auto all_subs = event_subscriber::on_all_events(
    [](std::string_view event_type, std::string_view event_id) {
        log_event(event_type, event_id);
    });
```

**Event Publishing:**

```cpp
// Publish events at processing stages
event_publisher::publish_message_received("ADT^A01", raw_message,
    connection_id, remote_endpoint);

event_publisher::publish_message_parsed("ADT^A01", control_id,
    segment_count, parse_time, correlation_id);

event_publisher::publish_worklist_updated(
    dicom_worklist_updated_event::operation_type::created,
    patient_id, accession_number, modality, correlation_id);
```

**Correlation ID Tracking:**

All events support correlation IDs for request tracking across the processing pipeline:

```cpp
// Generate correlation ID at message receipt
std::string correlation_id = generate_uuid();

// Pass through all processing stages
event_publisher::publish_message_received("ADT^A01", raw, conn_id, "", correlation_id);
event_publisher::publish_message_parsed("ADT^A01", ctrl_id, 5, parse_time, correlation_id);
event_publisher::publish_dicom_mapped("ADT^A01", ctrl_id, pat_id, acc_num, 42, correlation_id);
```

**Error Codes:** -800 to -839 (messaging module range)

---

## Transport Modules

### MLLP Module (`mllp/`)

**Purpose:** Handle Minimal Lower Layer Protocol for HL7 transport.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `mllp_server` | TCP server for inbound connections |
| `mllp_client` | TCP client for outbound connections |
| `mllp_connection` | Individual connection handler |
| `mllp_frame` | Message framing utilities |

**Configuration:**

```yaml
hl7:
  listener:
    port: 2575
    max_connections: 100
    idle_timeout: 300s
```

**Usage:**

```cpp
// Create server
mllp_server server(config);
server.set_message_handler([&](const std::string& message) {
    return router.route(parser.parse(message));
});
server.start();

// Create client
mllp_client client("ris.hospital.local", 2576);
auto result = client.send(hl7_message);
```

### FHIR Module (`fhir/`)

**Purpose:** RESTful FHIR R4 API gateway.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `fhir_server` | HTTP server for FHIR API |
| `resource_handler` | Base class for resource handlers |
| `patient_handler` | Patient resource operations |
| `service_request_handler` | ServiceRequest operations |
| `imaging_study_handler` | ImagingStudy operations |

**Endpoints:**

| Path | Methods | Resource |
|------|---------|----------|
| `/fhir/r4/Patient` | GET, POST | Patient |
| `/fhir/r4/Patient/{id}` | GET, PUT | Patient |
| `/fhir/r4/ServiceRequest` | GET, POST | ServiceRequest |
| `/fhir/r4/ImagingStudy` | GET | ImagingStudy |

### EMR Client Module (`emr/`)

**Purpose:** Client-side integration with external EMR/FHIR servers.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `fhir_client` | FHIR R4 REST client |
| `patient_lookup_provider` | Patient demographics lookup |
| `result_poster` | Post DiagnosticReport to EMR |
| `encounter_context_provider` | Encounter/visit context retrieval |

**Key Headers:**

```cpp
#include <pacs/bridge/emr/fhir_client.h>
#include <pacs/bridge/emr/patient_lookup.h>
#include <pacs/bridge/emr/result_poster.h>
#include <pacs/bridge/emr/encounter_context.h>
```

**Encounter Context Provider:**

The encounter context provider retrieves visit/encounter information from EMR systems via FHIR API. This enables linking imaging studies with patient visits for proper billing and clinical context.

**Encounter Status Types:**
- `planned` - Encounter is being planned
- `arrived` - Patient has arrived
- `triaged` - Patient is triaged
- `in_progress` - Encounter is active
- `on_leave` - Patient temporarily away
- `finished` - Encounter complete
- `cancelled` - Encounter cancelled

**Encounter Classes:**
- `inpatient` (IMP) - Inpatient admission
- `outpatient` (AMB) - Ambulatory/outpatient
- `emergency` (EMER) - Emergency visit
- `home_health` (HH) - Home health
- `virtual_visit` (VR) - Virtual/telehealth
- `preadmission` (PRENC) - Pre-admission
- `short_stay` (SS) - Observation/short stay

**Usage Example:**

```cpp
// Configure encounter context provider
encounter_context_config config;
config.client = fhir_client;
config.include_location = true;
config.include_participants = true;
config.cache_ttl = std::chrono::seconds{300};

encounter_context_provider provider(config);

// Get encounter by FHIR ID
auto result = provider.get_encounter("enc-12345");
if (auto* encounter = std::get_if<encounter_info>(&result)) {
    std::cout << "Visit: " << encounter->visit_number << std::endl;
    std::cout << "Status: " << to_string(encounter->status) << std::endl;

    if (auto location = encounter->current_location()) {
        std::cout << "Location: " << location->display << std::endl;
    }
}

// Find active encounter for patient
auto active_result = provider.find_active_encounter("patient-123");
if (auto* active = std::get_if<std::optional<encounter_info>>(&active_result)) {
    if (active->has_value()) {
        std::cout << "Active encounter: " << (*active)->id << std::endl;
    }
}

// Find encounter by visit number
auto visit_result = provider.find_by_visit_number("V2025001234");
```

**Error Handling:**

```cpp
auto result = provider.get_encounter("invalid-id");
if (auto* error = std::get_if<encounter_error>(&result)) {
    std::cerr << "Error: " << to_string(*error) << std::endl;
    // Handle specific errors
    switch (*error) {
        case encounter_error::not_found:
            // Encounter doesn't exist
            break;
        case encounter_error::query_failed:
            // FHIR query failed
            break;
        case encounter_error::multiple_active:
            // Ambiguous - multiple active encounters
            break;
    }
}
```

---

## Service Modules

### Message Router (`router/`)

**Purpose:** Route messages to appropriate handlers based on type and rules.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `message_router` | Main routing engine |
| `routing_rule` | Individual routing rule |
| `pattern_matcher` | Message type pattern matching |

**Configuration:**

```yaml
routing_rules:
  - name: "ADT to Cache"
    message_type_pattern: "ADT^A*"
    destination: "patient_cache"
    priority: 10
```

**Usage:**

```cpp
message_router router;
router.add_rule(routing_rule{
    .name = "ADT to Cache",
    .message_type_pattern = "ADT^A*",
    .destination = "patient_cache",
    .priority = 10
});
router.add_handler("patient_cache", std::make_unique<adt_handler>(cache));

auto result = router.route(message);
```

### Protocol Mapping (`mapping/`)

**Purpose:** Translate between HL7, FHIR, and DICOM formats.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `hl7_dicom_mapper` | HL7 to DICOM conversion |
| `dicom_hl7_mapper` | DICOM to HL7 conversion |
| `fhir_dicom_mapper` | FHIR to DICOM conversion |

**Mappings:**

| Source | Target | Use Case |
|--------|--------|----------|
| ORM^O01 | MWL Item | Order to worklist |
| MPPS | ORU^R01 | Status notification |
| Patient | MWL Patient | Demographics |
| ServiceRequest | MWL Item | FHIR order |

**Usage:**

```cpp
hl7_dicom_mapper mapper(config.mapping);
auto mwl_item = mapper.map_orm_to_mwl(orm_message);

dicom_hl7_mapper reverse_mapper(config.mapping);
auto oru = reverse_mapper.map_mpps_to_oru(mpps_event);
```

### PACS Adapter (`pacs_adapter/`)

**Purpose:** Interface with pacs_system for DICOM operations.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `mwl_client` | Modality Worklist operations |
| `mpps_handler` | MPPS event handling |

**Operations:**

| Operation | DICOM Service | Direction |
|-----------|---------------|-----------|
| Query Worklist | MWL C-FIND | Outbound |
| Update Worklist | MWL Update | Outbound |
| Receive MPPS | MPPS N-CREATE/N-SET | Inbound |

**Build Modes:**

The PACS Adapter supports two build modes:

| Mode | Option | Description |
|------|--------|-------------|
| Standalone | `BRIDGE_STANDALONE_BUILD=ON` | In-memory stub storage for testing |
| Full Integration | `BRIDGE_STANDALONE_BUILD=OFF` | Real pacs_system index_database |

When `PACS_BRIDGE_HAS_PACS_SYSTEM` is defined, the MWL client uses pacs_system's
`index_database` API for persistent worklist storage with SQLite backend.

**Configuration:**

```cpp
mwl_client_config config;
config.pacs_host = "localhost";
config.pacs_port = 11112;
config.our_ae_title = "PACS_BRIDGE";
config.max_retries = 3;

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
config.pacs_database_path = "/var/lib/pacs_bridge/worklist.db";
#endif
```

**Usage:**

```cpp
mwl_client client(pacs_config);
client.connect();

// Add MWL entry (from ORM order)
auto result = client.add_entry(mwl_item);

// Query worklist
mwl_query_filter filter;
filter.modality = "CT";
filter.scheduled_date = "20241215";
auto query_result = client.query(filter);

// Update entry status
client.update_entry(accession_number, updates);

// Cancel entry
client.cancel_entry(accession_number);

mpps_handler handler(dicom_hl7_mapper, message_queue);
handler.on_mpps_event(mpps_event);
```

**Error Codes:** -980 to -989 (mwl_client module range)

---

## Infrastructure Modules

### Patient Cache (`cache/`)

**Purpose:** Cache patient demographics for efficient lookup.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `patient_cache` | LRU cache with TTL |
| `patient_record` | Patient data structure |
| `patient_alias` | Alias management |

**Features:**

- LRU eviction
- Configurable TTL
- Patient ID aliasing (for merges)
- Thread-safe access

**Configuration:**

```yaml
patient_cache:
  max_entries: 10000
  ttl: 3600s
  evict_on_full: true
```

**Usage:**

```cpp
patient_cache cache(config);
cache.store(patient);
auto patient = cache.lookup("12345");
cache.add_alias("12345", "67890");  // Merge alias
```

### Message Queue (`queue/`)

**Purpose:** Reliable message delivery with persistence.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `message_queue` | Queue interface |
| `sqlite_queue` | SQLite-backed implementation |
| `queue_worker` | Delivery worker thread |

**Features:**

- SQLite persistence
- Retry with exponential backoff
- Dead letter queue
- Multiple workers

**Configuration:**

```yaml
queue:
  database_path: "/var/lib/pacs_bridge/queue.db"
  max_queue_size: 50000
  max_retry_count: 5
  worker_count: 4
```

**Usage:**

```cpp
sqlite_queue queue(config);
queue.enqueue(message, "RIS_PRIMARY");

queue.set_delivery_handler([](const message& msg, const std::string& dest) {
    return mllp_client(dest).send(msg);
});
queue.start_workers();
```

### Configuration (`config/`)

**Purpose:** Load and manage configuration.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `config_loader` | YAML configuration loading |
| `config_manager` | Runtime config management |
| `bridge_config` | Configuration data structure |

**Usage:**

```cpp
config_loader loader;
auto config = loader.load("/etc/pacs_bridge/config.yaml");

config_manager manager(config);
auto& hl7_config = manager.get_hl7_config();
```

### Security (`security/`)

**Purpose:** Security features including TLS, authentication, and auditing.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `tls_context` | TLS configuration |
| `tls_socket` | TLS-enabled socket |
| `audit_logger` | Security event logging |
| `access_control` | Authorization checks |
| `input_validator` | Input sanitization |
| `rate_limiter` | Rate limiting |

**Usage:**

```cpp
tls_context context;
context.load_certificate("/path/to/cert.pem");
context.load_private_key("/path/to/key.pem");

audit_logger audit;
audit.log_access(user, resource, action);
```

### Monitoring (`monitoring/`)

**Purpose:** Health checks and Prometheus metrics.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `health_server` | Health endpoint server |
| `bridge_metrics_collector` | Metrics collection |
| `health_checker` | Component health checks |

**Metrics:**

| Metric | Type | Description |
|--------|------|-------------|
| `hl7_messages_received_total` | Counter | Received messages |
| `hl7_message_processing_duration_seconds` | Histogram | Processing latency |
| `queue_depth` | Gauge | Queue depth |
| `mllp_active_connections` | Gauge | Active connections |

**Usage:**

```cpp
auto& metrics = bridge_metrics_collector::instance();
metrics.record_hl7_message_received("ADT");

{
    PACS_BRIDGE_TIME_HL7_PROCESSING("ADT");
    // Process message
}
```

### Distributed Tracing (`tracing/`)

**Purpose:** End-to-end distributed tracing with W3C Trace Context support.

**See also:** [GitHub Issue #144](https://github.com/kcenon/pacs_bridge/issues/144)

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `trace_manager` | Singleton trace manager for span creation |
| `span_wrapper` | RAII wrapper for span lifecycle management |
| `trace_context` | W3C Trace Context data structure |
| `trace_exporter` | Interface for trace data export |
| `exporter_factory` | Factory for creating trace exporters |
| `batch_exporter` | Batching wrapper with retry logic |

**Key Headers:**

```cpp
#include <pacs/bridge/tracing/trace_manager.h>
#include <pacs/bridge/tracing/span_wrapper.h>
#include <pacs/bridge/tracing/trace_propagation.h>
#include <pacs/bridge/tracing/exporter_factory.h>
```

**Span Types (span_kind):**

| Kind | Description |
|------|-------------|
| `server` | Server-side handling of a request |
| `client` | Client-side call to external service |
| `internal` | Internal processing operation |
| `producer` | Message producer |
| `consumer` | Message consumer |

**Export Formats:**

| Format | Description |
|--------|-------------|
| `jaeger_thrift` | Jaeger Thrift over HTTP |
| `jaeger_grpc` | Jaeger gRPC (OTLP compatible) |
| `zipkin_json` | Zipkin JSON v2 format |
| `otlp_grpc` | OpenTelemetry Protocol gRPC |
| `otlp_http_json` | OTLP JSON over HTTP |

**Usage Example:**

```cpp
// Initialize tracing
tracing_config config;
config.enabled = true;
config.service_name = "pacs_bridge";
config.format = trace_export_format::otlp_grpc;
config.endpoint = "http://localhost:4317";

auto& manager = trace_manager::instance();
manager.configure(config);

// Create spans with RAII
{
    auto span = manager.start_span("hl7_process", span_kind::server);
    span.set_attribute("hl7.message_type", "ADT^A01");
    span.set_attribute("hl7.message_id", "MSG001");

    // Processing...

    span.set_status(span_status::ok);
}  // Span automatically ends and exports

// Create child spans
{
    auto parent = manager.start_span("parent_operation", span_kind::server);

    {
        auto child = manager.start_span("child_operation", span_kind::internal);
        child.set_attribute("child.data", "value");
    }
}
```

**Trace Context Propagation:**

```cpp
// Inject trace context into HL7 message
hl7_propagation_config config;
config.enabled = true;
config.strategy = hl7_propagation_strategy::z_segment;
config.segment_name = "ZTR";

inject_trace_context(hl7_message, trace_context, config);

// Extract trace context from HL7 message
auto ctx = extract_trace_context(hl7_message, config);
if (ctx) {
    auto span = manager.start_span_from_context("process", span_kind::server, *ctx);
}
```

**Custom Exporter Registration:**

```cpp
// Register a custom exporter factory
exporter_factory::register_factory(
    trace_export_format::jaeger_thrift,
    [](const tracing_config& config) -> std::expected<std::unique_ptr<trace_exporter>, exporter_error> {
        return std::make_unique<my_jaeger_exporter>(config);
    }
);
```

---

## Workflow Modules

### MPPS-HL7 Workflow Orchestrator (`workflow/`)

> **See also:** [GitHub Issue #173](https://github.com/kcenon/pacs_bridge/issues/173)

**Purpose:** Wire MPPS status changes to HL7 message generation and outbound delivery with reliable routing and failure handling.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `mpps_hl7_workflow` | Main workflow coordinator |
| `mpps_hl7_workflow_config` | Configuration for workflow behavior |
| `workflow_config_builder` | Fluent API for building configurations |
| `destination_rule` | Rule for routing messages to destinations |
| `workflow_result` | Result of workflow processing |
| `workflow_statistics` | Runtime statistics (messages processed, successes, failures) |

**Key Headers:**

```cpp
#include <pacs/bridge/workflow/mpps_hl7_workflow.h>
```

**Destination Routing Criteria:**

| Criteria | Description |
|----------|-------------|
| `by_modality` | Route based on DICOM modality (CT, MR, etc.) |
| `by_station` | Route based on station/AE title |
| `by_accession_pattern` | Route based on accession number regex pattern |

**Usage Example:**

```cpp
// Build configuration
auto config = workflow_config_builder::create()
    .default_destination("PRIMARY_HIS")
    .add_routing_rule({
        .destination = "CT_HIS",
        .criteria = destination_criteria::by_modality,
        .pattern = "CT",
        .priority = 10
    })
    .add_routing_rule({
        .destination = "MR_HIS",
        .criteria = destination_criteria::by_modality,
        .pattern = "MR",
        .priority = 10
    })
    .enable_queue_fallback(true)
    .enable_statistics(true)
    .build();

// Create workflow
mpps_hl7_workflow workflow(config);
workflow.set_outbound_router(router);
workflow.set_queue_manager(queue);

// Wire to MPPS handler for automatic processing
workflow.wire_to_handler(mpps_handler);

// Start workflow
if (auto result = workflow.start(); !result) {
    std::cerr << "Failed: " << to_string(result.error()) << std::endl;
}

// Manual processing is also available
auto result = workflow.process(mpps_event::completed, mpps_data);
if (result) {
    std::cout << "Delivered to: " << result->destination << std::endl;
}
```

**Correlation and Tracing:**

The workflow automatically generates correlation IDs and trace IDs for distributed tracing:

```cpp
// Result includes tracing information
auto result = workflow.process(mpps_event::completed, mpps_data);
if (result) {
    std::cout << "Correlation ID: " << result->correlation_id << std::endl;
    std::cout << "Trace ID: " << result->trace_id << std::endl;
}
```

- **Correlation ID**: UUID v4 format (e.g., `550e8400-e29b-41d4-a716-446655440000`)
- **Trace ID**: OpenTelemetry format - 32 hex characters (e.g., `4bf92f3577b34da6a3ce929d0e0e4736`)

**Queue Fallback:**

When outbound delivery fails, messages are automatically enqueued to the queue_manager for reliable retry:

```cpp
// Enable queue fallback in configuration
auto config = workflow_config_builder::create()
    .default_destination("PRIMARY_HIS")
    .enable_queue_fallback(true)
    .build();

// When delivery fails, result indicates queued status
auto result = workflow.process(mpps_event::completed, mpps_data);
if (result && result->delivery_method == delivery_method::queued) {
    std::cout << "Message queued for later delivery" << std::endl;
}
```

**Statistics:**

```cpp
// Get runtime statistics
auto stats = workflow.get_statistics();
std::cout << "Processed: " << stats.messages_processed << std::endl;
std::cout << "Successes: " << stats.messages_succeeded << std::endl;
std::cout << "Failures: " << stats.messages_failed << std::endl;

// Reset statistics
workflow.reset_statistics();
```

**Error Codes:** -900 to -909 (workflow module range)

---

## Extension Points

### Adding a New Message Handler

1. **Create handler class:**

```cpp
// include/pacs/bridge/protocol/hl7/my_handler.h
class my_handler : public message_handler {
public:
    result<hl7_message> handle(const hl7_message& message) override;
    std::vector<std::string> supported_message_types() const override {
        return {"MYT^M01", "MYT^M02"};
    }
};
```

2. **Register with router:**

```cpp
router.add_handler("my_destination", std::make_unique<my_handler>());
```

3. **Add routing rule:**

```yaml
routing_rules:
  - name: "My Messages"
    message_type_pattern: "MYT^*"
    destination: "my_destination"
```

### Adding a New FHIR Resource

1. **Create resource handler:**

```cpp
class my_resource_handler : public resource_handler {
public:
    http_response handle_get(const http_request& request) override;
    http_response handle_post(const http_request& request) override;
};
```

2. **Register with FHIR server:**

```cpp
fhir_server.register_handler("/fhir/r4/MyResource",
    std::make_unique<my_resource_handler>());
```

### Adding Custom Metrics

```cpp
// Register custom metric
auto& metrics = bridge_metrics_collector::instance();
metrics.register_counter("my_custom_metric", "Description", {"label1"});

// Record values
metrics.increment_counter("my_custom_metric", {{"label1", "value1"}});
```

---

## Module Dependencies

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Internal Dependencies                         │
│                                                                       │
│  ┌─────────────┐                                                     │
│  │   config    │◄─────────────────────────────────────────┐         │
│  └─────────────┘                                           │         │
│         ▲                                                  │         │
│         │                                                  │         │
│  ┌──────┴──────┐    ┌─────────────┐    ┌─────────────┐    │         │
│  │   security  │    │  monitoring │    │   testing   │    │         │
│  └─────────────┘    └─────────────┘    └─────────────┘    │         │
│         ▲                  ▲                               │         │
│         │                  │                               │         │
│  ┌──────┴──────────────────┴────────────────────────────┐ │         │
│  │                    Core Modules                       │ │         │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────────┐  │ │         │
│  │  │  protocol  │  │    mllp    │  │      fhir      │  │ │         │
│  │  └─────┬──────┘  └─────┬──────┘  └────────┬───────┘  │ │         │
│  │        │               │                  │          │ │         │
│  │  ┌─────▼───────────────▼──────────────────▼───────┐  │ │         │
│  │  │                   router                        │  │ │         │
│  │  └────────────────────┬───────────────────────────┘  │ │         │
│  │                       │                               │ │         │
│  │  ┌────────────────────▼───────────────────────────┐  │ │         │
│  │  │                  mapping                        │  │ │         │
│  │  └────────────────────┬───────────────────────────┘  │ │         │
│  │                       │                               │ │         │
│  │  ┌──────────┬─────────▼─────────┬──────────────────┐ │ │         │
│  │  │  cache   │    pacs_adapter   │      queue       │ │ │         │
│  │  └──────────┘───────────────────┘──────────────────┘ │ │         │
│  └───────────────────────────────────────────────────────┘ │         │
│                                                            │         │
└────────────────────────────────────────────────────────────┘         │
                                                                       │
┌─────────────────────────────────────────────────────────────────────┐
│                     External Dependencies                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │
│  │ pacs_system │  │network_system│ │logger_system│  │ common_system│ │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Related Documentation

- [Architecture Overview](architecture.md) - System architecture
- [Contributing Guidelines](contributing.md) - How to contribute
- [API Reference](../api/hl7-messages.md) - API specifications
