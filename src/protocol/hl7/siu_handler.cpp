/**
 * @file siu_handler.cpp
 * @brief SIU message handler implementation
 *
 * Implements the SIU (Scheduling Information Unsolicited) message handler
 * for managing Modality Worklist entries based on appointment scheduling.
 * Handles appointment creation, rescheduling, modification, and cancellation.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/26
 */

#include "pacs/bridge/protocol/hl7/siu_handler.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace pacs::bridge::hl7 {

// =============================================================================
// siu_handler::impl
// =============================================================================

class siu_handler::impl {
public:
    std::shared_ptr<pacs_adapter::mwl_client> mwl_client_;
    std::shared_ptr<mapping::hl7_dicom_mapper> mapper_;
    siu_handler_config config_;
    statistics stats_;

    // Callbacks
    appointment_created_callback on_created_;
    appointment_updated_callback on_updated_;
    appointment_cancelled_callback on_cancelled_;
    status_changed_callback on_status_changed_;

    explicit impl(std::shared_ptr<pacs_adapter::mwl_client> mwl_client)
        : mwl_client_(std::move(mwl_client)),
          mapper_(std::make_shared<mapping::hl7_dicom_mapper>()) {}

    impl(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
         const siu_handler_config& config)
        : mwl_client_(std::move(mwl_client)),
          mapper_(std::make_shared<mapping::hl7_dicom_mapper>()),
          config_(config) {}

    impl(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
         std::shared_ptr<mapping::hl7_dicom_mapper> mapper)
        : mwl_client_(std::move(mwl_client)), mapper_(std::move(mapper)) {}

    impl(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
         std::shared_ptr<mapping::hl7_dicom_mapper> mapper,
         const siu_handler_config& config)
        : mwl_client_(std::move(mwl_client)),
          mapper_(std::move(mapper)),
          config_(config) {}

    // Get appointment ID to use as key (prefer filler, fallback to placer)
    std::string get_appointment_key(const appointment_info& appt) const {
        if (!appt.filler_appointment_id.empty()) {
            return appt.filler_appointment_id;
        }
        return appt.placer_appointment_id;
    }

    // Generate Study Instance UID if configured
    std::string generate_study_uid() const {
        if (config_.auto_generate_study_uid) {
            return mapping::hl7_dicom_mapper::generate_uid(
                config_.study_uid_root);
        }
        return "";
    }

    // Validate appointment data
    std::vector<std::string> validate_appointment(
        const appointment_info& appt) const {
        std::vector<std::string> errors;

        for (const auto& field : config_.required_fields) {
            if (field == "patient_id" && appt.patient_id.empty()) {
                errors.push_back("Patient ID is required");
            } else if (field == "patient_name" && appt.patient_name.empty()) {
                errors.push_back("Patient Name is required");
            } else if (field == "appointment_id" &&
                       appt.placer_appointment_id.empty() &&
                       appt.filler_appointment_id.empty()) {
                errors.push_back("Appointment ID is required");
            }
        }

        return errors;
    }

    // Convert appointment to MWL item
    mapping::mwl_item appointment_to_mwl(const appointment_info& appt) const {
        mapping::mwl_item mwl;

        // Patient information
        mwl.patient.patient_id = appt.patient_id;
        mwl.patient.patient_name = appt.patient_name;

        // Requested procedure
        mwl.requested_procedure.requested_procedure_id =
            get_appointment_key(appt);
        mwl.requested_procedure.requested_procedure_description =
            appt.procedure_description;

        // Study Instance UID
        if (!appt.study_instance_uid.empty()) {
            mwl.requested_procedure.study_instance_uid = appt.study_instance_uid;
        } else {
            mwl.requested_procedure.study_instance_uid = generate_study_uid();
        }

        // Scheduled procedure step
        mapping::dicom_scheduled_procedure_step sps;
        sps.scheduled_step_id = get_appointment_key(appt);

        // Use scheduled datetime or requested datetime
        // Parse datetime to separate date and time components
        std::string datetime_str;
        if (!appt.scheduled_datetime.empty()) {
            datetime_str = appt.scheduled_datetime;
        } else if (!appt.requested_start_datetime.empty()) {
            datetime_str = appt.requested_start_datetime;
        } else if (!appt.ais_start_datetime.empty()) {
            datetime_str = appt.ais_start_datetime;
        }

        // Split datetime into date (YYYYMMDD) and time (HHMMSS) components
        if (datetime_str.length() >= 8) {
            sps.scheduled_start_date = datetime_str.substr(0, 8);
            if (datetime_str.length() >= 14) {
                sps.scheduled_start_time = datetime_str.substr(8, 6);
            } else if (datetime_str.length() >= 12) {
                sps.scheduled_start_time = datetime_str.substr(8, 4) + "00";
            }
        }

        // Modality from resource type or default
        if (!appt.resource_type.empty()) {
            sps.modality = appt.resource_type;
        }

        // Procedure code - use description field
        if (!appt.procedure_description.empty()) {
            sps.scheduled_step_description = appt.procedure_description;
        } else if (!appt.procedure_code.empty()) {
            sps.scheduled_step_description = appt.procedure_code;
        }

        // Status
        sps.scheduled_step_status = to_mwl_status(appt.status);

        mwl.scheduled_steps.push_back(std::move(sps));

        return mwl;
    }

