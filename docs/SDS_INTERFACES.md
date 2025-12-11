# SDS - Interface Specifications

> **Version:** 0.1.0.0
> **Parent Document:** [SDS.md](SDS.md)
> **Last Updated:** 2025-12-07

---

## Table of Contents

- [1. Overview](#1-overview)
- [2. External System Interfaces](#2-external-system-interfaces)
- [3. Public API Interfaces](#3-public-api-interfaces)
- [4. Internal Module Interfaces](#4-internal-module-interfaces)
- [5. Ecosystem Integration Interfaces](#5-ecosystem-integration-interfaces)
- [6. Error Handling Interface](#6-error-handling-interface)
- [7. Configuration Interface](#7-configuration-interface)

---

## 1. Overview

### 1.1 Interface Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Interface Architecture                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │                      External Interfaces                             │   │
│   │                                                                      │   │
│   │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────────┐   │   │
│   │  │ HIS / RIS    │  │ EMR / EHR    │  │ pacs_system              │   │   │
│   │  │ (HL7 v2.x)   │  │ (FHIR R4)    │  │ (DICOM)                  │   │   │
│   │  └──────────────┘  └──────────────┘  └──────────────────────────┘   │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                      │                                       │
│                                      ▼                                       │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │                        Public API Layer                              │   │
│   │                                                                      │   │
│   │  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐             │   │
│   │  │ mllp_server   │ │ fhir_server   │ │ bridge_server │             │   │
│   │  │ (Port 2575)   │ │ (Port 8080)   │ │ (Orchestrator)│             │   │
│   │  └───────────────┘ └───────────────┘ └───────────────┘             │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                      │                                       │
│                                      ▼                                       │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │                      Internal Module Interfaces                      │   │
│   │                                                                      │   │
│   │  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐             │   │
│   │  │  hl7_parser   │ │  hl7_builder  │ │ message_router│             │   │
│   │  └───────────────┘ └───────────────┘ └───────────────┘             │   │
│   │  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐             │   │
│   │  │ hl7_dicom_    │ │ dicom_hl7_    │ │ queue_manager │             │   │
│   │  │ mapper        │ │ mapper        │ │               │             │   │
│   │  └───────────────┘ └───────────────┘ └───────────────┘             │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                      │                                       │
│                                      ▼                                       │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │                    Ecosystem Integration Layer                       │   │
│   │                                                                      │   │
│   │  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐             │   │
│   │  │ network_      │ │ thread_       │ │ logger_       │             │   │
│   │  │ adapter       │ │ adapter       │ │ adapter       │             │   │
│   │  └───────────────┘ └───────────────┘ └───────────────┘             │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. External System Interfaces

### INT-EXT-001: HL7 v2.x / MLLP Interface

**Traces to:** FR-1.3, PCR-1

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        HL7 v2.x / MLLP Protocol Interface                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Protocol: HL7 v2.x over MLLP                                               │
│  Transport: TCP/IP (TLS optional)                                           │
│  Default Port: 2575                                                          │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ MLLP Frame Structure                                                     ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  ┌────┬─────────────────────────────────────────────────────────┬────┐  ││
│  │  │ VT │                    HL7 Message                          │FS CR│ ││
│  │  │0x0B│                    (UTF-8)                              │0x1C│  ││
│  │  │    │                                                         │0x0D│  ││
│  │  └────┴─────────────────────────────────────────────────────────┴────┘  ││
│  │                                                                          ││
│  │  VT (0x0B) = Start of Message Block                                     ││
│  │  FS (0x1C) = End of Message Block                                       ││
│  │  CR (0x0D) = Carriage Return (block terminator)                         ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ Supported Message Types                                                  ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  Inbound (HIS/RIS → Bridge):                                            ││
│  │    • ADT^A01  Patient Admit                                             ││
│  │    • ADT^A04  Patient Registration                                      ││
│  │    • ADT^A08  Patient Information Update                                ││
│  │    • ADT^A40  Patient Merge                                             ││
│  │    • ORM^O01  Order Message                                             ││
│  │    • SIU^S12  Scheduling Request                                        ││
│  │    • SIU^S13  Scheduling Notification                                   ││
│  │    • SIU^S14  Scheduling Modification                                   ││
│  │    • SIU^S15  Scheduling Cancellation                                   ││
│  │                                                                          ││
│  │  Outbound (Bridge → RIS):                                               ││
│  │    • ACK     Acknowledgment                                             ││
│  │    • ORM^O01 Order Status Update                                        ││
│  │    • ORU^R01 Observation Result                                         ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ HL7 Message Structure                                                    ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  MSH|^~\&|SENDING_APP|SENDING_FAC|RECEIVING_APP|RECEIVING_FAC|...      ││
│  │  PID|||123456^^^ISSUER||DOE^JOHN^||19800101|M|||...                     ││
│  │  PV1|1|O|...                                                            ││
│  │  ORC|NW|12345^HIS|||SC||||...                                           ││
│  │  OBR|1|12345^HIS||CT^CT CHEST^LOCAL|...                                 ││
│  │  ZDS|1.2.840.113619.2.55.1234567890.123456|                             ││
│  │                                                                          ││
│  │  Segment Separator: CR (0x0D)                                           ││
│  │  Field Separator: | (MSH-1)                                             ││
│  │  Encoding Characters: ^~\& (MSH-2)                                      ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ Acknowledgment Codes                                                     ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  AA = Application Accept (message processed successfully)               ││
│  │  AE = Application Error (message rejected, error in message)            ││
│  │  AR = Application Reject (message rejected, not message error)          ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### INT-EXT-002: FHIR R4 REST Interface

**Traces to:** FR-2.1, FR-2.2, PCR-2

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          FHIR R4 REST Interface                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Protocol: HTTP/1.1 (HTTPS for production)                                  │
│  Content-Type: application/fhir+json, application/fhir+xml                  │
│  Default Port: 8080                                                          │
│  Base URL: /fhir                                                             │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ Supported Resources                                                      ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  Resource         │ Read │ Search │ Create │ Update │ Delete            ││
│  │  ─────────────────┼──────┼────────┼────────┼────────┼───────            ││
│  │  Patient          │  ✓   │   ✓    │   ✗    │   ✗    │   ✗               ││
│  │  ServiceRequest   │  ✓   │   ✓    │   ✓    │   ✓    │   ✓               ││
│  │  ImagingStudy     │  ✓   │   ✓    │   ✗    │   ✗    │   ✗               ││
│  │  DiagnosticReport │  ✓   │   ✓    │   ✗    │   ✗    │   ✗               ││
│  │  Task             │  ✓   │   ✓    │   ✓    │   ✓    │   ✗               ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ Endpoints                                                                ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  GET    /fhir/Patient/{id}              Read patient                    ││
│  │  GET    /fhir/Patient?name=xxx          Search patients                 ││
│  │                                                                          ││
│  │  GET    /fhir/ServiceRequest/{id}       Read order                      ││
│  │  GET    /fhir/ServiceRequest?patient=xx Search orders                   ││
│  │  POST   /fhir/ServiceRequest            Create order                    ││
│  │  PUT    /fhir/ServiceRequest/{id}       Update order                    ││
│  │  DELETE /fhir/ServiceRequest/{id}       Cancel order                    ││
│  │                                                                          ││
│  │  GET    /fhir/ImagingStudy/{id}         Read study                      ││
│  │  GET    /fhir/ImagingStudy?patient=xx   Search studies                  ││
│  │                                                                          ││
│  │  GET    /fhir/DiagnosticReport/{id}     Read report                     ││
│  │  GET    /fhir/DiagnosticReport?study=xx Search reports                  ││
│  │                                                                          ││
│  │  GET    /fhir/metadata                  Capability Statement            ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ Search Parameters                                                        ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  Patient:                                                                ││
│  │    • _id, identifier, name, birthdate, gender                           ││
│  │                                                                          ││
│  │  ServiceRequest:                                                         ││
│  │    • _id, patient, status, intent, authored, requester                  ││
│  │                                                                          ││
│  │  ImagingStudy:                                                           ││
│  │    • _id, patient, status, started, modality, identifier               ││
│  │                                                                          ││
│  │  DiagnosticReport:                                                       ││
│  │    • _id, patient, status, issued, result                               ││
│  │                                                                          ││
│  │  Common:                                                                 ││
│  │    • _count (pagination), _sort, _include                               ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ Response Codes                                                           ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  200 OK                 Successful read/search                          ││
│  │  201 Created            Resource created                                ││
│  │  204 No Content         Successful delete                               ││
│  │  400 Bad Request        Invalid request/resource                        ││
│  │  401 Unauthorized       Authentication required                         ││
│  │  404 Not Found          Resource not found                              ││
│  │  422 Unprocessable      Validation error                                ││
│  │  500 Internal Error     Server error                                    ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### INT-EXT-003: pacs_system DICOM Interface

**Traces to:** IR-1

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        pacs_system DICOM Interface                           │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Protocol: DICOM Upper Layer (PS3.8)                                        │
│  Transport: TCP/IP (TLS optional)                                           │
│  Default Port: 11112                                                         │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ Used SOP Classes                                                         ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  Bridge as SCU (Client):                                                 ││
│  │                                                                          ││
│  │    • Modality Worklist Information Model - FIND                         ││
│  │      UID: 1.2.840.10008.5.1.4.31                                        ││
│  │      Purpose: Query/Update worklist entries                             ││
│  │                                                                          ││
│  │    • Modality Performed Procedure Step SOP Class                        ││
│  │      UID: 1.2.840.10008.3.1.2.3.3                                       ││
│  │      Purpose: Receive MPPS notifications                                ││
│  │                                                                          ││
│  │    • Patient Root Query/Retrieve - FIND                                 ││
│  │      UID: 1.2.840.10008.5.1.4.1.2.1.1                                   ││
│  │      Purpose: Query patient/study information                           ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ MWL Query Attributes                                                     ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  Query Keys:                                                             ││
│  │    (0008,0050) AccessionNumber                                          ││
│  │    (0010,0020) PatientID                                                ││
│  │    (0040,0100) ScheduledProcedureStepSequence                           ││
│  │      └─ (0040,0001) ScheduledStationAETitle                             ││
│  │      └─ (0040,0002) ScheduledProcedureStepStartDate                     ││
│  │      └─ (0008,0060) Modality                                            ││
│  │                                                                          ││
│  │  Return Keys:                                                            ││
│  │    (0010,0010) PatientName                                              ││
│  │    (0010,0020) PatientID                                                ││
│  │    (0010,0030) PatientBirthDate                                         ││
│  │    (0010,0040) PatientSex                                               ││
│  │    (0020,000D) StudyInstanceUID                                         ││
│  │    (0008,0050) AccessionNumber                                          ││
│  │    (0040,1001) RequestedProcedureID                                     ││
│  │    (0040,0100) ScheduledProcedureStepSequence                           ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ MPPS Attributes                                                          ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  N-CREATE (IN PROGRESS):                                                ││
│  │    (0008,1115) ReferencedSeriesSequence                                 ││
│  │    (0040,0241) PerformedStationAETitle                                  ││
│  │    (0040,0244) PerformedProcedureStepStartDate                          ││
│  │    (0040,0245) PerformedProcedureStepStartTime                          ││
│  │    (0040,0252) PerformedProcedureStepStatus = "IN PROGRESS"             ││
│  │    (0040,0270) ScheduledStepAttributesSequence                          ││
│  │                                                                          ││
│  │  N-SET (COMPLETED/DISCONTINUED):                                        ││
│  │    (0040,0250) PerformedProcedureStepEndDate                            ││
│  │    (0040,0251) PerformedProcedureStepEndTime                            ││
│  │    (0040,0252) PerformedProcedureStepStatus                             ││
│  │    (0040,0340) PerformedSeriesSequence                                  ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Public API Interfaces

### INT-API-001: bridge_server Interface

**Traces to:** Main server orchestration

```cpp
namespace pacs::bridge {

/**
 * @brief Main bridge server
 *
 * Orchestrates all gateway components.
 */
class bridge_server {
public:
    // ═══════════════════════════════════════════════════════════════════
    // Construction
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Construct bridge server with configuration
     * @param config Bridge configuration
     * @throws std::invalid_argument if config is invalid
     */
    explicit bridge_server(const config::bridge_config& config);

    /**
     * @brief Construct from config file
     * @param config_path Path to YAML/JSON config
     */
    explicit bridge_server(const std::filesystem::path& config_path);

    // ═══════════════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Start all services
     * @return Success or error with details
     */
    [[nodiscard]] Result<void> start();

    /**
     * @brief Stop all services gracefully
     * @param timeout Maximum wait time for pending operations
     */
    void stop(std::chrono::seconds timeout = std::chrono::seconds{30});

    /**
     * @brief Block until shutdown signal
     */
    void wait_for_shutdown();

    /**
     * @brief Check if server is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // Runtime Configuration
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Reload configuration (hot-reload)
     * @param config_path New config file path
     */
    [[nodiscard]] Result<void> reload_config(
        const std::filesystem::path& config_path);

    /**
     * @brief Add outbound destination dynamically
     */
    [[nodiscard]] Result<void> add_destination(
        const mllp::mllp_client_config& dest);

    /**
     * @brief Remove outbound destination
     */
    void remove_destination(std::string_view name);

    // ═══════════════════════════════════════════════════════════════════
    // Monitoring
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Get server statistics
     */
    struct statistics {
        // MLLP statistics
        size_t mllp_active_connections;
        size_t mllp_messages_received;
        size_t mllp_messages_sent;
        size_t mllp_errors;

        // FHIR statistics
        size_t fhir_requests;
        size_t fhir_errors;

        // Queue statistics
        size_t queue_depth;
        size_t queue_dead_letters;

        // Cache statistics
        size_t cache_size;
        double cache_hit_rate;

        // Uptime
        std::chrono::seconds uptime;
    };

    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Health check
     * @return true if all components healthy
     */
    [[nodiscard]] bool is_healthy() const;

    /**
     * @brief Get component health details
     */
    struct health_status {
        bool mllp_server_healthy;
        bool fhir_server_healthy;
        bool pacs_connection_healthy;
        bool queue_healthy;
        std::string details;
    };

    [[nodiscard]] health_status get_health_status() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge
```

### INT-API-002: hl7_gateway Interface

**Traces to:** FR-1

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief HL7 Gateway public interface
 *
 * Provides HL7 v2.x message processing capabilities.
 */
class hl7_gateway {
public:
    explicit hl7_gateway(const hl7_gateway_config& config);

    // ═══════════════════════════════════════════════════════════════════
    // Handler Registration
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Set message handler for specific type
     * @param message_type Message type pattern (e.g., "ADT^A*", "ORM^O01")
     * @param handler Handler function
     *
     * Handler signature:
     *   Result<std::string>(const hl7_message& message)
     *   Returns ACK message or error
     */
    void set_handler(
        std::string_view message_type,
        std::function<Result<std::string>(const hl7_message&)> handler);

    /**
     * @brief Set default handler for unmatched messages
     */
    void set_default_handler(
        std::function<Result<std::string>(const hl7_message&)> handler);

    /**
     * @brief Set pre-processing hook
     * @param hook Called before message processing
     *             Return false to reject message
     */
    void set_pre_hook(
        std::function<bool(const std::string& raw)> hook);

    /**
     * @brief Set post-processing hook
     * @param hook Called after successful processing
     */
    void set_post_hook(
        std::function<void(const hl7_message&, const std::string& ack)> hook);

    // ═══════════════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════════════

    [[nodiscard]] Result<void> start();
    void stop();
    [[nodiscard]] bool is_running() const noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // Outbound Messaging
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Send message to destination
     * @param destination Destination name (from config)
     * @param message HL7 message to send
     * @return ACK response or error
     */
    [[nodiscard]] Result<std::string> send(
        std::string_view destination,
        const std::string& message);

    /**
     * @brief Send message asynchronously
     */
    [[nodiscard]] std::future<Result<std::string>> send_async(
        std::string_view destination,
        const std::string& message);

    /**
     * @brief Queue message for reliable delivery
     */
    [[nodiscard]] Result<std::string> queue_send(
        std::string_view destination,
        const std::string& message,
        int priority = 0);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::hl7
```

---

## 4. Internal Module Interfaces

### INT-MOD-001: hl7_message Access

**Traces to:** DES-HL7-001

```cpp
namespace pacs::bridge::hl7 {

// Common HL7 field paths for convenience
namespace field_paths {
    // MSH segment
    constexpr auto MSH_SENDING_APP = "MSH-3";
    constexpr auto MSH_SENDING_FAC = "MSH-4";
    constexpr auto MSH_RECEIVING_APP = "MSH-5";
    constexpr auto MSH_RECEIVING_FAC = "MSH-6";
    constexpr auto MSH_DATETIME = "MSH-7";
    constexpr auto MSH_MESSAGE_TYPE = "MSH-9";
    constexpr auto MSH_CONTROL_ID = "MSH-10";
    constexpr auto MSH_VERSION = "MSH-12";

    // PID segment
    constexpr auto PID_SET_ID = "PID-1";
    constexpr auto PID_PATIENT_ID = "PID-3";
    constexpr auto PID_PATIENT_NAME = "PID-5";
    constexpr auto PID_BIRTH_DATE = "PID-7";
    constexpr auto PID_SEX = "PID-8";

    // ORC segment
    constexpr auto ORC_ORDER_CONTROL = "ORC-1";
    constexpr auto ORC_PLACER_ORDER = "ORC-2";
    constexpr auto ORC_FILLER_ORDER = "ORC-3";
    constexpr auto ORC_ORDER_STATUS = "ORC-5";

    // OBR segment
    constexpr auto OBR_SET_ID = "OBR-1";
    constexpr auto OBR_PLACER_ORDER = "OBR-2";
    constexpr auto OBR_FILLER_ORDER = "OBR-3";
    constexpr auto OBR_PROCEDURE_CODE = "OBR-4";
    constexpr auto OBR_SCHEDULED_DT = "OBR-7";
    constexpr auto OBR_MODALITY = "OBR-24";

    // ZDS segment (Study Instance UID)
    constexpr auto ZDS_STUDY_UID = "ZDS-1";
}

/**
 * @brief HL7 message utilities
 */
class hl7_utils {
public:
    /**
     * @brief Extract patient info from message
     */
    [[nodiscard]] static pacs_adapter::patient_info
        extract_patient(const hl7_message& msg);

    /**
     * @brief Extract order info from ORM
     */
    struct order_info {
        std::string order_control;
        std::string placer_order_number;
        std::string filler_order_number;
        std::string accession_number;
        std::string procedure_code;
        std::string modality;
        std::string scheduled_datetime;
        std::string study_uid;
    };

    [[nodiscard]] static order_info extract_order(const hl7_message& msg);

    /**
     * @brief Check if message is a new order
     */
    [[nodiscard]] static bool is_new_order(const hl7_message& msg);

    /**
     * @brief Check if message is order cancellation
     */
    [[nodiscard]] static bool is_cancel_order(const hl7_message& msg);

    /**
     * @brief Check if message is order modification
     */
    [[nodiscard]] static bool is_modify_order(const hl7_message& msg);
};

} // namespace pacs::bridge::hl7
```

### INT-MOD-002: Mapper Interface

**Traces to:** DES-TRANS-001, DES-TRANS-002

```cpp
namespace pacs::bridge::mapping {

/**
 * @brief Base mapper interface
 */
class mapper_interface {
public:
    virtual ~mapper_interface() = default;

    /**
     * @brief Get mapper name
     */
    [[nodiscard]] virtual std::string_view name() const = 0;

    /**
     * @brief Check if mapper handles this message type
     */
    [[nodiscard]] virtual bool handles(
        std::string_view message_type) const = 0;
};

/**
 * @brief HL7 to DICOM mapper interface
 */
class hl7_to_dicom_mapper : public mapper_interface {
public:
    /**
     * @brief Map HL7 message to DICOM dataset
     */
    [[nodiscard]] virtual Result<pacs::core::dicom_dataset> map(
        const hl7::hl7_message& message) = 0;
};

/**
 * @brief DICOM to HL7 mapper interface
 */
class dicom_to_hl7_mapper : public mapper_interface {
public:
    /**
     * @brief Map DICOM dataset to HL7 message
     */
    [[nodiscard]] virtual Result<std::string> map(
        const pacs::core::dicom_dataset& dataset,
        std::string_view event_type) = 0;
};

/**
 * @brief Mapper registry
 */
class mapper_registry {
public:
    /**
     * @brief Register HL7→DICOM mapper
     */
    void register_mapper(std::shared_ptr<hl7_to_dicom_mapper> mapper);

    /**
     * @brief Register DICOM→HL7 mapper
     */
    void register_mapper(std::shared_ptr<dicom_to_hl7_mapper> mapper);

    /**
     * @brief Find mapper for HL7 message type
     */
    [[nodiscard]] std::shared_ptr<hl7_to_dicom_mapper>
        find_hl7_mapper(std::string_view message_type) const;

    /**
     * @brief Find mapper for DICOM event
     */
    [[nodiscard]] std::shared_ptr<dicom_to_hl7_mapper>
        find_dicom_mapper(std::string_view event_type) const;

private:
    std::vector<std::shared_ptr<hl7_to_dicom_mapper>> hl7_mappers_;
    std::vector<std::shared_ptr<dicom_to_hl7_mapper>> dicom_mappers_;
};

} // namespace pacs::bridge::mapping
```

---

## 5. Ecosystem Integration Interfaces

### INT-ECO-001: Result<T> Pattern

**Traces to:** common_system integration

```cpp
namespace pacs::bridge {

// Use common_system Result<T> for error handling
template<typename T>
using Result = common::Result<T>;

/**
 * @brief Error creation helpers
 */
namespace errors {

inline Result<void> invalid_message(std::string_view details) {
    return Result<void>::err(-900, fmt::format("Invalid message: {}", details));
}

inline Result<void> missing_segment(std::string_view segment) {
    return Result<void>::err(-902, fmt::format("Missing segment: {}", segment));
}

inline Result<void> connection_failed(std::string_view host, uint16_t port) {
    return Result<void>::err(-920,
        fmt::format("Connection failed: {}:{}", host, port));
}

inline Result<void> mapping_failed(std::string_view reason) {
    return Result<void>::err(-940, fmt::format("Mapping failed: {}", reason));
}

} // namespace errors

} // namespace pacs::bridge
```

### INT-ECO-002: Logger Integration

**Traces to:** logger_system integration

```cpp
namespace pacs::bridge::integration {

/**
 * @brief Log levels
 */
enum class log_level {
    trace,
    debug,
    info,
    warn,
    error
};

/**
 * @brief Bridge-specific log categories
 */
namespace log_category {
    constexpr auto HL7 = "hl7";
    constexpr auto MLLP = "mllp";
    constexpr auto FHIR = "fhir";
    constexpr auto MAPPING = "mapping";
    constexpr auto ROUTING = "routing";
    constexpr auto QUEUE = "queue";
    constexpr auto PACS = "pacs";
    constexpr auto AUDIT = "audit";
}

/**
 * @brief Structured log entry
 */
struct log_entry {
    log_level level;
    std::string category;
    std::string message;
    std::map<std::string, std::string> fields;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Log macros for convenience
 */
#define BRIDGE_LOG_TRACE(category, msg, ...) \
    pacs::bridge::integration::logger_adapter::log( \
        log_level::trace, category, msg, ##__VA_ARGS__)

#define BRIDGE_LOG_DEBUG(category, msg, ...) \
    pacs::bridge::integration::logger_adapter::log( \
        log_level::debug, category, msg, ##__VA_ARGS__)

#define BRIDGE_LOG_INFO(category, msg, ...) \
    pacs::bridge::integration::logger_adapter::log( \
        log_level::info, category, msg, ##__VA_ARGS__)

#define BRIDGE_LOG_WARN(category, msg, ...) \
    pacs::bridge::integration::logger_adapter::log( \
        log_level::warn, category, msg, ##__VA_ARGS__)

#define BRIDGE_LOG_ERROR(category, msg, ...) \
    pacs::bridge::integration::logger_adapter::log( \
        log_level::error, category, msg, ##__VA_ARGS__)

} // namespace pacs::bridge::integration
```

---

## 6. Error Handling Interface

### INT-ERR-001: Error Codes

**Traces to:** PRD Appendix C

```cpp
namespace pacs::bridge::error_codes {

// ═══════════════════════════════════════════════════════════════════════════
// HL7 Parsing Errors (-900 to -919)
// ═══════════════════════════════════════════════════════════════════════════

constexpr int INVALID_HL7_MESSAGE = -900;
constexpr int MISSING_MSH_SEGMENT = -901;
constexpr int INVALID_SEGMENT_STRUCTURE = -902;
constexpr int MISSING_REQUIRED_FIELD = -903;
constexpr int INVALID_FIELD_VALUE = -904;
constexpr int UNKNOWN_MESSAGE_TYPE = -905;
constexpr int UNSUPPORTED_VERSION = -906;
constexpr int INVALID_DELIMITER = -907;
constexpr int ENCODING_ERROR = -908;

// ═══════════════════════════════════════════════════════════════════════════
// MLLP Transport Errors (-920 to -939)
// ═══════════════════════════════════════════════════════════════════════════

constexpr int MLLP_CONNECTION_FAILED = -920;
constexpr int MLLP_SEND_FAILED = -921;
constexpr int MLLP_RECEIVE_TIMEOUT = -922;
constexpr int MLLP_INVALID_FRAME = -923;
constexpr int MLLP_TLS_HANDSHAKE_FAILED = -924;
constexpr int MLLP_CONNECTION_RESET = -925;
constexpr int MLLP_SERVER_UNAVAILABLE = -926;

// ═══════════════════════════════════════════════════════════════════════════
// Translation/Mapping Errors (-940 to -959)
// ═══════════════════════════════════════════════════════════════════════════

constexpr int MAPPING_FAILED = -940;
constexpr int MISSING_MAPPING_CONFIG = -941;
constexpr int INVALID_CODE_SYSTEM = -942;
constexpr int PATIENT_NOT_FOUND = -943;
constexpr int ORDER_NOT_FOUND = -944;
constexpr int INVALID_DATE_FORMAT = -945;
constexpr int INVALID_NAME_FORMAT = -946;
constexpr int UID_GENERATION_FAILED = -947;

// ═══════════════════════════════════════════════════════════════════════════
// Queue Errors (-960 to -979)
// ═══════════════════════════════════════════════════════════════════════════

constexpr int QUEUE_FULL = -960;
constexpr int MESSAGE_EXPIRED = -961;
constexpr int DELIVERY_FAILED = -962;
constexpr int RETRY_LIMIT_EXCEEDED = -963;
constexpr int QUEUE_DATABASE_ERROR = -964;
constexpr int MESSAGE_NOT_FOUND = -965;

// ═══════════════════════════════════════════════════════════════════════════
// pacs_system Integration Errors (-980 to -999)
// ═══════════════════════════════════════════════════════════════════════════

constexpr int PACS_CONNECTION_FAILED = -980;
constexpr int MWL_UPDATE_FAILED = -981;
constexpr int MPPS_HANDLER_ERROR = -982;
constexpr int DICOM_TRANSLATION_ERROR = -983;
constexpr int PACS_ASSOCIATION_FAILED = -984;
constexpr int MWL_QUERY_FAILED = -985;
constexpr int PATIENT_CACHE_ERROR = -986;

/**
 * @brief Get human-readable error message
 */
[[nodiscard]] std::string_view message(int code);

/**
 * @brief Get error category
 */
[[nodiscard]] std::string_view category(int code);

/**
 * @brief Check if error is recoverable
 */
[[nodiscard]] bool is_recoverable(int code);

/**
 * @brief Get suggested action for error
 */
[[nodiscard]] std::string_view suggested_action(int code);

} // namespace pacs::bridge::error_codes
```

---

## 7. Configuration Interface

### INT-CFG-001: Configuration File Format

**Traces to:** FR-5, PRD Appendix D

```yaml
# pacs_bridge.yaml - Configuration Example
# ═══════════════════════════════════════════════════════════════════════════

# Server Identity
name: "PACS_BRIDGE"

# ═══════════════════════════════════════════════════════════════════════════
# HL7 Gateway Configuration
# ═══════════════════════════════════════════════════════════════════════════
hl7:
  listener:
    port: 2575
    max_connections: 50
    connection_timeout_seconds: 60
    receive_timeout_seconds: 30
    tls_enabled: false
    # cert_path: "/etc/pacs_bridge/cert.pem"
    # key_path: "/etc/pacs_bridge/key.pem"

  outbound:
    - name: "RIS_PRIMARY"
      host: "ris.hospital.local"
      port: 2576
      tls_enabled: false
      keep_alive: true
      retry_count: 3
      retry_delay_ms: 1000

    - name: "RIS_BACKUP"
      host: "ris-backup.hospital.local"
      port: 2576
      tls_enabled: false

# ═══════════════════════════════════════════════════════════════════════════
# FHIR Gateway Configuration (Phase 3)
# ═══════════════════════════════════════════════════════════════════════════
fhir:
  enabled: false
  port: 8080
  base_path: "/fhir"
  tls_enabled: false
  max_connections: 100
  page_size: 100

# ═══════════════════════════════════════════════════════════════════════════
# pacs_system Connection
# ═══════════════════════════════════════════════════════════════════════════
pacs:
  host: "localhost"
  port: 11112
  ae_title: "PACS_BRIDGE"
  called_ae: "PACS_SCP"
  timeout_seconds: 30

# ═══════════════════════════════════════════════════════════════════════════
# Mapping Configuration
# ═══════════════════════════════════════════════════════════════════════════
mapping:
  institution_name: "General Hospital"
  uid_root: "1.2.840.113619.2.55"
  patient_id_issuer: "HOSPITAL_MRN"

  procedure_to_modality:
    "CT_CHEST": "CT"
    "CT_ABDOMEN": "CT"
    "MR_BRAIN": "MR"
    "MR_SPINE": "MR"
    "US_ABDOMEN": "US"
    "XR_CHEST": "CR"

  station_ae_mapping:
    CT: ["CT_SCANNER_1", "CT_SCANNER_2"]
    MR: ["MR_SCANNER_1"]
    US: ["US_ROOM_1", "US_ROOM_2"]
    CR: ["CR_ROOM_1"]

# ═══════════════════════════════════════════════════════════════════════════
# Routing Configuration
# ═══════════════════════════════════════════════════════════════════════════
routing:
  rules:
    - name: "ADT_to_cache"
      message_type_pattern: "ADT^A*"
      destination: "patient_cache"
      priority: 10

    - name: "ORM_to_mwl"
      message_type_pattern: "ORM^O01"
      destination: "mwl_handler"
      priority: 10

    - name: "default_log"
      message_type_pattern: "*"
      destination: "log_handler"
      priority: 0

# ═══════════════════════════════════════════════════════════════════════════
# Queue Configuration
# ═══════════════════════════════════════════════════════════════════════════
queue:
  database_path: "/var/lib/pacs_bridge/queue.db"
  max_queue_size: 50000
  max_retry_count: 5
  initial_retry_delay_seconds: 5
  retry_backoff_multiplier: 2.0
  max_retry_delay_seconds: 300
  message_ttl_hours: 24

# ═══════════════════════════════════════════════════════════════════════════
# Patient Cache Configuration
# ═══════════════════════════════════════════════════════════════════════════
patient_cache:
  max_entries: 10000
  ttl_seconds: 3600
  evict_on_full: true

# ═══════════════════════════════════════════════════════════════════════════
# Logging Configuration
# ═══════════════════════════════════════════════════════════════════════════
logging:
  level: "INFO"  # TRACE, DEBUG, INFO, WARN, ERROR
  format: "json"  # json, text
  file: "/var/log/pacs_bridge/bridge.log"
  audit_file: "/var/log/pacs_bridge/audit.log"
  max_file_size_mb: 100
  max_files: 10
```

---

*Document Version: 0.1.0.0*
*Created: 2025-12-07*
*Author: kcenon@naver.com*
