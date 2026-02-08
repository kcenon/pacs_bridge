# Troubleshooting Guide

> **Version:** 0.2.0.0
> **Last Updated:** 2026-02-08

---

## Table of Contents

- [Diagnostic Procedures](#diagnostic-procedures)
- [Common Issues](#common-issues)
- [Error Messages](#error-messages)
- [Connection Problems](#connection-problems)
- [Message Processing Issues](#message-processing-issues)
- [Performance Issues](#performance-issues)
- [Log Analysis](#log-analysis)
- [Support](#support)

---

## Diagnostic Procedures

### Health Check

Check the overall system health:

```bash
curl http://localhost:8081/health
```

**Healthy Response:**

```json
{
  "status": "healthy",
  "timestamp": "2025-12-10T12:00:00Z",
  "components": {
    "hl7_listener": "up",
    "pacs_connection": "up",
    "patient_cache": "up",
    "message_queue": "up"
  }
}
```

**Unhealthy Response:**

```json
{
  "status": "unhealthy",
  "timestamp": "2025-12-10T12:00:00Z",
  "components": {
    "hl7_listener": "up",
    "pacs_connection": "down",
    "patient_cache": "up",
    "message_queue": "degraded"
  },
  "issues": [
    "PACS connection timeout",
    "Queue depth exceeds threshold"
  ]
}
```

### Metrics Check

View Prometheus metrics:

```bash
curl http://localhost:8081/metrics
```

Key metrics to monitor:

| Metric | Description | Alert Threshold |
|--------|-------------|-----------------|
| `hl7_messages_received_total` | Total messages received | - |
| `hl7_message_errors_total` | Total message errors | > 5% of received |
| `queue_depth` | Current queue depth | > 1000 |
| `mllp_active_connections` | Active connections | > 90% of max |
| `process_resident_memory_bytes` | Memory usage | > 800MB |

### Log Inspection

View recent logs:

```bash
# View last 100 log entries
tail -100 /var/log/pacs_bridge/bridge.log

# Filter for errors
grep '"level":"error"' /var/log/pacs_bridge/bridge.log | tail -20

# Filter for specific message type
grep 'ADT' /var/log/pacs_bridge/bridge.log | tail -20
```

### Connection Test

Test MLLP connectivity:

```bash
# Test with netcat (Linux/macOS)
echo -e "\x0BMSH|^~\&|TEST|TEST|PACS_BRIDGE|PACS|20251210120000||ADT^A01|TEST001|P|2.5\x1C\x0D" | nc localhost 2575
```

---

## Common Issues

### Issue: Service Won't Start

**Symptoms:**
- Service fails to start
- Error message about configuration

**Possible Causes:**

1. **Invalid Configuration**
   ```bash
   # Validate configuration
   pacs_bridge --config /etc/pacs_bridge/config.yaml --validate
   ```

2. **Port Already in Use**
   ```bash
   # Check if port is in use
   lsof -i :2575
   netstat -tlnp | grep 2575
   ```

3. **Permission Denied**
   ```bash
   # Check file permissions
   ls -la /etc/pacs_bridge/config.yaml
   ls -la /var/log/pacs_bridge/
   ls -la /var/lib/pacs_bridge/
   ```

**Solutions:**

| Cause | Solution |
|-------|----------|
| Invalid config | Fix configuration errors and restart |
| Port in use | Stop conflicting service or change port |
| Permission denied | Fix file/directory permissions |

### Issue: PACS Connection Failure

**Symptoms:**
- Health check shows `pacs_connection: down`
- MWL updates failing
- Log shows connection timeouts

**Possible Causes:**

1. **Network Connectivity**
   ```bash
   # Test network connectivity
   ping pacs.hospital.local
   telnet pacs.hospital.local 11112
   ```

2. **Firewall Blocking**
   ```bash
   # Check firewall rules (Linux)
   iptables -L -n | grep 11112
   ```

3. **Wrong AE Title**
   - Verify AE titles match PACS configuration
   - Check for case sensitivity

**Solutions:**

```yaml
# Verify PACS configuration
pacs:
  host: "pacs.hospital.local"  # Verify hostname
  port: 11112                   # Verify port
  ae_title: "PACS_BRIDGE"       # Must match PACS config
  called_ae: "PACS_SCP"         # Must match PACS config
  timeout: 60s                  # Increase if network is slow
```

### Issue: HL7 Messages Not Received

**Symptoms:**
- No messages appearing in logs
- Sending systems report connection failures

**Possible Causes:**

1. **Listener Not Started**
   ```bash
   # Check listener is running
   netstat -tlnp | grep 2575
   ```

2. **TLS Mismatch**
   - Sender expects TLS but listener has TLS disabled
   - Certificate validation failure

3. **Message Size Too Large**
   - Message exceeds `max_message_size`

**Solutions:**

| Cause | Solution |
|-------|----------|
| Listener not started | Check service status, review logs |
| TLS mismatch | Align TLS configuration between systems |
| Message too large | Increase `max_message_size` |

### Issue: Messages Stuck in Queue

**Symptoms:**
- High `queue_depth` metric
- Messages not being delivered
- Dead letter queue growing

**Possible Causes:**

1. **Destination Unreachable**
   ```bash
   # Test destination connectivity
   telnet ris.hospital.local 2576
   ```

2. **All Workers Busy**
   - Check `worker_count` configuration
   - High message processing latency

3. **Destination Rejecting Messages**
   - Check destination system logs
   - Verify message format compatibility

**Solutions:**

```yaml
queue:
  worker_count: 8           # Increase workers
  max_retry_count: 10       # More retries
  initial_retry_delay: 10s  # Longer delay
```

---

## Error Messages

### Configuration Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `Missing required field: pacs.host` | PACS host not configured | Add `pacs.host` to config |
| `Invalid port number: 99999` | Port out of range | Use port 1-65535 |
| `Failed to load certificate` | TLS cert file not found | Check cert path |
| `Invalid duration format` | Wrong duration syntax | Use `30s`, `5m`, `1h` format |

### Connection Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `Connection refused` | Remote service not running | Start remote service |
| `Connection timeout` | Network latency or firewall | Check network/firewall |
| `TLS handshake failed` | Certificate mismatch | Verify certificates |
| `Association rejected` | AE title mismatch | Verify AE titles |

### Message Processing Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `Invalid HL7 message format` | Malformed message | Check message structure |
| `Unknown message type` | Unsupported message | Add routing rule |
| `Patient not found` | Patient not in cache | Check patient registration |
| `Mapping failed` | Invalid field values | Check field mappings |

### Queue Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `Queue full` | Max size reached | Increase `max_queue_size` |
| `Message expired` | TTL exceeded | Investigate delivery issues |
| `Dead letter` | Max retries exceeded | Check destination availability |
| `Database error` | SQLite issue | Check disk space/permissions |

---

## Connection Problems

### MLLP Connection Issues

**Diagnosis:**

```bash
# Check active connections
curl http://localhost:8081/metrics | grep mllp_active_connections

# Check connection errors
grep 'connection' /var/log/pacs_bridge/bridge.log | grep -i error
```

**Common Issues:**

1. **Connection Limit Reached**
   ```yaml
   hl7:
     listener:
       max_connections: 100  # Increase if needed
   ```

2. **Idle Timeout Too Short**
   ```yaml
   hl7:
     listener:
       idle_timeout: 600s  # Increase for slow clients
   ```

3. **TLS Certificate Issues**
   ```bash
   # Test certificate
   openssl s_client -connect localhost:2576

   # Verify certificate
   openssl x509 -in /etc/pacs_bridge/certs/server.crt -text -noout
   ```

### PACS Connection Issues

**Diagnosis:**

```bash
# Check PACS metrics
curl http://localhost:8081/metrics | grep pacs

# Test DICOM echo
dcmtk/echoscu -aet PACS_BRIDGE -aec PACS_SCP pacs.hospital.local 11112
```

**Common Issues:**

1. **AE Title Not Configured**
   - Add PACS Bridge AE title to PACS configuration

2. **Network Timeout**
   ```yaml
   pacs:
     timeout: 120s  # Increase for slow networks
   ```

---

## Message Processing Issues

### Messages Not Routed

**Diagnosis:**

```bash
# Check routing rules applied
grep 'route' /var/log/pacs_bridge/bridge.log
```

**Check routing configuration:**

```yaml
routing_rules:
  - name: "ADT Messages"
    message_type_pattern: "ADT^A*"  # Check pattern
    destination: "patient_cache"
    enabled: true  # Ensure enabled
```

### Patient Not Found

**Diagnosis:**

```bash
# Check patient cache size
curl http://localhost:8081/metrics | grep patient_cache
```

**Solutions:**

1. Ensure ADT messages are sent before orders
2. Increase cache TTL:
   ```yaml
   patient_cache:
     ttl: 7200s  # 2 hours
   ```

3. Increase cache size:
   ```yaml
   patient_cache:
     max_entries: 50000
   ```

### Mapping Errors

**Diagnosis:**

Check logs for mapping failures:

```bash
grep 'mapping' /var/log/pacs_bridge/bridge.log | grep -i error
```

**Common mapping issues:**

| Field | Issue | Solution |
|-------|-------|----------|
| Patient ID | Missing issuer | Configure `default_patient_id_issuer` |
| Procedure Code | Unknown code | Add to `procedure_to_modality` mapping |
| Modality | Unknown modality | Add to `modality_ae_titles` mapping |

---

## Performance Issues

### High Latency

**Diagnosis:**

```bash
# Check processing latency
curl http://localhost:8081/metrics | grep duration
```

**Solutions:**

| Symptom | Solution |
|---------|----------|
| High message latency | Increase worker threads |
| High queue latency | Add more workers, check destinations |
| High PACS latency | Optimize PACS or increase timeout |

### High Memory Usage

**Diagnosis:**

```bash
# Check memory usage
curl http://localhost:8081/metrics | grep memory

# Check process memory
ps aux | grep pacs_bridge
```

**Solutions:**

1. Reduce cache size:
   ```yaml
   patient_cache:
     max_entries: 5000  # Reduce if needed
   ```

2. Reduce queue size:
   ```yaml
   queue:
     max_queue_size: 25000  # Reduce if needed
   ```

### High CPU Usage

**Diagnosis:**

```bash
# Check CPU usage
top -p $(pgrep pacs_bridge)
```

**Solutions:**

1. Enable connection pooling
2. Reduce logging level:
   ```yaml
   logging:
     level: "warn"  # Reduce from debug/info
   ```

---

## Log Analysis

### Log Format

PACS Bridge uses JSON-formatted logs:

```json
{
  "timestamp": "2025-12-10T12:00:00.000Z",
  "level": "info",
  "message": "HL7 message received",
  "message_type": "ADT^A01",
  "message_id": "MSG001",
  "source": "HIS",
  "processing_time_ms": 15
}
```

### Useful Log Queries

**Find all errors in last hour:**

```bash
grep '"level":"error"' /var/log/pacs_bridge/bridge.log | \
  awk -v d="$(date -d '1 hour ago' +%Y-%m-%dT%H:)" '$0 ~ d'
```

**Count message types:**

```bash
grep 'message_type' /var/log/pacs_bridge/bridge.log | \
  jq -r '.message_type' | sort | uniq -c | sort -rn
```

**Find slow messages (>100ms):**

```bash
cat /var/log/pacs_bridge/bridge.log | \
  jq 'select(.processing_time_ms > 100)'
```

### Enable Debug Logging

Temporarily enable debug logging for troubleshooting:

```yaml
logging:
  level: "debug"
  include_source_location: true
```

> **Note:** Remember to disable debug logging after troubleshooting to avoid excessive log volume.

---

## Support

### Before Contacting Support

Gather the following information:

1. **System Information**
   ```bash
   pacs_bridge --version
   uname -a
   cat /etc/os-release
   ```

2. **Configuration** (remove sensitive data)
   ```bash
   cat /etc/pacs_bridge/config.yaml
   ```

3. **Health Status**
   ```bash
   curl http://localhost:8081/health
   ```

4. **Recent Logs**
   ```bash
   tail -500 /var/log/pacs_bridge/bridge.log > bridge_logs.txt
   ```

5. **Metrics Snapshot**
   ```bash
   curl http://localhost:8081/metrics > metrics.txt
   ```

### Getting Help

- **GitHub Issues:** https://github.com/kcenon/pacs_bridge/issues
- **Documentation:** https://github.com/kcenon/pacs_bridge/docs

### Reporting Bugs

When reporting bugs, include:

1. Steps to reproduce
2. Expected behavior
3. Actual behavior
4. System information
5. Relevant log excerpts
6. Configuration (sanitized)

---

## Related Documentation

- [Configuration Guide](configuration.md) - Configuration options
- [Message Flows](message-flows.md) - Message flow documentation
- [Error Codes](../api/error-codes.md) - Complete error code reference
- [Operations Runbook](../operations/runbook.md) - Operational procedures
