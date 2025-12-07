/**
 * @file dicom_hl7_mapper.cpp
 * @brief DICOM to HL7 mapper implementation for MPPS status notifications
 *
 * Converts DICOM MPPS datasets to HL7 v2.x ORM^O01 messages for status
 * update notifications to HIS/RIS systems. Implements the mapping rules
 * defined in IHE SWF (Scheduled Workflow) profile.
 *
 * Key implementation details:
 * - Uses hl7_builder for message construction
 * - Supports all three MPPS status transitions
 * - Handles DICOM to HL7 date/time format conversion
 * - Includes series-level information when configured
 *
 * @see include/pacs/bridge/mapping/dicom_hl7_mapper.h
 * @see https://github.com/kcenon/pacs_bridge/issues/24
 */

#include "pacs/bridge/mapping/dicom_hl7_mapper.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace pacs::bridge::mapping {

// =============================================================================
// dicom_hl7_mapper::impl
// =============================================================================

class dicom_hl7_mapper::impl {
public:
    dicom_hl7_mapper_config config_;
    std::unordered_map<std::string, transform_function> transforms_;
    mutable std::mutex mutex_;

    explicit impl(const dicom_hl7_mapper_config& config) : config_(config) {}

    /**
     * @brief Build ORM^O01 message from MPPS dataset
     */
    [[nodiscard]] std::expected<mpps_mapping_result, dicom_hl7_error>
    build_orm_message(const pacs_adapter::mpps_dataset& mpps,
                      pacs_adapter::mpps_event event) const {
        // Validate required fields
        auto validation_errors = validate_mpps_for_mapping(mpps);
        if (!validation_errors.empty() && config_.validate_before_build) {
            return std::unexpected(dicom_hl7_error::missing_required_attribute);
        }

        // Determine order control and status codes
        std::string order_control = mpps_status_to_order_control(event);
        std::string order_status = mpps_status_to_order_status(event);

        // Build the message using hl7_builder
        hl7::builder_options options;
        options.sending_application = config_.sending_application;
        options.sending_facility = config_.sending_facility;
        options.receiving_application = config_.receiving_application;
        options.receiving_facility = config_.receiving_facility;
        options.version = config_.hl7_version;
        options.processing_id = config_.processing_id;
        options.auto_generate_control_id = config_.auto_generate_control_id;
        options.auto_timestamp = true;

        auto builder = hl7::hl7_builder::create(options);

        // Set message type: ORM^O01
        builder.message_type("ORM", "O01");

        // Add PID segment (Patient Identification)
        add_pid_segment(builder, mpps);

        // Add PV1 segment (Patient Visit) - minimal
        add_pv1_segment(builder, mpps);

        // Add ORC segment (Common Order)
        add_orc_segment(builder, mpps, order_control, order_status);

        // Add OBR segment (Observation Request)
        add_obr_segment(builder, mpps, event);

        // Add OBX segments for series info if configured
        if (config_.include_series_info && !mpps.performed_series.empty()) {
            add_series_obx_segments(builder, mpps);
        }

        // Build the message
        auto build_result = builder.build();
        if (!build_result) {
            return std::unexpected(dicom_hl7_error::message_build_failed);
        }

        // Prepare result
        mpps_mapping_result result;
        result.message = std::move(*build_result);
        result.control_id = result.message.get_value("MSH.10");
        result.accession_number = mpps.accession_number;
        result.mpps_status = event;
        result.order_status = order_status;
        result.order_control = order_control;

        // Add warnings for non-critical validation issues
        for (const auto& warning : validation_errors) {
            result.warnings.push_back(warning);
        }

        return result;
    }

private:
    /**
     * @brief Add PID segment
     */
    void add_pid_segment(hl7::hl7_builder& builder,
                         const pacs_adapter::mpps_dataset& mpps) const {
        // PID-1: Set ID
        builder.set_field("PID.1", "1");

        // PID-3: Patient ID
        if (!mpps.patient_id.empty()) {
            builder.patient_id(mpps.patient_id);
        }

        // PID-5: Patient Name (convert from DICOM PN format)
        if (!mpps.patient_name.empty()) {
            auto hl7_name = dicom_hl7_mapper::dicom_name_to_hl7(mpps.patient_name);
            builder.patient_name(hl7_name);
        }
    }

