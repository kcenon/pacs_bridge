#ifndef PACS_BRIDGE_FHIR_SERVICE_REQUEST_RESOURCE_H
#define PACS_BRIDGE_FHIR_SERVICE_REQUEST_RESOURCE_H

/**
 * @file service_request_resource.h
 * @brief FHIR ServiceRequest resource implementation
 *
 * Implements the FHIR R4 ServiceRequest resource for managing imaging orders.
 * Creates MWL (Modality Worklist) entries from incoming ServiceRequest resources.
 *
 * @see https://hl7.org/fhir/R4/servicerequest.html
 * @see https://github.com/kcenon/pacs_bridge/issues/33
 */

#include "fhir_resource.h"
#include "fhir_types.h"
#include "resource_handler.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward declarations
namespace pacs::bridge::cache {
class patient_cache;
}  // namespace pacs::bridge::cache

namespace pacs::bridge::mapping {
class fhir_dicom_mapper;
struct fhir_service_request;
struct fhir_coding;
struct fhir_codeable_concept;
struct fhir_reference;
struct mwl_item;
}  // namespace pacs::bridge::mapping

namespace pacs::bridge::fhir {

// =============================================================================
// ServiceRequest Status Codes
// =============================================================================

/**
 * @brief FHIR ServiceRequest status codes
 *
 * @see https://hl7.org/fhir/R4/valueset-request-status.html
 */
enum class service_request_status {
    draft,
    active,
    on_hold,
    revoked,
    completed,
    entered_in_error,
    unknown
};

/**
 * @brief Convert service_request_status to FHIR code string
 */
[[nodiscard]] constexpr std::string_view to_string(
    service_request_status status) noexcept {
    switch (status) {
        case service_request_status::draft: return "draft";
        case service_request_status::active: return "active";
        case service_request_status::on_hold: return "on-hold";
        case service_request_status::revoked: return "revoked";
        case service_request_status::completed: return "completed";
        case service_request_status::entered_in_error: return "entered-in-error";
        case service_request_status::unknown: return "unknown";
    }
    return "unknown";
}

/**
 * @brief Parse service_request_status from string
 */
[[nodiscard]] std::optional<service_request_status> parse_service_request_status(
    std::string_view status_str) noexcept;

// =============================================================================
// ServiceRequest Intent Codes
// =============================================================================

/**
 * @brief FHIR ServiceRequest intent codes
 *
 * @see https://hl7.org/fhir/R4/valueset-request-intent.html
 */
enum class service_request_intent {
    proposal,
    plan,
    directive,
    order,
    original_order,
    reflex_order,
    filler_order,
    instance_order,
    option
};

/**
 * @brief Convert service_request_intent to FHIR code string
 */
[[nodiscard]] constexpr std::string_view to_string(
    service_request_intent intent) noexcept {
    switch (intent) {
        case service_request_intent::proposal: return "proposal";
        case service_request_intent::plan: return "plan";
        case service_request_intent::directive: return "directive";
        case service_request_intent::order: return "order";
        case service_request_intent::original_order: return "original-order";
        case service_request_intent::reflex_order: return "reflex-order";
        case service_request_intent::filler_order: return "filler-order";
        case service_request_intent::instance_order: return "instance-order";
        case service_request_intent::option: return "option";
    }
    return "order";
}

/**
 * @brief Parse service_request_intent from string
 */
[[nodiscard]] std::optional<service_request_intent> parse_service_request_intent(
    std::string_view intent_str) noexcept;

// =============================================================================
// ServiceRequest Priority Codes
// =============================================================================

/**
 * @brief FHIR ServiceRequest priority codes
 *
 * @see https://hl7.org/fhir/R4/valueset-request-priority.html
 */
enum class service_request_priority {
    routine,
    urgent,
    asap,
    stat
};

/**
 * @brief Convert service_request_priority to FHIR code string
 */
[[nodiscard]] constexpr std::string_view to_string(
    service_request_priority priority) noexcept {
    switch (priority) {
        case service_request_priority::routine: return "routine";
        case service_request_priority::urgent: return "urgent";
        case service_request_priority::asap: return "asap";
        case service_request_priority::stat: return "stat";
    }
    return "routine";
}

/**
 * @brief Parse service_request_priority from string
 */
[[nodiscard]] std::optional<service_request_priority> parse_service_request_priority(
    std::string_view priority_str) noexcept;

// =============================================================================
// FHIR Coding and Reference Types for ServiceRequest
// =============================================================================

/**
 * @brief FHIR Coding data type for ServiceRequest
 *
 * @see https://hl7.org/fhir/R4/datatypes.html#Coding
 */
struct service_request_coding {
    std::string system;
    std::optional<std::string> version;
    std::string code;
    std::string display;
};

/**
 * @brief FHIR CodeableConcept data type for ServiceRequest
 *
 * @see https://hl7.org/fhir/R4/datatypes.html#CodeableConcept
 */
struct service_request_codeable_concept {
    std::vector<service_request_coding> coding;
    std::optional<std::string> text;
};

/**
 * @brief FHIR Reference data type for ServiceRequest
 *
 * @see https://hl7.org/fhir/R4/references.html
 */
struct service_request_reference {
    std::optional<std::string> reference;
    std::optional<std::string> type;
    std::optional<std::string> identifier;
    std::optional<std::string> display;
};

/**
 * @brief FHIR Identifier data type for ServiceRequest
 */
struct service_request_identifier {
    std::optional<std::string> use;
    std::optional<std::string> system;
    std::string value;
    std::optional<std::string> type_text;
};

// =============================================================================
// FHIR ServiceRequest Resource
// =============================================================================

/**
 * @brief FHIR R4 ServiceRequest resource
 *
 * Represents an imaging order per FHIR R4 specification.
 * Maps to DICOM MWL (Modality Worklist) entries.
 *
 * @example Creating a ServiceRequest
 * ```cpp
 * service_request_resource request;
 * request.set_id("order-456");
 * request.set_status(service_request_status::active);
 * request.set_intent(service_request_intent::order);
 *
 * // Set procedure code
 * service_request_coding code;
 * code.system = "http://loinc.org";
 * code.code = "24558-9";
 * code.display = "CT Chest";
 * service_request_codeable_concept code_concept;
 * code_concept.coding.push_back(code);
 * request.set_code(code_concept);
 *
 * // Set patient reference
 * service_request_reference subject;
 * subject.reference = "Patient/patient-123";
 * request.set_subject(subject);
 *
 * // Set scheduled time
 * request.set_occurrence_date_time("2024-01-15T10:00:00Z");
 *
 * // Serialize to JSON
 * std::string json = request.to_json();
 * ```
 *
 * @see https://hl7.org/fhir/R4/servicerequest.html
 */
class service_request_resource : public fhir_resource {
public:
    /**
     * @brief Default constructor
     */
    service_request_resource();

