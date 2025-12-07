# SDS - Component Designs

> **Version:** 1.5.0
> **Parent Document:** [SDS.md](SDS.md)
> **Last Updated:** 2025-12-07

---

## Table of Contents

- [1. HL7 Gateway Module](#1-hl7-gateway-module)
- [2. MLLP Transport Module](#2-mllp-transport-module)
- [3. FHIR Gateway Module](#3-fhir-gateway-module)
- [4. Translation Layer Module](#4-translation-layer-module)
- [5. Message Routing Module](#5-message-routing-module)
- [6. pacs_system Adapter Module](#6-pacs_system-adapter-module)
- [7. Configuration Module](#7-configuration-module)
- [8. Integration Module](#8-integration-module)
- [9. Monitoring Module](#9-monitoring-module)
- [10. Security Module](#10-security-module)
  - [10.1 Module Overview](#101-module-overview)
  - [10.2 TLS Integration Points](#102-tls-integration-points)
  - [10.3 Security Hardening Components](#103-security-hardening-components)
  - [10.4 Security Pipeline Integration](#104-security-pipeline-integration)
- [11. Testing Module](#11-testing-module)

---

## 1. HL7 Gateway Module

### 1.1 Module Overview

**Namespace:** `pacs::bridge::hl7`
**Purpose:** Parse, validate, and construct HL7 v2.x messages

```
┌────────────────────────────────────────────────────────────────────────────┐
│                           HL7 Gateway Module                                │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌─────────────────────────────────────────────────────────────────────┐  │
│   │                         hl7_message                                  │  │
│   │                                                                      │  │
│   │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────────┐   │  │
│   │  │   segment    │  │    field     │  │      component           │   │  │
│   │  │  (MSH, PID)  │  │   (PID-3)    │  │    (PID-5.1)             │   │  │
│   │  └──────────────┘  └──────────────┘  └──────────────────────────┘   │  │
│   └─────────────────────────────────────────────────────────────────────┘  │
│                                      ▲                                      │
│                                      │                                      │
│         ┌────────────────────────────┼────────────────────────────┐        │
│         │                            │                            │        │
│   ┌─────┴─────────┐           ┌──────┴──────┐           ┌─────────┴─────┐  │
│   │  hl7_parser   │           │hl7_validator│           │  hl7_builder  │  │
│   │               │           │             │           │               │  │
│   │ parse(raw)    │           │ validate()  │           │ build()       │  │
│   │ → hl7_message │           │ → Result<>  │           │ → string      │  │
│   └───────────────┘           └─────────────┘           └───────────────┘  │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### DES-HL7-001: hl7_message

**Traces to:** FR-1.1.1, FR-1.1.2, FR-1.1.3

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief HL7 v2.x message representation
 *
 * Provides hierarchical access to message content:
 *   Message → Segment → Field → Component → Subcomponent
 */
class hl7_message {
public:
    // ═══════════════════════════════════════════════════════════════════
    // Message Header Access
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Get message type (e.g., "ADT^A01")
     */
    [[nodiscard]] std::string message_type() const;

    /**
     * @brief Get message control ID (MSH-10)
     */
    [[nodiscard]] std::string control_id() const;

    /**
     * @brief Get HL7 version (e.g., "2.5.1")
     */
    [[nodiscard]] std::string version() const;

    /**
     * @brief Get sending application (MSH-3)
     */
    [[nodiscard]] std::string sending_application() const;

    /**
     * @brief Get sending facility (MSH-4)
     */
    [[nodiscard]] std::string sending_facility() const;

    // ═══════════════════════════════════════════════════════════════════
    // Segment Access
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Get segment by name
     * @param name Segment identifier (e.g., "PID", "OBR")
     * @return Pointer to segment or nullptr if not found
     */
    [[nodiscard]] const hl7_segment* segment(std::string_view name) const;

    /**
     * @brief Get all segments with given name
     * @param name Segment identifier
     * @return Vector of matching segments (for repeating segments)
     */
    [[nodiscard]] std::vector<const hl7_segment*>
        segments(std::string_view name) const;

    /**
     * @brief Check if segment exists
     */
    [[nodiscard]] bool has_segment(std::string_view name) const;

    // ═══════════════════════════════════════════════════════════════════
    // Field Access (Convenience)
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Get field value by path
     * @param path Field path (e.g., "PID-3", "OBR-4.1")
     * @return Field value or empty string if not found
     *
     * Path format: SEGMENT-FIELD[.COMPONENT[.SUBCOMPONENT]]
     */
    [[nodiscard]] std::string get_field(std::string_view path) const;

    /**
     * @brief Get repeating field values
     * @param path Field path (e.g., "PID-3")
     * @return Vector of values for repeating field
     */
    [[nodiscard]] std::vector<std::string>
        get_repeating_field(std::string_view path) const;

    // ═══════════════════════════════════════════════════════════════════
    // Iteration
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Iterate over all segments
     */
    [[nodiscard]] auto begin() const;
    [[nodiscard]] auto end() const;

    /**
     * @brief Get segment count
     */
    [[nodiscard]] size_t segment_count() const noexcept;

private:
    std::vector<hl7_segment> segments_;
    hl7_delimiters delimiters_;
};

/**
 * @brief HL7 segment representation
 */
class hl7_segment {
public:
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] const hl7_field& field(size_t index) const;
    [[nodiscard]] size_t field_count() const noexcept;

    // Convenience access
    [[nodiscard]] std::string get_value(size_t field_index,
                                         size_t component = 0,
                                         size_t subcomponent = 0) const;
};

/**
 * @brief HL7 field representation (supports repetitions)
 */
class hl7_field {
public:
    [[nodiscard]] bool is_repeating() const noexcept;
    [[nodiscard]] size_t repetition_count() const noexcept;

    [[nodiscard]] std::string_view value() const;
    [[nodiscard]] std::string_view value(size_t repetition) const;

    [[nodiscard]] const hl7_component& component(size_t index,
                                                  size_t repetition = 0) const;
};

/**
 * @brief HL7 delimiter configuration
 */
struct hl7_delimiters {
    char field_separator = '|';      // MSH-1
    char component_separator = '^';  // MSH-2[0]
    char repetition_separator = '~'; // MSH-2[1]
    char escape_character = '\\';    // MSH-2[2]
    char subcomponent_separator = '&'; // MSH-2[3]
};

} // namespace pacs::bridge::hl7
```

### DES-HL7-002: hl7_parser

**Traces to:** FR-1.1.1, FR-1.1.2, FR-1.1.3, FR-1.1.5

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief HL7 v2.x message parser
 *
 * Streaming parser with segment-by-segment validation.
 * Thread-safe: stateless operation.
 */
class hl7_parser {
public:
    /**
     * @brief Parse raw HL7 message
     * @param raw Raw message bytes (MLLP framing already removed)
     * @return Parsed message or error
     *
     * Errors:
     *   -900: INVALID_HL7_MESSAGE
     *   -901: MISSING_MSH_SEGMENT
     *   -902: INVALID_SEGMENT_STRUCTURE
     *   -903: MISSING_REQUIRED_FIELD
     */
    [[nodiscard]] static Result<hl7_message> parse(std::string_view raw);

    /**
     * @brief Parse with specific version validation
     * @param raw Raw message
     * @param expected_version Expected HL7 version (e.g., "2.5.1")
     */
    [[nodiscard]] static Result<hl7_message> parse(
        std::string_view raw,
        std::string_view expected_version);

    /**
     * @brief Parse MSH segment only (for routing decisions)
     * @param raw Raw message
     * @return MSH segment or error
     */
    [[nodiscard]] static Result<hl7_segment> parse_msh(std::string_view raw);

private:
    static Result<hl7_delimiters> parse_delimiters(std::string_view msh);
    static Result<hl7_segment> parse_segment(
        std::string_view raw,
        const hl7_delimiters& delimiters);
    static std::string unescape(
        std::string_view value,
        const hl7_delimiters& delimiters);
};

} // namespace pacs::bridge::hl7
```

### DES-HL7-003: hl7_builder

**Traces to:** FR-1.2.3, FR-1.2.5

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief HL7 message builder (fluent interface)
 *
 * Constructs HL7 messages with proper formatting and escaping.
 */
class hl7_builder {
public:
    /**
     * @brief Start building ACK message
     * @param original Original message to acknowledge
     * @param ack_code Acknowledgment code (AA, AE, AR)
     * @return Builder for chaining
     */
    static hl7_builder ack(const hl7_message& original,
                           std::string_view ack_code);

    /**
     * @brief Start building ORU^R01 message
     * @return Builder for chaining
     */
    static hl7_builder oru_r01();

    /**
     * @brief Start building ORM^O01 message
     * @return Builder for chaining
     */
    static hl7_builder orm_o01();

    // ═══════════════════════════════════════════════════════════════════
    // Segment Building
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Add MSH segment
     */
    hl7_builder& msh(std::string_view sending_app,
                     std::string_view sending_facility,
                     std::string_view receiving_app,
                     std::string_view receiving_facility,
                     std::string_view message_type,
                     std::string_view version = "2.5.1");

    /**
     * @brief Add PID segment
     */
    hl7_builder& pid(std::string_view patient_id,
                     std::string_view patient_name,
                     std::string_view birth_date = "",
                     std::string_view sex = "");

    /**
     * @brief Add ORC segment
     */
    hl7_builder& orc(std::string_view order_control,
                     std::string_view placer_order,
                     std::string_view filler_order = "",
                     std::string_view order_status = "");

    /**
     * @brief Add OBR segment
     */
    hl7_builder& obr(int set_id,
                     std::string_view placer_order,
                     std::string_view filler_order,
                     std::string_view procedure_code);

    /**
     * @brief Add OBX segment
     */
    hl7_builder& obx(int set_id,
                     std::string_view value_type,
                     std::string_view observation_id,
                     std::string_view value,
                     std::string_view units = "");

    /**
     * @brief Add MSA segment (for ACK)
     */
    hl7_builder& msa(std::string_view ack_code,
                     std::string_view control_id,
                     std::string_view text_message = "");

    /**
     * @brief Add custom segment
     */
    hl7_builder& segment(std::string_view name,
                         const std::vector<std::string>& fields);

    // ═══════════════════════════════════════════════════════════════════
    // Output
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Build final message string
     * @return HL7 message (without MLLP framing)
     */
    [[nodiscard]] std::string build() const;

    /**
     * @brief Build as hl7_message object
     */
    [[nodiscard]] Result<hl7_message> build_message() const;

private:
    std::vector<std::string> segments_;
    hl7_delimiters delimiters_;
    std::string escape(std::string_view value) const;
};

} // namespace pacs::bridge::hl7
```

### DES-HL7-004: hl7_validator

**Traces to:** FR-1.1.4, FR-1.2

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief Message type-specific validation
 */
class hl7_validator {
public:
    /**
     * @brief Validate message structure
     * @param message Parsed HL7 message
     * @return Validation result with details
     */
    [[nodiscard]] static Result<void> validate(const hl7_message& message);

    /**
     * @brief Validate ADT message
     */
    [[nodiscard]] static Result<void> validate_adt(const hl7_message& message);

    /**
     * @brief Validate ORM message
     */
    [[nodiscard]] static Result<void> validate_orm(const hl7_message& message);

    /**
     * @brief Validate ORU message
     */
    [[nodiscard]] static Result<void> validate_oru(const hl7_message& message);

    /**
     * @brief Validate SIU message
     */
    [[nodiscard]] static Result<void> validate_siu(const hl7_message& message);

private:
    static Result<void> check_required_segments(
        const hl7_message& message,
        const std::vector<std::string>& required);

    static Result<void> check_required_fields(
        const hl7_segment& segment,
        const std::vector<int>& required_fields);
};

/**
 * @brief Validation configuration for message types
 */
struct validation_rules {
    std::vector<std::string> required_segments;
    std::map<std::string, std::vector<int>> required_fields;
};

} // namespace pacs::bridge::hl7
```

---

## 2. MLLP Transport Module

### 2.1 Module Overview

**Namespace:** `pacs::bridge::mllp`
**Purpose:** Handle MLLP protocol framing and network transport

### DES-MLLP-001: mllp_server

**Traces to:** FR-1.3.1, FR-1.3.3, FR-1.3.4

```cpp
namespace pacs::bridge::mllp {

/**
 * @brief MLLP server configuration
 */
struct mllp_server_config {
    uint16_t port = 2575;
    size_t max_connections = 50;
    std::chrono::seconds connection_timeout{60};
    std::chrono::seconds receive_timeout{30};
    bool tls_enabled = false;
    std::string cert_path;
    std::string key_path;
};

/**
 * @brief Message handler callback type
 *
 * @param message Received HL7 message (MLLP frame removed)
 * @param connection Connection context for response
 * @return Response message to send back
 */
using message_handler = std::function<
    std::string(const std::string& message, mllp_connection& connection)>;

/**
 * @brief MLLP server (listener)
 *
 * Accepts incoming HL7 connections using MLLP framing.
 */
class mllp_server {
public:
    explicit mllp_server(const mllp_server_config& config);
    ~mllp_server();

    // Non-copyable, movable
    mllp_server(const mllp_server&) = delete;
    mllp_server& operator=(const mllp_server&) = delete;
    mllp_server(mllp_server&&) noexcept;
    mllp_server& operator=(mllp_server&&) noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // Handler Registration
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Set message handler
     * @param handler Callback for each received message
     */
    void set_handler(message_handler handler);

    /**
     * @brief Set connection event handler
     */
    void set_connection_handler(
        std::function<void(const mllp_connection&, bool connected)> handler);

    // ═══════════════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Start accepting connections
     */
    [[nodiscard]] Result<void> start();

    /**
     * @brief Stop accepting new connections
     * @param wait_for_active Wait for active connections to complete
     */
    void stop(bool wait_for_active = true);

    /**
     * @brief Check if server is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // Statistics
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Get server statistics
     */
    struct statistics {
        size_t active_connections;
        size_t total_connections;
        size_t messages_received;
        size_t messages_sent;
        size_t errors;
    };

    [[nodiscard]] statistics get_statistics() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::mllp
```

### DES-MLLP-002: mllp_client

**Traces to:** FR-1.3.2, FR-1.3.3

```cpp
namespace pacs::bridge::mllp {

/**
 * @brief MLLP client configuration
 */
struct mllp_client_config {
    std::string host;
    uint16_t port = 2575;
    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds send_timeout{30};
    std::chrono::seconds receive_timeout{30};
    bool tls_enabled = false;
    std::string ca_cert_path;
    bool keep_alive = true;
    size_t retry_count = 3;
    std::chrono::milliseconds retry_delay{1000};
};

/**
 * @brief MLLP client (sender)
 *
 * Sends HL7 messages to remote MLLP servers.
 */
class mllp_client {
public:
    explicit mllp_client(const mllp_client_config& config);
    ~mllp_client();

    // Non-copyable, movable
    mllp_client(const mllp_client&) = delete;
    mllp_client& operator=(const mllp_client&) = delete;
    mllp_client(mllp_client&&) noexcept;
    mllp_client& operator=(mllp_client&&) noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // Connection
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Connect to remote server
     */
    [[nodiscard]] Result<void> connect();

    /**
     * @brief Disconnect from server
     */
    void disconnect();

    /**
     * @brief Check connection status
     */
    [[nodiscard]] bool is_connected() const noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // Messaging
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Send message and wait for response
     * @param message HL7 message (without MLLP framing)
     * @return Response message or error
     *
     * Errors:
     *   -920: MLLP_CONNECTION_FAILED
     *   -921: MLLP_SEND_FAILED
     *   -922: MLLP_RECEIVE_TIMEOUT
     */
    [[nodiscard]] Result<std::string> send(const std::string& message);

    /**
     * @brief Send message asynchronously
     * @param message HL7 message
     * @return Future with response
     */
    [[nodiscard]] std::future<Result<std::string>>
        send_async(const std::string& message);

    /**
     * @brief Send with automatic retry
     */
    [[nodiscard]] Result<std::string> send_with_retry(
        const std::string& message,
        size_t max_retries = 0); // 0 = use config default

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::mllp
```

### DES-MLLP-003: mllp_connection

**Traces to:** FR-1.3.4

```cpp
namespace pacs::bridge::mllp {

/**
 * @brief MLLP framing constants
 */
constexpr char MLLP_START = 0x0B;  // VT (Vertical Tab)
constexpr char MLLP_END_1 = 0x1C;  // FS (File Separator)
constexpr char MLLP_END_2 = 0x0D;  // CR (Carriage Return)

/**
 * @brief MLLP connection (server-side)
 */
class mllp_connection {
public:
    /**
     * @brief Get remote endpoint info
     */
    [[nodiscard]] std::string remote_address() const;
    [[nodiscard]] uint16_t remote_port() const;

    /**
     * @brief Get connection ID
     */
    [[nodiscard]] std::string connection_id() const;

    /**
     * @brief Send response
     * @param message HL7 message (MLLP framing added automatically)
     */
    [[nodiscard]] Result<void> send(const std::string& message);

    /**
     * @brief Close connection
     */
    void close();

    /**
     * @brief Check if connection is open
     */
    [[nodiscard]] bool is_open() const noexcept;

private:
    friend class mllp_server;
    class impl;
    std::shared_ptr<impl> pimpl_;
};

/**
 * @brief MLLP frame utilities
 */
class mllp_frame {
public:
    /**
     * @brief Add MLLP framing to message
     */
    [[nodiscard]] static std::string wrap(const std::string& message);

    /**
     * @brief Remove MLLP framing from message
     * @return Message content or error if invalid frame
     */
    [[nodiscard]] static Result<std::string> unwrap(const std::string& frame);

    /**
     * @brief Check if buffer contains complete MLLP message
     * @param buffer Input buffer
     * @param message_end Output: position after complete message (if found)
     * @return true if complete message found
     */
    [[nodiscard]] static bool is_complete(
        std::string_view buffer,
        size_t& message_end);
};

} // namespace pacs::bridge::mllp
```

---

## 3. FHIR Gateway Module

### 3.1 Module Overview

**Namespace:** `pacs::bridge::fhir`
**Purpose:** Provide FHIR R4 REST API for modern EHR integration

### DES-FHIR-001: fhir_server

**Traces to:** FR-2.1.1, FR-2.1.2, FR-2.1.3, FR-2.1.4

```cpp
namespace pacs::bridge::fhir {

/**
 * @brief FHIR server configuration
 */
struct fhir_server_config {
    uint16_t port = 8080;
    std::string base_path = "/fhir";
    bool tls_enabled = false;
    std::string cert_path;
    std::string key_path;
    size_t max_connections = 100;
    size_t page_size = 100;
};

/**
 * @brief FHIR R4 REST server
 */
class fhir_server {
public:
    explicit fhir_server(const fhir_server_config& config);
    ~fhir_server();

    // ═══════════════════════════════════════════════════════════════════
    // Resource Handlers
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Register resource handler
     * @tparam T Resource type (Patient, ServiceRequest, etc.)
     * @param handler Handler instance
     */
    template<typename T>
    void register_handler(std::shared_ptr<resource_handler<T>> handler);

    // ═══════════════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════════════

    [[nodiscard]] Result<void> start();
    void stop();
    [[nodiscard]] bool is_running() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief Resource handler interface
 */
template<typename T>
class resource_handler {
public:
    virtual ~resource_handler() = default;

    /**
     * @brief Read resource by ID
     */
    [[nodiscard]] virtual Result<T> read(const std::string& id) = 0;

    /**
     * @brief Search resources
     * @param params Search parameters
     * @return Bundle of matching resources
     */
    [[nodiscard]] virtual Result<std::vector<T>> search(
        const std::map<std::string, std::string>& params) = 0;

    /**
     * @brief Create new resource
     */
    [[nodiscard]] virtual Result<T> create(const T& resource) = 0;

    /**
     * @brief Update existing resource
     */
    [[nodiscard]] virtual Result<T> update(
        const std::string& id,
        const T& resource) = 0;

    /**
     * @brief Delete resource
     */
    [[nodiscard]] virtual Result<void> delete_resource(
        const std::string& id) = 0;
};

} // namespace pacs::bridge::fhir
```

### DES-FHIR-002: fhir_resource

**Traces to:** FR-2.2.1, FR-2.2.2, FR-2.2.3, FR-2.2.4

```cpp
namespace pacs::bridge::fhir {

/**
 * @brief Base FHIR resource
 */
class fhir_resource {
public:
    virtual ~fhir_resource() = default;

    [[nodiscard]] std::string id() const;
    [[nodiscard]] std::string resource_type() const;

    /**
     * @brief Serialize to JSON
     */
    [[nodiscard]] virtual std::string to_json() const = 0;

    /**
     * @brief Parse from JSON
     */
    [[nodiscard]] static Result<std::unique_ptr<fhir_resource>>
        from_json(const std::string& json);

protected:
    std::string id_;
    std::string resource_type_;
};

/**
 * @brief FHIR Patient resource
 */
class patient_resource : public fhir_resource {
public:
    struct name {
        std::string family;
        std::vector<std::string> given;
    };

    struct identifier {
        std::string system;
        std::string value;
    };

    std::vector<identifier> identifiers;
    std::vector<name> names;
    std::string birth_date;
    std::string gender; // male, female, other, unknown

    [[nodiscard]] std::string to_json() const override;
    [[nodiscard]] static Result<patient_resource> from_json(
        const std::string& json);
};

/**
 * @brief FHIR ServiceRequest resource (imaging order)
 */
class service_request_resource : public fhir_resource {
public:
    std::string status; // draft, active, completed, cancelled
    std::string intent; // order, original-order
    std::string patient_reference;
    std::string requester_reference;

    struct coding {
        std::string system;
        std::string code;
        std::string display;
    };
    std::vector<coding> code_codings;

    std::string scheduled_datetime;
    std::string performer_reference; // AE Title / Location

    [[nodiscard]] std::string to_json() const override;
};

/**
 * @brief FHIR ImagingStudy resource
 */
class imaging_study_resource : public fhir_resource {
public:
    std::string status; // registered, available, cancelled
    std::string patient_reference;
    std::string study_instance_uid;
    std::string started;
    std::string description;
    size_t number_of_series = 0;
    size_t number_of_instances = 0;

    struct series_info {
        std::string uid;
        std::string modality;
        size_t number_of_instances;
    };
    std::vector<series_info> series;

    [[nodiscard]] std::string to_json() const override;
};

/**
 * @brief FHIR DiagnosticReport resource
 */
class diagnostic_report_resource : public fhir_resource {
public:
    std::string status; // registered, partial, preliminary, final
    std::string patient_reference;
    std::string study_reference;
    std::string conclusion;
    std::string issued;

    [[nodiscard]] std::string to_json() const override;
};

} // namespace pacs::bridge::fhir
```

---

## 4. Translation Layer Module

### 4.1 Module Overview

**Namespace:** `pacs::bridge::mapping`
**Purpose:** Translate between HL7, FHIR, and DICOM protocols

### DES-TRANS-001: hl7_dicom_mapper

**Traces to:** FR-3.1.1, FR-3.1.2, FR-3.1.3, FR-3.1.4, FR-3.1.5

```cpp
namespace pacs::bridge::mapping {

/**
 * @brief HL7 to DICOM MWL mapper
 *
 * Converts HL7 ORM^O01 messages to DICOM Modality Worklist entries.
 */
class hl7_dicom_mapper {
public:
    explicit hl7_dicom_mapper(const mapping_config& config);

    // ═══════════════════════════════════════════════════════════════════
    // ORM → MWL Mapping
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Convert ORM message to MWL dataset
     * @param orm HL7 ORM^O01 message
     * @return DICOM MWL dataset or error
     *
     * Maps:
     *   PID-3 → PatientID (0010,0020)
     *   PID-5 → PatientName (0010,0010)
     *   PID-7 → PatientBirthDate (0010,0030)
     *   PID-8 → PatientSex (0010,0040)
     *   ORC-2 → PlacerOrderNumberImagingServiceRequest
     *   ORC-3 → AccessionNumber (0008,0050)
     *   OBR-4 → RequestedProcedureCodeSequence
     *   OBR-7 → ScheduledProcedureStepStartDate/Time
     *   OBR-24 → Modality (0008,0060)
     *   ZDS-1 → StudyInstanceUID (0020,000D)
     */
    [[nodiscard]] Result<pacs::core::dicom_dataset>
        orm_to_mwl(const hl7::hl7_message& orm);

    /**
     * @brief Convert ADT message to patient demographics
     * @param adt HL7 ADT message
     * @return Patient demographics dataset
     */
    [[nodiscard]] Result<pacs::core::dicom_dataset>
        adt_to_patient(const hl7::hl7_message& adt);

    // ═══════════════════════════════════════════════════════════════════
    // Field-Level Mapping
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Map patient name (HL7 XPN → DICOM PN)
     *
     * HL7 format: FAMILY^GIVEN^MIDDLE^SUFFIX^PREFIX
     * DICOM format: FAMILY^GIVEN MIDDLE^PREFIX^SUFFIX
     */
    [[nodiscard]] static std::string map_patient_name(
        std::string_view hl7_name);

    /**
     * @brief Map date (HL7 → DICOM DA)
     *
     * HL7 format: YYYYMMDD[HHMM[SS[.S[S[S[S]]]]]][+/-ZZZZ]
     * DICOM format: YYYYMMDD
     */
    [[nodiscard]] static std::string map_date(std::string_view hl7_date);

    /**
     * @brief Map time (HL7 → DICOM TM)
     *
     * HL7 format: HHMM[SS[.S[S[S[S]]]]]
     * DICOM format: HHMMSS.FFFFFF
     */
    [[nodiscard]] static std::string map_time(std::string_view hl7_time);

    /**
     * @brief Map procedure code
     *
     * Creates RequestedProcedureCodeSequence item
     */
    [[nodiscard]] Result<pacs::core::dicom_dataset>
        map_procedure_code(std::string_view hl7_code);

private:
    mapping_config config_;

    std::string lookup_modality_code(std::string_view procedure_code);
    std::string generate_uid();
};

/**
 * @brief Mapping configuration
 */
struct mapping_config {
    // Modality code mappings
    std::map<std::string, std::string> procedure_to_modality;

    // Institution-specific
    std::string institution_name;
    std::string station_ae_title_prefix;

    // UID generation
    std::string uid_root;

    // Patient ID domain
    std::string patient_id_issuer;
};

} // namespace pacs::bridge::mapping
```

### DES-TRANS-002: dicom_hl7_mapper

**Traces to:** FR-3.2.1, FR-3.2.2, FR-3.2.3, FR-3.2.4, FR-3.2.5

```cpp
namespace pacs::bridge::mapping {

/**
 * @brief DICOM to HL7 mapper
 *
 * Converts MPPS events to HL7 ORM status updates and ORU messages.
 */
class dicom_hl7_mapper {
public:
    explicit dicom_hl7_mapper(const mapping_config& config);

    // ═══════════════════════════════════════════════════════════════════
    // MPPS → HL7 Mapping
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Convert MPPS IN PROGRESS to HL7 ORM status update
     * @param mpps MPPS N-CREATE dataset
     * @return HL7 ORM^O01 message with status update
     *
     * Maps:
     *   PerformedProcedureStepStatus → ORC-5 (IP = In Progress)
     *   PerformedStationAETitle → OBR-21
     *   PerformedProcedureStepStartDateTime → OBR-22
     */
    [[nodiscard]] Result<std::string>
        mpps_in_progress_to_orm(const pacs::core::dicom_dataset& mpps);

    /**
     * @brief Convert MPPS COMPLETED to HL7 ORM status update
     * @param mpps MPPS N-SET dataset
     * @return HL7 ORM^O01 message with completion status
     *
     * Maps:
     *   PerformedProcedureStepStatus → ORC-5 (CM = Complete)
     *   PerformedProcedureStepEndDateTime → OBR-27
     *   PerformedSeriesSequence → (logging only)
     */
    [[nodiscard]] Result<std::string>
        mpps_completed_to_orm(const pacs::core::dicom_dataset& mpps);

    /**
     * @brief Convert MPPS DISCONTINUED to HL7 ORM cancellation
     * @param mpps MPPS N-SET dataset with DISCONTINUED status
     * @return HL7 ORM^O01 message with cancellation
     */
    [[nodiscard]] Result<std::string>
        mpps_discontinued_to_orm(const pacs::core::dicom_dataset& mpps);

    // ═══════════════════════════════════════════════════════════════════
    // Report → ORU Mapping
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Convert report to HL7 ORU^R01
     * @param study Study information
     * @param report Report content
     * @param status Report status (P = Preliminary, F = Final)
     * @return HL7 ORU^R01 message
     */
    [[nodiscard]] Result<std::string> report_to_oru(
        const pacs::core::dicom_dataset& study,
        std::string_view report,
        char status);

private:
    mapping_config config_;

    std::string format_datetime(
        std::string_view dicom_date,
        std::string_view dicom_time);
};

/**
 * @brief MPPS status to HL7 order control/status mapping
 */
struct mpps_status_mapping {
    static constexpr std::string_view orc_order_control(
        std::string_view mpps_status) {
        if (mpps_status == "IN PROGRESS") return "SC";  // Status Change
        if (mpps_status == "COMPLETED") return "SC";
        if (mpps_status == "DISCONTINUED") return "CA"; // Cancel
        return "SC";
    }

    static constexpr std::string_view orc_order_status(
        std::string_view mpps_status) {
        if (mpps_status == "IN PROGRESS") return "IP";
        if (mpps_status == "COMPLETED") return "CM";
        if (mpps_status == "DISCONTINUED") return "CA";
        return "";
    }
};

} // namespace pacs::bridge::mapping
```

### DES-TRANS-003: fhir_dicom_mapper

**Traces to:** FR-2.2.2, FR-2.2.3

```cpp
namespace pacs::bridge::mapping {

/**
 * @brief FHIR to DICOM mapper
 *
 * Converts FHIR ServiceRequest to DICOM MWL entries.
 */
class fhir_dicom_mapper {
public:
    explicit fhir_dicom_mapper(const mapping_config& config);

    /**
     * @brief Convert ServiceRequest to MWL dataset
     */
    [[nodiscard]] Result<pacs::core::dicom_dataset> service_request_to_mwl(
        const fhir::service_request_resource& request,
        const fhir::patient_resource& patient);

    /**
     * @brief Convert ImagingStudy from DICOM study
     */
    [[nodiscard]] Result<fhir::imaging_study_resource> study_to_imaging_study(
        const pacs::core::dicom_dataset& study);

    /**
     * @brief Convert Patient from DICOM demographics
     */
    [[nodiscard]] Result<fhir::patient_resource> dataset_to_patient(
        const pacs::core::dicom_dataset& dataset);

private:
    mapping_config config_;
};

} // namespace pacs::bridge::mapping
```

---

## 5. Message Routing Module

### 5.1 Module Overview

**Namespace:** `pacs::bridge::router`
**Purpose:** Route messages between gateways and handle message queuing

### DES-ROUTE-001: message_router

**Traces to:** FR-4.1.1, FR-4.1.2, FR-4.1.3, FR-4.2.1, FR-4.2.2

```cpp
namespace pacs::bridge::router {

/**
 * @brief Routing rule
 */
struct routing_rule {
    std::string name;
    std::string message_type_pattern;  // e.g., "ADT^A*", "ORM^O01"
    std::string source_pattern;        // e.g., "HIS_*"
    std::string destination;           // Handler name or endpoint
    int priority = 0;                  // Higher = more priority
};

/**
 * @brief Message router
 *
 * Routes incoming messages to appropriate handlers based on rules.
 */
class message_router {
public:
    explicit message_router(const std::vector<routing_rule>& rules);

    // ═══════════════════════════════════════════════════════════════════
    // Handler Registration
    // ═══════════════════════════════════════════════════════════════════

    using handler_fn = std::function<Result<void>(const hl7::hl7_message&)>;

    /**
     * @brief Register message handler
     * @param name Handler name (referenced in routing rules)
     * @param handler Handler function
     */
    void register_handler(std::string_view name, handler_fn handler);

    // ═══════════════════════════════════════════════════════════════════
    // Routing
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Route incoming message
     * @param message HL7 message to route
     * @return Result of handler execution
     */
    [[nodiscard]] Result<void> route(const hl7::hl7_message& message);

    /**
     * @brief Get matching rule for message
     * @param message HL7 message
     * @return Matching rule or nullptr
     */
    [[nodiscard]] const routing_rule* find_rule(
        const hl7::hl7_message& message) const;

private:
    std::vector<routing_rule> rules_;
    std::map<std::string, handler_fn> handlers_;

    bool matches_pattern(std::string_view value, std::string_view pattern);
};

} // namespace pacs::bridge::router
```

#### Routing Decision Logging

The `message_router` supports comprehensive logging of routing decisions through two mechanisms:

1. **GlobalLoggerRegistry Integration**: Routes logging through `kcenon::common::interfaces::GlobalLoggerRegistry` with logger name "message_router"
2. **Custom Callback**: Applications can register a custom logger callback for integration with other logging frameworks

**Log Levels:**
- `debug`: Detailed routing decisions (message start, handler execution, chain stops)
- `info`: Route matches and completion with timing information
- `warning`: No matching route (using default handler or unhandled)
- `error`: Handler failures and exceptions

**Example Usage:**
```cpp
message_router router;

// Set log level to debug for detailed logging
router.set_log_level(message_router::log_level::debug);

// Optional: Add custom callback for application-specific logging
router.set_logger([](const message_router::log_entry& entry) {
    // Custom logging logic
    audit_logger.log(entry.message_type, entry.message);
});
```

### DES-ROUTE-002: queue_manager

**Traces to:** FR-4.3.1, FR-4.3.2, FR-4.3.3, FR-4.3.4

```cpp
namespace pacs::bridge::router {

/**
 * @brief Queued message
 */
struct queued_message {
    std::string id;
    std::string destination;
    std::string payload;
    int priority = 0;
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point next_attempt;
    int attempt_count = 0;
    std::string last_error;
};

/**
 * @brief Queue configuration
 */
struct queue_config {
    std::string database_path = "queue.db";
    size_t max_queue_size = 50000;
    size_t max_retry_count = 5;
    std::chrono::seconds initial_retry_delay{5};
    double retry_backoff_multiplier = 2.0;
    std::chrono::seconds max_retry_delay{300};
    std::chrono::hours message_ttl{24};
};

/**
 * @brief Persistent message queue manager
 */
class queue_manager {
public:
    explicit queue_manager(const queue_config& config);
    ~queue_manager();

    // ═══════════════════════════════════════════════════════════════════
    // Queue Operations
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Enqueue message for delivery
     * @param destination Destination identifier
     * @param payload Message payload
     * @param priority Message priority (higher = more urgent)
     * @return Message ID or error if queue full
     */
    [[nodiscard]] Result<std::string> enqueue(
        std::string_view destination,
        std::string_view payload,
        int priority = 0);

    /**
     * @brief Dequeue next message for delivery
     * @param destination Destination to dequeue from
     * @return Message or nullopt if queue empty
     */
    [[nodiscard]] std::optional<queued_message> dequeue(
        std::string_view destination);

    /**
     * @brief Mark message as successfully delivered
     */
    Result<void> ack(std::string_view message_id);

    /**
     * @brief Mark message as failed (will retry later)
     * @param message_id Message ID
     * @param error Error description
     */
    Result<void> nack(std::string_view message_id, std::string_view error);

    /**
     * @brief Permanently fail message (no more retries)
     */
    Result<void> dead_letter(std::string_view message_id,
                             std::string_view reason);

    // ═══════════════════════════════════════════════════════════════════
    // Queue Status
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Get queue depth
     */
    [[nodiscard]] size_t queue_depth(std::string_view destination) const;

    /**
     * @brief Get total pending messages
     */
    [[nodiscard]] size_t total_pending() const;

    /**
     * @brief Get dead letter count
     */
    [[nodiscard]] size_t dead_letter_count() const;

    // ═══════════════════════════════════════════════════════════════════
    // Background Processing
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Start background delivery workers
     * @param sender Function to send messages
     */
    void start_workers(
        std::function<Result<void>(const queued_message&)> sender);

    /**
     * @brief Stop background workers
     */
    void stop_workers();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::router
```

---

## 6. pacs_system Adapter Module

### 6.1 Module Overview

**Namespace:** `pacs::bridge::pacs_adapter`
**Purpose:** Integrate with pacs_system for DICOM services

### DES-PACS-001: mwl_client

**Traces to:** IR-1 (worklist_scp), FR-3.1

```cpp
namespace pacs::bridge::pacs_adapter {

/**
 * @brief MWL client configuration
 */
struct mwl_client_config {
    std::string pacs_host = "localhost";
    uint16_t pacs_port = 11112;
    std::string our_ae_title = "PACS_BRIDGE";
    std::string pacs_ae_title = "PACS_SCP";
    std::chrono::seconds timeout{30};
};

/**
 * @brief Modality Worklist client
 *
 * Manages MWL entries in pacs_system.
 */
class mwl_client {
public:
    explicit mwl_client(const mwl_client_config& config);
    ~mwl_client();

    // ═══════════════════════════════════════════════════════════════════
    // MWL Operations
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Add new worklist entry
     * @param mwl_item MWL dataset (from HL7 ORM mapping)
     */
    [[nodiscard]] Result<void> add_entry(
        const pacs::core::dicom_dataset& mwl_item);

    /**
     * @brief Update existing worklist entry
     * @param accession_number Accession number to update
     * @param updates Fields to update
     */
    [[nodiscard]] Result<void> update_entry(
        std::string_view accession_number,
        const pacs::core::dicom_dataset& updates);

    /**
     * @brief Cancel (remove) worklist entry
     * @param accession_number Accession number to cancel
     */
    [[nodiscard]] Result<void> cancel_entry(std::string_view accession_number);

    /**
     * @brief Query worklist entries
     * @param query Query keys
     * @return Matching entries
     */
    [[nodiscard]] Result<std::vector<pacs::core::dicom_dataset>> query(
        const pacs::core::dicom_dataset& query);

    /**
     * @brief Check if entry exists
     */
    [[nodiscard]] bool exists(std::string_view accession_number);

private:
    mwl_client_config config_;
    // Uses pacs::services::worklist_scp internally
};

} // namespace pacs::bridge::pacs_adapter
```

### DES-PACS-002: mpps_handler

**Traces to:** IR-1 (mpps_scp), FR-3.2

```cpp
namespace pacs::bridge::pacs_adapter {

/**
 * @brief MPPS event types
 */
enum class mpps_event {
    in_progress,
    completed,
    discontinued
};

/**
 * @brief MPPS event callback
 */
using mpps_callback = std::function<void(
    mpps_event event,
    const pacs::core::dicom_dataset& mpps)>;

/**
 * @brief MPPS handler
 *
 * Receives MPPS notifications from pacs_system and forwards to HL7.
 */
class mpps_handler {
public:
    explicit mpps_handler(const mpps_callback& callback);

    /**
     * @brief Register with pacs_system MPPS SCP
     * @param mpps_scp Reference to pacs_system MPPS SCP
     */
    void register_with_pacs(pacs::services::mpps_scp& mpps_scp);

    /**
     * @brief Handle incoming MPPS N-CREATE
     */
    void on_n_create(const pacs::core::dicom_dataset& mpps);

    /**
     * @brief Handle incoming MPPS N-SET
     */
    void on_n_set(const pacs::core::dicom_dataset& mpps);

private:
    mpps_callback callback_;
};

} // namespace pacs::bridge::pacs_adapter
```

### DES-PACS-003: patient_cache

**Traces to:** FR-4.1.1

```cpp
namespace pacs::bridge::pacs_adapter {

/**
 * @brief Patient cache configuration
 */
struct patient_cache_config {
    size_t max_entries = 10000;
    std::chrono::seconds ttl{3600};  // 1 hour default
    bool evict_on_full = true;
};

/**
 * @brief Patient demographics
 */
struct patient_info {
    std::string patient_id;
    std::string patient_name;
    std::string birth_date;
    std::string sex;
    std::string issuer;
    std::chrono::system_clock::time_point cached_at;
};

/**
 * @brief In-memory patient cache
 *
 * Caches patient demographics from ADT messages for fast lookup.
 * Thread-safe with read-write locking.
 */
class patient_cache {
public:
    explicit patient_cache(const patient_cache_config& config);

    // ═══════════════════════════════════════════════════════════════════
    // Cache Operations
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Add or update patient
     * @param patient Patient information
     */
    void put(const patient_info& patient);

    /**
     * @brief Get patient by ID
     * @param patient_id Patient ID
     * @return Patient info or nullopt if not found/expired
     */
    [[nodiscard]] std::optional<patient_info> get(
        std::string_view patient_id) const;

    /**
     * @brief Remove patient from cache
     */
    void remove(std::string_view patient_id);

    /**
     * @brief Clear all entries
     */
    void clear();

    // ═══════════════════════════════════════════════════════════════════
    // Status
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Get cache size
     */
    [[nodiscard]] size_t size() const noexcept;

    /**
     * @brief Get hit rate
     */
    [[nodiscard]] double hit_rate() const noexcept;

    /**
     * @brief Get cache statistics
     */
    struct statistics {
        size_t entries;
        size_t hits;
        size_t misses;
        size_t evictions;
    };
    [[nodiscard]] statistics get_statistics() const;

private:
    patient_cache_config config_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, patient_info> cache_;
    std::list<std::string> lru_order_;  // For LRU eviction

    mutable size_t hits_ = 0;
    mutable size_t misses_ = 0;
    size_t evictions_ = 0;

    void evict_if_needed();
    void touch(std::string_view patient_id);
};

} // namespace pacs::bridge::pacs_adapter
```

---

## 7. Configuration Module

### DES-CFG-001: bridge_config

**Traces to:** FR-5.1, FR-5.2

```cpp
namespace pacs::bridge::config {

/**
 * @brief Complete bridge configuration
 */
struct bridge_config {
    // ═══════════════════════════════════════════════════════════════════
    // Server Identity
    // ═══════════════════════════════════════════════════════════════════

    std::string name = "PACS_BRIDGE";

    // ═══════════════════════════════════════════════════════════════════
    // HL7 Gateway
    // ═══════════════════════════════════════════════════════════════════

    struct hl7_config {
        mllp::mllp_server_config listener;
        std::vector<mllp::mllp_client_config> outbound_destinations;
    } hl7;

    // ═══════════════════════════════════════════════════════════════════
    // FHIR Gateway
    // ═══════════════════════════════════════════════════════════════════

    struct fhir_config {
        bool enabled = false;
        fhir::fhir_server_config server;
    } fhir;

    // ═══════════════════════════════════════════════════════════════════
    // pacs_system Connection
    // ═══════════════════════════════════════════════════════════════════

    struct pacs_config {
        std::string host = "localhost";
        uint16_t port = 11112;
        std::string ae_title = "PACS_BRIDGE";
        std::string called_ae = "PACS_SCP";
    } pacs;

    // ═══════════════════════════════════════════════════════════════════
    // Mapping Configuration
    // ═══════════════════════════════════════════════════════════════════

    mapping::mapping_config mapping;

    // ═══════════════════════════════════════════════════════════════════
    // Routing Configuration
    // ═══════════════════════════════════════════════════════════════════

    std::vector<router::routing_rule> routing_rules;

    // ═══════════════════════════════════════════════════════════════════
    // Queue Configuration
    // ═══════════════════════════════════════════════════════════════════

    router::queue_config queue;

    // ═══════════════════════════════════════════════════════════════════
    // Cache Configuration
    // ═══════════════════════════════════════════════════════════════════

    pacs_adapter::patient_cache_config patient_cache;

    // ═══════════════════════════════════════════════════════════════════
    // Logging Configuration
    // ═══════════════════════════════════════════════════════════════════

    struct logging_config {
        std::string level = "INFO";
        std::string format = "json";
        std::string file;
        std::string audit_file;
    } logging;
};

/**
 * @brief Configuration loader
 */
class config_loader {
public:
    /**
     * @brief Load configuration from YAML file
     */
    [[nodiscard]] static Result<bridge_config> load_yaml(
        const std::filesystem::path& path);

    /**
     * @brief Load configuration from JSON file
     */
    [[nodiscard]] static Result<bridge_config> load_json(
        const std::filesystem::path& path);

    /**
     * @brief Validate configuration
     */
    [[nodiscard]] static Result<void> validate(const bridge_config& config);

    /**
     * @brief Save configuration to file
     */
    [[nodiscard]] static Result<void> save_yaml(
        const bridge_config& config,
        const std::filesystem::path& path);
};

} // namespace pacs::bridge::config
```

---

## 8. Integration Module

### DES-INT-001: network_adapter

**Traces to:** IR-2 (network_system)

```cpp
namespace pacs::bridge::integration {

/**
 * @brief Network system adapter
 *
 * Wraps network_system for TCP/TLS operations.
 */
class network_adapter {
public:
    /**
     * @brief Create TCP server
     */
    [[nodiscard]] static std::unique_ptr<network_system::tcp_server>
        create_tcp_server(uint16_t port, size_t max_connections);

    /**
     * @brief Create TCP client connection
     */
    [[nodiscard]] static Result<network_system::tcp_socket>
        connect(const std::string& host, uint16_t port,
                std::chrono::milliseconds timeout);

    /**
     * @brief Configure TLS context
     */
    static void configure_tls(
        network_system::tls_context& ctx,
        const std::filesystem::path& cert_path,
        const std::filesystem::path& key_path);
};

} // namespace pacs::bridge::integration
```

### DES-INT-002: thread_adapter

**Traces to:** IR-2 (thread_system)

```cpp
namespace pacs::bridge::integration {

/**
 * @brief Thread system adapter
 *
 * Provides worker pools for async processing.
 */
class thread_adapter {
public:
    /**
     * @brief Get shared IO thread pool
     */
    [[nodiscard]] static thread_system::thread_pool& get_io_pool();

    /**
     * @brief Get message processing pool
     */
    [[nodiscard]] static thread_system::thread_pool& get_worker_pool();

    /**
     * @brief Submit async job
     */
    template<typename F>
    [[nodiscard]] static auto submit(F&& job);

    /**
     * @brief Schedule delayed job
     */
    template<typename F>
    static void schedule(F&& job, std::chrono::milliseconds delay);
};

} // namespace pacs::bridge::integration
```

### DES-INT-003: logger_adapter

**Traces to:** IR-2 (logger_system)

```cpp
namespace pacs::bridge::integration {

/**
 * @brief Logger system adapter
 *
 * Provides structured logging for bridge operations.
 */
class logger_adapter {
public:
    /**
     * @brief Log HL7 message
     */
    static void log_message(
        const hl7::hl7_message& message,
        std::string_view direction); // "IN" or "OUT"

    /**
     * @brief Log audit event
     */
    static void log_audit(
        std::string_view event_type,
        std::string_view patient_id,
        std::string_view details);

    /**
     * @brief Log error
     */
    static void log_error(
        std::string_view component,
        int error_code,
        std::string_view message);

    /**
     * @brief Get component logger
     */
    [[nodiscard]] static logger_system::logger& get_logger(
        std::string_view component);
};

} // namespace pacs::bridge::integration
```

---

## 9. Monitoring Module

### 9.1 Module Overview

**Namespace:** `pacs::bridge::monitoring`
**Purpose:** Provide health check endpoints for load balancer integration and operational monitoring

```
┌────────────────────────────────────────────────────────────────────────────┐
│                           Monitoring Module                                 │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌─────────────────────────────────────────────────────────────────────┐  │
│   │                       health_server                                  │  │
│   │                                                                      │  │
│   │  GET /health/live   → liveness_result   → 200/503                   │  │
│   │  GET /health/ready  → readiness_result  → 200/503                   │  │
│   │  GET /health/deep   → deep_health_result → 200/503                  │  │
│   └─────────────────────────────────────────────────────────────────────┘  │
│                                      ▲                                      │
│                                      │                                      │
│   ┌──────────────────────────────────┴───────────────────────────────────┐ │
│   │                        health_checker                                 │ │
│   │                                                                       │ │
│   │  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ │ │
│   │  │mllp_server   │ │pacs_system   │ │message_queue │ │memory_check  │ │ │
│   │  │    _check    │ │    _check    │ │    _check    │ │              │ │ │
│   │  └──────────────┘ └──────────────┘ └──────────────┘ └──────────────┘ │ │
│   └──────────────────────────────────────────────────────────────────────┘ │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### DES-MON-001: health_types

**Traces to:** NFR-2.1, NFR-2.3, NFR-2.4

```cpp
namespace pacs::bridge::monitoring {

/**
 * @brief Health status enumeration
 */
enum class health_status {
    healthy,   // Component is fully operational (UP)
    degraded,  // Operational but with warnings (DEGRADED)
    unhealthy  // Not operational (DOWN)
};

/**
 * @brief Health check error codes (-980 to -989)
 */
enum class health_error : int {
    timeout = -980,
    component_unavailable = -981,
    threshold_exceeded = -982,
    invalid_configuration = -983,
    not_initialized = -984,
    serialization_failed = -985
};

/**
 * @brief Component health information
 */
struct component_health {
    std::string name;
    health_status status;
    std::optional<int64_t> response_time_ms;
    std::optional<std::string> details;
    std::map<std::string, std::string> metrics;
};

/**
 * @brief Liveness check result (K8s livenessProbe)
 */
struct liveness_result {
    health_status status;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Readiness check result (K8s readinessProbe)
 */
struct readiness_result {
    health_status status;
    std::chrono::system_clock::time_point timestamp;
    std::map<std::string, health_status> components;
};

/**
 * @brief Deep health check result
 */
struct deep_health_result {
    health_status status;
    std::chrono::system_clock::time_point timestamp;
    std::vector<component_health> components;
    std::optional<std::string> message;
};

/**
 * @brief Health check configuration
 */
struct health_config {
    bool enabled = true;
    uint16_t port = 8081;
    std::string base_path = "/health";
    health_thresholds thresholds;
};

} // namespace pacs::bridge::monitoring
```

### DES-MON-002: health_checker

**Traces to:** NFR-2.1, NFR-2.3

```cpp
namespace pacs::bridge::monitoring {

/**
 * @brief Interface for component health checks
 */
class component_check {
public:
    virtual ~component_check() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual component_health check(
        std::chrono::milliseconds timeout) = 0;
    [[nodiscard]] virtual bool is_critical() const noexcept { return true; }
};

/**
 * @brief Central health checker
 *
 * Coordinates health checks across all registered components.
 * Thread-safe: All public methods are thread-safe.
 */
class health_checker {
public:
    explicit health_checker(const health_config& config);

    // ═══════════════════════════════════════════════════════════════════════
    // Component Registration
    // ═══════════════════════════════════════════════════════════════════════

    void register_check(std::unique_ptr<component_check> check);
    void register_check(
        std::string name,
        std::function<component_health(std::chrono::milliseconds)> check_fn,
        bool critical = true);
    bool unregister_check(std::string_view name);

    // ═══════════════════════════════════════════════════════════════════════
    // Health Check Operations
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] liveness_result check_liveness() const;
    [[nodiscard]] readiness_result check_readiness() const;
    [[nodiscard]] deep_health_result check_deep() const;
    [[nodiscard]] std::optional<component_health> check_component(
        std::string_view name) const;
};

} // namespace pacs::bridge::monitoring
```

### DES-MON-003: health_server

**Traces to:** NFR-2.1, NFR-2.4

```cpp
namespace pacs::bridge::monitoring {

/**
 * @brief HTTP server for health check endpoints
 *
 * Endpoints:
 *   GET /health/live  - Liveness check (100ms response)
 *   GET /health/ready - Readiness check (component status)
 *   GET /health/deep  - Deep health with metrics
 */
class health_server {
public:
    struct config {
        uint16_t port = 8081;
        std::string base_path = "/health";
        std::string bind_address = "0.0.0.0";
        int connection_timeout_seconds = 30;
        size_t max_connections = 100;
    };

    health_server(health_checker& checker, const config& cfg = {});

    // ═══════════════════════════════════════════════════════════════════════
    // Server Lifecycle
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] bool start();
    void stop(bool wait_for_connections = true);
    [[nodiscard]] bool is_running() const noexcept;

    // ═══════════════════════════════════════════════════════════════════════
    // Request Handling
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] http_response handle_request(std::string_view path) const;
};

/**
 * @brief Generate Kubernetes probe configuration
 */
[[nodiscard]] std::string generate_k8s_probe_config(
    uint16_t port, std::string_view base_path = "/health");

/**
 * @brief Generate Docker HEALTHCHECK instruction
 */
[[nodiscard]] std::string generate_docker_healthcheck(
    uint16_t port, std::string_view base_path = "/health");

} // namespace pacs::bridge::monitoring
```

### DES-MON-004: Built-in Component Checks

```cpp
namespace pacs::bridge::monitoring {

/**
 * @brief MLLP server health check
 */
class mllp_server_check : public component_check {
    // Checks if MLLP server is running and accepting connections
};

/**
 * @brief PACS system connection health check
 */
class pacs_connection_check : public component_check {
    // Performs DICOM C-ECHO to verify connectivity
};

/**
 * @brief Message queue health check
 */
class queue_health_check : public component_check {
    // Checks queue database and threshold metrics
};

/**
 * @brief FHIR server health check (non-critical)
 */
class fhir_server_check : public component_check {
    // Checks if FHIR server is running (optional component)
};

/**
 * @brief Memory usage health check (non-critical)
 */
class memory_health_check : public component_check {
    // Monitors process memory against thresholds
};

} // namespace pacs::bridge::monitoring
```

---

## 10. Security Module

### 10.1 Module Overview

**Namespace:** `pacs::bridge::security`
**Purpose:** Provide TLS/SSL support for secure MLLP and HTTPS communications

```
┌────────────────────────────────────────────────────────────────────────────┐
│                           Security Module                                   │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌─────────────────────────────────────────────────────────────────────┐  │
│   │                         tls_context                                  │  │
│   │                                                                      │  │
│   │  create_server_context()    create_client_context()                  │  │
│   │         │                           │                                │  │
│   │         ▼                           ▼                                │  │
│   │    ┌─────────────────────────────────────────────────────────┐      │  │
│   │    │                    SSL_CTX (OpenSSL)                     │      │  │
│   │    │  - Certificate loading       - Session cache            │      │  │
│   │    │  - Key verification          - Cipher configuration     │      │  │
│   │    │  - Client auth mode          - TLS version control      │      │  │
│   │    └─────────────────────────────────────────────────────────┘      │  │
│   └─────────────────────────────────────────────────────────────────────┘  │
│                                      │                                      │
│                                      ▼                                      │
│   ┌─────────────────────────────────────────────────────────────────────┐  │
│   │                          tls_socket                                  │  │
│   │                                                                      │  │
│   │  accept()  connect()  create_pending()                               │  │
│   │     │          │            │                                        │  │
│   │     ▼          ▼            ▼                                        │  │
│   │  ┌───────────────────────────────────────────────────────────────┐  │  │
│   │  │                      SSL (OpenSSL)                             │  │  │
│   │  │  - Handshake management    - Encrypted I/O                    │  │  │
│   │  │  - Peer certificate access - Session resumption               │  │  │
│   │  └───────────────────────────────────────────────────────────────┘  │  │
│   └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### DES-SEC-001: tls_types

**Traces to:** FR-1.3.5, NFR-4.1, SR-1, SRS-SEC-001

```cpp
namespace pacs::bridge::security {

/**
 * @brief TLS error codes (-990 to -999)
 */
enum class tls_error : int {
    initialization_failed = -990,
    certificate_invalid = -991,
    private_key_invalid = -992,
    ca_certificate_invalid = -993,
    key_certificate_mismatch = -994,
    handshake_failed = -995,
    client_verification_failed = -996,
    unsupported_version = -997,
    invalid_cipher_suite = -998,
    connection_closed = -999
};

/**
 * @brief TLS protocol version
 */
enum class tls_version {
    tls_1_2,  // Minimum for HIPAA compliance
    tls_1_3   // Preferred when available
};

/**
 * @brief Client certificate authentication mode
 */
enum class client_auth_mode {
    none,      // No client certificate required
    optional,  // Request but don't require
    required   // Require valid client certificate (mutual TLS)
};

/**
 * @brief TLS configuration
 */
struct tls_config {
    bool enabled = false;
    std::filesystem::path cert_path;     // Server/client certificate
    std::filesystem::path key_path;      // Private key
    std::filesystem::path ca_path;       // CA certificate(s)
    client_auth_mode client_auth = client_auth_mode::none;
    tls_version min_version = tls_version::tls_1_2;
    std::vector<std::string> cipher_suites;
    bool verify_peer = true;
    std::optional<std::string> verify_hostname;
    std::chrono::milliseconds handshake_timeout{5000};
    size_t session_cache_size = 1024;

    [[nodiscard]] bool is_valid_for_server() const noexcept;
    [[nodiscard]] bool is_valid_for_client() const noexcept;
    [[nodiscard]] bool is_mutual_tls() const noexcept;
};

/**
 * @brief TLS connection statistics
 */
struct tls_statistics {
    size_t handshakes_attempted = 0;
    size_t handshakes_succeeded = 0;
    size_t handshakes_failed = 0;
    size_t client_auth_failures = 0;
    size_t sessions_resumed = 0;
    double avg_handshake_ms = 0.0;
    size_t active_connections = 0;

    [[nodiscard]] double success_rate() const noexcept;
    [[nodiscard]] double resumption_rate() const noexcept;
};

/**
 * @brief X.509 certificate information
 */
struct certificate_info {
    std::string subject;
    std::string issuer;
    std::string serial_number;
    std::chrono::system_clock::time_point not_before;
    std::chrono::system_clock::time_point not_after;
    std::vector<std::string> san_entries;
    std::string fingerprint_sha256;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool expires_within(std::chrono::hours within) const noexcept;
};

} // namespace pacs::bridge::security
```

### DES-SEC-002: tls_context

**Traces to:** FR-1.3.5, NFR-4.1, SRS-SEC-001

```cpp
namespace pacs::bridge::security {

/**
 * @brief TLS context wrapper for OpenSSL SSL_CTX
 *
 * Manages TLS configuration including certificates, keys, and session cache.
 * Each context can create multiple TLS connections.
 */
class tls_context {
public:
    using verify_callback = std::function<bool(bool preverify_ok,
                                                const certificate_info& cert_info)>;

    // ═══════════════════════════════════════════════════════════════════════
    // Factory Methods
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Create server-side TLS context
     * @param config TLS configuration with cert and key paths
     */
    [[nodiscard]] static std::expected<tls_context, tls_error>
    create_server_context(const tls_config& config);

    /**
     * @brief Create client-side TLS context
     * @param config TLS configuration with CA path
     */
    [[nodiscard]] static std::expected<tls_context, tls_error>
    create_client_context(const tls_config& config);

    // ═══════════════════════════════════════════════════════════════════════
    // Configuration
    // ═══════════════════════════════════════════════════════════════════════

    void set_verify_callback(verify_callback callback);
    [[nodiscard]] std::expected<void, tls_error>
    load_ca_certificates(const std::filesystem::path& ca_path);
    [[nodiscard]] std::expected<void, tls_error>
    set_cipher_suites(std::string_view cipher_string);
    void enable_session_resumption(size_t cache_size);

    // ═══════════════════════════════════════════════════════════════════════
    // Information
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] bool is_server() const noexcept;
    [[nodiscard]] bool is_client() const noexcept;
    [[nodiscard]] tls_version min_version() const noexcept;
    [[nodiscard]] client_auth_mode client_auth() const noexcept;
    [[nodiscard]] std::optional<certificate_info> certificate_info() const noexcept;
    [[nodiscard]] tls_statistics statistics() const noexcept;
    [[nodiscard]] void* native_handle() noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Global TLS Functions
// ═══════════════════════════════════════════════════════════════════════════

[[nodiscard]] std::expected<void, tls_error> initialize_tls();
void cleanup_tls();

/**
 * @brief RAII guard for TLS library initialization
 */
class tls_library_guard {
public:
    tls_library_guard();
    ~tls_library_guard();
    [[nodiscard]] bool is_initialized() const noexcept;
};

// ═══════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════

[[nodiscard]] std::expected<security::certificate_info, tls_error>
read_certificate_info(const std::filesystem::path& cert_path);

[[nodiscard]] std::expected<void, tls_error>
verify_key_pair(const std::filesystem::path& cert_path,
                const std::filesystem::path& key_path);

[[nodiscard]] std::string openssl_version();

} // namespace pacs::bridge::security
```

### DES-SEC-003: tls_socket

**Traces to:** FR-1.3.5, SRS-MLLP-003, SRS-SEC-001

```cpp
namespace pacs::bridge::security {

/**
 * @brief TLS socket for encrypted communication
 *
 * Wraps an existing TCP socket with TLS encryption.
 * Supports both blocking and non-blocking operations.
 */
class tls_socket {
public:
    enum class handshake_status {
        not_started,
        want_read,
        want_write,
        complete,
        failed
    };

    enum class io_status {
        success,
        want_read,
        want_write,
        closed,
        error
    };

    // ═══════════════════════════════════════════════════════════════════════
    // Factory Methods
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Accept incoming TLS connection (server-side)
     */
    [[nodiscard]] static std::expected<tls_socket, tls_error>
    accept(tls_context& context, int socket_fd);

    /**
     * @brief Connect with TLS (client-side)
     */
    [[nodiscard]] static std::expected<tls_socket, tls_error>
    connect(tls_context& context, int socket_fd, std::string_view hostname);

    /**
     * @brief Create pending TLS socket for async handshake
     */
    [[nodiscard]] static std::expected<tls_socket, tls_error>
    create_pending(tls_context& context, int socket_fd, bool is_server,
                   std::string_view hostname = "");

    // ═══════════════════════════════════════════════════════════════════════
    // Handshake
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] handshake_status perform_handshake_step();
    [[nodiscard]] bool is_handshake_complete() const noexcept;

    // ═══════════════════════════════════════════════════════════════════════
    // I/O Operations
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] std::expected<size_t, tls_error> read(std::span<uint8_t> buffer);
    [[nodiscard]] std::expected<size_t, tls_error> write(std::span<const uint8_t> data);
    [[nodiscard]] std::expected<std::vector<uint8_t>, tls_error> read_all(size_t max_size);
    [[nodiscard]] std::expected<void, tls_error> write_all(std::span<const uint8_t> data);
    [[nodiscard]] bool has_pending_data() const noexcept;

    // Non-blocking variants
    [[nodiscard]] std::pair<io_status, size_t> try_read(std::span<uint8_t> buffer);
    [[nodiscard]] std::pair<io_status, size_t> try_write(std::span<const uint8_t> data);

    // ═══════════════════════════════════════════════════════════════════════
    // Connection Management
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] std::expected<void, tls_error> shutdown(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});
    void close();
    [[nodiscard]] bool is_open() const noexcept;

    // ═══════════════════════════════════════════════════════════════════════
    // Connection Information
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] int socket_fd() const noexcept;
    [[nodiscard]] std::optional<security::certificate_info> peer_certificate() const noexcept;
    [[nodiscard]] std::string protocol_version() const;
    [[nodiscard]] std::string cipher_suite() const;
    [[nodiscard]] bool is_session_resumed() const noexcept;
    [[nodiscard]] std::string last_error_message() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::security
```

### 10.2 TLS Integration Points

The security module integrates with:

1. **MLLP Server** (`mllp::mllp_server_config.tls`)
   - Server certificate for client connections
   - Optional client certificate verification (mTLS)

2. **MLLP Client** (`mllp::mllp_client_config.tls`)
   - CA certificate for server verification
   - Optional client certificate for mTLS

3. **FHIR Server** (`fhir::fhir_server_config.tls`)
   - HTTPS support for REST API

### 10.3 Security Hardening Components

The security module includes comprehensive hardening components for HIPAA-compliant
healthcare data protection:

```
┌────────────────────────────────────────────────────────────────────────────┐
│                    Security Hardening Components                            │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌───────────────────┐   ┌───────────────────┐   ┌───────────────────┐   │
│   │  input_validator  │   │  log_sanitizer    │   │  audit_logger     │   │
│   │                   │   │                   │   │                   │   │
│   │  - HL7 structure  │   │  - PHI detection  │   │  - Event logging  │   │
│   │  - SQL injection  │   │  - Pattern mask   │   │  - HIPAA trail    │   │
│   │  - Cmd injection  │   │  - HL7 segment    │   │  - JSON format    │   │
│   └───────────────────┘   └───────────────────┘   └───────────────────┘   │
│            │                       │                       │               │
│            ▼                       ▼                       ▼               │
│   ┌────────────────────────────────────────────────────────────────────┐  │
│   │                    Security Pipeline                                │  │
│   │  Request → Access → Rate → Validate → Sanitize → Process → Audit  │  │
│   └────────────────────────────────────────────────────────────────────┘  │
│            ▲                       ▲                                       │
│   ┌───────┴───────────┐   ┌───────┴───────────┐                           │
│   │  access_control   │   │   rate_limiter    │                           │
│   │                   │   │                   │                           │
│   │  - IP whitelist   │   │  - Per-IP limits  │                           │
│   │  - CIDR ranges    │   │  - Per-app limits │                           │
│   │  - App auth       │   │  - Size-based     │                           │
│   └───────────────────┘   └───────────────────┘                           │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### DES-SEC-004: input_validator

**Traces to:** FR-4.1.1, FR-4.1.2, NFR-4.1, SR-1, SRS-SEC-002

```cpp
namespace pacs::bridge::security {

/**
 * @brief Input validation error codes (-960 to -969)
 */
enum class validation_error : int {
    empty_message = -960,
    message_too_large = -961,
    invalid_hl7_structure = -962,
    missing_msh_segment = -963,
    invalid_msh_fields = -964,
    invalid_version = -965,
    invalid_application_id = -966,
    prohibited_characters = -967,
    invalid_encoding = -968,
    injection_detected = -969
};

/**
 * @brief Validation result with extracted metadata
 */
struct validation_result {
    bool valid = false;
    std::optional<validation_error> error;
    std::optional<std::string> error_message;
    std::optional<std::string> error_field;

    // Extracted message metadata
    std::optional<std::string> message_type;
    std::optional<std::string> message_control_id;
    std::optional<std::string> sending_app;
    std::optional<std::string> sending_facility;
    std::optional<std::string> receiving_app;
    std::optional<std::string> receiving_facility;

    size_t message_size = 0;
    size_t segment_count = 0;
    std::vector<std::string> warnings;

    static validation_result success();
    static validation_result failure(validation_error err,
                                      std::string_view message,
                                      std::string_view field = "");
};

/**
 * @brief HL7 message input validator
 *
 * Validates incoming HL7 messages for:
 * - Basic structure and format
 * - Required segments (MSH)
 * - Size limits
 * - Application ID whitelisting
 * - Injection attack detection
 */
class input_validator {
public:
    explicit input_validator(const validation_config& config = {});
    ~input_validator();

    // ═══════════════════════════════════════════════════════════════════════
    // Validation Methods
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] validation_result validate(std::string_view message) const;
    [[nodiscard]] std::pair<validation_result, std::string>
        validate_and_sanitize(std::string_view message) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Injection Detection
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] bool detect_sql_injection(std::string_view content) const;
    [[nodiscard]] bool detect_command_injection(std::string_view content) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Sanitization
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] std::string sanitize(std::string_view message) const;
    [[nodiscard]] static std::string strip_nulls(std::string_view message);
    [[nodiscard]] static std::string normalize_endings(std::string_view message);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::security
```

### DES-SEC-005: healthcare_log_sanitizer

**Traces to:** FR-4.2.1, FR-4.2.2, NFR-4.1, SR-2, SRS-SEC-003

```cpp
namespace pacs::bridge::security {

/**
 * @brief PHI field types for masking
 */
enum class phi_field_type {
    patient_name,
    patient_id,
    date_of_birth,
    ssn,
    address,
    phone_number,
    email,
    medical_record_number,
    insurance_id,
    account_number,
    device_serial,
    biometric_id
};

/**
 * @brief Masking style options
 */
enum class masking_style {
    asterisks,    // Replace with ****
    type_label,   // Replace with [PATIENT_NAME]
    x_characters, // Replace with XXXX
    partial,      // Show first/last characters
    remove        // Remove entirely
};

/**
 * @brief Healthcare-specific log sanitizer
 *
 * Extends base log_sanitizer with healthcare-specific PHI patterns:
 * - Patient identifiers (MRN, SSN)
 * - Personal information (name, DOB, address)
 * - Contact information (phone, email)
 * - HL7 segment-aware sanitization
 */
class healthcare_log_sanitizer {
public:
    explicit healthcare_log_sanitizer(
        const healthcare_sanitization_config& config = {});
    ~healthcare_log_sanitizer();

    // ═══════════════════════════════════════════════════════════════════════
    // Sanitization Methods
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] std::string sanitize(std::string_view content) const;
    [[nodiscard]] std::string sanitize_hl7(std::string_view hl7_message) const;
    [[nodiscard]] std::pair<std::string, std::vector<phi_detection>>
        sanitize_with_detections(std::string_view content) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Detection Methods
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] bool contains_phi(std::string_view content) const;
    [[nodiscard]] std::vector<phi_detection>
        detect_phi(std::string_view content) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Masking
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] std::string mask(std::string_view value,
                                    phi_field_type type) const;
    void add_custom_pattern(std::string_view pattern,
                            std::string_view replacement);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════

[[nodiscard]] std::string make_safe_hl7_summary(std::string_view hl7_message);
[[nodiscard]] std::string make_safe_session_desc(std::string_view remote_address,
                                                   uint16_t remote_port,
                                                   uint64_t session_id,
                                                   bool mask_ip = true);

} // namespace pacs::bridge::security
```

### DES-SEC-006: healthcare_audit_logger

**Traces to:** FR-4.3.1, FR-4.3.2, NFR-4.1, SR-3, SRS-SEC-004

```cpp
namespace pacs::bridge::security {

/**
 * @brief Healthcare-specific audit event categories
 */
enum class healthcare_audit_category {
    hl7_transaction,   // Message send/receive
    phi_access,        // Patient data access
    security,          // Authentication/authorization
    system,            // Server start/stop/config
    network            // Connection events
};

/**
 * @brief Specific audit event types
 */
enum class healthcare_audit_event {
    // HL7 Transaction Events
    message_received,
    message_sent,
    message_validated,
    message_rejected,
    ack_sent,
    nack_sent,

    // PHI Access Events
    patient_data_viewed,
    patient_data_created,
    patient_data_modified,
    patient_data_deleted,
    phi_exported,

    // Security Events
    login_attempt,
    login_success,
    login_failure,
    logout,
    access_granted,
    access_denied,
    certificate_validated,
    certificate_rejected,

    // System Events
    server_started,
    server_stopped,
    config_changed,
    health_check,

    // Network Events
    connection_established,
    connection_closed,
    connection_rejected,
    rate_limit_triggered
};

/**
 * @brief Healthcare-compliant audit logger
 *
 * Provides HIPAA-compliant audit logging with:
 * - JSON formatted output
 * - Tamper-evident event IDs
 * - PHI access tracking
 * - 7-year retention support
 */
class healthcare_audit_logger {
public:
    healthcare_audit_logger();
    ~healthcare_audit_logger();

    // ═══════════════════════════════════════════════════════════════════════
    // Singleton Access
    // ═══════════════════════════════════════════════════════════════════════

    static healthcare_audit_logger& instance();

    // ═══════════════════════════════════════════════════════════════════════
    // Event Logging
    // ═══════════════════════════════════════════════════════════════════════

    void log(const healthcare_audit_entry& entry);
    void log_hl7_transaction(std::string_view message_type,
                              std::string_view control_id,
                              std::string_view peer_ip,
                              uint16_t peer_port,
                              bool success);
    void log_security_event(healthcare_audit_event event,
                            std::string_view user_id,
                            bool success,
                            std::string_view details = "");

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief Fluent event builder
 */
class healthcare_audit_event_builder {
public:
    static healthcare_audit_event_builder create(
        healthcare_audit_category category,
        healthcare_audit_event event_type);

    healthcare_audit_event_builder& with_message_id(std::string_view id);
    healthcare_audit_event_builder& with_message_type(std::string_view type);
    healthcare_audit_event_builder& with_peer(std::string_view ip, uint16_t port);
    healthcare_audit_event_builder& with_user(std::string_view user_id);
    healthcare_audit_event_builder& with_patient_id(std::string_view patient_id);
    healthcare_audit_event_builder& with_success();
    healthcare_audit_event_builder& with_failure();
    healthcare_audit_event_builder& with_error(std::string_view error);
    healthcare_audit_event_builder& with_detail(std::string_view key,
                                                  std::string_view value);

    [[nodiscard]] healthcare_audit_entry build();
};

} // namespace pacs::bridge::security
```

### DES-SEC-007: access_controller

**Traces to:** FR-4.1.3, NFR-4.2, SR-1, SRS-SEC-005

```cpp
namespace pacs::bridge::security {

/**
 * @brief Access control error codes (-950 to -959)
 */
enum class access_error : int {
    ip_not_whitelisted = -950,
    ip_blacklisted = -951,
    temporarily_blocked = -952,
    connection_limit_exceeded = -953,
    invalid_ip_format = -954,
    application_not_allowed = -955,
    facility_not_allowed = -956,
    time_restriction = -957,
    geo_restriction = -958,
    internal_error = -959
};

/**
 * @brief Access control mode
 */
enum class access_control_mode {
    disabled,         // Allow all
    whitelist_only,   // Only allow whitelisted
    blacklist_only,   // Block blacklisted
    whitelist_blacklist // Whitelist with blacklist override
};

/**
 * @brief IP range with CIDR support
 */
struct ip_range {
    std::string network;
    uint32_t prefix_length = 32;

    static std::optional<ip_range> from_cidr(std::string_view cidr);
    [[nodiscard]] bool matches(std::string_view ip) const;
};

/**
 * @brief Access controller for IP and application authorization
 *
 * Features:
 * - IP whitelisting/blacklisting with CIDR support
 * - Application ID (MSH-3/MSH-4) validation
 * - Temporary blocking for failed attempts
 * - Connection rate limiting
 */
class access_controller {
public:
    explicit access_controller(const access_control_config& config = {});
    ~access_controller();

    // ═══════════════════════════════════════════════════════════════════════
    // Access Checks
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] access_result check_access(std::string_view ip) const;
    [[nodiscard]] access_result check_application(std::string_view app_id) const;
    [[nodiscard]] access_result check_facility(std::string_view facility_id) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Dynamic Blocking
    // ═══════════════════════════════════════════════════════════════════════

    void temporarily_block(std::string_view ip,
                           std::chrono::seconds duration);
    void unblock(std::string_view ip);
    [[nodiscard]] bool is_blocked(std::string_view ip) const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════

[[nodiscard]] bool is_valid_ip(std::string_view ip);
[[nodiscard]] bool is_private_ip(std::string_view ip);
[[nodiscard]] bool is_localhost(std::string_view ip);

} // namespace pacs::bridge::security
```

### DES-SEC-008: rate_limiter

**Traces to:** FR-4.1.4, NFR-4.3, SR-1, SRS-SEC-006

```cpp
namespace pacs::bridge::security {

/**
 * @brief Rate limit result
 */
struct rate_limit_result {
    bool allowed = true;
    size_t remaining = 0;
    std::chrono::seconds retry_after{0};
    std::string limit_key;
};

/**
 * @brief Rate limiter with multiple tiers
 *
 * Supports:
 * - Per-IP rate limiting
 * - Per-application rate limiting
 * - Global rate limiting
 * - Size-based limiting (bytes/second)
 * - Sliding window counters
 */
class rate_limiter {
public:
    explicit rate_limiter(const rate_limit_config& config = {});
    ~rate_limiter();

    // ═══════════════════════════════════════════════════════════════════════
    // Rate Checking
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] rate_limit_result check_limit(
        std::string_view client_ip,
        std::optional<std::string_view> application_id = std::nullopt,
        size_t message_size = 0);

    // ═══════════════════════════════════════════════════════════════════════
    // Configuration
    // ═══════════════════════════════════════════════════════════════════════

    void set_application_limit(std::string_view app_id,
                                size_t requests_per_second,
                                size_t requests_per_minute);
    void reset(std::string_view client_ip);
    void reset_all();

    // ═══════════════════════════════════════════════════════════════════════
    // Statistics
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] rate_limit_statistics statistics() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief Generate HTTP rate limit headers (RFC 6585)
 */
[[nodiscard]] std::unordered_map<std::string, std::string>
make_rate_limit_headers(const rate_limit_result& result);

} // namespace pacs::bridge::security
```

### 10.4 Security Pipeline Integration

The security hardening components integrate into the MLLP/FHIR request processing
pipeline as follows:

```
Incoming Request
      │
      ▼
┌─────────────────┐
│ Access Control  │ ──▶ Reject if IP/App not authorized
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Rate Limiter   │ ──▶ Reject if rate exceeded (429 Too Many Requests)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│Input Validation │ ──▶ Reject if structure invalid or injection detected
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Business Logic │ ──▶ Process HL7/FHIR message
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Log Sanitizer   │ ──▶ Remove PHI before logging
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Audit Logger   │ ──▶ Record transaction for compliance
└─────────────────┘
```

**Error Code Allocation:**

| Component        | Error Code Range |
|------------------|-----------------|
| Configuration    | -900 to -909    |
| Access Control   | -950 to -959    |
| Input Validation | -960 to -969    |
| MLLP Protocol    | -970 to -979    |
| Rate Limiting    | -980 to -989    |
| TLS/Security     | -990 to -999    |

---

## 11. Testing Module

### 11.1 Module Overview

**Namespace:** `pacs::bridge::testing`
**Purpose:** Provide comprehensive load and stress testing capabilities for PACS Bridge validation

```
┌────────────────────────────────────────────────────────────────────────────┐
│                           Testing Module                                    │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌─────────────────────────────────────────────────────────────────────┐  │
│   │                       load_runner                                    │  │
│   │                                                                      │  │
│   │  run_sustained()   run_peak()   run_endurance()   run_concurrent()  │  │
│   │         │              │              │                  │           │  │
│   │         ▼              ▼              ▼                  ▼           │  │
│   │    ┌─────────────────────────────────────────────────────────┐      │  │
│   │    │                    test_metrics                          │      │  │
│   │    │  - messages_sent/acked/failed    - latency histogram    │      │  │
│   │    │  - throughput                    - error tracking       │      │  │
│   │    └─────────────────────────────────────────────────────────┘      │  │
│   └─────────────────────────────────────────────────────────────────────┘  │
│                                      │                                      │
│         ┌────────────────────────────┼────────────────────────────┐        │
│         │                            │                            │        │
│   ┌─────┴─────────┐           ┌──────┴──────┐           ┌─────────┴─────┐  │
│   │load_generator │           │test_result  │           │load_reporter  │  │
│   │               │           │             │           │               │  │
│   │ generate_orm()│           │ summary()   │           │ to_json()     │  │
│   │ generate_adt()│           │ passed()    │           │ to_csv()      │  │
│   │ generate_siu()│           │             │           │ to_markdown() │  │
│   └───────────────┘           └─────────────┘           └───────────────┘  │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### DES-TEST-001: load_types

**Traces to:** NFR-1.1 to NFR-1.6, NFR-2.1, NFR-3.2

```cpp
namespace pacs::bridge::testing {

/**
 * @brief Load testing error codes (-960 to -969)
 */
enum class load_error : int {
    invalid_configuration = -960,
    not_initialized = -961,
    already_running = -962,
    cancelled = -963,
    connection_failed = -964,
    generation_failed = -965,
    timeout = -966,
    resource_exhausted = -967,
    target_error = -968,
    report_failed = -969
};

/**
 * @brief Type of load test to execute
 */
enum class test_type {
    sustained,     // Constant rate for extended duration
    peak,          // Find system limits
    endurance,     // Long-duration memory leak detection
    concurrent,    // Connection stress test
    queue_stress,  // Queue accumulation under failure
    failover       // Failover behavior verification
};

/**
 * @brief Current test state
 */
enum class test_state {
    idle, initializing, running, stopping,
    completed, failed, cancelled
};

/**
 * @brief HL7 message type for load generation
 */
enum class hl7_message_type {
    ORM, ADT, SIU, ORU, MDM
};

/**
 * @brief Message type distribution for mixed workloads
 */
struct message_distribution {
    uint8_t orm_percent = 70;
    uint8_t adt_percent = 20;
    uint8_t siu_percent = 10;
    uint8_t oru_percent = 0;
    uint8_t mdm_percent = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept;
    [[nodiscard]] static constexpr message_distribution default_mix() noexcept;
};

/**
 * @brief Load test configuration parameters
 */
struct load_config {
    test_type type = test_type::sustained;
    std::string target_host = "localhost";
    uint16_t target_port = 2575;
    std::chrono::seconds duration{3600};
    uint32_t messages_per_second = 500;
    size_t concurrent_connections = 10;
    message_distribution distribution;
    bool use_tls = false;
    std::chrono::seconds ramp_up{30};
    std::chrono::milliseconds message_timeout{5000};

    [[nodiscard]] bool is_valid() const noexcept;

    // Factory methods
    [[nodiscard]] static load_config sustained(/*...*/);
    [[nodiscard]] static load_config peak(/*...*/);
    [[nodiscard]] static load_config endurance(/*...*/);
    [[nodiscard]] static load_config concurrent(/*...*/);
};

/**
 * @brief Latency histogram for percentile calculations
 */
struct latency_histogram {
    void record(uint64_t latency_us) noexcept;
    [[nodiscard]] double mean_us() const noexcept;
    [[nodiscard]] uint64_t percentile_us(double percentile) const noexcept;
    void reset() noexcept;
};

/**
 * @brief Real-time test metrics (thread-safe)
 */
struct test_metrics {
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_acked{0};
    std::atomic<uint64_t> messages_failed{0};
    std::atomic<uint64_t> connection_errors{0};
    std::atomic<uint64_t> timeout_errors{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    latency_histogram latency;

    [[nodiscard]] double success_rate() const noexcept;
    [[nodiscard]] double overall_throughput() const noexcept;
    void reset() noexcept;
};

/**
 * @brief Test result summary
 */
struct test_result {
    test_type type;
    test_state state;
    std::chrono::seconds duration;
    uint64_t messages_sent;
    uint64_t messages_acked;
    double success_rate_percent;
    double throughput;
    double latency_p50_ms;
    double latency_p95_ms;
    double latency_p99_ms;

    [[nodiscard]] bool passed(double min_success_rate = 100.0,
                               double max_p95_latency_ms = 50.0) const noexcept;
    [[nodiscard]] std::string summary() const;
};

} // namespace pacs::bridge::testing
```

### DES-TEST-002: load_generator

**Traces to:** NFR-1.1, NFR-1.2

```cpp
namespace pacs::bridge::testing {

/**
 * @brief HL7 message generator for load testing
 *
 * Thread-safe message generator that creates realistic HL7 messages
 * for various message types (ORM, ADT, SIU, ORU, MDM).
 */
class load_generator {
public:
    struct config {
        std::string sending_application = "PACS_BRIDGE_TEST";
        std::string sending_facility = "LOAD_TEST";
        std::string receiving_application = "RIS";
        std::string receiving_facility = "HOSPITAL";
        bool include_optional_fields = true;
        uint64_t seed = 0;  // 0 = random seed
    };

    load_generator();
    explicit load_generator(const config& cfg);

    // ═══════════════════════════════════════════════════════════════════════
    // Message Generation
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] std::expected<std::string, load_error>
    generate(hl7_message_type type);

    [[nodiscard]] std::expected<std::string, load_error>
    generate_random(const message_distribution& dist);

    [[nodiscard]] std::expected<std::string, load_error> generate_orm();
    [[nodiscard]] std::expected<std::string, load_error> generate_adt();
    [[nodiscard]] std::expected<std::string, load_error> generate_siu();
    [[nodiscard]] std::expected<std::string, load_error> generate_oru();
    [[nodiscard]] std::expected<std::string, load_error> generate_mdm();

    // ═══════════════════════════════════════════════════════════════════════
    // Utilities
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] std::string generate_message_id();
    [[nodiscard]] std::string generate_patient_id();
    [[nodiscard]] std::string generate_accession_number();
    [[nodiscard]] static std::string current_timestamp();

    [[nodiscard]] uint64_t messages_generated() const noexcept;
    [[nodiscard]] uint64_t messages_generated(hl7_message_type type) const noexcept;
    void reset();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::testing
```

### DES-TEST-003: load_runner

**Traces to:** NFR-1.1 to NFR-1.6, NFR-2.1

```cpp
namespace pacs::bridge::testing {

/**
 * @brief Load test executor
 *
 * Orchestrates load testing including connection management, message
 * generation, rate limiting, and metrics collection.
 */
class load_runner {
public:
    load_runner();
    ~load_runner();

    // ═══════════════════════════════════════════════════════════════════════
    // Test Execution
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] std::expected<test_result, load_error>
    run(const load_config& config);

    [[nodiscard]] std::expected<test_result, load_error>
    run_sustained(std::string_view host, uint16_t port,
                  std::chrono::seconds duration, uint32_t rate);

    [[nodiscard]] std::expected<test_result, load_error>
    run_peak(std::string_view host, uint16_t port, uint32_t max_rate);

    [[nodiscard]] std::expected<test_result, load_error>
    run_endurance(std::string_view host, uint16_t port,
                  std::chrono::seconds duration = std::chrono::hours(24));

    [[nodiscard]] std::expected<test_result, load_error>
    run_concurrent(std::string_view host, uint16_t port,
                   size_t connections, size_t messages_per_connection);

    [[nodiscard]] std::expected<test_result, load_error>
    run_queue_stress(std::string_view host, uint16_t port,
                     std::chrono::minutes accumulation_time);

    // ═══════════════════════════════════════════════════════════════════════
    // Control
    // ═══════════════════════════════════════════════════════════════════════

    void cancel();
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] test_state state() const noexcept;
    [[nodiscard]] std::optional<test_metrics> current_metrics() const;

    // ═══════════════════════════════════════════════════════════════════════
    // Configuration
    // ═══════════════════════════════════════════════════════════════════════

    void on_progress(progress_callback callback);
    void set_progress_interval(std::chrono::milliseconds interval);
    void set_generator(std::shared_ptr<load_generator> generator);

    [[nodiscard]] std::optional<test_result> last_result() const;
    [[nodiscard]] bool validate_target(std::string_view host, uint16_t port,
                                        std::chrono::milliseconds timeout);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief Run multiple test configurations in sequence
 */
[[nodiscard]] std::vector<test_result> run_test_suite(
    load_runner& runner,
    std::span<const load_config> configs);

} // namespace pacs::bridge::testing
```

### DES-TEST-004: load_reporter

**Traces to:** NFR-1.1, NFR-3.2

```cpp
namespace pacs::bridge::testing {

/**
 * @brief Report output format
 */
enum class report_format {
    text, json, markdown, csv, html
};

/**
 * @brief Report configuration
 */
struct report_config {
    report_format format = report_format::markdown;
    bool include_timing_details = true;
    bool include_resource_usage = true;
    bool include_comparison = false;
    std::filesystem::path baseline_path;
    std::string title = "PACS Bridge Load Test Report";
};

/**
 * @brief Load test report generator
 */
class load_reporter {
public:
    load_reporter();
    explicit load_reporter(const report_config& config);

    // ═══════════════════════════════════════════════════════════════════════
    // Report Generation
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] std::expected<std::string, load_error>
    generate(const test_result& result, report_format format) const;

    [[nodiscard]] std::expected<void, load_error>
    save(const test_result& result, const std::filesystem::path& path,
         std::optional<report_format> format = std::nullopt) const;

    [[nodiscard]] std::expected<std::string, load_error>
    generate_suite_summary(std::span<const test_result> results,
                           report_format format = report_format::markdown) const;

    [[nodiscard]] std::expected<std::string, load_error>
    generate_comparison(const test_result& current, const test_result& baseline,
                        report_format format = report_format::markdown) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Format Conversion
    // ═══════════════════════════════════════════════════════════════════════

    [[nodiscard]] std::expected<std::string, load_error>
    to_json(const test_result& result) const;

    [[nodiscard]] static std::expected<test_result, load_error>
    from_json(std::string_view json);

    [[nodiscard]] std::expected<std::string, load_error>
    to_csv(const test_result& result) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Console Output
    // ═══════════════════════════════════════════════════════════════════════

    static void print_summary(const test_result& result);
    static void print_progress(const progress_info& info);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::testing
```

### 11.2 Test Scenarios

The testing module supports the following test scenarios as specified in Issue #45:

| Scenario | Duration | Rate | Success Criteria |
|----------|----------|------|------------------|
| Sustained Load | 1 hour | 500 msg/s | <50ms P95, 0 errors |
| Peak Load | 15 minutes | 1000 msg/s | Graceful degradation |
| Endurance | 24 hours | 200 msg/s | No memory leaks |
| Concurrent | N/A | 100 connections | All connections handled |
| Queue Stress | 1 hour accumulation | 500 msg/s | All messages delivered |
| Failover | During peak | Auto | No message loss |

### 11.3 Error Code Allocation

| Range | Module |
|-------|--------|
| -900 to -909 | Configuration |
| -940 to -949 | Performance |
| -960 to -969 | Testing |
| -970 to -979 | MLLP |
| -980 to -989 | Health Check |
| -990 to -999 | TLS/Security |

---

## 12. Performance Module

### 12.1 Module Overview

**Namespace:** `pacs::bridge::performance`
**Purpose:** High-performance optimization layer for meeting SRS throughput, latency, and memory targets

```
┌────────────────────────────────────────────────────────────────────────────┐
│                         Performance Module                                  │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌───────────────────┐   ┌───────────────────┐   ┌───────────────────┐   │
│   │  zero_copy_parser │   │  object_pool      │   │  lockfree_queue   │   │
│   │                   │   │                   │   │                   │   │
│   │  - String views   │   │  - Pre-allocation │   │  - MPMC queue     │   │
│   │  - Lazy parsing   │   │  - Cache hits     │   │  - Work stealing  │   │
│   │  - Field index    │   │  - Buffer pool    │   │  - Priority queue │   │
│   └───────────────────┘   └───────────────────┘   └───────────────────┘   │
│            │                       │                       │               │
│            ▼                       ▼                       ▼               │
│   ┌────────────────────────────────────────────────────────────────────┐  │
│   │                   Thread Pool Manager                               │  │
│   │   Work-Stealing Scheduler │ Priority Tasks │ Dynamic Scaling       │  │
│   └────────────────────────────────────────────────────────────────────┘  │
│            │                       │                                       │
│   ┌────────┴───────────┐   ┌──────┴────────────┐                          │
│   │ connection_pool    │   │ benchmark_runner  │                          │
│   │                    │   │                   │                          │
│   │  - Pre-warming     │   │  - Throughput     │                          │
│   │  - Health check    │   │  - Latency        │                          │
│   │  - TCP tuning      │   │  - Memory         │                          │
│   └────────────────────┘   └───────────────────┘                          │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### 12.2 Performance Targets

From SRS NFR-1.1 to NFR-1.6:

| Target | Value | Implementation |
|--------|-------|----------------|
| Throughput | ≥500 msg/s | Thread pool with work-stealing |
| Latency P95 | <50 ms | Zero-copy parser, lock-free queues |
| MWL Creation | <100 ms | Object pooling, connection reuse |
| Connections | ≥50 concurrent | Connection pooling, TCP tuning |
| Memory | <200 MB | Buffer pooling, size-class allocation |
| CPU Idle | <20% | Work-stealing scheduler |

### DES-PERF-001: performance_types

**Traces to:** NFR-1.1 to NFR-1.6, SRS-PERF-001 to SRS-PERF-006

```cpp
namespace pacs::bridge::performance {

/**
 * @brief Performance error codes (-940 to -949)
 */
enum class performance_error : int {
    thread_pool_init_failed = -940,
    pool_exhausted = -941,
    queue_full = -942,
    invalid_configuration = -943,
    allocation_failed = -944,
    timeout = -945,
    not_initialized = -946,
    benchmark_failed = -947,
    parser_error = -948,
    memory_limit_exceeded = -949
};

/**
 * @brief Performance target constants from SRS requirements
 */
struct performance_targets {
    static constexpr size_t MIN_THROUGHPUT_MSG_PER_SEC = 500;
    static constexpr std::chrono::milliseconds MAX_P95_LATENCY{50};
    static constexpr std::chrono::milliseconds MAX_MWL_LATENCY{100};
    static constexpr size_t MIN_CONCURRENT_CONNECTIONS = 50;
    static constexpr size_t MAX_MEMORY_BASELINE_MB = 200;
    static constexpr double MAX_CPU_IDLE_PERCENT = 20.0;
};

} // namespace pacs::bridge::performance
```

### DES-PERF-002: zero_copy_parser

**Traces to:** NFR-1.1, NFR-1.2, SRS-PERF-002

```cpp
namespace pacs::bridge::performance {

/**
 * @brief Zero-copy field reference
 */
struct field_ref {
    std::string_view value;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::string_view get() const noexcept;
    [[nodiscard]] std::string to_string() const;
};

/**
 * @brief Zero-copy HL7 message parser
 */
class zero_copy_parser {
public:
    [[nodiscard]] static std::expected<zero_copy_parser, performance_error>
    parse(std::string_view data, const zero_copy_config& config = {});

    [[nodiscard]] std::optional<segment_ref> segment(std::string_view segment_id) const;
    [[nodiscard]] size_t segment_count() const noexcept;

    [[nodiscard]] field_ref message_type() const;
    [[nodiscard]] field_ref message_control_id() const;

    [[nodiscard]] std::chrono::nanoseconds parse_duration() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::performance
```

### DES-PERF-003: object_pool

**Traces to:** NFR-1.5, SRS-PERF-005

```cpp
namespace pacs::bridge::performance {

/**
 * @brief Pool statistics for monitoring
 */
struct pool_statistics {
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    [[nodiscard]] double hit_rate() const noexcept;
};

/**
 * @brief Thread-safe buffer pool with size classes
 */
class message_buffer_pool {
public:
    struct buffer_handle {
        uint8_t* data = nullptr;
        size_t capacity = 0;
        size_t size = 0;
        [[nodiscard]] bool valid() const noexcept;
    };

    explicit message_buffer_pool(const memory_config& config = {});
    [[nodiscard]] std::expected<buffer_handle, performance_error> acquire(size_t min_size);
    void release(buffer_handle& buffer) noexcept;
    [[nodiscard]] const pool_statistics& statistics() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::performance
```

### DES-PERF-004: thread_pool_manager

**Traces to:** NFR-1.1, NFR-1.4, SRS-PERF-001, SRS-PERF-004

```cpp
namespace pacs::bridge::performance {

enum class task_priority : uint8_t {
    critical = 0, high = 1, normal = 2, low = 3, background = 4
};

/**
 * @brief Work-stealing thread pool manager
 */
class thread_pool_manager {
public:
    explicit thread_pool_manager(const thread_pool_config& config = {});

    [[nodiscard]] std::expected<void, performance_error> start();
    [[nodiscard]] std::expected<void, performance_error> stop(bool wait_for_tasks = true);
    [[nodiscard]] bool is_running() const noexcept;

    template <typename F, typename... Args>
    [[nodiscard]] auto submit_priority(task_priority priority, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    bool post(task_fn task, task_priority priority = task_priority::normal);

    [[nodiscard]] static thread_pool_manager& instance();
    static void initialize(const thread_pool_config& config);
    static void shutdown();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::performance
```

### DES-PERF-005: benchmark_runner

**Traces to:** NFR-1.1 to NFR-1.6, SRS-PERF-001 to SRS-PERF-006

```cpp
namespace pacs::bridge::performance {

enum class benchmark_type {
    parsing, throughput, latency, memory, concurrent, pool_efficiency, thread_scaling
};

struct benchmark_result {
    benchmark_type type;
    double throughput = 0.0;
    double p95_latency_us = 0.0;
    uint64_t total_messages = 0;
    bool targets_met = false;
    [[nodiscard]] bool passed() const noexcept;
};

/**
 * @brief Performance benchmark runner
 */
class benchmark_runner {
public:
    explicit benchmark_runner(const benchmark_config& config = {});

    [[nodiscard]] std::expected<benchmark_suite_result, performance_error> run_all();
    [[nodiscard]] std::expected<benchmark_result, performance_error>
    run_benchmark(benchmark_type type);

    [[nodiscard]] std::expected<void, performance_error>
    save_results(const std::string& path, const std::string& format = "json");

    void cancel();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::performance
```

### 12.3 Integration with thread_system

The Performance Module integrates with the kcenon ecosystem's `thread_system`:

```cpp
#include <thread_system/thread_pool.h>
#include <thread_system/job_queue.h>

auto pool = thread_system::create_thread_pool({
    .min_threads = 4,
    .max_threads = std::thread::hardware_concurrency(),
    .enable_work_stealing = true
});

auto job_queue = thread_system::create_lockfree_queue<hl7_message>();
```

---

*Document Version: 1.4.0*
*Created: 2025-12-07*
*Updated: 2025-12-07 - Added Performance Module (Section 12)*
*Author: kcenon@naver.com*