    /**
     * @brief Add PV1 segment (minimal for status update)
     */
    void add_pv1_segment(hl7::hl7_builder& builder,
                         const pacs_adapter::mpps_dataset& /*mpps*/) const {
        // PV1-1: Set ID
        builder.set_field("PV1.1", "1");

        // PV1-2: Patient Class (O = Outpatient for imaging)
        builder.patient_class("O");
    }

    /**
     * @brief Add ORC segment (Common Order)
     */
    void add_orc_segment(hl7::hl7_builder& builder,
                         const pacs_adapter::mpps_dataset& mpps,
                         const std::string& order_control,
                         const std::string& order_status) const {
        // ORC-1: Order Control
        builder.order_control(order_control);

        // ORC-2: Placer Order Number (use Scheduled Procedure Step ID)
        if (!mpps.scheduled_procedure_step_id.empty()) {
            builder.placer_order_number(mpps.scheduled_procedure_step_id);
        }

        // ORC-3: Filler Order Number (use Accession Number)
        if (!mpps.accession_number.empty()) {
            builder.filler_order_number(mpps.accession_number);
        }

        // ORC-5: Order Status
        builder.order_status(order_status);

        // ORC-9: Date/Time of Transaction
        builder.set_field("ORC.9", hl7::hl7_timestamp::now().to_string());
    }

    /**
     * @brief Add OBR segment (Observation Request)
     */
    void add_obr_segment(hl7::hl7_builder& builder,
                         const pacs_adapter::mpps_dataset& mpps,
                         pacs_adapter::mpps_event event) const {
        // OBR-1: Set ID
        builder.set_field("OBR.1", "1");

        // OBR-2: Placer Order Number
        if (!mpps.scheduled_procedure_step_id.empty()) {
            builder.set_field("OBR.2", mpps.scheduled_procedure_step_id);
        }

        // OBR-3: Filler Order Number
        if (!mpps.accession_number.empty()) {
            builder.set_field("OBR.3", mpps.accession_number);
        }

        // OBR-4: Universal Service Identifier (Procedure)
        if (!mpps.performed_procedure_description.empty()) {
            builder.procedure_code("", mpps.performed_procedure_description);
        }

        // OBR-21: Filler Field 1 (Station AE Title)
        if (!mpps.station_ae_title.empty()) {
            builder.set_field("OBR.21", mpps.station_ae_title);
        }

        // OBR-22: Results Rpt/Status Chng - Date/Time (Start DateTime)
        if (!mpps.start_date.empty()) {
            std::string start_datetime = mpps.start_date;
            if (!mpps.start_time.empty()) {
                start_datetime += mpps.start_time.substr(0, 6);  // HHMMSS
            }
            builder.set_field("OBR.22", start_datetime);
        }

        // OBR-24: Diagnostic Service Section ID (Modality)
        if (!mpps.modality.empty()) {
            builder.set_field("OBR.24", mpps.modality);
        }

        // OBR-25: Result Status
        builder.result_status(mpps_status_to_order_status(event));

        // OBR-27: Quantity/Timing (End DateTime for completed/discontinued)
        if (event != pacs_adapter::mpps_event::in_progress) {
            if (!mpps.end_date.empty()) {
                std::string end_datetime = mpps.end_date;
                if (!mpps.end_time.empty()) {
                    end_datetime += mpps.end_time.substr(0, 6);  // HHMMSS
                }
                builder.set_field("OBR.27.4", end_datetime);
            }
        }

        // OBR-31: Reason for Study (Discontinuation reason)
        if (event == pacs_adapter::mpps_event::discontinued &&
            config_.include_discontinuation_reason &&
            !mpps.discontinuation_reason.empty()) {
            builder.set_field("OBR.31", mpps.discontinuation_reason);
        }
    }

    /**
     * @brief Add OBX segments for series information
     */
    void add_series_obx_segments(hl7::hl7_builder& builder,
                                  const pacs_adapter::mpps_dataset& mpps) const {
        int obx_seq = 1;

        // Add Study Instance UID as OBX
        if (!mpps.study_instance_uid.empty()) {
            builder.add_observation("ST",
                                    std::to_string(obx_seq) + "^STUDY_UID",
                                    mpps.study_instance_uid);
            obx_seq++;
        }

        // Add total instances count
        size_t total_instances = mpps.total_instances();
        if (total_instances > 0) {
            builder.add_observation("NM",
                                    std::to_string(obx_seq) + "^TOTAL_INSTANCES",
                                    std::to_string(total_instances));
            obx_seq++;
        }

        // Add series information
        for (const auto& series : mpps.performed_series) {
            std::stringstream series_info;
            series_info << series.series_instance_uid;
            if (!series.series_description.empty()) {
                series_info << "^" << series.series_description;
            }
            series_info << "^" << series.number_of_instances << " images";

            builder.add_observation("ST",
                                    std::to_string(obx_seq) + "^SERIES_INFO",
                                    series_info.str());
            obx_seq++;
        }
    }