    /**
     * @brief Destructor
     */
    ~service_request_resource() override;

    // Non-copyable, movable
    service_request_resource(const service_request_resource&) = delete;
    service_request_resource& operator=(const service_request_resource&) = delete;
    service_request_resource(service_request_resource&&) noexcept;
    service_request_resource& operator=(service_request_resource&&) noexcept;

    // =========================================================================
    // fhir_resource Interface
    // =========================================================================

    /**
     * @brief Get resource type
     */
    [[nodiscard]] resource_type type() const noexcept override;

    /**
     * @brief Get resource type name
     */
    [[nodiscard]] std::string type_name() const override;

    /**
     * @brief Serialize to JSON
     */
    [[nodiscard]] std::string to_json() const override;

    /**
     * @brief Validate the resource
     */
    [[nodiscard]] bool validate() const override;

    // =========================================================================
    // Identifiers
    // =========================================================================

    /**
     * @brief Add an identifier to the service request
     */
    void add_identifier(const service_request_identifier& identifier);

    /**
     * @brief Get all identifiers
     */
    [[nodiscard]] const std::vector<service_request_identifier>& identifiers() const noexcept;

    /**
     * @brief Clear all identifiers
     */
    void clear_identifiers();

    // =========================================================================
    // Status and Intent
    // =========================================================================

    /**
     * @brief Set status (required)
     */
    void set_status(service_request_status status);

    /**
     * @brief Get status
     */
    [[nodiscard]] service_request_status status() const noexcept;

    /**
     * @brief Set intent (required)
     */
    void set_intent(service_request_intent intent);

    /**
     * @brief Get intent
     */
    [[nodiscard]] service_request_intent intent() const noexcept;

    /**
     * @brief Set priority
     */
    void set_priority(service_request_priority priority);

    /**
     * @brief Get priority
     */
    [[nodiscard]] std::optional<service_request_priority> priority() const noexcept;

    // =========================================================================
    // Code and Category
    // =========================================================================

