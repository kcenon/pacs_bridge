/**
 * @file hl7_dicom_mapper.cpp
 * @brief HL7 to DICOM mapper implementation
 */

#include "pacs/bridge/mapping/hl7_dicom_mapper.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace pacs::bridge::mapping {

// =============================================================================
// hl7_dicom_mapper::impl
// =============================================================================

class hl7_dicom_mapper::impl {
public:
    mapper_config config_;
    std::unordered_map<std::string, transform_function> transforms_;

    explicit impl(const mapper_config& config) : config_(config) {
        register_default_transforms();
    }

    void register_default_transforms() {
        // Uppercase transform
        transforms_["uppercase"] = [](std::string_view value)
            -> std::expected<std::string, mapping_error> {
            std::string result(value);
            std::transform(result.begin(), result.end(), result.begin(),
                           ::toupper);
            return result;
        };

        // Trim whitespace transform
        transforms_["trim"] = [](std::string_view value)
            -> std::expected<std::string, mapping_error> {
            size_t start = value.find_first_not_of(" \t\r\n");
            size_t end = value.find_last_not_of(" \t\r\n");
            if (start == std::string_view::npos) return std::string{};
            return std::string(value.substr(start, end - start + 1));
        };

        // Date format transform
        transforms_["date_to_dicom"] = [](std::string_view value)
            -> std::expected<std::string, mapping_error> {
            auto ts = hl7::hl7_timestamp::parse(value);
            if (!ts) {
                return std::unexpected(mapping_error::datetime_parse_failed);
            }
            return hl7_dicom_mapper::hl7_datetime_to_dicom_date(*ts);
        };

        // Time format transform
        transforms_["time_to_dicom"] = [](std::string_view value)
            -> std::expected<std::string, mapping_error> {
            auto ts = hl7::hl7_timestamp::parse(value);
            if (!ts) {
                return std::unexpected(mapping_error::datetime_parse_failed);
            }
            return hl7_dicom_mapper::hl7_datetime_to_dicom_time(*ts);
        };

        // Name format transform
        transforms_["name_to_dicom"] = [](std::string_view value)
            -> std::expected<std::string, mapping_error> {
            auto name = hl7::hl7_person_name::from_dicom_pn(value);
            return hl7_dicom_mapper::hl7_name_to_dicom(name);
        };
    }

    std::expected<std::string, mapping_error> apply_transform(
        std::string_view value, std::string_view transform_name) const {
        auto it = transforms_.find(std::string(transform_name));
        if (it == transforms_.end()) {
            return std::unexpected(mapping_error::no_mapping_rule);
        }
        return it->second(value);
    }
};

// =============================================================================
// hl7_dicom_mapper Implementation
// =============================================================================

hl7_dicom_mapper::hl7_dicom_mapper()
    : pimpl_(std::make_unique<impl>(mapper_config{})) {}