    /**
     * @brief Validate MPPS dataset
     */
    [[nodiscard]] std::vector<std::string>
    validate_mpps_for_mapping(const pacs_adapter::mpps_dataset& mpps) const {
        std::vector<std::string> errors;

        if (mpps.accession_number.empty()) {
            errors.push_back("Missing AccessionNumber");
        }

        if (mpps.patient_id.empty()) {
            errors.push_back("Missing PatientID");
        }

        if (mpps.start_date.empty()) {
            errors.push_back("Missing Performed Procedure Step Start Date");
        }

        return errors;
    }

public:
    /**
     * @brief Public validation method (called from dicom_hl7_mapper)
     */
    [[nodiscard]] std::vector<std::string>
    validate_mpps(const pacs_adapter::mpps_dataset& mpps) const {
        return validate_mpps_for_mapping(mpps);
    }

private:

    /**
     * @brief Map MPPS status to HL7 order control code
     */
    [[nodiscard]] static std::string
    mpps_status_to_order_control(pacs_adapter::mpps_event event) {
        switch (event) {
            case pacs_adapter::mpps_event::in_progress:
            case pacs_adapter::mpps_event::completed:
                return "SC";  // Status Changed
            case pacs_adapter::mpps_event::discontinued:
                return "DC";  // Discontinue Order
            default:
                return "SC";
        }
    }

    /**
     * @brief Map MPPS status to HL7 order status code
     */
    [[nodiscard]] static std::string
    mpps_status_to_order_status(pacs_adapter::mpps_event event) {
        switch (event) {
            case pacs_adapter::mpps_event::in_progress:
                return "IP";  // In Progress
            case pacs_adapter::mpps_event::completed:
                return "CM";  // Completed
            case pacs_adapter::mpps_event::discontinued:
                return "CA";  // Cancelled
            default:
                return "IP";
        }
    }
};

// =============================================================================
// dicom_hl7_mapper Public Interface
// =============================================================================

dicom_hl7_mapper::dicom_hl7_mapper()
    : pimpl_(std::make_unique<impl>(dicom_hl7_mapper_config{})) {}

dicom_hl7_mapper::dicom_hl7_mapper(const dicom_hl7_mapper_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

dicom_hl7_mapper::~dicom_hl7_mapper() = default;

dicom_hl7_mapper::dicom_hl7_mapper(dicom_hl7_mapper&&) noexcept = default;
dicom_hl7_mapper& dicom_hl7_mapper::operator=(dicom_hl7_mapper&&) noexcept = default;

// =============================================================================
// MPPS to ORM Mapping
// =============================================================================

std::expected<mpps_mapping_result, dicom_hl7_error>
dicom_hl7_mapper::mpps_to_orm(const pacs_adapter::mpps_dataset& mpps,
                               pacs_adapter::mpps_event event) const {
    return pimpl_->build_orm_message(mpps, event);
}

std::expected<mpps_mapping_result, dicom_hl7_error>
dicom_hl7_mapper::mpps_in_progress_to_orm(
    const pacs_adapter::mpps_dataset& mpps) const {
    return mpps_to_orm(mpps, pacs_adapter::mpps_event::in_progress);
}

std::expected<mpps_mapping_result, dicom_hl7_error>
dicom_hl7_mapper::mpps_completed_to_orm(
    const pacs_adapter::mpps_dataset& mpps) const {
    return mpps_to_orm(mpps, pacs_adapter::mpps_event::completed);
}

std::expected<mpps_mapping_result, dicom_hl7_error>
dicom_hl7_mapper::mpps_discontinued_to_orm(
    const pacs_adapter::mpps_dataset& mpps) const {
    return mpps_to_orm(mpps, pacs_adapter::mpps_event::discontinued);
}

// =============================================================================
// Utility Conversion Functions
// =============================================================================

std::expected<std::string, dicom_hl7_error>
dicom_hl7_mapper::dicom_date_to_hl7(std::string_view dicom_date) {
    // DICOM date format: YYYYMMDD
    // HL7 date format: YYYYMMDD (same)

    if (dicom_date.empty()) {
        return std::unexpected(dicom_hl7_error::datetime_conversion_failed);
    }

    // Remove any trailing whitespace
    while (!dicom_date.empty() && std::isspace(dicom_date.back())) {
        dicom_date.remove_suffix(1);
    }

    // Validate length (must be exactly 8 characters)
    if (dicom_date.length() != 8) {
        return std::unexpected(dicom_hl7_error::datetime_conversion_failed);
    }

    // Validate all characters are digits
    for (char c : dicom_date) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return std::unexpected(dicom_hl7_error::datetime_conversion_failed);
        }
    }

    return std::string(dicom_date);
}

