#ifndef PACS_BRIDGE_EMR_ADAPTERS_GENERIC_FHIR_ADAPTER_H
#define PACS_BRIDGE_EMR_ADAPTERS_GENERIC_FHIR_ADAPTER_H

/**
 * @file generic_fhir_adapter.h
 * @brief Generic FHIR R4 EMR adapter implementation
 *
 * Provides a standard FHIR R4 compliant adapter that works with any
 * EMR system supporting the FHIR R4 specification. This serves as
 * the default adapter and base class for vendor-specific adapters.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/107
 * @see https://www.hl7.org/fhir/
 */

#include "../emr_adapter.h"
#include "../fhir_client.h"

#include <memory>
#include <mutex>

namespace pacs::bridge::emr {

/**
 * @brief Generic FHIR R4 adapter implementation
 *
 * Standard FHIR R4 compliant adapter that works with any EMR system
 * supporting the FHIR R4 specification. Implements the emr_adapter
 * interface using fhir_client, emr_patient_lookup, and emr_result_poster.
 *
 * Thread-safe: All operations are protected by mutex.
 */
class generic_fhir_adapter : public emr_adapter {
public:
    /**
     * @brief Construct with configuration
     *
     * @param config Adapter configuration
     */
    explicit generic_fhir_adapter(const emr_adapter_config& config);

    /**
     * @brief Construct with existing FHIR client
     *
     * @param client FHIR client to use
     * @param config Adapter configuration
     */
    generic_fhir_adapter(std::shared_ptr<fhir_client> client,
                         const emr_adapter_config& config);

    /**
     * @brief Destructor
     */
    ~generic_fhir_adapter() override;

    // Non-copyable, movable
    generic_fhir_adapter(const generic_fhir_adapter&) = delete;
    generic_fhir_adapter& operator=(const generic_fhir_adapter&) = delete;
    generic_fhir_adapter(generic_fhir_adapter&&) noexcept;
    generic_fhir_adapter& operator=(generic_fhir_adapter&&) noexcept;

    // =========================================================================
    // Identification (emr_adapter interface)
    // =========================================================================

    [[nodiscard]] emr_vendor vendor() const noexcept override;
    [[nodiscard]] std::string_view vendor_name() const noexcept override;
    [[nodiscard]] std::string_view version() const noexcept override;
    [[nodiscard]] adapter_features features() const noexcept override;

    // =========================================================================
    // Connection Management (emr_adapter interface)
    // =========================================================================

    [[nodiscard]] VoidResult initialize() override;
    void shutdown() noexcept override;
    [[nodiscard]] bool is_initialized() const noexcept override;
    [[nodiscard]] bool is_connected() const noexcept override;

    // =========================================================================
    // Health Check (emr_adapter interface)
    // =========================================================================

    [[nodiscard]] Result<adapter_health_status>
    health_check() override;

    [[nodiscard]] adapter_health_status
    get_health_status() const noexcept override;

    // =========================================================================
    // Patient Operations (emr_adapter interface)
    // =========================================================================

    [[nodiscard]] Result<patient_record>
    query_patient(const patient_query& query) override;

    [[nodiscard]] Result<std::vector<patient_match>>
    search_patients(const patient_query& query) override;

    // =========================================================================
    // Result Operations (emr_adapter interface)
    // =========================================================================

    [[nodiscard]] Result<posted_result>
    post_result(const study_result& result) override;

    [[nodiscard]] VoidResult
    update_result(std::string_view report_id,
                  const study_result& result) override;

    // =========================================================================
    // Encounter Operations (emr_adapter interface)
    // =========================================================================

    [[nodiscard]] Result<encounter_info>
    get_encounter(std::string_view encounter_id) override;

    [[nodiscard]] Result<std::optional<encounter_info>>
    find_active_encounter(std::string_view patient_id) override;

    // =========================================================================
    // Configuration (emr_adapter interface)
    // =========================================================================

    [[nodiscard]] const emr_adapter_config& config() const noexcept override;

    [[nodiscard]] VoidResult
    set_config(const emr_adapter_config& config) override;

    // =========================================================================
    // Statistics (emr_adapter interface)
    // =========================================================================

    [[nodiscard]] statistics get_statistics() const noexcept override;
    void reset_statistics() noexcept override;

    // =========================================================================
    // Generic FHIR Specific Methods
    // =========================================================================

    /**
     * @brief Get underlying FHIR client
     */
    [[nodiscard]] std::shared_ptr<fhir_client> get_fhir_client() const noexcept;

protected:
    /**
     * @brief Create FHIR client from configuration
     */
    [[nodiscard]] Result<std::shared_ptr<fhir_client>>
    create_fhir_client();

    /**
     * @brief Update internal statistics
     */
    void record_request(bool success, std::chrono::milliseconds duration);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_ADAPTERS_GENERIC_FHIR_ADAPTER_H
