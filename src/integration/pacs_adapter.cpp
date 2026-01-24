/**
 * @file pacs_adapter.cpp
 * @brief Implementation of PACS adapter for pacs_bridge
 *
 * @see include/pacs/bridge/integration/pacs_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/283
 */

#include "pacs/bridge/integration/pacs_adapter.h"

#include <algorithm>
#include <cassert>

namespace pacs::bridge::integration {

// =============================================================================
// Error Handling
// =============================================================================

std::string_view to_string(pacs_error error) noexcept {
    switch (error) {
        case pacs_error::connection_failed:
            return "Connection to PACS server failed";
        case pacs_error::query_failed:
            return "Query execution failed";
        case pacs_error::store_failed:
            return "Store operation failed";
        case pacs_error::invalid_dataset:
            return "Invalid or malformed DICOM dataset";
        case pacs_error::association_failed:
            return "DICOM association failed";
        case pacs_error::timeout:
            return "Operation timeout";
        case pacs_error::not_found:
            return "Resource not found";
        case pacs_error::duplicate_entry:
            return "Duplicate entry detected";
        case pacs_error::validation_failed:
            return "Validation failed";
        case pacs_error::mpps_create_failed:
            return "MPPS N-CREATE failed";
        case pacs_error::mpps_update_failed:
            return "MPPS N-SET failed";
        case pacs_error::mwl_query_failed:
            return "MWL query failed";
        case pacs_error::storage_failed:
            return "DICOM storage failed";
        case pacs_error::invalid_sop_uid:
            return "Invalid SOP Instance UID";
        default:
            return "Unknown PACS error";
    }
}

// =============================================================================
// dicom_dataset Implementation
// =============================================================================

std::optional<std::string> dicom_dataset::get_string(uint32_t tag) const {
    auto it = attributes.find(tag);
    if (it != attributes.end()) {
        return it->second;
    }
    return std::nullopt;
}

void dicom_dataset::set_string(uint32_t tag, std::string_view value) {
    attributes[tag] = std::string(value);
}

bool dicom_dataset::has_tag(uint32_t tag) const {
    return attributes.find(tag) != attributes.end();
}

void dicom_dataset::remove_tag(uint32_t tag) {
    attributes.erase(tag);
}

void dicom_dataset::clear() {
    sop_class_uid.clear();
    sop_instance_uid.clear();
    attributes.clear();
}

// =============================================================================
// mpps_record Implementation
// =============================================================================

bool mpps_record::is_valid() const {
    // Check required fields
    if (sop_instance_uid.empty()) return false;
    if (scheduled_procedure_step_id.empty()) return false;
    if (performed_procedure_step_id.empty()) return false;
    if (status.empty()) return false;

    // Validate status
    if (status != "IN PROGRESS" && status != "COMPLETED" && status != "DISCONTINUED") {
        return false;
    }

    // COMPLETED status must have end_datetime
    if (status == "COMPLETED" && !end_datetime.has_value()) {
        return false;
    }

    return true;
}

// =============================================================================
// mwl_item Implementation
// =============================================================================

bool mwl_item::is_valid() const {
    // Check required fields
    if (accession_number.empty()) return false;
    if (scheduled_procedure_step_id.empty()) return false;
    if (patient_id.empty()) return false;
    if (patient_name.empty()) return false;
    if (modality.empty()) return false;

    return true;
}

// =============================================================================
// Stub MPPS Adapter
// =============================================================================

/**
 * @brief Stub implementation of MPPS adapter (standalone mode)
 *
 * Provides no-op implementations for testing and standalone usage.
 */
class stub_mpps_adapter : public mpps_adapter {
public:
    std::expected<void, pacs_error> create_mpps(const mpps_record& record) override {
        if (!record.is_valid()) {
            return std::unexpected(pacs_error::validation_failed);
        }
        // Stub: no-op implementation
        return {};
    }

    std::expected<void, pacs_error> update_mpps(const mpps_record& record) override {
        if (!record.is_valid()) {
            return std::unexpected(pacs_error::validation_failed);
        }
        // Stub: no-op implementation
        return {};
    }

    std::expected<std::vector<mpps_record>, pacs_error> query_mpps(
        const mpps_query_params& params) override {
        // Stub: return empty result
        return std::vector<mpps_record>{};
    }