std::expected<std::string, dicom_hl7_error>
dicom_hl7_mapper::dicom_time_to_hl7(std::string_view dicom_time) {
    // DICOM time format: HHMMSS.FFFFFF (up to 6 fractional digits)
    // HL7 time format: HHMMSS[.S[S[S[S]]]] (up to 4 fractional digits)

    if (dicom_time.empty()) {
        return std::unexpected(dicom_hl7_error::datetime_conversion_failed);
    }

    // Remove trailing whitespace
    while (!dicom_time.empty() && std::isspace(dicom_time.back())) {
        dicom_time.remove_suffix(1);
    }

    // Find the decimal point position
    auto dot_pos = dicom_time.find('.');

    std::string result;

    if (dot_pos == std::string_view::npos) {
        // No fractional part
        if (dicom_time.length() < 2) {
            return std::unexpected(dicom_hl7_error::datetime_conversion_failed);
        }
        result = std::string(dicom_time.substr(0, std::min(size_t(6), dicom_time.length())));
    } else {
        // Has fractional part
        std::string_view time_part = dicom_time.substr(0, dot_pos);
        std::string_view frac_part = dicom_time.substr(dot_pos + 1);

        // Validate time part
        if (time_part.length() < 2 || time_part.length() > 6) {
            return std::unexpected(dicom_hl7_error::datetime_conversion_failed);
        }

        result = std::string(time_part);

        // HL7 supports up to 4 fractional digits
        if (!frac_part.empty()) {
            result += ".";
            result += frac_part.substr(0, std::min(size_t(4), frac_part.length()));
        }
    }

    return result;
}

std::expected<hl7::hl7_timestamp, dicom_hl7_error>
dicom_hl7_mapper::dicom_datetime_to_hl7_timestamp(std::string_view dicom_date,
                                                   std::string_view dicom_time) {
    hl7::hl7_timestamp ts{};

    // Parse date (YYYYMMDD)
    if (dicom_date.length() >= 8) {
        int year = 0, month = 0, day = 0;

        auto year_result = std::from_chars(
            dicom_date.data(), dicom_date.data() + 4, year);
        auto month_result = std::from_chars(
            dicom_date.data() + 4, dicom_date.data() + 6, month);
        auto day_result = std::from_chars(
            dicom_date.data() + 6, dicom_date.data() + 8, day);

        if (year_result.ec != std::errc{} ||
            month_result.ec != std::errc{} ||
            day_result.ec != std::errc{}) {
            return std::unexpected(dicom_hl7_error::datetime_conversion_failed);
        }

        ts.year = year;
        ts.month = month;
        ts.day = day;
    } else {
        return std::unexpected(dicom_hl7_error::datetime_conversion_failed);
    }

    // Parse time (HHMMSS[.FFFFFF])
    if (!dicom_time.empty() && dicom_time.length() >= 2) {
        int hour = 0, minute = 0, second = 0, millisecond = 0;

        auto hour_result = std::from_chars(
            dicom_time.data(),
            dicom_time.data() + std::min(size_t(2), dicom_time.length()),
            hour);

        if (hour_result.ec == std::errc{}) {
            ts.hour = hour;
        }

        if (dicom_time.length() >= 4) {
            auto minute_result = std::from_chars(
                dicom_time.data() + 2, dicom_time.data() + 4, minute);
            if (minute_result.ec == std::errc{}) {
                ts.minute = minute;
            }
        }

        if (dicom_time.length() >= 6) {
            auto second_result = std::from_chars(
                dicom_time.data() + 4, dicom_time.data() + 6, second);
            if (second_result.ec == std::errc{}) {
                ts.second = second;
            }
        }

        // Handle fractional seconds
        auto dot_pos = dicom_time.find('.');
        if (dot_pos != std::string_view::npos && dot_pos + 1 < dicom_time.length()) {
            std::string_view frac = dicom_time.substr(dot_pos + 1);
            // Convert to milliseconds (first 3 digits)
            if (frac.length() >= 3) {
                auto ms_result = std::from_chars(
                    frac.data(), frac.data() + 3, millisecond);
                if (ms_result.ec == std::errc{}) {
                    ts.millisecond = millisecond;
                }
            } else if (frac.length() >= 1) {
                // Pad with zeros if less than 3 digits
                std::string frac_padded(frac);
                while (frac_padded.length() < 3) {
                    frac_padded += '0';
                }
                auto ms_result = std::from_chars(
                    frac_padded.data(), frac_padded.data() + 3, millisecond);
                if (ms_result.ec == std::errc{}) {
                    ts.millisecond = millisecond;
                }
            }
        }
    }

    return ts;
}

