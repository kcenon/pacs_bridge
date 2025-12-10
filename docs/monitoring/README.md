# PACS Bridge Monitoring

This directory contains monitoring configuration files for PACS Bridge.

## Overview

PACS Bridge exports Prometheus metrics via the `/metrics` HTTP endpoint.
These metrics can be scraped by Prometheus and visualized in Grafana.

## Metrics Endpoint

The metrics endpoint is integrated with the health server:

```yaml
monitoring:
  health:
    port: 8081
    base_path: /health
    enable_metrics_endpoint: true
    metrics_path: /metrics
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

### Enabling Metrics Collection

```cpp
#include <pacs/bridge/monitoring/bridge_metrics.h>

// Initialize metrics collector
auto& metrics = pacs::bridge::monitoring::bridge_metrics_collector::instance();
metrics.initialize("pacs_bridge", 9090);

// Connect to health server
health_server server(checker, config);
server.set_metrics_provider([]() {
    return pacs::bridge::monitoring::bridge_metrics_collector::instance()
        .get_prometheus_metrics();
});
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
```
