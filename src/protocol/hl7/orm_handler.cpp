/**
 * @file orm_handler.cpp
 * @brief ORM message handler implementation
 *
 * Implements the ORM^O01 message handler for managing Modality Worklist
 * entries. Handles order creation, modification, cancellation, and
 * status changes with integration to pacs_system via MWL client.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/15
 */

#include "pacs/bridge/protocol/hl7/orm_handler.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/monitoring/bridge_metrics.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace pacs::bridge::hl7 {

// =============================================================================
// orm_handler::impl
// =============================================================================

class orm_handler::impl {
public:
    std::shared_ptr<pacs_adapter::mwl_client> mwl_client_;
    std::shared_ptr<mapping::hl7_dicom_mapper> mapper_;
    orm_handler_config config_;
    statistics stats_;

    // Callbacks
    order_created_callback on_created_;
    order_updated_callback on_updated_;
    order_cancelled_callback on_cancelled_;
    status_changed_callback on_status_changed_;

    explicit impl(std::shared_ptr<pacs_adapter::mwl_client> mwl_client)
        : mwl_client_(std::move(mwl_client)),
          mapper_(std::make_shared<mapping::hl7_dicom_mapper>()) {}

    impl(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
         const orm_handler_config& config)
        : mwl_client_(std::move(mwl_client)),
          mapper_(std::make_shared<mapping::hl7_dicom_mapper>()),
          config_(config) {}

    impl(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
         std::shared_ptr<mapping::hl7_dicom_mapper> mapper)
        : mwl_client_(std::move(mwl_client)), mapper_(std::move(mapper)) {}

    impl(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
         std::shared_ptr<mapping::hl7_dicom_mapper> mapper,
         const orm_handler_config& config)
        : mwl_client_(std::move(mwl_client)),
          mapper_(std::move(mapper)),
          config_(config) {}

    // Extract ZDS segment for Study Instance UID (if present)
    std::string extract_study_uid(const hl7_message& message) const {
        const auto* zds = message.segment("ZDS");
        if (zds) {
            // ZDS-1 contains pre-assigned Study Instance UID
            std::string_view uid = zds->field_value(1);
            if (!uid.empty()) {
                return std::string(uid);
            }
        }

        // Auto-generate if configured and not found
        if (config_.auto_generate_study_uid) {
            return mapping::hl7_dicom_mapper::generate_uid(
                config_.study_uid_root);
        }

        return "";
    }

    // Validate order data
    std::vector<std::string> validate_order(const order_info& order) const {
        std::vector<std::string> errors;

        for (const auto& field : config_.required_fields) {
            if (field == "patient_id" && order.patient_id.empty()) {
                errors.push_back("Patient ID is required");
            } else if (field == "patient_name" && order.patient_name.empty()) {
                errors.push_back("Patient Name is required");
            } else if (field == "accession_number" &&
                       order.filler_order_number.empty()) {
                errors.push_back("Accession Number is required");
            }
        }

        return errors;
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
// orm_handler Implementation
// =============================================================================

orm_handler::orm_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client)
    : pimpl_(std::make_unique<impl>(std::move(mwl_client))) {}

orm_handler::orm_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                         const orm_handler_config& config)
    : pimpl_(std::make_unique<impl>(std::move(mwl_client), config)) {}

orm_handler::orm_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                         std::shared_ptr<mapping::hl7_dicom_mapper> mapper)
    : pimpl_(std::make_unique<impl>(std::move(mwl_client), std::move(mapper))) {
}

orm_handler::orm_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                         std::shared_ptr<mapping::hl7_dicom_mapper> mapper,
                         const orm_handler_config& config)
    : pimpl_(std::make_unique<impl>(std::move(mwl_client), std::move(mapper),
                                    config)) {}

orm_handler::~orm_handler() = default;

orm_handler::orm_handler(orm_handler&&) noexcept = default;
orm_handler& orm_handler::operator=(orm_handler&&) noexcept = default;

