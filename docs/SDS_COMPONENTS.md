# SDS - Component Designs

> **Version:** 1.2.0
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

---

*Document Version: 1.2.0*
*Created: 2025-12-07*
*Updated: 2025-12-07 - Added Security Module (Section 10)*
*Author: kcenon@naver.com*
