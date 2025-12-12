# PACS Bridge Monitoring

This directory contains monitoring configuration files for PACS Bridge.

## Overview

PACS Bridge exports Prometheus metrics via the `/metrics` HTTP endpoint.
These metrics can be scraped by Prometheus and visualized in Grafana.

The health server provides a lightweight HTTP server using BSD sockets for
cross-platform TCP networking. It supports health check probes for Kubernetes
and Prometheus metrics scraping.

## HTTP Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health/live` | GET | Kubernetes liveness probe |
| `/health/ready` | GET | Kubernetes readiness probe |
| `/health/deep` | GET | Deep health check with component details |
| `/metrics` | GET | Prometheus metrics endpoint |

## Metrics Endpoint Configuration

The metrics endpoint is integrated with the health server:

```yaml
monitoring:
  health:
    port: 8081
    bind_address: 0.0.0.0
    base_path: /health
    enable_metrics_endpoint: true
    metrics_path: /metrics
    max_connections: 100
    connection_timeout_seconds: 30
```

## Available Metrics

### HL7 Message Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `hl7_messages_received_total` | Counter | message_type | Total HL7 messages received |
| `hl7_messages_sent_total` | Counter | message_type | Total HL7 messages sent |
| `hl7_message_processing_duration_seconds` | Histogram | message_type | Message processing latency |
| `hl7_message_errors_total` | Counter | message_type, error_type | Total message errors |

### MWL Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `mwl_entries_created_total` | Counter | - | Total MWL entries created |
| `mwl_entries_updated_total` | Counter | - | Total MWL entries updated |
| `mwl_entries_cancelled_total` | Counter | - | Total MWL entries cancelled |
| `mwl_query_duration_seconds` | Histogram | - | MWL query latency |

### Queue Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `queue_depth` | Gauge | destination | Current queue depth |
| `queue_messages_enqueued_total` | Counter | destination | Total messages enqueued |
| `queue_messages_delivered_total` | Counter | destination | Total messages delivered |
| `queue_delivery_failures_total` | Counter | destination | Total delivery failures |
| `queue_dead_letters_total` | Counter | destination | Total dead letters |

### Connection Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `mllp_active_connections` | Gauge | - | Current active MLLP connections |
| `mllp_total_connections` | Counter | - | Total MLLP connections |
| `fhir_active_requests` | Gauge | - | Current active FHIR requests |
| `fhir_requests_total` | Counter | method, resource | Total FHIR requests |

### System Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `process_cpu_seconds_total` | Counter | - | Total CPU time in seconds |
| `process_resident_memory_bytes` | Gauge | - | Resident memory size |
| `process_open_fds` | Gauge | - | Number of open file descriptors |

## Prometheus Configuration

Add PACS Bridge as a scrape target in `prometheus.yml`:

```yaml
scrape_configs:
  - job_name: 'pacs_bridge'
    scrape_interval: 15s
    static_configs:
      - targets: ['pacs-bridge:8081']
    metrics_path: /metrics
```

## Grafana Dashboard

Import the dashboard from `grafana/pacs_bridge_dashboard.json`:

1. In Grafana, go to **Dashboards** > **Import**
2. Upload the JSON file or paste its contents
3. Select your Prometheus datasource
4. Click **Import**

The dashboard includes:
- Overview stats (message rate, connections, queue depth, error rate, latency, memory)
- HL7 message throughput by type
- HL7 processing latency percentiles
- Queue depth by destination
- Queue operations rate
- MWL entry operations
- MWL query latency
- System resource usage

## Alerting Rules

Deploy the alerting rules from `prometheus/alerts.yml`:

```bash
cp prometheus/alerts.yml /etc/prometheus/rules/pacs_bridge_alerts.yml
```

### Critical Alerts

| Alert | Condition | Description |
|-------|-----------|-------------|
| `PACBridgeHighErrorRate` | Error rate > 5% | Message processing errors too high |
| `PACBridgeServiceDown` | Service unreachable | PACS Bridge is not responding |
| `PACBridgeQueueCritical` | Queue depth > 10000 | Message queue is backing up |
| `PACBridgeDeadLettersGrowing` | >10 dead letters in 15m | Messages failing permanently |

### Warning Alerts

| Alert | Condition | Description |
|-------|-----------|-------------|
| `PACBridgeHighLatency` | P95 latency > 500ms | Processing latency elevated |
| `PACBridgeHighMemory` | Memory > 800MB | Memory usage high |
| `PACBridgeQueueWarning` | Queue depth > 1000 | Queue starting to back up |
| `PACBridgeNoMessages` | No messages in 10m | Not receiving any messages |
| `PACBridgeDeliveryFailures` | Failure rate > 0.1/s | Message delivery issues |
| `PACBridgeConnectionDrop` | Lost >5 connections in 5m | MLLP connections dropping |

## Usage in Code

### Starting the HTTP Server

```cpp
#include <pacs/bridge/monitoring/health_checker.h>
#include <pacs/bridge/monitoring/health_server.h>
#include <pacs/bridge/monitoring/bridge_metrics.h>

using namespace pacs::bridge::monitoring;

// Create health checker with configuration
health_config health_cfg;
health_checker checker(health_cfg);

// Configure health server
health_server::config server_cfg;
server_cfg.port = 8081;
server_cfg.bind_address = "0.0.0.0";
server_cfg.base_path = "/health";
server_cfg.enable_metrics_endpoint = true;
server_cfg.metrics_path = "/metrics";

// Create and start server
health_server server(checker, server_cfg);
if (server.start()) {
    std::cout << "Health server started at " << server.metrics_url() << std::endl;
}

// Server automatically integrates with bridge_metrics_collector
// The /metrics endpoint returns Prometheus-formatted metrics
```

### Automatic Metrics Integration

When the health server starts, it automatically configures the metrics provider
to use `bridge_metrics_collector::get_prometheus_metrics()`. No manual setup
is required for basic usage.

To use a custom metrics provider:

```cpp
server.set_metrics_provider([]() {
    return custom_metrics_function();
});
```

### Manual Metrics Collection Setup

```cpp
#include <pacs/bridge/monitoring/bridge_metrics.h>

// Initialize metrics collector
auto& metrics = pacs::bridge::monitoring::bridge_metrics_collector::instance();
metrics.initialize("pacs_bridge", 0);  // Port 0 = HTTP disabled
metrics.set_enabled(true);
```

### Recording Metrics

```cpp
// Record HL7 message
metrics.record_hl7_message_received("ADT");

// Time message processing
{
    PACS_BRIDGE_TIME_HL7_PROCESSING("ADT");
    // ... process message ...
}

// Record MWL operations
metrics.record_mwl_entry_created();
metrics.record_mwl_query_duration(duration);

// Update queue metrics
metrics.set_queue_depth("pacs_destination", queue.size());
metrics.record_message_enqueued("pacs_destination");

// MLLP connection metrics (automatically tracked by MLLP server/client)
// Server: records on accept, updates active count on connect/disconnect
// Client: records on successful connect
metrics.record_mllp_connection();
metrics.set_mllp_active_connections(active_count);
```
