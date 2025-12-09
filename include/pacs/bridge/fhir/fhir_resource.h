#ifndef PACS_BRIDGE_FHIR_FHIR_RESOURCE_H
#define PACS_BRIDGE_FHIR_FHIR_RESOURCE_H

/**
 * @file fhir_resource.h
 * @brief FHIR Gateway Module - Base resource class
 *
 * Provides the base class for FHIR R4 resources.
 *
 * @see docs/SDS_COMPONENTS.md - Section 3: FHIR Gateway Module (DES-FHIR-002)
 * @see https://github.com/kcenon/pacs_bridge/issues/32
 */

#include "fhir_types.h"

#include <memory>
#include <string>

namespace pacs::bridge::fhir {

/**
 * @brief Base FHIR resource
 *
 * Abstract base class for all FHIR resources.
 * Provides common functionality for resource serialization
 * and metadata management.
 */
class fhir_resource {
public:
    virtual ~fhir_resource() = default;

    /**
     * @brief Get the resource type
     */
    [[nodiscard]] virtual resource_type type() const noexcept = 0;

    /**
     * @brief Get the resource type as string
     */
    [[nodiscard]] virtual std::string type_name() const = 0;

    /**
     * @brief Get the resource ID
     */
    [[nodiscard]] const std::string& id() const noexcept { return id_; }

    /**
     * @brief Set the resource ID
     */
    void set_id(std::string id) { id_ = std::move(id); }

    /**
     * @brief Get the version ID
     */
    [[nodiscard]] const std::string& version_id() const noexcept {
        return version_id_;
    }

    /**
     * @brief Set the version ID
     */
    void set_version_id(std::string version) {
        version_id_ = std::move(version);
    }

    /**
     * @brief Serialize resource to JSON string
     */
    [[nodiscard]] virtual std::string to_json() const = 0;

    /**
     * @brief Validate the resource
     * @return true if valid, false otherwise
     */
    [[nodiscard]] virtual bool validate() const = 0;

protected:
    fhir_resource() = default;

private:
    std::string id_;
    std::string version_id_;
};

/**
 * @brief Create a FHIR resource from JSON
 * @param json JSON string representing the resource
 * @return Parsed resource or nullptr on failure
 */
[[nodiscard]] std::unique_ptr<fhir_resource> parse_resource(
    const std::string& json);

} // namespace pacs::bridge::fhir

#endif // PACS_BRIDGE_FHIR_FHIR_RESOURCE_H