hl7_dicom_mapper::hl7_dicom_mapper(const mapper_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

hl7_dicom_mapper::~hl7_dicom_mapper() = default;

hl7_dicom_mapper::hl7_dicom_mapper(hl7_dicom_mapper&&) noexcept = default;
hl7_dicom_mapper& hl7_dicom_mapper::operator=(hl7_dicom_mapper&&) noexcept = default;

std::expected<mwl_item, mapping_error> hl7_dicom_mapper::to_mwl(
    const hl7::hl7_message& message) const {
    // Check message type
    auto header = message.header();
    if (header.type != hl7::message_type::ORM) {
        return std::unexpected(mapping_error::unsupported_message_type);
    }

    mwl_item mwl;
    mwl.specific_character_set = pimpl_->config_.specific_character_set;
    mwl.hl7_message_control_id = header.message_control_id;

    // Map patient information (PID segment)
    const auto* pid = message.segment("PID");
    if (pid) {
        // Patient ID (PID-3)
        std::string_view pid3 = pid->field_value(3);
        mwl.patient.patient_id = std::string(pid->field(3).component(1).value());
        mwl.patient.issuer_of_patient_id =
            std::string(pid->field(3).component(4).value());

        // Patient Name (PID-5)
        hl7::hl7_person_name name;
        name.family_name = std::string(pid->field(5).component(1).value());
        name.given_name = std::string(pid->field(5).component(2).value());
        name.middle_name = std::string(pid->field(5).component(3).value());
        name.suffix = std::string(pid->field(5).component(4).value());
        name.prefix = std::string(pid->field(5).component(5).value());
        mwl.patient.patient_name = hl7_name_to_dicom(name);

        // Patient Birth Date (PID-7)
        if (auto ts = hl7::hl7_timestamp::parse(pid->field_value(7))) {
            mwl.patient.patient_birth_date = hl7_datetime_to_dicom_date(*ts);
        }

        // Patient Sex (PID-8)
        mwl.patient.patient_sex = hl7_sex_to_dicom(pid->field_value(8));
    } else if (!pimpl_->config_.allow_partial_mapping) {
        return std::unexpected(mapping_error::missing_required_field);
    }

    // Map order information (ORC segment)
    const auto* orc = message.segment("ORC");
    if (orc) {
        // Placer Order Number (ORC-2)
        mwl.imaging_service_request.placer_order_number =
            std::string(orc->field(2).component(1).value());

        // Filler Order Number (ORC-3)
        mwl.imaging_service_request.filler_order_number =
            std::string(orc->field(3).component(1).value());

        // Ordering Provider (ORC-12) -> Requesting Physician
        hl7::hl7_person_name physician;
        physician.family_name = std::string(orc->field(12).component(2).value());
        physician.given_name = std::string(orc->field(12).component(3).value());
        mwl.imaging_service_request.requesting_physician = hl7_name_to_dicom(physician);

        // Order Entry DateTime (ORC-9)
        if (auto ts = hl7::hl7_timestamp::parse(orc->field_value(9))) {
            mwl.imaging_service_request.order_entry_datetime =
                hl7_datetime_to_dicom(*ts);
        }
    }

    // Map observation request (OBR segment)
    const auto* obr = message.segment("OBR");
    if (obr) {
        // Accession Number (OBR-18 or generate)
        std::string_view accession = obr->field_value(18);
        if (accession.empty()) {
            accession = obr->field(2).component(1).value();  // Placer field #
        }
        mwl.imaging_service_request.accession_number = std::string(accession);

        // Procedure Code (OBR-4)
        mwl.requested_procedure.procedure_code_value =
            std::string(obr->field(4).component(1).value());
        mwl.requested_procedure.procedure_code_meaning =
            std::string(obr->field(4).component(2).value());
        mwl.requested_procedure.procedure_coding_scheme =
            std::string(obr->field(4).component(3).value());

        mwl.requested_procedure.requested_procedure_description =
            std::string(obr->field(4).component(2).value());

        // Requested Procedure ID
        mwl.requested_procedure.requested_procedure_id =
            std::string(obr->field(19).value());
        if (mwl.requested_procedure.requested_procedure_id.empty()) {
            mwl.requested_procedure.requested_procedure_id =
                mwl.imaging_service_request.accession_number;
        }

        // Priority (OBR-5)
        mwl.requested_procedure.requested_procedure_priority =
            hl7_priority_to_dicom(obr->field_value(5));

        // Reason for Study (OBR-31 or OBR-13)
        std::string_view reason = obr->field_value(31);
        if (reason.empty()) {
            reason = obr->field_value(13);  // Clinical info
        }
        mwl.requested_procedure.reason_for_procedure = std::string(reason);

        // Referring Physician (OBR-16)
        hl7::hl7_person_name referring;
        referring.family_name = std::string(obr->field(16).component(2).value());
        referring.given_name = std::string(obr->field(16).component(3).value());
        mwl.requested_procedure.referring_physician_name = hl7_name_to_dicom(referring);
        mwl.requested_procedure.referring_physician_id =
            std::string(obr->field(16).component(1).value());

        // Study Instance UID
        if (pimpl_->config_.auto_generate_study_uid) {
            mwl.requested_procedure.study_instance_uid = generate_uid();
        }

        // Scheduled Procedure Step
        dicom_scheduled_procedure_step sps;

        // Scheduled Start Date/Time (OBR-7 or OBR-6)
        std::string_view scheduled_dt = obr->field_value(7);
        if (scheduled_dt.empty()) {
            scheduled_dt = obr->field_value(6);
        }
        if (auto ts = hl7::hl7_timestamp::parse(scheduled_dt)) {
            sps.scheduled_start_date = hl7_datetime_to_dicom_date(*ts);
            sps.scheduled_start_time = hl7_datetime_to_dicom_time(*ts);
        }

        // Modality (OBR-24)
        std::string_view modality = obr->field_value(24);
        sps.modality = modality.empty() ? pimpl_->config_.default_modality
                                        : std::string(modality);

        // Scheduled Station AE Title
        sps.scheduled_station_ae_title = pimpl_->config_.default_station_ae_title;

        // SPS ID
        if (pimpl_->config_.auto_generate_sps_id) {
            sps.scheduled_step_id = "SPS_" + mwl.requested_procedure.requested_procedure_id;
        }

        // SPS Description
        sps.scheduled_step_description = mwl.requested_procedure.requested_procedure_description;

        // Scheduled Performing Physician (OBR-34)
        hl7::hl7_person_name performing;
        performing.family_name = std::string(obr->field(34).component(2).value());
        performing.given_name = std::string(obr->field(34).component(3).value());
        sps.scheduled_performing_physician = hl7_name_to_dicom(performing);

        mwl.scheduled_steps.push_back(std::move(sps));
    } else if (!pimpl_->config_.allow_partial_mapping) {
        return std::unexpected(mapping_error::missing_required_field);
    }

    // Validate if enabled
    if (pimpl_->config_.validate_output) {
        auto errors = validate_mwl(mwl);
        if (!errors.empty() && !pimpl_->config_.allow_partial_mapping) {
            return std::unexpected(mapping_error::validation_failed);
        }
    }

    return mwl;
}

std::expected<dicom_patient, mapping_error> hl7_dicom_mapper::to_patient(
    const hl7::hl7_message& message) const {
    auto header = message.header();

    // ADT and ORM messages are acceptable
    if (header.type != hl7::message_type::ADT &&
        header.type != hl7::message_type::ORM) {
        return std::unexpected(mapping_error::unsupported_message_type);
    }

    const auto* pid = message.segment("PID");
    if (!pid) {
        return std::unexpected(mapping_error::missing_required_field);
    }

    dicom_patient patient;

    // Patient ID (PID-3)
    patient.patient_id = std::string(pid->field(3).component(1).value());
    patient.issuer_of_patient_id = std::string(pid->field(3).component(4).value());

    // Patient Name (PID-5)
    hl7::hl7_person_name name;
    name.family_name = std::string(pid->field(5).component(1).value());
    name.given_name = std::string(pid->field(5).component(2).value());
    name.middle_name = std::string(pid->field(5).component(3).value());
    name.suffix = std::string(pid->field(5).component(4).value());
    name.prefix = std::string(pid->field(5).component(5).value());
    patient.patient_name = hl7_name_to_dicom(name);

    // Patient Birth Date (PID-7)
    if (auto ts = hl7::hl7_timestamp::parse(pid->field_value(7))) {
        patient.patient_birth_date = hl7_datetime_to_dicom_date(*ts);
    }

    // Patient Sex (PID-8)
    patient.patient_sex = hl7_sex_to_dicom(pid->field_value(8));

    // Patient Comments (PID-48 or NK1 notes)
    patient.patient_comments = std::string(pid->field_value(48));

    return patient;
}

bool hl7_dicom_mapper::can_map_to_mwl(const hl7::hl7_message& message) const {
    auto header = message.header();
    if (header.type != hl7::message_type::ORM) {
        return false;
    }

    // Check for required segments
    return message.has_segment("PID") && message.has_segment("OBR");
}

std::expected<hl7::hl7_message, mapping_error> hl7_dicom_mapper::to_oru(
    const mwl_item& mwl, std::string_view status) const {
    hl7::hl7_message msg;

    // Build MSH
    auto& msh = msg.add_segment("MSH");
    msh.set_field(1, "|");
    msh.set_field(2, "^~\\&");
    msh.set_value("9.1", "ORU");
    msh.set_value("9.2", "R01");
    msh.set_field(10, hl7::message_id_generator::generate());
    msh.set_field(11, "P");
    msh.set_field(12, "2.5.1");

    // Build PID
    auto& pid = msg.add_segment("PID");
    pid.set_field(1, "1");
    pid.set_field(3, mwl.patient.patient_id);
    pid.set_field(5, mwl.patient.patient_name);
    pid.set_field(7, mwl.patient.patient_birth_date);
    pid.set_field(8, mwl.patient.patient_sex);

    // Build OBR
    auto& obr = msg.add_segment("OBR");
    obr.set_field(1, "1");
    obr.set_field(2, mwl.imaging_service_request.placer_order_number);
    obr.set_field(3, mwl.imaging_service_request.filler_order_number);
    obr.set_value("4.1", mwl.requested_procedure.procedure_code_value);
    obr.set_value("4.2", mwl.requested_procedure.procedure_code_meaning);
    obr.set_field(18, mwl.imaging_service_request.accession_number);

    // Result status
    std::string obr25_status = "P";  // Preliminary
    if (status == "COMPLETED") {
        obr25_status = "F";  // Final
    } else if (status == "DISCONTINUED") {
        obr25_status = "X";  // Cancelled
    }
    obr.set_field(25, obr25_status);

    return msg;
}

// =============================================================================
// Utility Conversion Functions
// =============================================================================

std::string hl7_dicom_mapper::hl7_name_to_dicom(const hl7::hl7_person_name& name) {
    // DICOM PN: FamilyName^GivenName^MiddleName^Prefix^Suffix
    std::string result = name.family_name;
    result += '^';
    result += name.given_name;
    result += '^';
    result += name.middle_name;
    result += '^';
    result += name.prefix;
    result += '^';
    result += name.suffix;

    // Trim trailing carets
    while (!result.empty() && result.back() == '^') {
        result.pop_back();
    }

    return result;
}

hl7::hl7_person_name hl7_dicom_mapper::dicom_name_to_hl7(std::string_view dicom_pn) {
    return hl7::hl7_person_name::from_dicom_pn(dicom_pn);
}

std::string hl7_dicom_mapper::hl7_datetime_to_dicom_date(
    const hl7::hl7_timestamp& ts) {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << ts.year << std::setw(2)
        << ts.month << std::setw(2) << ts.day;
    return oss.str();
}

std::string hl7_dicom_mapper::hl7_datetime_to_dicom_time(
    const hl7::hl7_timestamp& ts) {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << ts.hour << std::setw(2)
        << ts.minute << std::setw(2) << ts.second;
    return oss.str();
}

std::string hl7_dicom_mapper::hl7_datetime_to_dicom(const hl7::hl7_timestamp& ts) {
    return hl7_datetime_to_dicom_date(ts) + hl7_datetime_to_dicom_time(ts);
}

std::expected<std::string, mapping_error> hl7_dicom_mapper::parse_hl7_datetime(
    std::string_view hl7_ts) {
    auto ts = hl7::hl7_timestamp::parse(hl7_ts);
    if (!ts) {
        return std::unexpected(mapping_error::datetime_parse_failed);
    }
    return hl7_datetime_to_dicom(*ts);
}

std::string hl7_dicom_mapper::hl7_sex_to_dicom(std::string_view hl7_sex) {
    if (hl7_sex == "M" || hl7_sex == "m") return "M";
    if (hl7_sex == "F" || hl7_sex == "f") return "F";
    return "O";  // Other for U, A, N, O
}

std::string hl7_dicom_mapper::hl7_priority_to_dicom(std::string_view hl7_priority) {
    if (hl7_priority == "S") return "STAT";
    if (hl7_priority == "A") return "HIGH";
    if (hl7_priority == "R") return "MEDIUM";
    if (hl7_priority == "T") return "HIGH";  // Timing critical -> high
    return "MEDIUM";  // Default
}

std::string hl7_dicom_mapper::generate_uid(std::string_view root) {
    // Default root if not provided (placeholder, should be organization-specific)
    std::string uid_root = root.empty() ? "1.2.840.10008.5.1.4" : std::string(root);

    // Generate unique suffix using timestamp and random
    auto now = std::chrono::system_clock::now();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch())
                      .count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 999999);

    std::ostringstream oss;
    oss << uid_root << "." << millis << "." << dis(gen);

    return oss.str();
}