    // Update statistics for processing time
    void update_processing_time(std::chrono::milliseconds elapsed) {
        double total_time =
            stats_.avg_processing_ms * static_cast<double>(stats_.total_processed);
        total_time += static_cast<double>(elapsed.count());
        stats_.avg_processing_ms =
            total_time / static_cast<double>(stats_.total_processed + 1);
    }
};

// =============================================================================
// siu_handler Implementation
// =============================================================================

siu_handler::siu_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client)
    : pimpl_(std::make_unique<impl>(std::move(mwl_client))) {}

siu_handler::siu_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                         const siu_handler_config& config)
    : pimpl_(std::make_unique<impl>(std::move(mwl_client), config)) {}

siu_handler::siu_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                         std::shared_ptr<mapping::hl7_dicom_mapper> mapper)
    : pimpl_(std::make_unique<impl>(std::move(mwl_client), std::move(mapper))) {
}

siu_handler::siu_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                         std::shared_ptr<mapping::hl7_dicom_mapper> mapper,
                         const siu_handler_config& config)
    : pimpl_(std::make_unique<impl>(std::move(mwl_client), std::move(mapper),
                                    config)) {}

siu_handler::~siu_handler() = default;

siu_handler::siu_handler(siu_handler&&) noexcept = default;
siu_handler& siu_handler::operator=(siu_handler&&) noexcept = default;

std::expected<siu_result, siu_error> siu_handler::handle(
    const hl7_message& message) {
    auto start = std::chrono::steady_clock::now();
    pimpl_->stats_.total_processed++;

    // Validate message type
    auto header = message.header();
    if (header.type != message_type::SIU) {
        pimpl_->stats_.failure_count++;
        return std::unexpected(siu_error::not_siu_message);
    }

    // Extract appointment information
    auto appt_result = extract_appointment_info(message);
    if (!appt_result) {
        pimpl_->stats_.failure_count++;
        return std::unexpected(appt_result.error());
    }

    const auto& appt = *appt_result;

    // Validate if configured
    if (pimpl_->config_.validate_appointment_data) {
        auto errors = pimpl_->validate_appointment(appt);
        if (!errors.empty()) {
            pimpl_->stats_.failure_count++;
            return std::unexpected(siu_error::invalid_appointment_data);
        }
    }

    // Dispatch to appropriate handler based on trigger event
    std::expected<siu_result, siu_error> result;
    switch (appt.trigger) {
        case siu_trigger_event::s12_new_appointment:
            pimpl_->stats_.s12_count++;
            result = handle_s12(message);
            break;

        case siu_trigger_event::s13_rescheduled:
            pimpl_->stats_.s13_count++;
            result = handle_s13(message);
            break;

        case siu_trigger_event::s14_modification:
            pimpl_->stats_.s14_count++;
            result = handle_s14(message);
            break;

        case siu_trigger_event::s15_cancellation:
            pimpl_->stats_.s15_count++;
            result = handle_s15(message);
            break;

        default:
            pimpl_->stats_.failure_count++;
            return std::unexpected(siu_error::unsupported_trigger_event);
    }

    // Update statistics
    auto end = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    pimpl_->update_processing_time(elapsed);

    if (result) {
        pimpl_->stats_.success_count++;
    } else {
        pimpl_->stats_.failure_count++;
    }

    return result;
}

bool siu_handler::can_handle(const hl7_message& message) const noexcept {
    auto header = message.header();
    if (header.type != message_type::SIU) {
        return false;
    }

    // Check for required segments: SCH and PID at minimum
    return message.has_segment("SCH") && message.has_segment("PID");
}

std::vector<std::string> siu_handler::supported_triggers() const {
    return {"S12", "S13", "S14", "S15"};
}