Result<orm_result> orm_handler::handle(
    const hl7_message& message) {
    // Record message received metric
    auto& metrics = monitoring::bridge_metrics_collector::instance();
    metrics.record_hl7_message_received("ORM");

    auto start = std::chrono::steady_clock::now();
    pimpl_->stats_.total_processed++;

    // Validate message type
    auto header = message.header();
    if (header.type != message_type::ORM) {
        pimpl_->stats_.failure_count++;
        metrics.record_hl7_error("ORM", "not_orm_message");
        return to_error_info(orm_error::not_orm_message);
    }

    // Extract order information
    auto order_info_result = extract_order_info(message);
    if (!order_info_result.is_ok()) {
        pimpl_->stats_.failure_count++;
        metrics.record_hl7_error("ORM", "extraction_failed");
        return order_info_result.error();
    }

    const auto& order = order_info_result.value();

    // Validate if configured
    if (pimpl_->config_.validate_order_data) {
        auto errors = pimpl_->validate_order(order);
        if (!errors.empty()) {
            pimpl_->stats_.failure_count++;
            metrics.record_hl7_error("ORM", "invalid_order_data");
            return to_error_info(orm_error::invalid_order_data);
        }
    }

    // Dispatch to appropriate handler based on order control
    Result<orm_result> result = Result<orm_result>::uninitialized();
    switch (order.control) {
        case order_control::new_order:
            pimpl_->stats_.nw_count++;
            result = handle_new_order(message);
            break;

        case order_control::change_order:
            pimpl_->stats_.xo_count++;
            result = handle_change_order(message);
            break;

        case order_control::cancel_order:
            pimpl_->stats_.ca_count++;
            result = handle_cancel_order(message);
            break;

        case order_control::discontinue_order:
            pimpl_->stats_.dc_count++;
            result = handle_discontinue_order(message);
            break;

        case order_control::status_change:
            pimpl_->stats_.sc_count++;
            result = handle_status_change(message);
            break;

        default:
            pimpl_->stats_.failure_count++;
            metrics.record_hl7_error("ORM", "unsupported_order_control");
            return to_error_info(orm_error::unsupported_order_control);
    }

    // Update statistics and record processing duration
    auto end = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    pimpl_->update_processing_time(elapsed);

    // Record processing duration metric (convert to nanoseconds)
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - start);
    metrics.record_hl7_processing_duration("ORM", duration_ns);

    if (result.is_ok()) {
        pimpl_->stats_.success_count++;
        // Record ACK sent
        metrics.record_hl7_message_sent("ORM_ACK");
    } else {
        pimpl_->stats_.failure_count++;
        metrics.record_hl7_error("ORM", "processing_failed");
    }

    return result;
}

bool orm_handler::can_handle(const hl7_message& message) const noexcept {
    auto header = message.header();
    if (header.type != message_type::ORM) {
        return false;
    }

    // Check for required segments
    return message.has_segment("ORC") && message.has_segment("OBR") &&
           message.has_segment("PID");
}

std::vector<std::string> orm_handler::supported_controls() const {
    return {"NW", "XO", "CA", "DC", "SC"};
}

Result<orm_result> orm_handler::handle_new_order(
    const hl7_message& message) {
    // Extract order info
    auto order_result = extract_order_info(message);
    if (!order_result.is_ok()) {
        return order_result.error();
    }
    const auto& order = order_result.value();

    // Convert to MWL item using mapper
    auto mwl_result = pimpl_->mapper_->to_mwl(message);
    if (!mwl_result) {
        return to_error_info(orm_error::invalid_order_data);
    }
    auto& mwl = *mwl_result;

    // Set Study Instance UID from ZDS or generate
    if (!order.study_instance_uid.empty()) {
        mwl.requested_procedure.study_instance_uid = order.study_instance_uid;
    }

    // Set SPS status based on order status
    if (!mwl.scheduled_steps.empty()) {
        mwl.scheduled_steps[0].scheduled_step_status =
            to_mwl_status(order.status);
    }

    // Check if entry already exists
    bool exists = pimpl_->mwl_client_->exists(order.filler_order_number);

    if (exists) {
        if (pimpl_->config_.allow_nw_update) {
            // Update existing entry
            auto update_result = pimpl_->mwl_client_->update_entry(
                order.filler_order_number, mwl);
            if (!update_result) {
                return to_error_info(orm_error::mwl_update_failed);
            }
            pimpl_->stats_.entries_updated++;
        } else {
            return to_error_info(orm_error::duplicate_order);
        }
    } else {
        // Create new entry
        auto add_result = pimpl_->mwl_client_->add_entry(mwl);
        if (!add_result) {
            return to_error_info(orm_error::mwl_create_failed);
        }
        pimpl_->stats_.entries_created++;
    }

    // Invoke callback
    if (pimpl_->on_created_) {
        pimpl_->on_created_(order, mwl);
    }

    // Build result
    orm_result result;
    result.success = true;
    result.control = order_control::new_order;
    result.status = order.status;
    result.accession_number = order.filler_order_number;
    result.patient_id = order.patient_id;
    result.placer_order_number = order.placer_order_number;
    result.filler_order_number = order.filler_order_number;
    result.study_instance_uid = mwl.requested_procedure.study_instance_uid;
    result.description = exists ? "Order updated (NW with existing entry)"
                                : "New order created";
    result.ack_message = generate_ack(message, true);

    return result;
}