const mapper_config& hl7_dicom_mapper::config() const noexcept {
    return pimpl_->config_;
}

void hl7_dicom_mapper::set_config(const mapper_config& config) {
    pimpl_->config_ = config;
}

void hl7_dicom_mapper::register_transform(std::string_view name,
                                           transform_function func) {
    pimpl_->transforms_[std::string(name)] = std::move(func);
}

std::vector<std::string> hl7_dicom_mapper::validate_mwl(const mwl_item& item) const {
    std::vector<std::string> errors;

    // Required patient fields
    if (item.patient.patient_id.empty()) {
        errors.push_back("Patient ID is required");
    }
    if (item.patient.patient_name.empty()) {
        errors.push_back("Patient Name is required");
    }

    // Required procedure fields
    if (item.imaging_service_request.accession_number.empty()) {
        errors.push_back("Accession Number is required");
    }

    // At least one scheduled step
    if (item.scheduled_steps.empty()) {
        errors.push_back("At least one Scheduled Procedure Step is required");
    } else {
        for (size_t i = 0; i < item.scheduled_steps.size(); ++i) {
            const auto& sps = item.scheduled_steps[i];
            if (sps.modality.empty()) {
                errors.push_back("Modality is required for SPS " +
                                 std::to_string(i + 1));
            }
        }
    }

    return errors;
}