std::expected<siu_result, siu_error> siu_handler::handle_s12(
    const hl7_message& message) {
    // Extract appointment info
    auto appt_result = extract_appointment_info(message);
    if (!appt_result) {
        return std::unexpected(appt_result.error());
    }
    const auto& appt = *appt_result;

    // Get appointment key
    std::string appt_key = pimpl_->get_appointment_key(appt);

    // Convert to MWL item
    auto mwl = pimpl_->appointment_to_mwl(appt);

    // Check if entry already exists
    bool exists = pimpl_->mwl_client_->exists(appt_key);

    if (exists) {
        if (pimpl_->config_.allow_s12_update) {
            // Update existing entry
            auto update_result = pimpl_->mwl_client_->update_entry(appt_key, mwl);
            if (!update_result) {
                return std::unexpected(siu_error::mwl_update_failed);
            }
            pimpl_->stats_.entries_updated++;
        } else {
            return std::unexpected(siu_error::duplicate_appointment);
        }
    } else {
        // Create new entry
        auto add_result = pimpl_->mwl_client_->add_entry(mwl);
        if (!add_result) {
            return std::unexpected(siu_error::mwl_create_failed);
        }
        pimpl_->stats_.entries_created++;
    }

    // Invoke callback
    if (pimpl_->on_created_) {
        pimpl_->on_created_(appt, mwl);
    }

    // Build result
    siu_result result;
    result.success = true;
    result.trigger = siu_trigger_event::s12_new_appointment;
    result.status = appt.status;
    result.placer_appointment_id = appt.placer_appointment_id;
    result.filler_appointment_id = appt.filler_appointment_id;
    result.patient_id = appt.patient_id;
    result.scheduled_datetime = appt.scheduled_datetime;
    result.study_instance_uid = mwl.requested_procedure.study_instance_uid;
    result.description = exists ? "Appointment updated (S12 with existing entry)"
                                : "New appointment created";
    result.ack_message = generate_ack(message, true);

    return result;
}

std::expected<siu_result, siu_error> siu_handler::handle_s13(
    const hl7_message& message) {
    // Extract appointment info
    auto appt_result = extract_appointment_info(message);
    if (!appt_result) {
        return std::unexpected(appt_result.error());
    }
    const auto& appt = *appt_result;

    // Get appointment key
    std::string appt_key = pimpl_->get_appointment_key(appt);

    // Check if entry exists
    bool exists = pimpl_->mwl_client_->exists(appt_key);

    if (!exists) {
        if (pimpl_->config_.allow_reschedule_create) {
            // Create new entry (delegate to S12 handler logic)
            return handle_s12(message);
        }
        return std::unexpected(siu_error::appointment_not_found);
    }

    // Get existing entry for callback
    auto existing_result = pimpl_->mwl_client_->get_entry(appt_key);
    mapping::mwl_item old_mwl;
    if (existing_result) {
        old_mwl = *existing_result;
    }

    // Convert to MWL item with updated timing
    auto mwl = pimpl_->appointment_to_mwl(appt);

    // Preserve Study Instance UID from existing entry
    if (mwl.requested_procedure.study_instance_uid.empty() &&
        !old_mwl.requested_procedure.study_instance_uid.empty()) {
        mwl.requested_procedure.study_instance_uid =
            old_mwl.requested_procedure.study_instance_uid;
    }

    // Update entry
    auto update_result = pimpl_->mwl_client_->update_entry(appt_key, mwl);
    if (!update_result) {
        return std::unexpected(siu_error::mwl_update_failed);
    }
    pimpl_->stats_.entries_updated++;

    // Invoke callback
    if (pimpl_->on_updated_) {
        pimpl_->on_updated_(appt, old_mwl, mwl);
    }

    // Build result
    siu_result result;
    result.success = true;
    result.trigger = siu_trigger_event::s13_rescheduled;
    result.status = appt.status;
    result.placer_appointment_id = appt.placer_appointment_id;
    result.filler_appointment_id = appt.filler_appointment_id;
    result.patient_id = appt.patient_id;
    result.scheduled_datetime = appt.scheduled_datetime;
    result.study_instance_uid = mwl.requested_procedure.study_instance_uid;
    result.description = "Appointment rescheduled";
    result.ack_message = generate_ack(message, true);

    return result;
}

