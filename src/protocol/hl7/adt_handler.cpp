/**
 * @file adt_handler.cpp
 * @brief ADT (Admission, Discharge, Transfer) message handler implementation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/14
 */

#include "pacs/bridge/protocol/hl7/adt_handler.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <chrono>
#include <mutex>
#include <sstream>

namespace pacs::bridge::hl7 {

// =============================================================================
// Implementation Class
// =============================================================================

class adt_handler::impl {
public:
    explicit impl(std::shared_ptr<cache::patient_cache> cache)
        : cache_(std::move(cache)), mapper_() {}

    impl(std::shared_ptr<cache::patient_cache> cache,
         const adt_handler_config& config)
        : cache_(std::move(cache)), config_(config), mapper_() {}

    // =========================================================================
    // Message Handling
    // =========================================================================

    std::expected<adt_result, adt_error> handle(const hl7_message& message) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check if ADT message
        auto header = message.header();
        if (header.type != message_type::ADT) {
            return std::unexpected(adt_error::not_adt_message);
        }

        // Parse trigger event
        auto trigger = parse_adt_trigger(header.trigger_event);
        if (trigger == adt_trigger_event::unknown) {
            return std::unexpected(adt_error::unsupported_trigger_event);
        }

        // Update statistics
        stats_.total_processed++;

        // Dispatch to appropriate handler
        std::expected<adt_result, adt_error> result;
        switch (trigger) {
            case adt_trigger_event::A01:
                result = handle_admit_impl(message);
                stats_.a01_count++;
                break;
            case adt_trigger_event::A04:
                result = handle_register_impl(message);
                stats_.a04_count++;
                break;
            case adt_trigger_event::A08:
                result = handle_update_impl(message);
                stats_.a08_count++;
                break;
            case adt_trigger_event::A40:
                result = handle_merge_impl(message);
                stats_.a40_count++;
                break;
            default:
                return std::unexpected(adt_error::unsupported_trigger_event);
        }

        // Update statistics based on result
        if (result) {
            stats_.success_count++;
        } else {
            stats_.failure_count++;
        }

        return result;
    }

    bool can_handle(const hl7_message& message) const noexcept {
        auto header = message.header();
        if (header.type != message_type::ADT) {
            return false;
        }

        auto trigger = parse_adt_trigger(header.trigger_event);
        return trigger != adt_trigger_event::unknown;
    }

    std::vector<std::string> supported_triggers() const {
        return {"A01", "A04", "A08", "A40"};
    }

    // =========================================================================
    // Individual Event Handlers
    // =========================================================================

    std::expected<adt_result, adt_error> handle_admit_impl(
        const hl7_message& message) {
        return create_or_update_patient(message, adt_trigger_event::A01,
                                         config_.allow_a01_update);
    }

    std::expected<adt_result, adt_error> handle_register_impl(
        const hl7_message& message) {
        // A04 always creates (for outpatient registration)
        return create_or_update_patient(message, adt_trigger_event::A04, true);
    }

    std::expected<adt_result, adt_error> handle_update_impl(
        const hl7_message& message) {
        // Extract patient data
        auto patient_result = mapper_.to_patient(message);
        if (!patient_result) {
            return std::unexpected(adt_error::invalid_patient_data);
        }

        auto& patient = *patient_result;
        if (patient.patient_id.empty()) {
            return std::unexpected(adt_error::missing_patient_id);
        }

        // Check if patient exists
        auto existing = cache_->get(patient.patient_id);
        if (!existing) {
            if (config_.allow_a08_create) {
                // Create if configured to allow
                return create_patient(message, patient, adt_trigger_event::A08);
            }
            return std::unexpected(adt_error::patient_not_found);
        }

        // Update patient
        return update_patient(message, *existing, patient);
    }

    std::expected<adt_result, adt_error> handle_merge_impl(
        const hl7_message& message) {
        // Extract merge information from MRG segment
        auto merge_info_opt = extract_merge_info(message);
        if (!merge_info_opt) {
            return std::unexpected(adt_error::merge_failed);
        }

        auto& merge = *merge_info_opt;

        // Get primary patient data from PID segment
        auto patient_result = mapper_.to_patient(message);
        if (!patient_result) {
            return std::unexpected(adt_error::invalid_patient_data);
        }

        auto& primary_patient = *patient_result;
        if (primary_patient.patient_id.empty()) {
            return std::unexpected(adt_error::missing_patient_id);
        }

        // Perform merge in cache
        // First, remove the secondary patient
        cache_->remove(merge.secondary_patient_id);

        // Update/create the primary patient
        cache_->put(primary_patient.patient_id, primary_patient);

        // Add alias for the merged patient ID
        cache_->add_alias(merge.secondary_patient_id, primary_patient.patient_id);

        // Invoke callback
        if (merged_callback_) {
            merged_callback_(merge);
        }

        stats_.patients_merged++;

        // Build result
        adt_result result;
        result.success = true;
        result.trigger = adt_trigger_event::A40;
        result.patient_id = primary_patient.patient_id;
        result.merged_patient_id = merge.secondary_patient_id;
        result.description =
            "Merged patient " + merge.secondary_patient_id + " into " +
            primary_patient.patient_id;
        result.ack_message =
            create_ack(message, ack_code::AA, result.description);

        return result;
    }