// =============================================================================
// patient_id_mapper Implementation
// =============================================================================

dicom_patient patient_id_mapper::map_identifiers(
    const std::vector<hl7::hl7_patient_id>& hl7_ids,
    std::string_view primary_domain) {
    dicom_patient patient;

    for (const auto& hl7_id : hl7_ids) {
        if (patient.patient_id.empty() ||
            (!primary_domain.empty() &&
             hl7_id.assigning_authority == primary_domain)) {
            patient.patient_id = hl7_id.id;
            patient.issuer_of_patient_id = hl7_id.assigning_authority;
        } else {
            patient.other_patient_ids.push_back(hl7_id.id);
        }
    }

    return patient;
}

std::vector<hl7::hl7_patient_id> patient_id_mapper::parse_pid3(
    const hl7::hl7_field& pid3) {
    std::vector<hl7::hl7_patient_id> result;

    // PID-3 can have repetitions
    size_t rep_count = pid3.repetition_count();
    if (rep_count == 0) rep_count = 1;

    for (size_t i = 1; i <= rep_count; ++i) {
        hl7::hl7_patient_id id;
        id.id = std::string(pid3.component(i, 1).value());
        id.assigning_authority = std::string(pid3.component(i, 4).value());
        id.id_type = std::string(pid3.component(i, 5).value());

        if (!id.empty()) {
            result.push_back(std::move(id));
        }
    }

    return result;
}

}  // namespace pacs::bridge::mapping