Result<orm_result> orm_handler::handle_change_order(
    const hl7_message& message) {
    // Extract order info
    auto order_result = extract_order_info(message);
    if (!order_result.is_ok()) {
        return order_result.error();
    }
    const auto& order = order_result.value();

    // Check if entry exists
    bool exists = pimpl_->mwl_client_->exists(order.filler_order_number);

    if (!exists) {
        if (pimpl_->config_.allow_xo_create) {
            // Create new entry (delegate to new_order handler logic)
            return handle_new_order(message);
        }
        return to_error_info(orm_error::order_not_found);
    }

    // Get existing entry for callback
    auto existing_result =
        pimpl_->mwl_client_->get_entry(order.filler_order_number);
    mapping::mwl_item old_mwl;
    if (existing_result) {
        old_mwl = *existing_result;
    }

    // Convert to MWL item
    auto mwl_result = pimpl_->mapper_->to_mwl(message);
    if (!mwl_result) {
        return to_error_info(orm_error::invalid_order_data);
    }
    auto& mwl = *mwl_result;

    // Preserve Study Instance UID from existing entry if not provided
    if (mwl.requested_procedure.study_instance_uid.empty() &&
        !old_mwl.requested_procedure.study_instance_uid.empty()) {
        mwl.requested_procedure.study_instance_uid =
            old_mwl.requested_procedure.study_instance_uid;
    }

    // Update entry
    auto update_result =
        pimpl_->mwl_client_->update_entry(order.filler_order_number, mwl);
    if (!update_result) {
        return to_error_info(orm_error::mwl_update_failed);
    }
    pimpl_->stats_.entries_updated++;

    // Invoke callback
    if (pimpl_->on_updated_) {
        pimpl_->on_updated_(order, old_mwl, mwl);
    }

    // Build result
    orm_result result;
    result.success = true;
    result.control = order_control::change_order;
    result.status = order.status;
    result.accession_number = order.filler_order_number;
    result.patient_id = order.patient_id;
    result.placer_order_number = order.placer_order_number;
    result.filler_order_number = order.filler_order_number;
    result.study_instance_uid = mwl.requested_procedure.study_instance_uid;
    result.description = "Order updated";
    result.ack_message = generate_ack(message, true);

    return result;
}

Result<orm_result> orm_handler::handle_cancel_order(
    const hl7_message& message) {
    // Extract order info
    auto order_result = extract_order_info(message);
    if (!order_result.is_ok()) {
        return order_result.error();
    }
    const auto& order = order_result.value();

    // Check if entry exists
    if (!pimpl_->mwl_client_->exists(order.filler_order_number)) {
        return to_error_info(orm_error::order_not_found);
    }

    // Cancel (remove) entry
    auto cancel_result =
        pimpl_->mwl_client_->cancel_entry(order.filler_order_number);
    if (!cancel_result) {
        return to_error_info(orm_error::mwl_cancel_failed);
    }
    pimpl_->stats_.entries_cancelled++;

    // Invoke callback
    if (pimpl_->on_cancelled_) {
        pimpl_->on_cancelled_(order.filler_order_number, "Order cancelled");
    }

    // Build result
    orm_result result;
    result.success = true;
    result.control = order_control::cancel_order;
    result.status = order_status::cancelled;
    result.accession_number = order.filler_order_number;
    result.patient_id = order.patient_id;
    result.placer_order_number = order.placer_order_number;
    result.filler_order_number = order.filler_order_number;
    result.description = "Order cancelled";
    result.ack_message = generate_ack(message, true);

    return result;
}