    // =========================================================================
    // Helper Methods
    // =========================================================================

    std::expected<adt_result, adt_error> create_or_update_patient(
        const hl7_message& message, adt_trigger_event trigger,
        bool allow_update) {
        // Extract patient data
        auto patient_result = mapper_.to_patient(message);
        if (!patient_result) {
            return std::unexpected(adt_error::invalid_patient_data);
        }

        auto& patient = *patient_result;
        if (patient.patient_id.empty()) {
            return std::unexpected(adt_error::missing_patient_id);
        }

        // Validate if configured
        if (config_.validate_patient_data) {
            auto validation_errors = validate_patient(patient);
            if (!validation_errors.empty()) {
                return std::unexpected(adt_error::invalid_patient_data);
            }
        }

        // Check if patient exists
        auto existing = cache_->get(patient.patient_id);
        if (existing) {
            if (allow_update) {
                return update_patient(message, *existing, patient);
            }
            return std::unexpected(adt_error::duplicate_patient);
        }

        return create_patient(message, patient, trigger);
    }

    std::expected<adt_result, adt_error> create_patient(
        const hl7_message& message, const mapping::dicom_patient& patient,
        adt_trigger_event trigger) {
        // Add to cache
        cache_->put(patient.patient_id, patient);

        // Add issuer-qualified key if available
        if (!patient.issuer_of_patient_id.empty()) {
            std::string qualified_key =
                patient.issuer_of_patient_id + ":" + patient.patient_id;
            cache_->add_alias(qualified_key, patient.patient_id);
        }

        // Invoke callback
        if (created_callback_) {
            created_callback_(patient);
        }

        stats_.patients_created++;

        // Build result
        adt_result result;
        result.success = true;
        result.trigger = trigger;
        result.patient_id = patient.patient_id;
        result.description = "Created patient record for " + patient.patient_id;
        result.ack_message =
            create_ack(message, ack_code::AA, result.description);

        return result;
    }

    std::expected<adt_result, adt_error> update_patient(
        const hl7_message& message, const mapping::dicom_patient& old_patient,
        const mapping::dicom_patient& new_patient) {
        // Update in cache
        cache_->put(new_patient.patient_id, new_patient);

        // Update issuer alias if changed
        if (!new_patient.issuer_of_patient_id.empty() &&
            new_patient.issuer_of_patient_id != old_patient.issuer_of_patient_id) {
            // Remove old alias
            if (!old_patient.issuer_of_patient_id.empty()) {
                std::string old_key =
                    old_patient.issuer_of_patient_id + ":" + old_patient.patient_id;
                cache_->remove_alias(old_key);
            }
            // Add new alias
            std::string new_key =
                new_patient.issuer_of_patient_id + ":" + new_patient.patient_id;
            cache_->add_alias(new_key, new_patient.patient_id);
        }

        // Invoke callback
        if (updated_callback_) {
            updated_callback_(old_patient, new_patient);
        }

        stats_.patients_updated++;

        // Build result
        adt_result result;
        result.success = true;
        result.trigger = adt_trigger_event::A08;
        result.patient_id = new_patient.patient_id;
        result.description = "Updated patient record for " + new_patient.patient_id;
        result.ack_message =
            create_ack(message, ack_code::AA, result.description);

        return result;
    }

    std::optional<merge_info> extract_merge_info(
        const hl7_message& message) const {
        // Get MRG segment
        auto* mrg = message.segment("MRG");
        if (!mrg) {
            return std::nullopt;
        }

        merge_info info;

        // MRG-1: Prior Patient Identifier List (the patient being merged away)
        info.secondary_patient_id = std::string(mrg->field_value(1));
        if (info.secondary_patient_id.empty()) {
            return std::nullopt;
        }

        // Extract issuer from MRG-1.4 if present
        const auto& mrg1 = mrg->field(1);
        if (mrg1.component_count() >= 4) {
            info.secondary_issuer = std::string(mrg1.component(4).value());
        }

        // Primary patient ID comes from PID-3
        auto* pid = message.segment("PID");
        if (pid) {
            info.primary_patient_id = std::string(pid->field_value(3));
            const auto& pid3 = pid->field(3);
            if (pid3.component_count() >= 4) {
                info.primary_issuer = std::string(pid3.component(4).value());
            }
        }

        // Get merge datetime from EVN-2 if available
        auto* evn = message.segment("EVN");
        if (evn) {
            info.merge_datetime = std::string(evn->field_value(2));
        }

        return info;
    }

