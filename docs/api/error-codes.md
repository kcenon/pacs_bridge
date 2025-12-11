# Error Codes Reference

> **Version:** 1.0.0
> **Last Updated:** 2025-12-10

---

## Table of Contents

- [Error Code Format](#error-code-format)
- [Error Categories](#error-categories)
- [Configuration Errors](#configuration-errors)
- [Connection Errors](#connection-errors)
- [Message Processing Errors](#message-processing-errors)
- [Queue Errors](#queue-errors)
- [PACS Errors](#pacs-errors)
- [Security Errors](#security-errors)
- [Resolution Steps](#resolution-steps)

---

## Error Code Format

PACS Bridge uses structured error codes in the format:

```
PACS-XXXX
```

Where:
- `PACS` - Error prefix
- `XXXX` - 4-digit error code

### Error Response Format

```json
{
  "error": {
    "code": "PACS-1001",
    "message": "Configuration file not found",
    "details": "Unable to locate configuration file at /etc/pacs_bridge/config.yaml",
    "timestamp": "2025-12-10T12:00:00Z",
    "trace_id": "abc123"
  }
}
```

---

## Error Categories

| Range | Category | Description |
|-------|----------|-------------|
| 1000-1999 | Configuration | Configuration and startup errors |
| 2000-2999 | Connection | Network and connection errors |
| 3000-3999 | Message Processing | HL7/FHIR message errors |
| 4000-4999 | Queue | Message queue errors |
| 5000-5999 | PACS | DICOM/PACS integration errors |
| 6000-6999 | Security | Authentication and authorization errors |
| 9000-9999 | System | Internal system errors |

---

## Configuration Errors

### PACS-1001: Configuration File Not Found

**Description:** The specified configuration file does not exist.

**Cause:** Invalid path or missing file.

**Resolution:**
1. Verify the configuration file path
2. Ensure the file exists and is readable
3. Check file permissions

```bash
ls -la /etc/pacs_bridge/config.yaml
```

---

### PACS-1002: Invalid Configuration Format

**Description:** The configuration file contains syntax errors.

**Cause:** YAML parsing error or invalid structure.

**Resolution:**
1. Validate YAML syntax
2. Check for indentation errors
3. Verify required fields are present

```bash
# Validate YAML syntax
python3 -c "import yaml; yaml.safe_load(open('/etc/pacs_bridge/config.yaml'))"
```

---

### PACS-1003: Missing Required Field

**Description:** A required configuration field is not specified.

**Cause:** Incomplete configuration.

**Resolution:**
1. Check the error message for the missing field name
2. Add the required field to configuration
3. Restart the service

---

### PACS-1004: Invalid Port Number

**Description:** Port number is outside valid range (1-65535).

**Cause:** Invalid port configuration.

**Resolution:**
1. Use a port number between 1 and 65535
2. Ports below 1024 may require root privileges

---

### PACS-1005: Invalid Duration Format

**Description:** Duration value is not in valid format.

**Cause:** Wrong duration syntax.

**Resolution:**
1. Use valid duration format: `30s`, `5m`, `1h`
2. Valid units: `s` (seconds), `m` (minutes), `h` (hours)

---

### PACS-1006: Certificate Not Found

**Description:** TLS certificate file not found.

**Cause:** Invalid certificate path.

**Resolution:**
1. Verify certificate file exists
2. Check file permissions
3. Ensure path is absolute

```bash
ls -la /etc/pacs_bridge/certs/
openssl x509 -in /etc/pacs_bridge/certs/server.crt -noout -text
```

---

## Connection Errors

### PACS-2001: Connection Refused

**Description:** Unable to connect to remote host.

**Cause:** Remote service not running or wrong port.

**Resolution:**
1. Verify remote host is running
2. Check hostname and port
3. Test connectivity with telnet/nc

```bash
telnet ris.hospital.local 2576
```

---

### PACS-2002: Connection Timeout

**Description:** Connection attempt timed out.

**Cause:** Network latency, firewall, or unresponsive host.

**Resolution:**
1. Check network connectivity
2. Verify firewall rules
3. Increase timeout if needed

```yaml
pacs:
  timeout: 60s  # Increase timeout
```

---

### PACS-2003: TLS Handshake Failed

**Description:** TLS/SSL handshake could not be completed.

**Cause:** Certificate mismatch, protocol version, or cipher suite incompatibility.

**Resolution:**
1. Verify certificates are valid
2. Check TLS version compatibility
3. Review cipher suite configuration

```bash
openssl s_client -connect localhost:2576 -debug
```

---

### PACS-2004: Connection Reset

**Description:** Connection was reset by the remote host.

**Cause:** Remote service crashed or forcibly closed connection.

**Resolution:**
1. Check remote service logs
2. Verify message format is correct
3. Review timeout settings

---

### PACS-2005: Host Not Found

**Description:** DNS resolution failed.

**Cause:** Invalid hostname or DNS issues.

**Resolution:**
1. Verify hostname is correct
2. Check DNS configuration
3. Try using IP address

```bash
nslookup ris.hospital.local
```

---

### PACS-2006: Connection Limit Reached

**Description:** Maximum connection limit exceeded.

**Cause:** Too many concurrent connections.

**Resolution:**
1. Increase `max_connections` limit
2. Investigate connection leak
3. Check for slow consumers

```yaml
hl7:
  listener:
    max_connections: 200  # Increase limit
```

---

## Message Processing Errors

### PACS-3001: Invalid HL7 Message Format

**Description:** Message does not conform to HL7 structure.

**Cause:** Malformed message or missing required segments.

**Resolution:**
1. Verify message starts with MSH segment
2. Check segment delimiters
3. Validate required fields

---

### PACS-3002: Unknown Message Type

**Description:** Message type is not supported.

**Cause:** Unrecognized message type in MSH-9.

**Resolution:**
1. Check supported message types
2. Configure routing rule for message type
3. Update message sender if incorrect

---

### PACS-3003: Missing Required Field

**Description:** A required HL7 field is empty.

**Cause:** Incomplete message from sender.

**Resolution:**
1. Check which field is missing (see log details)
2. Contact sender system administrator
3. Configure default value if appropriate

---

### PACS-3004: Invalid Field Value

**Description:** Field value does not match expected format.

**Cause:** Type mismatch or constraint violation.

**Resolution:**
1. Verify field value format
2. Check for encoding issues
3. Validate against data dictionary

---

### PACS-3005: Patient Not Found

**Description:** Patient lookup failed in cache.

**Cause:** Patient not registered before order.

**Resolution:**
1. Ensure ADT message sent before order
2. Check patient cache TTL
3. Verify patient ID matching

---

### PACS-3006: Mapping Failed

**Description:** HL7 to DICOM mapping failed.

**Cause:** Unknown procedure code or missing mapping.

**Resolution:**
1. Check mapping configuration
2. Add missing procedure code mapping
3. Verify modality configuration

```yaml
mapping:
  procedure_to_modality:
    "NEW_CODE": "CT"  # Add missing mapping
```

---

### PACS-3007: Message Too Large

**Description:** Message exceeds maximum size limit.

**Cause:** Message larger than configured limit.

**Resolution:**
1. Increase `max_message_size`
2. Review message content
3. Split large messages if possible

```yaml
hl7:
  listener:
    max_message_size: 20971520  # 20MB
```

---

### PACS-3010: FHIR Validation Error

**Description:** FHIR resource failed validation.

**Cause:** Resource does not conform to profile.

**Resolution:**
1. Check OperationOutcome for details
2. Verify resource structure
3. Include required elements

---

## Queue Errors

### PACS-4001: Queue Full

**Description:** Message queue has reached maximum capacity.

**Cause:** Queue not draining fast enough.

**Resolution:**
1. Increase `max_queue_size`
2. Add more worker threads
3. Investigate destination availability

```yaml
queue:
  max_queue_size: 100000
  worker_count: 8
```

---

### PACS-4002: Message Expired

**Description:** Message exceeded time-to-live.

**Cause:** Unable to deliver within TTL.

**Resolution:**
1. Check destination availability
2. Increase message TTL
3. Review retry settings

---

### PACS-4003: Dead Letter

**Description:** Message moved to dead letter queue.

**Cause:** Exceeded maximum retry attempts.

**Resolution:**
1. Check dead letter queue
2. Investigate delivery failures
3. Manually reprocess if needed

```sql
-- Query dead letters
SELECT * FROM dead_letters ORDER BY created_at DESC LIMIT 10;
```

---

### PACS-4004: Database Error

**Description:** Queue database operation failed.

**Cause:** SQLite error, disk full, or corruption.

**Resolution:**
1. Check disk space
2. Verify database permissions
3. Run integrity check

```bash
df -h /var/lib/pacs_bridge/
sqlite3 /var/lib/pacs_bridge/queue.db "PRAGMA integrity_check;"
```

---

### PACS-4005: Duplicate Message

**Description:** Message with same ID already exists.

**Cause:** Retry of already processed message.

**Resolution:**
1. This may be expected behavior
2. Check for duplicate sends
3. Review idempotency settings

---

## PACS Errors

### PACS-5001: Association Rejected

**Description:** DICOM association was rejected by PACS.

**Cause:** Invalid AE title or not configured in PACS.

**Resolution:**
1. Verify AE title configuration
2. Add PACS Bridge AE to PACS
3. Check PACS logs

---

### PACS-5002: DIMSE Service Error

**Description:** DICOM service operation failed.

**Cause:** PACS returned error status.

**Resolution:**
1. Check DICOM status code
2. Review PACS logs
3. Verify data format

---

### PACS-5003: MWL Update Failed

**Description:** Modality worklist update failed.

**Cause:** Invalid worklist item or PACS error.

**Resolution:**
1. Verify worklist item data
2. Check PACS connectivity
3. Review PACS MWL configuration

---

### PACS-5004: MPPS Processing Failed

**Description:** MPPS notification processing failed.

**Cause:** Invalid MPPS data or state transition.

**Resolution:**
1. Verify MPPS status values
2. Check procedure step sequence
3. Review PACS MPPS configuration

---

### PACS-5005: Study Not Found

**Description:** Requested study not found in PACS.

**Cause:** Study not stored or wrong identifier.

**Resolution:**
1. Verify study UID/accession number
2. Check if study is archived
3. Query PACS directly

---

## Security Errors

### PACS-6001: Authentication Failed

**Description:** Authentication credentials are invalid.

**Cause:** Wrong username/password or expired token.

**Resolution:**
1. Verify credentials
2. Check token expiration
3. Reset password if needed

---

### PACS-6002: Authorization Failed

**Description:** User not authorized for operation.

**Cause:** Insufficient permissions.

**Resolution:**
1. Check user roles and permissions
2. Review access control configuration
3. Contact administrator

---

### PACS-6003: Certificate Expired

**Description:** TLS certificate has expired.

**Cause:** Certificate validity period ended.

**Resolution:**
1. Renew certificate
2. Update certificate files
3. Restart service

```bash
openssl x509 -in /etc/pacs_bridge/certs/server.crt -noout -dates
```

---

### PACS-6004: Rate Limit Exceeded

**Description:** Too many requests from client.

**Cause:** Rate limit threshold exceeded.

**Resolution:**
1. Reduce request frequency
2. Implement backoff strategy
3. Request rate limit increase

---

### PACS-6005: IP Blocked

**Description:** Client IP address is blocked.

**Cause:** IP in blocklist or repeated failures.

**Resolution:**
1. Check IP blocklist
2. Review security events
3. Whitelist IP if legitimate

---

### PACS-6020: OAuth2 Token Request Failed

**Description:** Failed to obtain access token from OAuth2 authorization server.

**Cause:** Network error, server unavailable, or invalid endpoint.

**Resolution:**
1. Verify OAuth2 token URL is correct
2. Check network connectivity to authorization server
3. Review authorization server logs

---

### PACS-6021: OAuth2 Invalid Credentials

**Description:** OAuth2 client credentials are invalid.

**Cause:** Wrong client_id or client_secret.

**Resolution:**
1. Verify client_id and client_secret
2. Check if credentials have expired
3. Regenerate credentials if needed

---

### PACS-6022: OAuth2 Token Expired

**Description:** OAuth2 access token has expired.

**Cause:** Token lifetime exceeded.

**Resolution:**
1. Token will auto-refresh on next request
2. Verify token_refresh_margin is configured properly
3. Check authorization server clock synchronization

---

### PACS-6023: OAuth2 Refresh Failed

**Description:** Failed to refresh OAuth2 access token.

**Cause:** Refresh token invalid or revoked.

**Resolution:**
1. Re-authenticate to obtain new tokens
2. Check if refresh token was revoked
3. Verify refresh token endpoint

---

### PACS-6024: OAuth2 Scope Denied

**Description:** Requested OAuth2 scope was denied.

**Cause:** Insufficient client permissions.

**Resolution:**
1. Check configured scopes
2. Request required scopes from administrator
3. Review client registration

---

### PACS-6025: Smart-on-FHIR Discovery Failed

**Description:** Failed to discover Smart-on-FHIR configuration.

**Cause:** Discovery endpoint unavailable or invalid response.

**Resolution:**
1. Verify FHIR server base URL
2. Check .well-known/smart-configuration endpoint
3. Ensure FHIR server supports Smart-on-FHIR

---

## EMR Client Errors

EMR Client errors occur when communicating with external EMR/FHIR servers.

### PACS-1000: EMR Connection Failed

**Description:** Failed to connect to EMR server.

**Cause:** Network error, server unavailable, or invalid URL.

**Resolution:**
1. Verify EMR server URL is correct
2. Check network connectivity
3. Verify firewall rules

---

### PACS-1001: EMR Request Timeout

**Description:** EMR request timed out.

**Cause:** Server slow to respond or network latency.

**Resolution:**
1. Increase timeout configuration
2. Check EMR server health
3. Review network latency

---

### PACS-1002: EMR Invalid Response

**Description:** Invalid or malformed response from EMR server.

**Cause:** EMR server returned unexpected format.

**Resolution:**
1. Verify EMR server is FHIR R4 compliant
2. Check Content-Type headers
3. Review EMR server logs

---

### PACS-1003: EMR Resource Not Found

**Description:** Requested resource was not found (HTTP 404).

**Cause:** Resource ID does not exist.

**Resolution:**
1. Verify resource ID is correct
2. Check if resource was deleted
3. Review access permissions

---

### PACS-1004: EMR Unauthorized

**Description:** Authentication failed (HTTP 401).

**Cause:** Invalid or expired credentials.

**Resolution:**
1. Verify OAuth2 configuration
2. Check token expiration
3. Refresh authentication credentials

---

### PACS-1005: EMR Rate Limited

**Description:** Rate limit exceeded (HTTP 429).

**Cause:** Too many requests to EMR server.

**Resolution:**
1. Implement backoff strategy
2. Reduce request frequency
3. Contact EMR administrator for limit increase

---

## Result Posting Errors

Result posting errors occur when posting DiagnosticReport resources to EMR systems.

### PACS-1060: Result Post Failed

**Description:** Failed to post result to EMR.

**Cause:** Network error, server unavailable, or authentication failure.

**Resolution:**
1. Verify EMR server connectivity
2. Check authentication credentials
3. Review EMR server logs

---

### PACS-1061: Result Update Failed

**Description:** Failed to update existing result.

**Cause:** Resource not found, conflict, or authorization error.

**Resolution:**
1. Verify report ID exists
2. Check ETag for optimistic locking
3. Review authorization permissions

---

### PACS-1062: Duplicate Result Detected

**Description:** A result for this study already exists.

**Cause:** Duplicate posting attempt.

**Resolution:**
1. This is expected behavior - duplicate prevention working
2. Use update operation if status change needed
3. Check result tracking for existing entry

---

### PACS-1063: Invalid Result Data

**Description:** Study result data is invalid.

**Cause:** Missing required fields in study_result.

**Resolution:**
1. Ensure study_instance_uid is set
2. Ensure patient_id is set
3. Ensure modality and study_datetime are set

---

### PACS-1064: EMR Rejected Result

**Description:** EMR rejected the DiagnosticReport.

**Cause:** Resource validation failed or business rule violation.

**Resolution:**
1. Check OperationOutcome for details
2. Verify FHIR resource format
3. Review EMR validation rules

---

### PACS-1065: Result Not Found

**Description:** Result not found for update.

**Cause:** Invalid report ID or resource deleted.

**Resolution:**
1. Verify report ID is correct
2. Check if resource was deleted
3. Post as new result if appropriate

---

### PACS-1066: Invalid Status Transition

**Description:** Invalid status transition.

**Cause:** Attempted invalid status change (e.g., final -> registered).

**Resolution:**
1. Check allowed status transitions
2. Use `amended` or `corrected` for post-final changes

---

### PACS-1067: Missing Reference

**Description:** Missing required reference (patient, study, etc.).

**Cause:** Required FHIR reference not provided.

**Resolution:**
1. Ensure patient_reference or patient_id is set
2. Use auto_lookup_patient option if needed
3. Verify all required references

---

### PACS-1068: DiagnosticReport Build Failed

**Description:** Failed to build DiagnosticReport resource.

**Cause:** Builder validation failed.

**Resolution:**
1. Check status, code, and subject are set
2. Review validation errors
3. Verify all required fields

---

### PACS-1069: Tracker Error

**Description:** Result tracker operation failed.

**Cause:** Internal tracking error.

**Resolution:**
1. Check tracker configuration
2. Verify memory limits
3. Review cleanup settings

---

## Resolution Steps

### General Troubleshooting

1. **Check Logs**
   ```bash
   tail -100 /var/log/pacs_bridge/bridge.log
   grep "PACS-XXXX" /var/log/pacs_bridge/bridge.log
   ```

2. **Check Health**
   ```bash
   curl http://localhost:8081/health
   ```

3. **Check Metrics**
   ```bash
   curl http://localhost:8081/metrics | grep error
   ```

4. **Validate Configuration**
   ```bash
   pacs_bridge --config /etc/pacs_bridge/config.yaml --validate
   ```

5. **Test Connectivity**
   ```bash
   # MLLP
   nc -vz localhost 2575

   # PACS
   telnet pacs.hospital.local 11112

   # RIS
   telnet ris.hospital.local 2576
   ```

### Error Escalation

| Severity | Action | Response Time |
|----------|--------|---------------|
| Warning | Monitor and investigate | 24 hours |
| Error | Investigate and resolve | 4 hours |
| Critical | Immediate action | 15 minutes |

---

## Related Documentation

- [Troubleshooting Guide](../user-guide/troubleshooting.md) - Common issues
- [Configuration Guide](../user-guide/configuration.md) - Configuration options
- [Operations Runbook](../operations/runbook.md) - Operational procedures
