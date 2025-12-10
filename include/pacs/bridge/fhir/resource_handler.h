#ifndef PACS_BRIDGE_FHIR_RESOURCE_HANDLER_H
#define PACS_BRIDGE_FHIR_RESOURCE_HANDLER_H

/**
 * @file resource_handler.h
 * @brief FHIR resource handler interface and base implementations
 *
 * Provides the abstract interface for handling FHIR resource operations
 * (CRUD + search). Concrete implementations handle specific resource types.
 *
 * @see https://hl7.org/fhir/R4/http.html
 * @see https://github.com/kcenon/pacs_bridge/issues/31
 */

#include "fhir_resource.h"
#include "fhir_types.h"
#include "operation_outcome.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pacs::bridge::fhir {

// =============================================================================
// Result Types
// =============================================================================

/**
 * @brief Result type for single resource operations
 *
 * Contains either a resource or an OperationOutcome with error details.
 */
template <typename T>
using resource_result = std::variant<T, operation_outcome>;

/**
 * @brief Check if result contains a resource (success)
 */
template <typename T>
[[nodiscard]] bool is_success(const resource_result<T>& result) noexcept {
    return std::holds_alternative<T>(result);
}

/**
 * @brief Get the resource from a successful result
 * @throws std::bad_variant_access if result is an error
 */
template <typename T>
[[nodiscard]] const T& get_resource(const resource_result<T>& result) {
    return std::get<T>(result);
}

/**
 * @brief Get the outcome from a failed result
 * @throws std::bad_variant_access if result is a success
 */
template <typename T>
[[nodiscard]] const operation_outcome& get_outcome(
    const resource_result<T>& result) {
    return std::get<operation_outcome>(result);
}

// =============================================================================
// Search Result
// =============================================================================

/**
 * @brief Search result containing multiple resources
 */
struct search_result {
    /** Matched resources */
    std::vector<std::unique_ptr<fhir_resource>> entries;

    /** Total count (may differ from entries.size() due to pagination) */
    size_t total = 0;

    /** Pagination links */
    std::vector<bundle_link> links;

    /** Search mode for each entry ("match" or "include") */
    std::vector<std::string> search_modes;
};

// =============================================================================
// Resource Handler Interface
// =============================================================================

/**
 * @brief Abstract interface for handling FHIR resource operations
 *
 * Implementations of this interface handle specific resource types
 * (Patient, ServiceRequest, etc.). The server routes requests to
 * the appropriate handler based on resource type.
 *
 * Thread-safety: Implementations must be thread-safe.
 */
class resource_handler {
public:
    virtual ~resource_handler() = default;

    /**
     * @brief Get the resource type this handler manages
     */
    [[nodiscard]] virtual resource_type handled_type() const noexcept = 0;

    /**
     * @brief Get the resource type name (e.g., "Patient")
     */
    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;

    // =========================================================================
    // CRUD Operations
    // =========================================================================

    /**
     * @brief Read a resource by ID
     *
     * @param id Resource ID
     * @return Resource or OperationOutcome
     */
    [[nodiscard]] virtual resource_result<std::unique_ptr<fhir_resource>> read(
        const std::string& id) = 0;

    /**
     * @brief Read a specific version of a resource
     *
     * @param id Resource ID
     * @param version_id Version ID
     * @return Resource or OperationOutcome
     */
    [[nodiscard]] virtual resource_result<std::unique_ptr<fhir_resource>> vread(
        const std::string& id, const std::string& version_id);

    /**
     * @brief Create a new resource
     *
     * @param resource Resource to create
     * @return Created resource with assigned ID, or OperationOutcome
     */
    [[nodiscard]] virtual resource_result<std::unique_ptr<fhir_resource>> create(
        std::unique_ptr<fhir_resource> resource);

    /**
     * @brief Update an existing resource
     *
     * @param id Resource ID
     * @param resource Updated resource
     * @return Updated resource, or OperationOutcome
     */
    [[nodiscard]] virtual resource_result<std::unique_ptr<fhir_resource>> update(
        const std::string& id, std::unique_ptr<fhir_resource> resource);

    /**
     * @brief Delete a resource
     *
     * @param id Resource ID
     * @return Empty outcome on success, or error outcome
     */
    [[nodiscard]] virtual resource_result<std::monostate> delete_resource(
        const std::string& id);

    // =========================================================================
    // Search Operations
    // =========================================================================

    /**
     * @brief Search for resources
     *
     * @param params Search parameters
     * @param pagination Pagination settings
     * @return Search result bundle or OperationOutcome
     */
    [[nodiscard]] virtual resource_result<search_result> search(
        const std::map<std::string, std::string>& params,
        const pagination_params& pagination);

    // =========================================================================
    // Capabilities
    // =========================================================================

    /**
     * @brief Get supported search parameters
     * @return Map of parameter name to description
     */
    [[nodiscard]] virtual std::map<std::string, std::string>
    supported_search_params() const;

    /**
     * @brief Check if an interaction type is supported
     */
    [[nodiscard]] virtual bool supports_interaction(
        interaction_type type) const noexcept;

    /**
     * @brief Get supported interactions
     */
    [[nodiscard]] virtual std::vector<interaction_type>
    supported_interactions() const;

protected:
    resource_handler() = default;

    /**
     * @brief Create a "not implemented" outcome
     */
    [[nodiscard]] static operation_outcome not_implemented(
        std::string_view operation);

    /**
     * @brief Create a "not found" outcome
     */
    [[nodiscard]] operation_outcome resource_not_found(
        const std::string& id) const;
};

// =============================================================================
// Handler Registry
// =============================================================================

/**
 * @brief Registry for resource handlers
 *
 * Manages registration and lookup of resource handlers by type.
 * Thread-safe for concurrent access.
 */
class handler_registry {
public:
    handler_registry() = default;
    ~handler_registry() = default;

    // Non-copyable, non-movable (contains mutex)
    handler_registry(const handler_registry&) = delete;
    handler_registry& operator=(const handler_registry&) = delete;
    handler_registry(handler_registry&&) = delete;
    handler_registry& operator=(handler_registry&&) = delete;

    /**
     * @brief Register a handler for a resource type
     *
     * @param handler Handler to register
     * @return true if registered, false if type already has a handler
     */
    bool register_handler(std::shared_ptr<resource_handler> handler);

    /**
     * @brief Get handler for a resource type
     *
     * @param type Resource type
     * @return Handler or nullptr if not found
     */
    [[nodiscard]] std::shared_ptr<resource_handler> get_handler(
        resource_type type) const;

    /**
     * @brief Get handler by resource type name
     *
     * @param type_name Resource type name (e.g., "Patient")
     * @return Handler or nullptr if not found
     */
    [[nodiscard]] std::shared_ptr<resource_handler> get_handler(
        std::string_view type_name) const;

    /**
     * @brief Get all registered handlers
     */
    [[nodiscard]] std::vector<std::shared_ptr<resource_handler>>
    all_handlers() const;

    /**
     * @brief Get all registered resource types
     */
    [[nodiscard]] std::vector<resource_type> registered_types() const;

    /**
     * @brief Check if a resource type has a registered handler
     */
    [[nodiscard]] bool has_handler(resource_type type) const;

    /**
     * @brief Clear all registered handlers
     */
    void clear();

private:
    mutable std::mutex mutex_;
    std::map<resource_type, std::shared_ptr<resource_handler>> handlers_;
};

}  // namespace pacs::bridge::fhir

#endif  // PACS_BRIDGE_FHIR_RESOURCE_HANDLER_H
