# Error Codes Reference

> **Version:** 0.3.0.0
> **Last Updated:** 2026-02-08

---

## Table of Contents

- [Error Code Format](#error-code-format)
- [Range Allocation Table](#range-allocation-table)
- [Known Range Collisions](#known-range-collisions)
- [Integration Layer](#integration-layer)
- [Configuration Layer](#configuration-layer)
- [FHIR Layer](#fhir-layer)
- [Messaging Layer](#messaging-layer)
- [Bridge Server](#bridge-server)
- [PACS Adapter Layer](#pacs-adapter-layer)
- [HL7 Protocol Layer](#hl7-protocol-layer)
- [Mapping Layer](#mapping-layer)
- [Router Layer](#router-layer)
- [Cache Layer](#cache-layer)
- [Workflow Layer](#workflow-layer)
- [Performance Layer](#performance-layer)
- [Security Layer](#security-layer)
- [Transport Layer (MLLP)](#transport-layer-mllp)
- [Monitoring Layer](#monitoring-layer)
- [Tracing Layer](#tracing-layer)
- [Testing Layer](#testing-layer)
- [EMR Layer](#emr-layer)
- [Resolution Steps](#resolution-steps)
- [Related Documentation](#related-documentation)

---

## Error Code Format

PACS Bridge uses C++ `enum class` types with **negative integer values** for
internal error codes. Each module defines its own strongly-typed error enum that
is returned via `std::expected<T, error>` or the `Result<T>` alias.

```cpp
// Example: workflow_error from workflow/mpps_hl7_workflow.h
enum class workflow_error : int {
    not_running          = -900,
    already_running      = -901,
    mapping_failed       = -902,
    // ...
};
```

### Conventions

| Aspect | Convention |
|--------|-----------|
| Base type | `enum class xxx_error : int` |
| Sign | Negative integers |
| Overall range | -700 to -1124 |
| Conversion | `to_error_code(e)` returns `int`, `to_string(e)` returns description |
| Transport | Returned via `std::expected<T, xxx_error>` or `Result<T>` |

### Error Response Format (External APIs)

When errors are exposed via REST or health endpoints, they are wrapped in JSON:

```json
{
  "error": {
    "code": -900,
    "message": "Workflow is not running",
    "module": "workflow",
    "details": "Call start() before processing messages",
    "timestamp": "2026-02-08T12:00:00Z",
    "trace_id": "abc123"
  }
}
```

---

## Range Allocation Table

> Sorted by range start. **Bold** entries were relocated in
> [#344](https://github.com/kcenon/pacs_bridge/issues/344).
> Collision markers (!) indicate overlapping ranges.

| Range | Module | Enum Type | Header | Values |
|-------|--------|-----------|--------|--------|
| -700 to -705 | Integration | `integration_error` | `integration/network_adapter.h` | 6 |
| **-750 to -759** | **Configuration** | **`config_error`** | **`config/bridge_config.h`** | **10** |
| -800 to -807 | FHIR ! | `fhir_error` | `fhir/fhir_types.h` | 8 |
| -800 to -808 | Message Bus ! | `message_bus_error` | `messaging/hl7_message_bus.h` | 9 |
| -800 to -809 | Bridge Server ! | `bridge_server_error` | `bridge_server.h` | 10 |
| -800 to -812 | Database ! | `database_error` | `integration/database_adapter.h` | 13 |
| -810 to -818 | Request Handler | `request_error` | `messaging/hl7_request_handler.h` | 9 |
| -820 to -828 | Pipeline | `pipeline_error` | `messaging/hl7_pipeline.h` | 9 |
| -830 to -835 | Backend | `backend_error` | `messaging/messaging_backend.h` | 6 |
| -850 to -859 | ADT Handler ! | `adt_error` | `protocol/hl7/adt_handler.h` | 10 |
| -850 to -863 | PACS Adapter ! | `pacs_error` | `integration/pacs_adapter.h` | 14 |
| -860 to -869 | ORM Handler | `orm_error` | `protocol/hl7/orm_handler.h` | 10 |
| -870 to -878 | MWL Adapter ! | `mwl_adapter_error` | `integration/mwl_adapter.h` | 9 |
| -870 to -879 | SIU Handler ! | `siu_error` | `protocol/hl7/siu_handler.h` | 10 |
| -880 to -884 | Handler Base ! | `handler_error` | `protocol/hl7/hl7_handler_base.h` | 5 |
| **-880 to -893** | **MPPS Handler** ! | **`mpps_error`** | **`pacs_adapter/mpps_handler.h`** | **14** |
| -890 to -894 | Handler Registry ! | `registry_error` | `protocol/hl7/hl7_handler_registry.h` | 5 |
| -900 to -909 | Workflow | `workflow_error` | `workflow/mpps_hl7_workflow.h` | 10 |
| -910 to -919 | Queue | `queue_error` | `router/queue_manager.h` | 10 |
| -920 to -925 | Cache ! | `cache_error` | `cache/patient_cache.h` | 6 |
| -920 to -929 | Outbound Router ! | `outbound_error` | `router/outbound_router.h` | 10 |
| -930 to -937 | Reliable Sender ! | `reliable_sender_error` | `router/reliable_outbound_sender.h` | 8 |
| -930 to -938 | DICOM-HL7 Mapper ! | `dicom_hl7_error` | `mapping/dicom_hl7_mapper.h` | 9 |
| -930 to -938 | Message Router ! | `router_error` | `router/message_router.h` | 9 |
| -940 to -948 | HL7-DICOM Mapper ! | `mapping_error` | `mapping/hl7_dicom_mapper.h` | 9 |
| -940 to -949 | Performance ! | `performance_error` | `performance/performance_types.h` | 10 |
| -950 to -954 | Trace Manager ! | `trace_error` | `tracing/trace_manager.h` | 5 |
| -950 to -958 | Access Control ! | `access_error` | `security/access_control.h` | 9 |
| -950 to -958 | FHIR-DICOM Mapper ! | `fhir_dicom_error` | `mapping/fhir_dicom_mapper.h` | 9 |
| -950 to -967 | HL7 Protocol ! | `hl7_error` | `protocol/hl7/hl7_types.h` | 18 |
| -960 to -965 | Exporter ! | `exporter_error` | `tracing/exporter_factory.h` | 6 |
| -960 to -969 | Input Validation ! | `validation_error` | `security/input_validator.h` | 10 |
| -960 to -969 | Load Testing ! | `load_error` | `testing/load_types.h` | 10 |
| -970 to -979 | MLLP | `mllp_error` | `mllp/mllp_types.h` | 10 |
| -980 to -985 | Health Check ! | `health_error` | `monitoring/health_types.h` | 6 |
| -980 to -987 | Network ! | `network_error` | `mllp/mllp_network_adapter.h` | 8 |
| -980 to -989 | MWL Client ! | `mwl_error` | `pacs_adapter/mwl_client.h` | 10 |
| -990 to -999 | TLS | `tls_error` | `security/tls_types.h` | 10 |
| -1000 to -1019 | EMR Client | `emr_error` | `emr/emr_types.h` | 20 |
| -1020 to -1034 | OAuth2 | `oauth2_error` | `security/oauth2_types.h` | 15 |
| -1040 to -1048 | Patient Record | `patient_error` | `emr/patient_record.h` | 9 |
| -1060 to -1069 | Result Poster | `result_error` | `emr/result_poster.h` | 10 |
| -1080 to -1089 | Encounter | `encounter_error` | `emr/encounter_context.h` | 10 |
| -1100 to -1108 | EMR Config ! | `emr_config_error` | `config/emr_config.h` | 9 |
| -1100 to -1109 | EMR Adapter ! | `adapter_error` | `emr/emr_adapter.h` | 10 |
| **-1120 to -1124** | **Result Tracker** | **`tracker_error`** | **`emr/result_tracker.h`** | **5** |

**Total: 46 enum types, 431 error codes**

---

## Known Range Collisions

The following ranges have overlapping allocations. Each enum is scoped to its
own `enum class`, so there is no runtime ambiguity — only documentation and log
analysis may be affected. A follow-up issue will reassign ranges to eliminate
collisions.

| Collision Group | Range | Enums Involved |
|----------------|-------|----------------|
| A | -800 to -812 | `fhir_error`, `message_bus_error`, `bridge_server_error`, `database_error` |
| B | -850 to -863 | `adt_error`, `pacs_error` |
| C | -870 to -879 | `mwl_adapter_error`, `siu_error` |
| D | -880 to -894 | `handler_error`, `mpps_error`, `registry_error` |
| E | -920 to -929 | `cache_error`, `outbound_error` |
| F | -930 to -938 | `reliable_sender_error`, `dicom_hl7_error`, `router_error` |
| G | -940 to -949 | `mapping_error`, `performance_error` |
| H | -950 to -967 | `trace_error`, `access_error`, `fhir_dicom_error`, `hl7_error` |
| I | -960 to -969 | `exporter_error`, `validation_error`, `load_error` |
| J | -980 to -989 | `health_error`, `network_error`, `mwl_error` |
| K | -1100 to -1109 | `emr_config_error`, `adapter_error` |

> **Disambiguation:** When the same numeric value appears in logs, identify the
> module from the log source or component name. The `to_error_info()` functions
> include a `module` field for programmatic disambiguation.

---

## Integration Layer

### integration_error (-700 to -705)

**Header:** `include/pacs/bridge/integration/network_adapter.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -700 | `connection_failed` | Connection failed | Check remote host, verify network connectivity |
| -701 | `connection_timeout` | Connection timeout | Increase timeout, check network latency |
| -702 | `send_failed` | Send failed | Verify connection is alive, retry |
| -703 | `receive_failed` | Receive failed | Check remote host status, verify protocol |
| -704 | `tls_handshake_failed` | TLS handshake failed | Verify certificates, check TLS version |
| -705 | `invalid_config` | Invalid configuration | Review adapter configuration settings |

### database_error (-800 to -812)

**Header:** `include/pacs/bridge/integration/database_adapter.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -800 | `connection_failed` | Connection to database failed | Check database host and credentials |
| -801 | `connection_timeout` | Connection timeout exceeded | Increase timeout, check database load |
| -802 | `query_failed` | Query execution failed | Review SQL syntax, check permissions |
| -803 | `prepare_failed` | Statement preparation failed | Verify SQL syntax |
| -804 | `bind_failed` | Parameter binding failed | Check parameter types and values |
| -805 | `transaction_failed` | Transaction operation failed | Check for deadlocks, retry |
| -806 | `pool_exhausted` | Connection pool exhausted | Increase pool size or reduce concurrency |
| -807 | `invalid_config` | Invalid configuration provided | Review database configuration |
| -808 | `constraint_violation` | Database constraint violation | Check unique/FK constraints |
| -809 | `timeout` | Operation timeout | Increase query timeout |
| -810 | `no_result` | No result available | Verify query returns data |
| -811 | `invalid_column` | Invalid column index | Check column count and indices |
| -812 | `type_conversion_failed` | Type conversion failed | Verify column types match expected |

### pacs_error (-850 to -863)

**Header:** `include/pacs/bridge/integration/pacs_adapter.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -850 | `connection_failed` | Connection to PACS server failed | Verify PACS host and port |
| -851 | `query_failed` | Query execution failed | Check query parameters |
| -852 | `store_failed` | Store operation failed | Review DICOM dataset |
| -853 | `invalid_dataset` | Invalid or malformed DICOM dataset | Validate DICOM data |
| -854 | `association_failed` | DICOM association failed | Verify AE title configuration |
| -855 | `timeout` | Operation timeout | Increase PACS timeout |
| -856 | `not_found` | Resource not found | Verify UID/accession number |
| -857 | `duplicate_entry` | Duplicate entry detected | Check for existing data |
| -858 | `validation_failed` | Validation failed | Review data format |
| -859 | `mpps_create_failed` | MPPS N-CREATE failed | Check PACS MPPS configuration |
| -860 | `mpps_update_failed` | MPPS N-SET failed | Verify MPPS state |
| -861 | `mwl_query_failed` | MWL query failed | Check MWL SCP configuration |
| -862 | `storage_failed` | DICOM storage failed | Check disk space and permissions |
| -863 | `invalid_sop_uid` | Invalid SOP Instance UID | Validate UID format |

### mwl_adapter_error (-870 to -878)

**Header:** `include/pacs/bridge/integration/mwl_adapter.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -870 | `init_failed` | Storage initialization failed | Check storage backend configuration |
| -871 | `not_found` | Entry not found | Verify accession number or UID |
| -872 | `duplicate` | Duplicate entry exists | Check for existing worklist item |
| -873 | `invalid_data` | Invalid data provided | Validate MWL entry fields |
| -874 | `query_failed` | Query failed | Check query parameters |
| -875 | `add_failed` | Add operation failed | Review storage backend logs |
| -876 | `update_failed` | Update operation failed | Verify entry exists before update |
| -877 | `delete_failed` | Delete operation failed | Check entry state |
| -878 | `storage_unavailable` | Storage not accessible | Check storage backend health |

---

## Configuration Layer

### config_error (-750 to -759)

**Header:** `include/pacs/bridge/config/bridge_config.h`

> Relocated from -900 range in [#344](https://github.com/kcenon/pacs_bridge/issues/344).

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -750 | `file_not_found` | Configuration file not found | Verify file path: `ls -la /etc/pacs_bridge/config.yaml` |
| -751 | `parse_error` | Failed to parse configuration file | Validate YAML syntax |
| -752 | `validation_error` | Configuration validation failed | Check required fields and value ranges |
| -753 | `missing_required_field` | Required field is missing | Add missing field to configuration |
| -754 | `invalid_value` | Invalid value for configuration field | Check field type and allowed values |
| -755 | `env_var_not_found` | Environment variable not found | Set required environment variable |
| -756 | `invalid_format` | Invalid file format (not YAML or JSON) | Use supported configuration format |
| -757 | `empty_config` | Configuration file is empty | Add configuration content |
| -758 | `circular_include` | Circular include detected | Remove circular file references |
| -759 | `io_error` | IO error reading file | Check file permissions and disk |

### emr_config_error (-1100 to -1108)

**Header:** `include/pacs/bridge/config/emr_config.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -1100 | `config_invalid` | General configuration invalid | Review EMR configuration section |
| -1101 | `missing_url` | Missing required base URL | Add `base_url` to EMR config |
| -1102 | `invalid_auth` | Invalid authentication configuration | Check auth type and credentials |
| -1103 | `missing_credentials` | Missing required credentials | Add client_id/secret or API key |
| -1104 | `invalid_timeout` | Invalid timeout value | Use positive duration value |
| -1105 | `invalid_vendor` | Invalid vendor type | Use supported vendor: epic, cerner, generic |
| -1106 | `invalid_retry` | Invalid retry configuration | Check max_retries and backoff settings |
| -1107 | `invalid_cache` | Invalid cache configuration | Review cache TTL and size settings |
| -1108 | `env_var_not_found` | Environment variable not found | Set required environment variable |

---

## FHIR Layer

### fhir_error (-800 to -807)

**Header:** `include/pacs/bridge/fhir/fhir_types.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -800 | `invalid_resource` | Invalid FHIR resource | Validate resource structure |
| -801 | `resource_not_found` | Resource not found | Verify resource ID |
| -802 | `validation_failed` | Resource validation failed | Check OperationOutcome for details |
| -803 | `unsupported_resource_type` | Unsupported resource type | Use supported FHIR R4 resource types |
| -804 | `server_error` | Server error | Check FHIR server logs |
| -805 | `subscription_error` | Subscription error | Review subscription configuration |
| -806 | `json_parse_error` | JSON parsing error | Validate JSON syntax |
| -807 | `missing_required_field` | Missing required field | Add required FHIR elements |

---

## Messaging Layer

### message_bus_error (-800 to -808)

**Header:** `include/pacs/bridge/messaging/hl7_message_bus.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -800 | `not_started` | Message bus not started | Call `start()` before publishing |
| -801 | `already_started` | Message bus already started | Check lifecycle management |
| -802 | `publish_failed` | Failed to publish message | Check topic and message format |
| -803 | `subscribe_failed` | Failed to subscribe | Verify topic pattern |
| -804 | `invalid_topic` | Invalid topic pattern | Use valid topic format |
| -805 | `subscription_not_found` | Subscription not found | Check subscription ID |
| -806 | `shutting_down` | Message bus shutdown in progress | Wait for shutdown to complete |
| -807 | `backend_init_failed` | Backend initialization failed | Check backend configuration |
| -808 | `conversion_failed` | Message conversion failed | Verify message format |

### request_error (-810 to -818)

**Header:** `include/pacs/bridge/messaging/hl7_request_handler.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -810 | `timeout` | Request timeout | Increase timeout or check handler performance |
| -811 | `no_handler` | No handler registered | Register appropriate handler |
| -812 | `handler_error` | Handler returned error | Check handler logs |
| -813 | `invalid_request` | Invalid request message | Validate message format |
| -814 | `service_unavailable` | Service not available | Check service health |
| -815 | `correlation_not_found` | Correlation ID not found | Verify correlation tracking |
| -816 | `response_failed` | Response generation failed | Check response builder |
| -817 | `connection_lost` | Connection lost during request | Retry with new connection |
| -818 | `cancelled` | Request cancelled | Request was intentionally cancelled |

### pipeline_error (-820 to -828)

**Header:** `include/pacs/bridge/messaging/hl7_pipeline.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -820 | `not_started` | Pipeline not started | Call `start()` before processing |
| -821 | `stage_failed` | Stage processing failed | Check stage logs |
| -822 | `invalid_stage` | Invalid stage configuration | Review stage setup |
| -823 | `stage_not_found` | Stage not found | Verify stage name |
| -824 | `timeout` | Pipeline execution timeout | Increase pipeline timeout |
| -825 | `transform_failed` | Message transformation failed | Check transform function |
| -826 | `filtered` | Stage filter rejected message | Message does not match filter criteria |
| -827 | `max_retries_exceeded` | Maximum retries exceeded | Message moved to dead letter queue |
| -828 | `already_running` | Pipeline already running | Stop before restarting |

### backend_error (-830 to -835)

**Header:** `include/pacs/bridge/messaging/messaging_backend.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -830 | `not_initialized` | Backend not initialized | Initialize backend before use |
| -831 | `already_initialized` | Backend already initialized | Check initialization sequence |
| -832 | `invalid_type` | Invalid backend type | Use supported backend type |
| -833 | `creation_failed` | Backend creation failed | Check backend dependencies |
| -834 | `executor_unavailable` | External executor not available | Verify executor is running |
| -835 | `config_error` | Configuration error | Review backend configuration |

---

## Bridge Server

### bridge_server_error (-800 to -809)

**Header:** `include/pacs/bridge/bridge_server.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -800 | `already_running` | Server is already running | Stop server before restarting |
| -801 | `not_running` | Server is not running | Start server before operations |
| -802 | `invalid_configuration` | Configuration is invalid | Validate configuration file |
| -803 | `config_load_failed` | Failed to load configuration file | Check file path and format |
| -804 | `mpps_init_failed` | MPPS handler initialization failed | Check PACS system connectivity |
| -805 | `outbound_init_failed` | Outbound sender initialization failed | Verify destination configuration |
| -806 | `workflow_init_failed` | Workflow initialization failed | Check workflow dependencies |
| -807 | `health_init_failed` | Health checker initialization failed | Review health check configuration |
| -808 | `shutdown_timeout` | Shutdown timeout exceeded | Force shutdown if necessary |
| -809 | `internal_error` | Internal error | Check detailed error logs |

---

## PACS Adapter Layer

### mpps_error (-880 to -893)

**Header:** `include/pacs/bridge/pacs_adapter/mpps_handler.h`

> Relocated from shared -900 range in [#344](https://github.com/kcenon/pacs_bridge/issues/344).

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -880 | `connection_failed` | Cannot connect to pacs_system MPPS SCP | Verify PACS host and port |
| -881 | `registration_failed` | Registration with MPPS SCP failed | Check AE title configuration |
| -882 | `invalid_dataset` | Invalid MPPS dataset received | Validate DICOM dataset |
| -883 | `status_parse_failed` | MPPS status parsing failed | Check MPPS status values |
| -884 | `missing_attribute` | Missing required attribute in MPPS | Add required DICOM attributes |
| -885 | `callback_failed` | Callback invocation failed | Check callback registration |
| -886 | `not_registered` | Handler not registered | Register MPPS handler first |
| -887 | `already_registered` | Handler already registered | Unregister before re-registering |
| -888 | `invalid_sop_instance` | Invalid MPPS SOP Instance UID | Validate UID format |
| -889 | `unexpected_operation` | Unexpected MPPS operation | Check operation type |
| -890 | `database_error` | Database operation failed | Check database connectivity |
| -891 | `record_not_found` | MPPS record not found in database | Verify SOP Instance UID |
| -892 | `invalid_state_transition` | Invalid state transition | Check MPPS state machine rules |
| -893 | `persistence_disabled` | Persistence is disabled | Enable persistence in configuration |

### mwl_error (-980 to -989)

**Header:** `include/pacs/bridge/pacs_adapter/mwl_client.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -980 | `connection_failed` | Cannot connect to pacs_system | Verify PACS host and port |
| -981 | `add_failed` | MWL add operation failed | Check MWL data validity |
| -982 | `update_failed` | MWL update operation failed | Verify entry exists |
| -983 | `cancel_failed` | MWL cancel operation failed | Check entry state |
| -984 | `query_failed` | MWL query operation failed | Review query parameters |
| -985 | `entry_not_found` | Entry not found | Verify accession number |
| -986 | `duplicate_entry` | Duplicate entry exists | Check for existing worklist item |
| -987 | `invalid_data` | Invalid MWL data | Validate worklist item fields |
| -988 | `timeout` | Connection timeout | Increase timeout or check network |
| -989 | `association_rejected` | DICOM association rejected | Verify AE title in PACS configuration |

---

## HL7 Protocol Layer

### hl7_error (-950 to -967)

**Header:** `include/pacs/bridge/protocol/hl7/hl7_types.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -950 | `empty_message` | Message is empty or null | Verify message content |
| -951 | `missing_msh` | Missing required MSH segment | Ensure message starts with MSH |
| -952 | `invalid_msh` | Invalid MSH segment structure | Check MSH field delimiters |
| -953 | `invalid_segment` | Invalid segment structure | Validate segment format |
| -954 | `missing_required_field` | Required field is missing | Add required HL7 field |
| -955 | `invalid_field_value` | Field value is invalid | Check field value format |
| -956 | `unknown_message_type` | Unknown message type | Verify MSH-9 message type |
| -957 | `unknown_trigger_event` | Unknown trigger event | Check supported trigger events |
| -958 | `invalid_escape_sequence` | Invalid escape sequence | Fix HL7 escape characters |
| -959 | `message_too_large` | Message exceeds maximum size | Increase `max_message_size` or split message |
| -960 | `segment_too_long` | Segment exceeds maximum length | Shorten segment content |
| -961 | `invalid_encoding` | Invalid encoding characters | Check character encoding |
| -962 | `unsupported_version` | Version not supported | Use supported HL7 version (2.3.1–2.5.1) |
| -963 | `duplicate_segment` | Duplicate segment where only one allowed | Remove duplicate segments |
| -964 | `invalid_segment_order` | Segment order violation | Reorder segments per HL7 spec |
| -965 | `validation_failed` | Validation failed | Check validation error details |
| -966 | `parse_error` | Parse error | Verify message structure |
| -967 | `build_error` | Build error | Check message builder parameters |

### adt_error (-850 to -859)

**Header:** `include/pacs/bridge/protocol/hl7/adt_handler.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -850 | `not_adt_message` | Message is not an ADT message | Check MSH-9 message type |
| -851 | `unsupported_trigger_event` | Unsupported ADT trigger event | Use supported events: A01-A08, A28, A31, A40 |
| -852 | `missing_patient_id` | Patient ID not found in message | Add PID-3 patient identifier |
| -853 | `patient_not_found` | Patient not found for update/merge | Ensure ADT^A01 sent before order |
| -854 | `merge_failed` | Merge operation failed | Check merge parameters |
| -855 | `cache_operation_failed` | Cache operation failed | Check patient cache health |
| -856 | `invalid_patient_data` | Invalid patient data | Validate PID segment fields |
| -857 | `duplicate_patient` | Duplicate patient | Check patient ID matching |
| -858 | `handler_not_registered` | Handler not registered | Register ADT handler |
| -859 | `processing_failed` | Processing failed | Check handler error logs |

### orm_error (-860 to -869)

**Header:** `include/pacs/bridge/protocol/hl7/orm_handler.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -860 | `not_orm_message` | Message is not an ORM message | Check MSH-9 message type |
| -861 | `unsupported_order_control` | Unsupported order control code | Use NW, CA, XO, SC |
| -862 | `missing_required_field` | Missing required field | Add Accession Number, Patient ID |
| -863 | `order_not_found` | Order not found for update/cancel | Verify accession number |
| -864 | `mwl_create_failed` | MWL entry creation failed | Check MWL adapter status |
| -865 | `mwl_update_failed` | MWL entry update failed | Verify entry exists |
| -866 | `mwl_cancel_failed` | MWL entry cancel failed | Check entry state |
| -867 | `duplicate_order` | Duplicate order exists | Check for existing order |
| -868 | `invalid_order_data` | Invalid order data | Validate ORC/OBR segments |
| -869 | `processing_failed` | Processing failed | Check handler error logs |

### siu_error (-870 to -879)

**Header:** `include/pacs/bridge/protocol/hl7/siu_handler.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -870 | `not_siu_message` | Message is not an SIU message | Check MSH-9 message type |
| -871 | `unsupported_trigger_event` | Unsupported trigger event | Use supported SIU events |
| -872 | `missing_required_field` | Missing required field | Add Appointment ID, Patient ID |
| -873 | `appointment_not_found` | Appointment not found | Verify appointment ID |
| -874 | `mwl_create_failed` | MWL entry creation failed | Check MWL adapter status |
| -875 | `mwl_update_failed` | MWL entry update failed | Verify entry exists |
| -876 | `mwl_cancel_failed` | MWL entry cancel failed | Check entry state |
| -877 | `duplicate_appointment` | Duplicate appointment exists | Check for existing appointment |
| -878 | `invalid_appointment_data` | Invalid appointment data | Validate SCH/AIS segments |
| -879 | `processing_failed` | Processing failed | Check handler error logs |

### handler_error (-880 to -884)

**Header:** `include/pacs/bridge/protocol/hl7/hl7_handler_base.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -880 | `unsupported_message_type` | Handler cannot process this message type | Route to correct handler |
| -881 | `processing_failed` | Handler processing failed | Check handler-specific error logs |
| -882 | `not_initialized` | Handler not initialized | Initialize handler before use |
| -883 | `busy` | Handler is busy | Retry after a short delay |
| -884 | `invalid_state` | Invalid handler state | Reset handler state |

### registry_error (-890 to -894)

**Header:** `include/pacs/bridge/protocol/hl7/hl7_handler_registry.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -890 | `handler_exists` | Handler already registered | Unregister existing handler first |
| -891 | `no_handler` | No handler found for message | Register handler for this message type |
| -892 | `registration_failed` | Handler registration failed | Check handler configuration |
| -893 | `ambiguous_handler` | Multiple handlers can process message | Refine handler message type matching |
| -894 | `empty_registry` | No handlers registered | Register at least one handler |

---

## Mapping Layer

### dicom_hl7_error (-930 to -938)

**Header:** `include/pacs/bridge/mapping/dicom_hl7_mapper.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -930 | `missing_required_attribute` | Missing required MPPS attribute | Add required DICOM attribute |
| -931 | `invalid_mpps_status` | Invalid MPPS status for mapping | Check MPPS status values |
| -932 | `datetime_conversion_failed` | Date/time format conversion failed | Verify date/time format |
| -933 | `name_conversion_failed` | Patient name conversion failed | Check name format |
| -934 | `message_build_failed` | Message building failed | Check HL7 message builder |
| -935 | `invalid_accession_number` | Invalid accession number | Validate accession number format |
| -936 | `missing_study_uid` | Missing study instance UID | Add Study Instance UID |
| -937 | `custom_transform_error` | Custom transform function error | Check custom transform implementation |
| -938 | `serialization_failed` | Message serialization failed | Check message structure |

### mapping_error (-940 to -948)

**Header:** `include/pacs/bridge/mapping/hl7_dicom_mapper.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -940 | `unsupported_message_type` | Message type not supported | Use supported HL7 message types |
| -941 | `missing_required_field` | Required field missing in source | Add required source field |
| -942 | `invalid_field_format` | Field value format is invalid | Check field format specification |
| -943 | `charset_conversion_failed` | Character set conversion failed | Verify character encoding |
| -944 | `datetime_parse_failed` | Date/time parsing failed | Use correct date/time format |
| -945 | `name_conversion_failed` | Name format conversion failed | Check HL7 name format |
| -946 | `no_mapping_rule` | Mapping rule not found | Add mapping rule for this type |
| -947 | `validation_failed` | Mapping validation failed | Review mapping output |
| -948 | `custom_mapper_error` | Custom mapping function error | Check custom mapper implementation |

### fhir_dicom_error (-950 to -958)

**Header:** `include/pacs/bridge/mapping/fhir_dicom_mapper.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -950 | `unsupported_resource_type` | Unsupported resource type | Use supported FHIR resource types |
| -951 | `missing_required_field` | Missing required field in FHIR resource | Add required FHIR elements |
| -952 | `invalid_field_value` | Invalid field value | Check field value format |
| -953 | `patient_not_found` | Patient reference could not be resolved | Verify patient reference |
| -954 | `code_translation_failed` | Code system translation failed | Check code system mappings |
| -955 | `datetime_conversion_failed` | Date/time format conversion failed | Verify date/time format |
| -956 | `uid_generation_failed` | UID generation failed | Check UID generator |
| -957 | `validation_failed` | Validation failed | Review mapping validation errors |
| -958 | `internal_error` | Internal mapping error | Check mapper logs |

---

## Router Layer

### queue_error (-910 to -919)

**Header:** `include/pacs/bridge/router/queue_manager.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -910 | `database_error` | Failed to open or initialize database | Check disk space and permissions |
| -911 | `message_not_found` | Message not found in queue | Verify message ID |
| -912 | `queue_full` | Queue has reached maximum capacity | Increase `max_queue_size` or drain queue |
| -913 | `invalid_message` | Invalid message data | Check message payload |
| -914 | `message_expired` | Message has expired (TTL exceeded) | Check destination availability |
| -915 | `serialization_error` | Failed to serialize/deserialize message | Verify message format |
| -916 | `not_running` | Queue manager is not running | Start queue manager first |
| -917 | `already_running` | Queue manager is already running | Stop before restarting |
| -918 | `transaction_error` | Transaction failed | Check database integrity |
| -919 | `worker_error` | Worker operation failed | Check worker thread status |

### outbound_error (-920 to -929)

**Header:** `include/pacs/bridge/router/outbound_router.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -920 | `no_destination` | No destination configured | Add routing rules or default destination |
| -921 | `all_destinations_failed` | All destinations are unavailable | Check destination connectivity |
| -922 | `destination_not_found` | Destination not found by name | Verify destination name |
| -923 | `delivery_failed` | Message delivery failed | Check MLLP endpoint availability |
| -924 | `invalid_configuration` | Invalid destination configuration | Review router configuration |
| -925 | `health_check_failed` | Health check failed | Check destination health |
| -926 | `not_running` | Router is not running | Start router first |
| -927 | `already_running` | Router is already running | Stop before restarting |
| -928 | `queue_full` | Queue is full | Increase queue capacity |
| -929 | `timeout` | Delivery timeout | Increase delivery timeout |

### router_error (-930 to -938)

**Header:** `include/pacs/bridge/router/message_router.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -930 | `no_matching_route` | No matching route found | Add routing rule for this message |
| -931 | `handler_error` | Handler returned error | Check handler logs |
| -932 | `invalid_route` | Invalid route configuration | Fix route definition |
| -933 | `invalid_pattern` | Route pattern is invalid | Check route pattern syntax |
| -934 | `handler_not_found` | Handler not found | Register handler |
| -935 | `route_exists` | Route already exists | Remove existing route first |
| -936 | `max_handlers_exceeded` | Maximum handlers exceeded | Increase handler limit |
| -937 | `message_rejected` | Message rejected by filter | Message does not match criteria |
| -938 | `timeout` | Routing timeout | Increase routing timeout |

### reliable_sender_error (-930 to -937)

**Header:** `include/pacs/bridge/router/reliable_outbound_sender.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -930 | `not_running` | Sender is not running | Start sender first |
| -931 | `already_running` | Sender is already running | Stop before restarting |
| -932 | `queue_init_failed` | Failed to initialize queue | Check queue database path |
| -933 | `router_init_failed` | Failed to initialize router | Check router configuration |
| -934 | `enqueue_failed` | Message enqueue failed | Check queue capacity |
| -935 | `invalid_configuration` | Invalid configuration | Review sender configuration |
| -936 | `destination_not_found` | Destination not found | Verify destination name |
| -937 | `internal_error` | Internal error | Check detailed error logs |

---

## Cache Layer

### cache_error (-920 to -925)

**Header:** `include/pacs/bridge/cache/patient_cache.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -920 | `not_found` | Entry not found in cache | Entry may have expired or was never cached |
| -921 | `expired` | Entry has expired | Re-fetch data from source |
| -922 | `capacity_exceeded` | Cache capacity reached | Increase `max_entries` or reduce TTL |
| -923 | `invalid_key` | Invalid key format | Check key format (patient ID) |
| -924 | `serialization_error` | Serialization error | Verify data format |
| -925 | `cache_disabled` | Cache is disabled | Enable cache in configuration |

---

## Workflow Layer

### workflow_error (-900 to -909)

**Header:** `include/pacs/bridge/workflow/mpps_hl7_workflow.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -900 | `not_running` | Workflow is not running | Call `start()` before processing |
| -901 | `already_running` | Workflow is already running | Call `stop()` before restarting |
| -902 | `mapping_failed` | MPPS to HL7 mapping failed | Check MPPS dataset and mapper config |
| -903 | `delivery_failed` | Outbound delivery failed | Check destination connectivity |
| -904 | `enqueue_failed` | Queue enqueue failed | Check queue capacity and database |
| -905 | `no_destination` | No destination configured | Add routing rules or default destination |
| -906 | `invalid_configuration` | Invalid workflow configuration | Check `config.is_valid()` |
| -907 | `correlation_failed` | Correlation ID generation failed | Check ID generator |
| -908 | `destination_selection_failed` | Destination selection failed | Review routing rules |
| -909 | `initialization_failed` | Component initialization failed | Check component dependencies |

---

## Performance Layer

### performance_error (-940 to -949)

**Header:** `include/pacs/bridge/performance/performance_types.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -940 | `thread_pool_init_failed` | Thread pool initialization failed | Check system resources |
| -941 | `pool_exhausted` | Object pool exhausted | Increase pool size |
| -942 | `queue_full` | Queue is full | Increase queue capacity |
| -943 | `invalid_configuration` | Invalid configuration | Review performance settings |
| -944 | `allocation_failed` | Resource allocation failed | Check memory availability |
| -945 | `timeout` | Operation timed out | Increase timeout or reduce load |
| -946 | `not_initialized` | Component not initialized | Initialize performance component |
| -947 | `benchmark_failed` | Benchmark execution failed | Check benchmark configuration |
| -948 | `parser_error` | Parser error | Verify input format |
| -949 | `memory_limit_exceeded` | Memory limit exceeded | Increase memory limit or reduce data |

---

## Security Layer

### access_error (-950 to -958)

**Header:** `include/pacs/bridge/security/access_control.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -950 | `not_whitelisted` | IP address is not in whitelist | Add IP to whitelist |
| -951 | `blacklisted` | IP address is in blacklist | Remove IP from blacklist if legitimate |
| -952 | `invalid_ip_address` | Invalid IP address format | Check IP address format |
| -953 | `invalid_cidr` | Invalid CIDR notation | Fix CIDR format (e.g., 192.168.1.0/24) |
| -954 | `not_initialized` | Access control not initialized | Initialize access control module |
| -955 | `config_error` | Configuration error | Review access control configuration |
| -956 | `rate_limited` | Rate limit exceeded | Reduce request frequency |
| -957 | `too_many_failures` | Too many failed attempts | Wait for cooldown period |
| -958 | `connection_rejected` | Connection rejected | Check security policy |

### validation_error (-960 to -969)

**Header:** `include/pacs/bridge/security/input_validator.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -960 | `empty_message` | Message is empty or null | Provide non-empty message |
| -961 | `message_too_large` | Message exceeds maximum size | Reduce message size or increase limit |
| -962 | `invalid_hl7_structure` | Invalid HL7 message structure | Validate HL7 format |
| -963 | `missing_msh_segment` | Missing required MSH segment | Add MSH segment |
| -964 | `invalid_msh_fields` | Invalid MSH field values | Fix MSH field values |
| -965 | `prohibited_characters` | Prohibited control characters | Remove control characters |
| -966 | `injection_detected` | Potential injection attack | Sanitize input data |
| -967 | `invalid_encoding` | Invalid character encoding | Use supported encoding |
| -968 | `invalid_timestamp` | Message timestamp validation failed | Fix timestamp format |
| -969 | `invalid_application_id` | Application ID validation failed | Check sending/receiving application |

### tls_error (-990 to -999)

**Header:** `include/pacs/bridge/security/tls_types.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -990 | `initialization_failed` | TLS library initialization failed | Check TLS library installation |
| -991 | `certificate_invalid` | Certificate file not found or invalid | Verify certificate path and format |
| -992 | `private_key_invalid` | Private key file not found or invalid | Check key file permissions |
| -993 | `ca_certificate_invalid` | CA certificate invalid | Verify CA certificate chain |
| -994 | `key_certificate_mismatch` | Private key does not match certificate | Regenerate matching key/cert pair |
| -995 | `handshake_failed` | TLS handshake failed | Check protocol version and cipher suites |
| -996 | `client_verification_failed` | Client certificate verification failed | Verify client certificate |
| -997 | `unsupported_version` | Unsupported TLS version | Use TLS 1.2 or 1.3 |
| -998 | `invalid_cipher_suite` | Invalid cipher suite configuration | Use recommended cipher suites |
| -999 | `connection_closed` | TLS connection closed unexpectedly | Check for network interruption |

### oauth2_error (-1020 to -1034)

**Header:** `include/pacs/bridge/security/oauth2_types.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -1020 | `token_request_failed` | Token request failed | Check token URL and network |
| -1021 | `invalid_credentials` | Invalid client credentials | Verify client_id and client_secret |
| -1022 | `token_expired` | Access token has expired | Token will auto-refresh |
| -1023 | `refresh_failed` | Token refresh failed | Re-authenticate |
| -1024 | `scope_denied` | Requested scope denied | Request required scopes from admin |
| -1025 | `discovery_failed` | Smart-on-FHIR discovery failed | Check .well-known/smart-configuration |
| -1026 | `invalid_response` | Invalid response from auth server | Check auth server logs |
| -1027 | `network_error` | Network error during OAuth2 request | Check network connectivity |
| -1028 | `invalid_token` | Access token is invalid or malformed | Re-authenticate |
| -1029 | `token_revoked` | Access token has been revoked | Re-authenticate |
| -1030 | `invalid_configuration` | Invalid OAuth2 configuration | Review OAuth2 settings |
| -1031 | `missing_parameter` | Missing required OAuth2 parameter | Add missing parameter |
| -1032 | `unsupported_grant_type` | Unsupported grant type | Use supported grant type |
| -1033 | `server_error` | Authorization server error | Check auth server status |
| -1034 | `timeout` | Request timeout | Increase timeout |

---

## Transport Layer (MLLP)

### mllp_error (-970 to -979)

**Header:** `include/pacs/bridge/mllp/mllp_types.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -970 | `invalid_frame` | Invalid MLLP frame structure | Check VT/FS/CR framing |
| -971 | `message_too_large` | Message exceeds maximum allowed size | Increase `max_message_size` |
| -972 | `timeout` | Connection timeout | Increase timeout or check network |
| -973 | `connection_closed` | Connection was closed by peer | Reconnect |
| -974 | `connection_failed` | Failed to connect to remote host | Verify host and port |
| -975 | `invalid_configuration` | Invalid server configuration | Review MLLP configuration |
| -976 | `already_running` | Server is already running | Stop before restarting |
| -977 | `not_running` | Server is not running | Start server first |
| -978 | `socket_error` | Socket operation failed | Check OS socket limits |
| -979 | `ack_error` | HL7 acknowledgment indicated error | Check ACK message for details |

### network_error (-980 to -987)

**Header:** `include/pacs/bridge/mllp/mllp_network_adapter.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -980 | `timeout` | Operation timed out | Increase timeout |
| -981 | `connection_closed` | Connection closed by peer | Reconnect |
| -982 | `socket_error` | Socket operation failed | Check OS resources |
| -983 | `bind_failed` | Failed to bind or listen on port | Check port availability |
| -984 | `tls_handshake_failed` | TLS handshake failed | Check certificates |
| -985 | `invalid_config` | Invalid configuration | Review network configuration |
| -986 | `would_block` | Operation would block (non-blocking I/O) | Retry later |
| -987 | `connection_refused` | Connection refused by peer | Check remote service status |

---

## Monitoring Layer

### health_error (-980 to -985)

**Header:** `include/pacs/bridge/monitoring/health_types.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -980 | `timeout` | Health check operation timed out | Increase health check timeout |
| -981 | `component_unavailable` | A monitored component is unavailable | Check component status |
| -982 | `threshold_exceeded` | A health threshold exceeded | Investigate resource usage |
| -983 | `invalid_configuration` | Health check configuration invalid | Review health check settings |
| -984 | `not_initialized` | Health check not initialized | Initialize health checker |
| -985 | `serialization_failed` | Failed to serialize health response | Check response format |

---

## Tracing Layer

### trace_error (-950 to -954)

**Header:** `include/pacs/bridge/tracing/trace_manager.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -950 | `not_initialized` | Tracing is not initialized | Initialize trace manager |
| -951 | `invalid_config` | Invalid configuration | Review tracing configuration |
| -952 | `exporter_failed` | Exporter connection failed | Check exporter backend connectivity |
| -953 | `span_creation_failed` | Span creation failed | Check trace context |
| -954 | `propagation_failed` | Context propagation failed | Verify trace context headers |

### exporter_error (-960 to -965)

**Header:** `include/pacs/bridge/tracing/exporter_factory.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -960 | `not_initialized` | Exporter not initialized | Initialize exporter |
| -961 | `connection_failed` | Connection to backend failed | Check backend URL |
| -962 | `export_failed` | Export request failed | Check backend status |
| -963 | `invalid_config` | Invalid configuration | Review exporter settings |
| -964 | `backend_unavailable` | Backend not reachable | Check backend connectivity |
| -965 | `timeout` | Export timeout | Increase export timeout |

---

## Testing Layer

### load_error (-960 to -969)

**Header:** `include/pacs/bridge/testing/load_types.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -960 | `invalid_configuration` | Test configuration is invalid | Review test configuration |
| -961 | `not_initialized` | Test runner not initialized | Initialize test runner |
| -962 | `already_running` | Test is already running | Stop test before restarting |
| -963 | `cancelled` | Test was cancelled by user | Restart test if needed |
| -964 | `connection_failed` | Connection to target failed | Check target availability |
| -965 | `generation_failed` | Message generation failed | Check message templates |
| -966 | `timeout` | Test timeout exceeded | Increase test timeout |
| -967 | `resource_exhausted` | Resource exhaustion | Reduce concurrent connections |
| -968 | `target_error` | Target system returned error | Check target system logs |
| -969 | `report_failed` | Report generation failed | Check report output path |

---

## EMR Layer

### emr_error (-1000 to -1019)

**Header:** `include/pacs/bridge/emr/emr_types.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -1000 | `connection_failed` | Connection to EMR server failed | Verify EMR URL and network |
| -1001 | `timeout` | Request timed out | Increase timeout |
| -1002 | `invalid_response` | Invalid response from EMR server | Check EMR FHIR compliance |
| -1003 | `resource_not_found` | Resource not found (HTTP 404) | Verify resource ID |
| -1004 | `unauthorized` | Authentication failed (HTTP 401) | Check OAuth2 credentials |
| -1005 | `rate_limited` | Rate limit exceeded (HTTP 429) | Implement backoff strategy |
| -1006 | `server_error` | Server error (HTTP 5xx) | Contact EMR administrator |
| -1007 | `invalid_resource` | Invalid FHIR resource format | Validate resource structure |
| -1008 | `network_error` | Network error during request | Check network connectivity |
| -1009 | `tls_error` | TLS/SSL error | Verify certificates |
| -1010 | `invalid_configuration` | Invalid configuration | Review EMR configuration |
| -1011 | `validation_failed` | Resource validation failed | Check OperationOutcome |
| -1012 | `conflict` | Conflict error (HTTP 409) | Resolve conflict (ETag mismatch) |
| -1013 | `gone` | Resource deleted (HTTP 410) | Resource no longer exists |
| -1014 | `forbidden` | Forbidden (HTTP 403) | Check permissions |
| -1015 | `bad_request` | Bad request (HTTP 400) | Fix request parameters |
| -1016 | `not_supported` | Operation not supported | Use supported FHIR operations |
| -1017 | `retry_exhausted` | Retry limit exceeded | Check EMR availability |
| -1018 | `cancelled` | Request was cancelled | Request intentionally cancelled |
| -1019 | `unknown` | Unknown error | Check detailed logs |

### adapter_error (-1100 to -1109)

**Header:** `include/pacs/bridge/emr/emr_adapter.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -1100 | `not_initialized` | Adapter not initialized | Initialize adapter before use |
| -1101 | `connection_failed` | Connection to EMR failed | Check EMR server status |
| -1102 | `authentication_failed` | Authentication failed | Verify credentials |
| -1103 | `not_supported` | Operation not supported by adapter | Check vendor capabilities |
| -1104 | `invalid_configuration` | Invalid adapter configuration | Review adapter settings |
| -1105 | `timeout` | Adapter operation timed out | Increase timeout |
| -1106 | `rate_limited` | Rate limited by EMR | Implement backoff |
| -1107 | `invalid_vendor` | Invalid vendor type | Use: epic, cerner, generic |
| -1108 | `health_check_failed` | Health check failed | Check EMR connectivity |
| -1109 | `feature_unavailable` | Feature not available | Check vendor support |

### patient_error (-1040 to -1048)

**Header:** `include/pacs/bridge/emr/patient_record.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -1040 | `not_found` | Patient not found in EMR | Verify patient ID |
| -1041 | `multiple_found` | Multiple patients found | Use more specific search criteria |
| -1042 | `query_failed` | Patient query failed | Check FHIR client status |
| -1043 | `invalid_data` | Invalid patient data in response | Check EMR data quality |
| -1044 | `merge_detected` | Patient merged into another record | Follow merge link |
| -1045 | `invalid_query` | Invalid search parameters | Fix query parameters |
| -1046 | `inactive_patient` | Patient record is inactive | Verify patient active status |
| -1047 | `parse_failed` | Patient data parsing failed | Check response format |
| -1048 | `cache_failed` | Cache operation failed | Check cache health |

### result_error (-1060 to -1069)

**Header:** `include/pacs/bridge/emr/result_poster.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -1060 | `post_failed` | Failed to post result to EMR | Check EMR connectivity |
| -1061 | `update_failed` | Failed to update existing result | Verify report ID |
| -1062 | `duplicate` | Duplicate result detected | Use update for existing results |
| -1063 | `invalid_data` | Invalid result data | Validate study_result fields |
| -1064 | `rejected` | EMR rejected the result | Check OperationOutcome |
| -1065 | `not_found` | Result not found for update | Verify report ID |
| -1066 | `invalid_status_transition` | Invalid status transition | Use amended/corrected for post-final |
| -1067 | `missing_reference` | Missing required reference | Set patient_reference or patient_id |
| -1068 | `build_failed` | Failed to build DiagnosticReport | Check status, code, subject |
| -1069 | `tracker_error` | Tracker operation failed | Check tracker configuration |

### encounter_error (-1080 to -1089)

**Header:** `include/pacs/bridge/emr/encounter_context.h`

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -1080 | `not_found` | Encounter not found in EMR | Verify encounter ID |
| -1081 | `query_failed` | Encounter query failed | Check FHIR client status |
| -1082 | `multiple_active` | Multiple active encounters found | Use specific encounter ID |
| -1083 | `encounter_ended` | Encounter has already ended | Find active encounter |
| -1084 | `invalid_data` | Invalid encounter data in response | Check EMR FHIR compliance |
| -1085 | `visit_not_found` | Visit number not found | Verify visit number format |
| -1086 | `invalid_status` | Invalid encounter status value | Check FHIR EncounterStatus |
| -1087 | `location_not_found` | Location not found | Location may be optional |
| -1088 | `practitioner_not_found` | Practitioner not found | Practitioner may be optional |
| -1089 | `parse_failed` | Encounter data parsing failed | Check response format |

### tracker_error (-1120 to -1124)

**Header:** `include/pacs/bridge/emr/result_tracker.h`

> Relocated from -1020 range in [#344](https://github.com/kcenon/pacs_bridge/issues/344).

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| -1120 | `not_found` | Entry not found | Verify Study Instance UID |
| -1121 | `capacity_exceeded` | Tracker is full | Increase `max_entries` or run cleanup |
| -1122 | `invalid_entry` | Invalid entry data | Check entry fields |
| -1123 | `already_exists` | Entry already exists | Use update instead of track |
| -1124 | `operation_failed` | Operation failed | Check tracker logs |

---

## Resolution Steps

### General Troubleshooting

1. **Check Logs**
   ```bash
   tail -100 /var/log/pacs_bridge/bridge.log
   grep "\-9[0-9][0-9]" /var/log/pacs_bridge/bridge.log   # Workflow/queue errors
   grep "\-10[0-9][0-9]" /var/log/pacs_bridge/bridge.log  # EMR errors
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

   # EMR/FHIR
   curl -s https://emr.hospital.local/fhir/metadata | jq '.status'
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
- [SDS Traceability](../SDS_TRACEABILITY.md) - INT-ERR-001 cross-reference (Appendix C)