std::expected<siu_result, siu_error> siu_handler::handle_s14(
    const hl7_message& message) {
    // Extract appointment info
    auto appt_result = extract_appointment_info(message);
    if (!appt_result) {
        return std::unexpected(appt_result.error());
    }
    const auto& appt = *appt_result;

    // Get appointment key
    std::string appt_key = pimpl_->get_appointment_key(appt);

    // Check if entry exists
    auto existing_result = pimpl_->mwl_client_->get_entry(appt_key);
    if (!existing_result) {
        return std::unexpected(siu_error::appointment_not_found);
    }
    auto old_mwl = *existing_result;

    // Convert to MWL item
    auto mwl = pimpl_->appointment_to_mwl(appt);

    // Preserve Study Instance UID from existing entry
    if (mwl.requested_procedure.study_instance_uid.empty() &&
        !old_mwl.requested_procedure.study_instance_uid.empty()) {
        mwl.requested_procedure.study_instance_uid =
            old_mwl.requested_procedure.study_instance_uid;
    }

    // Update entry
    auto update_result = pimpl_->mwl_client_->update_entry(appt_key, mwl);
    if (!update_result) {
        return std::unexpected(siu_error::mwl_update_failed);
    }
    pimpl_->stats_.entries_updated++;

    // Invoke callback
    if (pimpl_->on_updated_) {
        pimpl_->on_updated_(appt, old_mwl, mwl);
    }

    // Build result
    siu_result result;
    result.success = true;
    result.trigger = siu_trigger_event::s14_modification;
    result.status = appt.status;
    result.placer_appointment_id = appt.placer_appointment_id;
    result.filler_appointment_id = appt.filler_appointment_id;
    result.patient_id = appt.patient_id;
    result.scheduled_datetime = appt.scheduled_datetime;
    result.study_instance_uid = mwl.requested_procedure.study_instance_uid;
    result.description = "Appointment modified";
    result.ack_message = generate_ack(message, true);

    return result;
}

std::expected<siu_result, siu_error> siu_handler::handle_s15(
    const hl7_message& message) {
    // Extract appointment info
    auto appt_result = extract_appointment_info(message);
    if (!appt_result) {
        return std::unexpected(appt_result.error());
    }
    const auto& appt = *appt_result;

    // Get appointment key
    std::string appt_key = pimpl_->get_appointment_key(appt);

    // Check if entry exists
    if (!pimpl_->mwl_client_->exists(appt_key)) {
        return std::unexpected(siu_error::appointment_not_found);
    }

    // Cancel (remove) entry
    auto cancel_result = pimpl_->mwl_client_->cancel_entry(appt_key);
    if (!cancel_result) {
        return std::unexpected(siu_error::mwl_cancel_failed);
    }
    pimpl_->stats_.entries_cancelled++;

    // Invoke callback
    if (pimpl_->on_cancelled_) {
        pimpl_->on_cancelled_(appt_key, "Appointment cancelled via SIU^S15");
    }

    // Build result
    siu_result result;
    result.success = true;
    result.trigger = siu_trigger_event::s15_cancellation;
    result.status = appointment_status::cancelled;
    result.placer_appointment_id = appt.placer_appointment_id;
    result.filler_appointment_id = appt.filler_appointment_id;
    result.patient_id = appt.patient_id;
    result.description = "Appointment cancelled";
    result.ack_message = generate_ack(message, true);

    return result;
}

std::expected<appointment_info, siu_error> siu_handler::extract_appointment_info(
    const hl7_message& message) const {
    appointment_info info;
    auto header = message.header();
    info.message_control_id = header.message_control_id;

    // Get trigger event from MSH-9.2
    info.trigger = parse_siu_trigger_event(header.trigger_event);
    if (info.trigger == siu_trigger_event::unknown) {
        return std::unexpected(siu_error::unsupported_trigger_event);
    }

    // Extract SCH segment
    const auto* sch = message.segment("SCH");
    if (!sch) {
        return std::unexpected(siu_error::missing_required_field);
    }

    // Placer Appointment ID (SCH-1)
    info.placer_appointment_id = std::string(sch->field(1).component(1).value());

    // Filler Appointment ID (SCH-2)
    info.filler_appointment_id = std::string(sch->field(2).component(1).value());

    // Appointment Timing Quantity (SCH-11)
    // Format: duration^units^start_datetime^end_datetime
    auto sch11 = sch->field(11);
    info.duration = std::string(sch11.component(1).value());
    // Start datetime is in component 4 for timing quantity
    info.scheduled_datetime = std::string(sch11.component(4).value());

    // Requested Start Date/Time (SCH-16)
    info.requested_start_datetime = std::string(sch->field_value(16));

    // Filler Status Code (SCH-25)
    info.status = parse_appointment_status(sch->field_value(25));

    // Extract PID segment
    const auto* pid = message.segment("PID");
    if (!pid) {
        return std::unexpected(siu_error::missing_required_field);
    }

    // Patient ID (PID-3)
    info.patient_id = std::string(pid->field(3).component(1).value());

    // Patient Name (PID-5)
    std::string family = std::string(pid->field(5).component(1).value());
    std::string given = std::string(pid->field(5).component(2).value());
    info.patient_name = family;
    if (!given.empty()) {
        info.patient_name += "^" + given;
    }

    // Extract RGS segment (Resource Group Segment) if present
    const auto* rgs = message.segment("RGS");
    if (rgs) {
        // RGS-3 contains resource group ID
        // (used for grouping related resources)
    }

    // Extract AIS segment (Appointment Information - Service) if present
    const auto* ais = message.segment("AIS");
    if (ais) {
        // Universal Service ID (AIS-3)
        info.procedure_code = std::string(ais->field(3).component(1).value());
        info.procedure_description =
            std::string(ais->field(3).component(2).value());

        // Start Date/Time (AIS-4)
        info.ais_start_datetime = std::string(ais->field_value(4));

        // Resource type - may be in AIS or derived from procedure
        info.resource_type = std::string(ais->field(3).component(3).value());
    }

    // Generate Study Instance UID if configured
    if (pimpl_->config_.auto_generate_study_uid) {
        info.study_instance_uid = mapping::hl7_dicom_mapper::generate_uid(
            pimpl_->config_.study_uid_root);
    }

    return info;
}