Result<orm_result> orm_handler::handle_discontinue_order(
    const hl7_message& message) {
    // Extract order info
    auto order_result = extract_order_info(message);
    if (!order_result.is_ok()) {
        return order_result.error();
    }
    const auto& order = order_result.value();

    // Check if entry exists
    auto existing_result =
        pimpl_->mwl_client_->get_entry(order.filler_order_number);
    if (!existing_result) {
        return to_error_info(orm_error::order_not_found);
    }
    auto mwl = *existing_result;

    // Update status to DISCONTINUED
    if (!mwl.scheduled_steps.empty()) {
        mwl.scheduled_steps[0].scheduled_step_status = "DISCONTINUED";
    }

    // Update entry
    auto update_result =
        pimpl_->mwl_client_->update_entry(order.filler_order_number, mwl);
    if (!update_result) {
        return to_error_info(orm_error::mwl_update_failed);
    }
    pimpl_->stats_.entries_updated++;

    // Invoke callback
    if (pimpl_->on_status_changed_) {
        pimpl_->on_status_changed_(order.filler_order_number, order.status,
                                   order_status::discontinued);
    }

    // Build result
    orm_result result;
    result.success = true;
    result.control = order_control::discontinue_order;
    result.status = order_status::discontinued;
    result.accession_number = order.filler_order_number;
    result.patient_id = order.patient_id;
    result.placer_order_number = order.placer_order_number;
    result.filler_order_number = order.filler_order_number;
    result.study_instance_uid = mwl.requested_procedure.study_instance_uid;
    result.description = "Order discontinued";
    result.ack_message = generate_ack(message, true);

    return result;
}

Result<orm_result> orm_handler::handle_status_change(
    const hl7_message& message) {
    // Extract order info
    auto order_result = extract_order_info(message);
    if (!order_result.is_ok()) {
        return order_result.error();
    }
    const auto& order = order_result.value();

    // Check if entry exists
    auto existing_result =
        pimpl_->mwl_client_->get_entry(order.filler_order_number);
    if (!existing_result) {
        return to_error_info(orm_error::order_not_found);
    }
    auto mwl = *existing_result;

    // Determine old status
    order_status old_status = order_status::unknown;
    if (!mwl.scheduled_steps.empty()) {
        const auto& current_status = mwl.scheduled_steps[0].scheduled_step_status;
        if (current_status == "SCHEDULED") old_status = order_status::scheduled;
        else if (current_status == "STARTED")
            old_status = order_status::in_progress;
        else if (current_status == "COMPLETED")
            old_status = order_status::completed;
        else if (current_status == "DISCONTINUED")
            old_status = order_status::discontinued;
    }

    // Update status
    if (!mwl.scheduled_steps.empty()) {
        mwl.scheduled_steps[0].scheduled_step_status =
            to_mwl_status(order.status);
    }

    // Update entry
    auto update_result =
        pimpl_->mwl_client_->update_entry(order.filler_order_number, mwl);
    if (!update_result) {
        return to_error_info(orm_error::mwl_update_failed);
    }
    pimpl_->stats_.entries_updated++;

    // Invoke callback
    if (pimpl_->on_status_changed_) {
        pimpl_->on_status_changed_(order.filler_order_number, old_status,
                                   order.status);
    }

    // Build result
    orm_result result;
    result.success = true;
    result.control = order_control::status_change;
    result.status = order.status;
    result.accession_number = order.filler_order_number;
    result.patient_id = order.patient_id;
    result.placer_order_number = order.placer_order_number;
    result.filler_order_number = order.filler_order_number;
    result.study_instance_uid = mwl.requested_procedure.study_instance_uid;
    result.description =
        "Order status changed to " + std::string(to_mwl_status(order.status));
    result.ack_message = generate_ack(message, true);

    return result;
}