hl7::hl7_person_name
dicom_hl7_mapper::dicom_name_to_hl7(std::string_view dicom_pn) {
    // DICOM PN format: Family^Given^Middle^Prefix^Suffix
    // HL7 XPN format: Family^Given^Middle^Suffix^Prefix^Degree

    hl7::hl7_person_name result;

    if (dicom_pn.empty()) {
        return result;
    }

    // Split by '^'
    std::vector<std::string_view> components;
    size_t start = 0;
    size_t pos = 0;

    while ((pos = dicom_pn.find('^', start)) != std::string_view::npos) {
        components.push_back(dicom_pn.substr(start, pos - start));
        start = pos + 1;
    }
    components.push_back(dicom_pn.substr(start));

    // DICOM: Family(0), Given(1), Middle(2), Prefix(3), Suffix(4)
    // HL7:   Family(0), Given(1), Middle(2), Suffix(3), Prefix(4), Degree(5)
    if (components.size() > 0) {
        result.family_name = std::string(components[0]);
    }
    if (components.size() > 1) {
        result.given_name = std::string(components[1]);
    }
    if (components.size() > 2) {
        result.middle_name = std::string(components[2]);
    }
    if (components.size() > 3) {
        // DICOM Prefix -> HL7 Prefix
        result.prefix = std::string(components[3]);
    }
    if (components.size() > 4) {
        // DICOM Suffix -> HL7 Suffix
        result.suffix = std::string(components[4]);
    }

    return result;
}

std::string
dicom_hl7_mapper::mpps_status_to_hl7_order_status(pacs_adapter::mpps_event event) {
    switch (event) {
        case pacs_adapter::mpps_event::in_progress:
            return "IP";  // In Progress
        case pacs_adapter::mpps_event::completed:
            return "CM";  // Completed
        case pacs_adapter::mpps_event::discontinued:
            return "CA";  // Cancelled
        default:
            return "IP";
    }
}

std::string
dicom_hl7_mapper::mpps_status_to_hl7_order_control(pacs_adapter::mpps_event event) {
    switch (event) {
        case pacs_adapter::mpps_event::in_progress:
        case pacs_adapter::mpps_event::completed:
            return "SC";  // Status Changed
        case pacs_adapter::mpps_event::discontinued:
            return "DC";  // Discontinue Order
        default:
            return "SC";
    }
}

// =============================================================================
// Configuration
// =============================================================================

const dicom_hl7_mapper_config& dicom_hl7_mapper::config() const noexcept {
    return pimpl_->config_;
}

void dicom_hl7_mapper::set_config(const dicom_hl7_mapper_config& config) {
    std::lock_guard lock(pimpl_->mutex_);
    pimpl_->config_ = config;
}

void dicom_hl7_mapper::register_transform(std::string_view name,
                                           transform_function func) {
    std::lock_guard lock(pimpl_->mutex_);
    pimpl_->transforms_[std::string(name)] = std::move(func);
}

// =============================================================================
// Validation
// =============================================================================

std::vector<std::string>
dicom_hl7_mapper::validate_mpps(const pacs_adapter::mpps_dataset& mpps) const {
    return pimpl_->validate_mpps(mpps);
}

}  // namespace pacs::bridge::mapping