    /**
     * @brief Set code (what is being requested)
     */
    void set_code(const service_request_codeable_concept& code);

    /**
     * @brief Get code
     */
    [[nodiscard]] const std::optional<service_request_codeable_concept>& code() const noexcept;

    /**
     * @brief Set category
     */
    void set_category(const service_request_codeable_concept& category);

    /**
     * @brief Get category
     */
    [[nodiscard]] const std::optional<service_request_codeable_concept>& category() const noexcept;

    // =========================================================================
    // Subject (Patient Reference)
    // =========================================================================

    /**
     * @brief Set subject (patient reference, required)
     */
    void set_subject(const service_request_reference& subject);

    /**
     * @brief Get subject
     */
    [[nodiscard]] const std::optional<service_request_reference>& subject() const noexcept;

    // =========================================================================
    // Requester and Performer
    // =========================================================================

    /**
     * @brief Set requester (who/what is requesting service)
     */
    void set_requester(const service_request_reference& requester);

    /**
     * @brief Get requester
     */
    [[nodiscard]] const std::optional<service_request_reference>& requester() const noexcept;

    /**
     * @brief Add performer (requested performer)
     */
    void add_performer(const service_request_reference& performer);

    /**
     * @brief Get performers
     */
    [[nodiscard]] const std::vector<service_request_reference>& performers() const noexcept;

    /**
     * @brief Clear performers
     */
    void clear_performers();

    // =========================================================================
    // Occurrence (When service should occur)
    // =========================================================================

    /**
     * @brief Set occurrence dateTime (ISO 8601 format)
     */
    void set_occurrence_date_time(std::string datetime);

    /**
     * @brief Get occurrence dateTime
     */
    [[nodiscard]] const std::optional<std::string>& occurrence_date_time() const noexcept;

    // =========================================================================
    // Additional Fields
    // =========================================================================

    /**
     * @brief Set reason code
     */
    void set_reason_code(std::string reason);

    /**
     * @brief Get reason code
     */
    [[nodiscard]] const std::optional<std::string>& reason_code() const noexcept;

    /**
     * @brief Set note
     */
    void set_note(std::string note);

    /**
     * @brief Get note
     */
    [[nodiscard]] const std::optional<std::string>& note() const noexcept;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Create ServiceRequest resource from JSON
     * @param json JSON string
     * @return ServiceRequest resource or nullptr on parse error
     */
    [[nodiscard]] static std::unique_ptr<service_request_resource> from_json(
        const std::string& json);

    /**
     * @brief Convert to mapping::fhir_service_request
     * @return fhir_service_request structure for mapping
     */
    [[nodiscard]] mapping::fhir_service_request to_mapping_struct() const;

    /**
     * @brief Create from mapping::fhir_service_request
     * @param request fhir_service_request structure
     * @return ServiceRequest resource
     */
    [[nodiscard]] static std::unique_ptr<service_request_resource> from_mapping_struct(
        const mapping::fhir_service_request& request);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// ServiceRequest Resource Handler
// =============================================================================

/**
 * @brief MWL storage interface for ServiceRequest handler
 *
 * Abstracts the MWL storage to allow different backend implementations.
 */
class mwl_storage {
public:
    virtual ~mwl_storage() = default;

    /**
     * @brief Store MWL item
     * @param id Resource ID
     * @param item MWL item to store
     * @return true on success
     */
    virtual bool store(const std::string& id, const mapping::mwl_item& item) = 0;

    /**
     * @brief Get MWL item by ID
     * @param id Resource ID
     * @return MWL item or nullptr if not found
     */
    [[nodiscard]] virtual std::optional<mapping::mwl_item> get(
        const std::string& id) const = 0;

    /**
     * @brief Update MWL item
     * @param id Resource ID
     * @param item Updated MWL item
     * @return true on success
     */
    virtual bool update(const std::string& id, const mapping::mwl_item& item) = 0;

    /**
     * @brief Delete MWL item
     * @param id Resource ID
     * @return true on success
     */
    virtual bool remove(const std::string& id) = 0;

    /**
     * @brief Get all MWL item IDs
     * @return List of IDs
     */
    [[nodiscard]] virtual std::vector<std::string> keys() const = 0;
};

/**
 * @brief In-memory MWL storage implementation
 */
class in_memory_mwl_storage : public mwl_storage {
public:
    in_memory_mwl_storage();
    ~in_memory_mwl_storage() override;