Result<order_info> orm_handler::extract_order_info(
    const hl7_message& message) const {
    order_info info;
    auto header = message.header();
    info.message_control_id = header.message_control_id;

    // Extract ORC segment
    const auto* orc = message.segment("ORC");
    if (!orc) {
        return to_error_info(orm_error::missing_required_field);
    }

    // Order Control (ORC-1)
    info.control = parse_order_control(orc->field_value(1));
    if (info.control == order_control::unknown) {
        return to_error_info(orm_error::unsupported_order_control);
    }

    // Placer Order Number (ORC-2)
    info.placer_order_number = std::string(orc->field(2).component(1).value());

    // Filler Order Number / Accession Number (ORC-3)
    info.filler_order_number = std::string(orc->field(3).component(1).value());
    if (info.filler_order_number.empty()) {
        // Fallback to placer order number
        info.filler_order_number = info.placer_order_number;
    }

    // Order Status (ORC-5)
    info.status = parse_order_status(orc->field_value(5));

    // Ordering Provider (ORC-12)
    info.ordering_provider = std::string(orc->field(12).component(2).value());
    if (!orc->field(12).component(3).value().empty()) {
        info.ordering_provider +=
            "^" + std::string(orc->field(12).component(3).value());
    }

    // Extract PID segment
    const auto* pid = message.segment("PID");
    if (!pid) {
        return to_error_info(orm_error::missing_required_field);
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

    // Extract OBR segment
    const auto* obr = message.segment("OBR");
    if (!obr) {
        return to_error_info(orm_error::missing_required_field);
    }

    // Procedure Code (OBR-4)
    info.procedure_code = std::string(obr->field(4).component(1).value());
    info.procedure_description = std::string(obr->field(4).component(2).value());

    // Scheduled DateTime (OBR-7)
    info.scheduled_datetime = std::string(obr->field_value(7));

    // Modality (OBR-24)
    info.modality = std::string(obr->field_value(24));

    // Study Instance UID from ZDS segment
    info.study_instance_uid = pimpl_->extract_study_uid(message);

    return info;
}

hl7_message orm_handler::generate_ack(const hl7_message& original, bool success,
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

    // Message Type: ACK^O01
    msh.set_value("9.1", "ACK");
    msh.set_value("9.2", "O01");

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
        err.set_field(1, "ORC^1");
        err.set_field(2, std::string(error_code));
        err.set_field(3, std::string(error_message));
    }

    return ack;
}

// =============================================================================
// Callback Registration
// =============================================================================

void orm_handler::on_order_created(order_created_callback callback) {
    pimpl_->on_created_ = std::move(callback);
}

void orm_handler::on_order_updated(order_updated_callback callback) {
    pimpl_->on_updated_ = std::move(callback);
}

void orm_handler::on_order_cancelled(order_cancelled_callback callback) {
    pimpl_->on_cancelled_ = std::move(callback);
}

void orm_handler::on_status_changed(status_changed_callback callback) {
    pimpl_->on_status_changed_ = std::move(callback);
}

// =============================================================================
// Configuration
// =============================================================================

const orm_handler_config& orm_handler::config() const noexcept {
    return pimpl_->config_;
}

void orm_handler::set_config(const orm_handler_config& config) {
    pimpl_->config_ = config;
}

std::shared_ptr<pacs_adapter::mwl_client> orm_handler::mwl_client()
    const noexcept {
    return pimpl_->mwl_client_;
}

std::shared_ptr<mapping::hl7_dicom_mapper> orm_handler::mapper() const noexcept {
    return pimpl_->mapper_;
}

// =============================================================================
// Statistics
// =============================================================================

orm_handler::statistics orm_handler::get_statistics() const {
    return pimpl_->stats_;
}

void orm_handler::reset_statistics() {
    pimpl_->stats_ = statistics{};
}

}  // namespace pacs::bridge::hl7
