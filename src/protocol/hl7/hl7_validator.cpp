/**
 * @file hl7_validator.cpp
 * @brief HL7 message validator implementation
 */

#include "pacs/bridge/protocol/hl7/hl7_validator.h"

#include <sstream>

namespace pacs::bridge::hl7 {

// =============================================================================
// validator_result Implementation
// =============================================================================

void validator_result::add_error(validation_issue_type type,
                                  std::string_view location,
                                  std::string_view msg) {
    valid = false;
    issues.push_back({validation_severity::error, type, std::string(location),
                      std::string(msg), hl7_error::validation_failed});
}

void validator_result::add_warning(validation_issue_type type,
                                    std::string_view location,
                                    std::string_view msg) {
    issues.push_back({validation_severity::warning, type, std::string(location),
                      std::string(msg), hl7_error::validation_failed});
}

size_t validator_result::error_count() const noexcept {
    size_t count = 0;
    for (const auto& issue : issues) {
        if (issue.severity == validation_severity::error) {
            ++count;
        }
    }
    return count;
}

size_t validator_result::warning_count() const noexcept {
    size_t count = 0;
    for (const auto& issue : issues) {
        if (issue.severity == validation_severity::warning) {
            ++count;
        }
    }
    return count;
}

std::string validator_result::summary() const {
    std::ostringstream oss;

    if (valid) {
        oss << "Validation passed";
        if (!issues.empty()) {
            oss << " with " << warning_count() << " warning(s)";
        }
    } else {
        oss << "Validation failed: " << error_count() << " error(s)";
        if (warning_count() > 0) {
            oss << ", " << warning_count() << " warning(s)";
        }
    }

    oss << "\n";

    for (const auto& issue : issues) {
        oss << "  [" << (issue.severity == validation_severity::error ? "ERROR"
                                                                       : "WARN")
            << "] " << issue.location << ": " << issue.message << "\n";
    }

    return oss.str();
}

// =============================================================================
// hl7_validator Implementation
// =============================================================================

validator_result hl7_validator::validate(const hl7_message& message) {
    auto header = message.header();

    switch (header.type) {
        case message_type::ADT:
            return validate_adt(message);
        case message_type::ORM:
            return validate_orm(message);
        case message_type::ORU:
            return validate_oru(message);
        case message_type::SIU:
            return validate_siu(message);
        case message_type::ACK:
            return validate_ack(message);
        default: {
            validator_result result;
            result.type = header.type;
            result.trigger_event = header.trigger_event;

            // For unknown types, just validate MSH
            validate_msh(message, result);
            return result;
        }
    }
}

validator_result hl7_validator::validate_adt(const hl7_message& message) {
    validator_result result;
    auto header = message.header();
    result.type = message_type::ADT;
    result.trigger_event = header.trigger_event;

    // Validate MSH
    validate_msh(message, result);

    // Required segments: MSH, EVN, PID
    check_segment(message, "EVN", result, true);
    check_segment(message, "PID", result, true);

    // Optional but common segments
    check_segment(message, "PV1", result, false);

    // Validate PID fields
    validate_pid(message, result, true);

    // ADT-A40 (Merge) requires MRG segment
    if (header.trigger_event == "A40") {
        if (!check_segment(message, "MRG", result, true)) {
            result.add_error(validation_issue_type::missing_segment, "MRG",
                             "MRG segment is required for ADT^A40 (Merge)");
        }
    }

    return result;
}

validator_result hl7_validator::validate_orm(const hl7_message& message) {
    validator_result result;
    auto header = message.header();
    result.type = message_type::ORM;
    result.trigger_event = header.trigger_event;

    // Validate MSH
    validate_msh(message, result);

    // Required segments: MSH, PID, ORC, OBR
    check_segment(message, "PID", result, true);
    check_segment(message, "ORC", result, true);
    check_segment(message, "OBR", result, true);

    // Validate PID fields
    validate_pid(message, result, false);  // Name not strictly required for orders

    // Validate ORC fields
    const auto* orc = message.segment("ORC");
    if (orc) {
        // ORC-1 (Order Control) is required
        if (orc->field_value(1).empty()) {
            result.add_error(validation_issue_type::missing_field, "ORC.1",
                             "Order Control (ORC-1) is required");
        }

        // ORC-2 or ORC-3 (Order Numbers) - at least one required
        bool has_placer = !orc->field(2).component(1).value().empty();
        bool has_filler = !orc->field(3).component(1).value().empty();
        if (!has_placer && !has_filler) {
            result.add_error(
                validation_issue_type::missing_field, "ORC.2/ORC.3",
                "Either Placer Order Number (ORC-2) or Filler Order Number "
                "(ORC-3) is required");
        }
    }

    // Validate OBR fields
    const auto* obr = message.segment("OBR");
    if (obr) {
        // OBR-4 (Universal Service ID) is required
        if (obr->field(4).component(1).value().empty()) {
            result.add_error(validation_issue_type::missing_field, "OBR.4",
                             "Universal Service ID (OBR-4) is required");
        }
    }

    return result;
}

validator_result hl7_validator::validate_oru(const hl7_message& message) {
    validator_result result;
    auto header = message.header();
    result.type = message_type::ORU;
    result.trigger_event = header.trigger_event;

    // Validate MSH
    validate_msh(message, result);

    // Required segments: MSH, PID, OBR, OBX
    check_segment(message, "PID", result, true);
    check_segment(message, "OBR", result, true);
    check_segment(message, "OBX", result, true);

    // Validate PID fields
    validate_pid(message, result, false);

    // Validate OBR fields
    const auto* obr = message.segment("OBR");
    if (obr) {
        // OBR-25 (Result Status) is required
        if (obr->field_value(25).empty()) {
            result.add_error(validation_issue_type::missing_field, "OBR.25",
                             "Result Status (OBR-25) is required");
        }
    }

    // Validate OBX segments
    auto obx_segments = message.segments("OBX");
    if (!obx_segments.empty()) {
        for (size_t i = 0; i < obx_segments.size(); ++i) {
            const auto* obx = obx_segments[i];
            std::string loc = "OBX[" + std::to_string(i) + "]";

            // OBX-2 (Value Type) is required
            if (obx->field_value(2).empty()) {
                result.add_error(validation_issue_type::missing_field,
                                 loc + ".2", "Value Type (OBX-2) is required");
            }

            // OBX-3 (Observation Identifier) is required
            if (obx->field(3).component(1).value().empty()) {
                result.add_error(validation_issue_type::missing_field,
                                 loc + ".3",
                                 "Observation Identifier (OBX-3) is required");
            }

            // OBX-11 (Observation Result Status) is required
            if (obx->field_value(11).empty()) {
                result.add_warning(validation_issue_type::missing_field,
                                   loc + ".11",
                                   "Observation Result Status (OBX-11) is "
                                   "recommended");
            }
        }
    }

    return result;
}

validator_result hl7_validator::validate_siu(const hl7_message& message) {
    validator_result result;
    auto header = message.header();
    result.type = message_type::SIU;
    result.trigger_event = header.trigger_event;

    // Validate MSH
    validate_msh(message, result);

    // Required segments: MSH, SCH, PID
    check_segment(message, "SCH", result, true);
    check_segment(message, "PID", result, true);

    // Validate PID fields
    validate_pid(message, result, true);

    // Validate SCH fields
    const auto* sch = message.segment("SCH");
    if (sch) {
        // SCH-1 (Placer Appointment ID) is required
        if (sch->field(1).component(1).value().empty()) {
            result.add_error(validation_issue_type::missing_field, "SCH.1",
                             "Placer Appointment ID (SCH-1) is required");
        }

        // SCH-25 (Filler Status Code) is recommended
        if (sch->field_value(25).empty()) {
            result.add_warning(validation_issue_type::missing_field, "SCH.25",
                               "Filler Status Code (SCH-25) is recommended");
        }
    }

    // Optional AIS, AIG, AIL, AIP segments for resource info
    if (!message.has_segment("AIS") && !message.has_segment("AIG") &&
        !message.has_segment("AIL") && !message.has_segment("AIP")) {
        result.add_warning(
            validation_issue_type::missing_segment, "AIS/AIG/AIL/AIP",
            "At least one resource segment (AIS, AIG, AIL, or AIP) is "
            "recommended");
    }

    return result;
}

validator_result hl7_validator::validate_ack(const hl7_message& message) {
    validator_result result;
    auto header = message.header();
    result.type = message_type::ACK;
    result.trigger_event = header.trigger_event;

    // Validate MSH
    validate_msh(message, result);

    // Required segment: MSA
    if (!check_segment(message, "MSA", result, true)) {
        return result;
    }

    // Validate MSA fields
    const auto* msa = message.segment("MSA");
    if (msa) {
        // MSA-1 (Acknowledgment Code) is required
        std::string_view ack_code = msa->field_value(1);
        if (ack_code.empty()) {
            result.add_error(validation_issue_type::missing_field, "MSA.1",
                             "Acknowledgment Code (MSA-1) is required");
        } else {
            // Validate ack code value
            if (ack_code != "AA" && ack_code != "AE" && ack_code != "AR" &&
                ack_code != "CA" && ack_code != "CE" && ack_code != "CR") {
                result.add_error(
                    validation_issue_type::invalid_field_value, "MSA.1",
                    "Invalid Acknowledgment Code. Expected: AA, AE, AR, CA, "
                    "CE, or CR");
            }
        }

        // MSA-2 (Message Control ID) is required
        if (msa->field_value(2).empty()) {
            result.add_error(validation_issue_type::missing_field, "MSA.2",
                             "Message Control ID (MSA-2) is required");
        }
    }

    // Check for ERR segment if AE or AR
    if (message.has_segment("MSA")) {
        std::string_view ack_code = message.get_value("MSA.1");
        if (ack_code == "AE" || ack_code == "AR" || ack_code == "CE" ||
            ack_code == "CR") {
            if (!message.has_segment("ERR")) {
                result.add_warning(validation_issue_type::missing_segment,
                                   "ERR",
                                   "ERR segment is recommended for error/reject "
                                   "acknowledgments");
            }
        }
    }

    return result;
}

void hl7_validator::validate_msh(const hl7_message& message,
                                  validator_result& result) {
    const auto* msh = message.segment("MSH");
    if (!msh) {
        result.add_error(validation_issue_type::missing_segment, "MSH",
                         "MSH segment is required");
        return;
    }

    // MSH-9 (Message Type) is required
    if (msh->field(9).component(1).value().empty()) {
        result.add_error(validation_issue_type::missing_field, "MSH.9.1",
                         "Message Type (MSH-9.1) is required");
    }

    // MSH-9.2 (Trigger Event) is recommended
    if (msh->field(9).component(2).value().empty()) {
        result.add_warning(validation_issue_type::missing_field, "MSH.9.2",
                           "Trigger Event (MSH-9.2) is recommended");
    }

    // MSH-10 (Message Control ID) is required
    if (msh->field_value(10).empty()) {
        result.add_error(validation_issue_type::missing_field, "MSH.10",
                         "Message Control ID (MSH-10) is required");
    }

    // MSH-11 (Processing ID) is required
    if (msh->field_value(11).empty()) {
        result.add_error(validation_issue_type::missing_field, "MSH.11",
                         "Processing ID (MSH-11) is required");
    }

    // MSH-12 (Version ID) is required
    if (msh->field_value(12).empty()) {
        result.add_error(validation_issue_type::missing_field, "MSH.12",
                         "Version ID (MSH-12) is required");
    }
}

void hl7_validator::validate_pid(const hl7_message& message,
                                  validator_result& result,
                                  bool require_patient_name) {
    const auto* pid = message.segment("PID");
    if (!pid) {
        // PID segment absence should be handled by check_segment
        return;
    }

    // PID-3 (Patient ID) is required
    if (pid->field(3).component(1).value().empty()) {
        result.add_error(validation_issue_type::missing_field, "PID.3",
                         "Patient ID (PID-3) is required");
    }

    // PID-5 (Patient Name) - conditionally required
    if (require_patient_name) {
        if (pid->field(5).component(1).value().empty()) {
            result.add_error(validation_issue_type::missing_field, "PID.5",
                             "Patient Name (PID-5) is required");
        }
    } else {
        if (pid->field(5).component(1).value().empty()) {
            result.add_warning(validation_issue_type::missing_field, "PID.5",
                               "Patient Name (PID-5) is recommended");
        }
    }
}

bool hl7_validator::check_segment(const hl7_message& message,
                                   std::string_view segment_id,
                                   validator_result& result, bool required) {
    bool exists = message.has_segment(segment_id);

    if (!exists) {
        std::string msg =
            std::string(segment_id) + " segment is " +
            (required ? "required" : "recommended");

        if (required) {
            result.add_error(validation_issue_type::missing_segment, segment_id,
                             msg);
        } else {
            result.add_warning(validation_issue_type::missing_segment,
                               segment_id, msg);
        }
    }

    return exists;
}

bool hl7_validator::check_field(const hl7_message& message,
                                 std::string_view path,
                                 validator_result& result, bool required) {
    std::string_view value = message.get_value(path);
    bool exists = !value.empty();

    if (!exists) {
        std::string msg = std::string(path) + " is " +
                          (required ? "required" : "recommended");

        if (required) {
            result.add_error(validation_issue_type::missing_field, path, msg);
        } else {
            result.add_warning(validation_issue_type::missing_field, path, msg);
        }
    }

    return exists;
}

}  // namespace pacs::bridge::hl7