    std::expected<mpps_record, pacs_error> get_mpps(
        std::string_view sop_instance_uid) override {
        if (sop_instance_uid.empty()) {
            return std::unexpected(pacs_error::invalid_sop_uid);
        }
        // Stub: not found
        return std::unexpected(pacs_error::not_found);
    }
};

// =============================================================================
// Stub MWL Adapter
// =============================================================================

/**
 * @brief Stub implementation of MWL adapter (standalone mode)
 *
 * Provides no-op implementations for testing and standalone usage.
 */
class stub_mwl_adapter : public mwl_adapter {
public:
    std::expected<std::vector<mwl_item>, pacs_error> query_mwl(
        const mwl_query_params& params) override {
        // Stub: return empty result
        return std::vector<mwl_item>{};
    }

    std::expected<mwl_item, pacs_error> get_mwl_item(
        std::string_view accession_number) override {
        if (accession_number.empty()) {
            return std::unexpected(pacs_error::validation_failed);
        }
        // Stub: not found
        return std::unexpected(pacs_error::not_found);
    }
};

// =============================================================================
// Stub Storage Adapter
// =============================================================================

/**
 * @brief Stub implementation of storage adapter (standalone mode)
 *
 * Provides no-op implementations for testing and standalone usage.
 */
class stub_storage_adapter : public storage_adapter {
public:
    std::expected<void, pacs_error> store(const dicom_dataset& dataset) override {
        if (dataset.sop_instance_uid.empty()) {
            return std::unexpected(pacs_error::invalid_dataset);
        }
        // Stub: no-op implementation
        return {};
    }

    std::expected<dicom_dataset, pacs_error> retrieve(
        std::string_view sop_instance_uid) override {
        if (sop_instance_uid.empty()) {
            return std::unexpected(pacs_error::invalid_sop_uid);
        }
        // Stub: not found
        return std::unexpected(pacs_error::not_found);
    }

    bool exists(std::string_view sop_instance_uid) const override {
        // Stub: always return false
        return false;
    }
};

// =============================================================================
// Stub PACS Adapter
// =============================================================================

/**
 * @brief Stub implementation of PACS adapter (standalone mode)
 *
 * Provides a complete stub implementation for standalone usage
 * without pacs_system integration.
 */
class stub_pacs_adapter : public pacs_adapter {
public:
    explicit stub_pacs_adapter(const pacs_config& config)
        : config_(config)
        , mpps_adapter_(std::make_shared<stub_mpps_adapter>())
        , mwl_adapter_(std::make_shared<stub_mwl_adapter>())
        , storage_adapter_(std::make_shared<stub_storage_adapter>())
        , connected_(false) {}

    std::shared_ptr<mpps_adapter> get_mpps_adapter() override {
        return mpps_adapter_;
    }

    std::shared_ptr<mwl_adapter> get_mwl_adapter() override {
        return mwl_adapter_;
    }

    std::shared_ptr<storage_adapter> get_storage_adapter() override {
        return storage_adapter_;
    }

    std::expected<void, pacs_error> connect() override {
        // Stub: simulate successful connection
        connected_ = true;
        return {};
    }

    void disconnect() override {
        // Stub: simulate disconnection
        connected_ = false;
    }

    bool is_connected() const override {
        return connected_;
    }

    bool is_healthy() const override {
        // Stub: always healthy if connected
        return connected_;
    }

private:
    pacs_config config_;
    std::shared_ptr<stub_mpps_adapter> mpps_adapter_;
    std::shared_ptr<stub_mwl_adapter> mwl_adapter_;
    std::shared_ptr<stub_storage_adapter> storage_adapter_;
    bool connected_;
};

// =============================================================================
// Factory Functions
// =============================================================================

std::shared_ptr<pacs_adapter> create_pacs_adapter(const pacs_config& config) {
    return std::make_shared<stub_pacs_adapter>(config);
}

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
std::shared_ptr<pacs_adapter> create_pacs_adapter(
    std::shared_ptr<kcenon::pacs::services::pacs_server> server) {
    // TODO: Implement pacs_system integration in Phase 4d
    // For now, return stub adapter
    return std::make_shared<stub_pacs_adapter>(pacs_config{});
}
#endif

}  // namespace pacs::bridge::integration