    std::vector<std::string> validate_patient(
        const mapping::dicom_patient& patient) const {
        std::vector<std::string> errors;

        for (const auto& field : config_.required_fields) {
            if (field == "patient_id" && patient.patient_id.empty()) {
                errors.push_back("Missing required field: patient_id");
            } else if (field == "patient_name" && patient.patient_name.empty()) {
                errors.push_back("Missing required field: patient_name");
            } else if (field == "patient_birth_date" &&
                       patient.patient_birth_date.empty()) {
                errors.push_back("Missing required field: patient_birth_date");
            } else if (field == "patient_sex" && patient.patient_sex.empty()) {
                errors.push_back("Missing required field: patient_sex");
            }
        }

        return errors;
    }

    hl7_message create_ack(const hl7_message& original, ack_code code,
                           std::string_view text) const {
        if (config_.detailed_ack) {
            return original.create_ack(code, text);
        }
        return original.create_ack(code, "");
    }

    // =========================================================================
    // Callbacks
    // =========================================================================

    void set_created_callback(patient_created_callback callback) {
        created_callback_ = std::move(callback);
    }

    void set_updated_callback(patient_updated_callback callback) {
        updated_callback_ = std::move(callback);
    }

    void set_merged_callback(patient_merged_callback callback) {
        merged_callback_ = std::move(callback);
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    const adt_handler_config& config() const noexcept { return config_; }

    void set_config(const adt_handler_config& config) { config_ = config; }

    std::shared_ptr<cache::patient_cache> cache() const noexcept {
        return cache_;
    }

    // =========================================================================
    // Statistics
    // =========================================================================

    statistics get_statistics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    void reset_statistics() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_ = statistics{};
    }

private:
    std::shared_ptr<cache::patient_cache> cache_;
    adt_handler_config config_;
    mapping::hl7_dicom_mapper mapper_;

    patient_created_callback created_callback_;
    patient_updated_callback updated_callback_;
    patient_merged_callback merged_callback_;

    mutable std::mutex mutex_;
    statistics stats_;
};

// =============================================================================
// adt_handler Public Interface
// =============================================================================

adt_handler::adt_handler(std::shared_ptr<cache::patient_cache> cache)
    : pimpl_(std::make_unique<impl>(std::move(cache))) {}

adt_handler::adt_handler(std::shared_ptr<cache::patient_cache> cache,
                         const adt_handler_config& config)
    : pimpl_(std::make_unique<impl>(std::move(cache), config)) {}

adt_handler::~adt_handler() = default;

adt_handler::adt_handler(adt_handler&&) noexcept = default;
adt_handler& adt_handler::operator=(adt_handler&&) noexcept = default;

std::expected<adt_result, adt_error> adt_handler::handle(
    const hl7_message& message) {
    return pimpl_->handle(message);
}

bool adt_handler::can_handle(const hl7_message& message) const noexcept {
    return pimpl_->can_handle(message);
}

std::vector<std::string> adt_handler::supported_triggers() const {
    return pimpl_->supported_triggers();
}

std::expected<adt_result, adt_error> adt_handler::handle_admit(
    const hl7_message& message) {
    return pimpl_->handle_admit_impl(message);
}

std::expected<adt_result, adt_error> adt_handler::handle_register(
    const hl7_message& message) {
    return pimpl_->handle_register_impl(message);
}

std::expected<adt_result, adt_error> adt_handler::handle_update(
    const hl7_message& message) {
    return pimpl_->handle_update_impl(message);
}

std::expected<adt_result, adt_error> adt_handler::handle_merge(
    const hl7_message& message) {
    return pimpl_->handle_merge_impl(message);
}

void adt_handler::on_patient_created(patient_created_callback callback) {
    pimpl_->set_created_callback(std::move(callback));
}

void adt_handler::on_patient_updated(patient_updated_callback callback) {
    pimpl_->set_updated_callback(std::move(callback));
}

void adt_handler::on_patient_merged(patient_merged_callback callback) {
    pimpl_->set_merged_callback(std::move(callback));
}

const adt_handler_config& adt_handler::config() const noexcept {
    return pimpl_->config();
}

void adt_handler::set_config(const adt_handler_config& config) {
    pimpl_->set_config(config);
}

std::shared_ptr<cache::patient_cache> adt_handler::cache() const noexcept {
    return pimpl_->cache();
}

adt_handler::statistics adt_handler::get_statistics() const {
    return pimpl_->get_statistics();
}

void adt_handler::reset_statistics() {
    pimpl_->reset_statistics();
}

}  // namespace pacs::bridge::hl7