hl7_message siu_handler::generate_ack(const hl7_message& original, bool success,
                                       std::string_view error_code,
                                       std::string_view error_message) const {
    hl7_message ack;
    auto orig_header = original.header();

    // Build MSH
    auto& msh = ack.add_segment("MSH");
    msh.set_field(1, "|");
    msh.set_field(2, "^~\\&");
    msh.set_field(3, pimpl_->config_.ack_sending_application);
    msh.set_field(4, pimpl_->config_.ack_sending_facility);
    msh.set_field(5, orig_header.sending_application);
    msh.set_field(6, orig_header.sending_facility);

    // DateTime
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
#if defined(_WIN32)
    localtime_s(&tm_now, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_now);
#endif
    std::ostringstream dt_oss;
    dt_oss << std::put_time(&tm_now, "%Y%m%d%H%M%S");
    msh.set_field(7, dt_oss.str());

    // Message Type: ACK^S12 (or appropriate trigger)
    msh.set_value("9.1", "ACK");
    msh.set_value("9.2", orig_header.trigger_event);

    // Message Control ID
    msh.set_field(10, message_id_generator::generate());

    // Processing ID
    msh.set_field(11, orig_header.processing_id);

    // Version
    msh.set_field(12, orig_header.version_id);

    // Build MSA
    auto& msa = ack.add_segment("MSA");
    msa.set_field(1, success ? "AA" : std::string(error_code));
    msa.set_field(2, orig_header.message_control_id);

    if (!success && !error_message.empty()) {
        msa.set_field(3, std::string(error_message));
    }

    // Add ERR segment if failed and detailed ACK is enabled
    if (!success && pimpl_->config_.detailed_ack && !error_message.empty()) {
        auto& err = ack.add_segment("ERR");
        err.set_field(1, "SCH^1");
        err.set_field(2, std::string(error_code));
        err.set_field(3, std::string(error_message));
    }

    return ack;
}

// =============================================================================
// Callback Registration
// =============================================================================

void siu_handler::on_appointment_created(appointment_created_callback callback) {
    pimpl_->on_created_ = std::move(callback);
}

void siu_handler::on_appointment_updated(appointment_updated_callback callback) {
    pimpl_->on_updated_ = std::move(callback);
}

void siu_handler::on_appointment_cancelled(
    appointment_cancelled_callback callback) {
    pimpl_->on_cancelled_ = std::move(callback);
}

void siu_handler::on_status_changed(status_changed_callback callback) {
    pimpl_->on_status_changed_ = std::move(callback);
}

// =============================================================================
// Configuration
// =============================================================================

const siu_handler_config& siu_handler::config() const noexcept {
    return pimpl_->config_;
}

void siu_handler::set_config(const siu_handler_config& config) {
    pimpl_->config_ = config;
}

std::shared_ptr<pacs_adapter::mwl_client> siu_handler::mwl_client()
    const noexcept {
    return pimpl_->mwl_client_;
}

std::shared_ptr<mapping::hl7_dicom_mapper> siu_handler::mapper() const noexcept {
    return pimpl_->mapper_;
}

// =============================================================================
// Statistics
// =============================================================================

siu_handler::statistics siu_handler::get_statistics() const {
    return pimpl_->stats_;
}

void siu_handler::reset_statistics() {
    pimpl_->stats_ = statistics{};
}

}  // namespace pacs::bridge::hl7