    bool store(const std::string& id, const mapping::mwl_item& item) override;
    [[nodiscard]] std::optional<mapping::mwl_item> get(
        const std::string& id) const override;
    bool update(const std::string& id, const mapping::mwl_item& item) override;
    bool remove(const std::string& id) override;
    [[nodiscard]] std::vector<std::string> keys() const override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief Handler for FHIR ServiceRequest resource operations
 *
 * Implements CRUD operations for ServiceRequest resources by mapping
 * to DICOM MWL entries.
 *
 * Supported operations:
 * - create: POST /ServiceRequest (creates MWL entry)
 * - read: GET /ServiceRequest/{id}
 * - search: GET /ServiceRequest?patient=xxx
 * - search: GET /ServiceRequest?status=xxx
 * - update: PUT /ServiceRequest/{id}
 *
 * @example Basic Usage
 * ```cpp
 * auto cache = std::make_shared<patient_cache>();
 * auto mapper = std::make_shared<fhir_dicom_mapper>();
 * auto storage = std::make_shared<in_memory_mwl_storage>();
 *
 * auto handler = std::make_shared<service_request_handler>(cache, mapper, storage);
 *
 * // Create a new order
 * auto request = std::make_unique<service_request_resource>();
 * request->set_status(service_request_status::active);
 * request->set_intent(service_request_intent::order);
 * // ... set other fields ...
 *
 * auto result = handler->create(std::move(request));
 * if (is_success(result)) {
 *     auto& created = get_resource(result);
 *     std::cout << "Created: " << created->id() << std::endl;
 * }
 * ```
 *
 * Thread-safety: All operations are thread-safe.
 */
class service_request_handler : public resource_handler {
public:
    /**
     * @brief Constructor
     * @param patient_cache Patient cache for resolving patient references
     * @param mapper FHIR-DICOM mapper for converting to MWL
     * @param storage MWL storage backend
     */
    service_request_handler(
        std::shared_ptr<cache::patient_cache> patient_cache,
        std::shared_ptr<mapping::fhir_dicom_mapper> mapper,
        std::shared_ptr<mwl_storage> storage);

    /**
     * @brief Destructor
     */
    ~service_request_handler() override;

    // Non-copyable, movable
    service_request_handler(const service_request_handler&) = delete;
    service_request_handler& operator=(const service_request_handler&) = delete;
    service_request_handler(service_request_handler&&) noexcept;
    service_request_handler& operator=(service_request_handler&&) noexcept;

    // =========================================================================
    // resource_handler Interface
    // =========================================================================

    /**
     * @brief Get handled resource type
     */
    [[nodiscard]] resource_type handled_type() const noexcept override;

    /**
     * @brief Get resource type name
     */
    [[nodiscard]] std::string_view type_name() const noexcept override;

    /**
     * @brief Read ServiceRequest by ID
     */
    [[nodiscard]] resource_result<std::unique_ptr<fhir_resource>> read(
        const std::string& id) override;

    /**
     * @brief Create a new ServiceRequest (creates MWL entry)
     */
    [[nodiscard]] resource_result<std::unique_ptr<fhir_resource>> create(
        std::unique_ptr<fhir_resource> resource) override;

    /**
     * @brief Update an existing ServiceRequest
     */
    [[nodiscard]] resource_result<std::unique_ptr<fhir_resource>> update(
        const std::string& id, std::unique_ptr<fhir_resource> resource) override;

    /**
     * @brief Search for ServiceRequests
     *
     * Supported search parameters:
     * - _id: Resource ID
     * - patient: Patient reference (e.g., "Patient/123")
     * - status: ServiceRequest status (active, completed, etc.)
     * - code: Procedure code
     */
    [[nodiscard]] resource_result<search_result> search(
        const std::map<std::string, std::string>& params,
        const pagination_params& pagination) override;

    /**
     * @brief Get supported search parameters
     */
    [[nodiscard]] std::map<std::string, std::string>
    supported_search_params() const override;

    /**
     * @brief Check if interaction is supported
     */
    [[nodiscard]] bool supports_interaction(
        interaction_type type) const noexcept override;

    /**
     * @brief Get supported interactions
     */
    [[nodiscard]] std::vector<interaction_type>
    supported_interactions() const override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Generate a unique resource ID
 * @return UUID-like ID string
 */
[[nodiscard]] std::string generate_resource_id();

}  // namespace pacs::bridge::fhir

#endif  // PACS_BRIDGE_FHIR_SERVICE_REQUEST_RESOURCE_H
