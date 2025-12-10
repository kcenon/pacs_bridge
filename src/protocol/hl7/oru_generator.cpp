/**
 * @file oru_generator.cpp
 * @brief ORU^R01 message generator implementation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/25
 */

#include "pacs/bridge/protocol/hl7/oru_generator.h"

#include <sstream>

namespace pacs::bridge::hl7 {

// =============================================================================
// oru_generator::impl
// =============================================================================

class oru_generator::impl {
public:
    oru_generator_config config_;

    explicit impl(const oru_generator_config& config) : config_(config) {}

    [[nodiscard]] std::expected<hl7_message, hl7_error> generate(
        const oru_study_info& study,
        std::string_view report_text,
        report_status status) const {

        if (!study.is_valid()) {
            return std::unexpected(hl7_error::missing_required_field);
        }

        // Create builder with configured options
        builder_options opts;
        opts.sending_application = config_.sending_application;
        opts.sending_facility = config_.sending_facility;
        opts.receiving_application = config_.receiving_application;
        opts.receiving_facility = config_.receiving_facility;
        opts.version = config_.version;
        opts.processing_id = config_.processing_id;

        auto builder = hl7_builder::create(opts);

        // Set message type to ORU^R01
        builder.message_type("ORU", "R01");

        // Build PID segment
        builder.patient_id(study.patient_id, study.patient_id_authority);
        builder.patient_name(study.patient_family_name, study.patient_given_name);

        if (!study.patient_birth_date.empty()) {
            auto ts = hl7_timestamp::parse(study.patient_birth_date);
            if (ts) {
                builder.patient_birth_date(*ts);
            }
        }

        if (!study.patient_sex.empty()) {
            builder.patient_sex(study.patient_sex);
        }

        // Build ORC segment (Common Order)
        builder.order_control("RE");  // Observations/Performed Service to follow
        builder.placer_order_number(study.placer_order_number);
        builder.filler_order_number(study.accession_number);
        builder.order_status("CM");  // Completed

        if (!study.referring_physician_id.empty()) {
            builder.ordering_provider(
                study.referring_physician_id,
                study.referring_physician_family_name,
                study.referring_physician_given_name);
        }

        // Build OBR segment (Observation Request)
        builder.procedure_code(
            study.procedure_code,
            study.procedure_description,
            study.procedure_coding_system);

        if (study.observation_datetime) {
            builder.observation_datetime(*study.observation_datetime);
        } else {
            builder.observation_datetime(hl7_timestamp::now());
        }

        builder.result_status(to_string(status));

        // Set radiologist as principal result interpreter (OBR-32)
        if (!study.radiologist_id.empty()) {
            auto& obr = *builder.get_segment("OBR");
            obr.set_value("32.1", study.radiologist_id);
            obr.set_value("32.2", study.radiologist_family_name);
            obr.set_value("32.3", study.radiologist_given_name);
        }

        // Build OBX segment for report text
        auto result = build_obx_segment(builder, report_text, status);
        if (!result) {
            return std::unexpected(result.error());
        }

        return builder.build();
    }

    [[nodiscard]] std::expected<void, hl7_error> build_obx_segment(
        hl7_builder& builder,
        std::string_view report_text,
        report_status status) const {

        // Get or create OBX segment
        auto& obx = builder.add_segment("OBX");

        // OBX-1: Set ID
        obx.set_field(1, "1");

        // OBX-2: Value type - FT (Formatted Text) for reports
        obx.set_field(2, "FT");

        // OBX-3: Observation identifier
        if (config_.use_loinc_codes) {
            obx.set_value("3.1", config_.loinc_report_code);
            obx.set_value("3.2", config_.loinc_report_description);
            obx.set_value("3.3", config_.loinc_coding_system);
        } else {
            obx.set_value("3.1", "REPORT");
            obx.set_value("3.2", "Radiology Report");
        }

        // OBX-5: Observation value (encoded report text)
        std::string encoded_text = encode_report_text(report_text, {});
        obx.set_field(5, encoded_text);

        // OBX-11: Observation result status
        obx.set_field(11, to_string(status));

        // OBX-14: Date/Time of observation
        obx.set_field(14, hl7_timestamp::now().to_string());

        return {};
    }

    [[nodiscard]] static std::string encode_report_text(
        std::string_view text,
        const hl7_encoding_characters& encoding) {

        std::string result;
        result.reserve(text.size() * 2);  // Reserve extra space for escapes

        char field_sep = encoding.field_separator;
        char comp_sep = encoding.component_separator;
        char rep_sep = encoding.repetition_separator;
        char esc_char = encoding.escape_character;
        char subcomp_sep = encoding.subcomponent_separator;

        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];

            if (c == field_sep) {
                result += esc_char;
                result += 'F';
                result += esc_char;
            } else if (c == comp_sep) {
                result += esc_char;
                result += 'S';
                result += esc_char;
            } else if (c == rep_sep) {
                result += esc_char;
                result += 'R';
                result += esc_char;
            } else if (c == esc_char) {
                result += esc_char;
                result += 'E';
                result += esc_char;
            } else if (c == subcomp_sep) {
                result += esc_char;
                result += 'T';
                result += esc_char;
            } else if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                // CRLF -> line break escape sequence
                result += esc_char;
                result += ".br";
                result += esc_char;
                ++i;  // Skip the \n
            } else if (c == '\n') {
                // LF only -> line break escape sequence
                result += esc_char;
                result += ".br";
                result += esc_char;
            } else if (c == '\r') {
                // CR only -> line break escape sequence
                result += esc_char;
                result += ".br";
                result += esc_char;
            } else {
                result += c;
            }
        }

        return result;
    }

    [[nodiscard]] static std::string decode_report_text(
        std::string_view encoded_text,
        const hl7_encoding_characters& encoding) {

        std::string result;
        result.reserve(encoded_text.size());

        char esc_char = encoding.escape_character;

        for (size_t i = 0; i < encoded_text.size(); ++i) {
            char c = encoded_text[i];

            if (c == esc_char && i + 2 < encoded_text.size()) {
                // Check for escape sequence
                char seq_char = encoded_text[i + 1];

                if (seq_char == 'F' && encoded_text[i + 2] == esc_char) {
                    result += encoding.field_separator;
                    i += 2;
                } else if (seq_char == 'S' && encoded_text[i + 2] == esc_char) {
                    result += encoding.component_separator;
                    i += 2;
                } else if (seq_char == 'R' && encoded_text[i + 2] == esc_char) {
                    result += encoding.repetition_separator;
                    i += 2;
                } else if (seq_char == 'E' && encoded_text[i + 2] == esc_char) {
                    result += encoding.escape_character;
                    i += 2;
                } else if (seq_char == 'T' && encoded_text[i + 2] == esc_char) {
                    result += encoding.subcomponent_separator;
                    i += 2;
                } else if (seq_char == '.' && i + 4 < encoded_text.size()) {
                    // Check for line break escape sequence (.br)
                    if (encoded_text.substr(i + 1, 3) == ".br" &&
                        encoded_text[i + 4] == esc_char) {
                        result += '\n';
                        i += 4;
                    } else {
                        result += c;
                    }
                } else {
                    result += c;
                }
            } else {
                result += c;
            }
        }

        return result;
    }
};

// =============================================================================
// oru_generator Implementation
// =============================================================================

oru_generator::oru_generator()
    : pimpl_(std::make_unique<impl>(oru_generator_config{})) {}

oru_generator::oru_generator(const oru_generator_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

oru_generator::~oru_generator() = default;

oru_generator::oru_generator(oru_generator&&) noexcept = default;
oru_generator& oru_generator::operator=(oru_generator&&) noexcept = default;

std::expected<hl7_message, hl7_error> oru_generator::generate(
    const oru_study_info& study,
    std::string_view report_text,
    report_status status) const {
    return pimpl_->generate(study, report_text, status);
}

std::expected<hl7_message, hl7_error> oru_generator::generate_preliminary(
    const oru_study_info& study,
    std::string_view report_text) const {
    return generate(study, report_text, report_status::preliminary);
}

std::expected<hl7_message, hl7_error> oru_generator::generate_final(
    const oru_study_info& study,
    std::string_view report_text) const {
    return generate(study, report_text, report_status::final_report);
}

std::expected<hl7_message, hl7_error> oru_generator::generate_corrected(
    const oru_study_info& study,
    std::string_view report_text) const {
    return generate(study, report_text, report_status::corrected);
}

std::expected<hl7_message, hl7_error> oru_generator::generate_cancelled(
    const oru_study_info& study,
    std::string_view cancellation_reason) const {
    std::string reason = cancellation_reason.empty()
        ? "Report cancelled"
        : std::string(cancellation_reason);
    return generate(study, reason, report_status::cancelled);
}

std::expected<std::string, hl7_error> oru_generator::generate_string(
    const oru_study_info& study,
    std::string_view report_text,
    report_status status) {
    oru_generator gen;
    auto result = gen.generate(study, report_text, status);
    if (!result) {
        return std::unexpected(result.error());
    }
    return result->serialize();
}

const oru_generator_config& oru_generator::config() const noexcept {
    return pimpl_->config_;
}

void oru_generator::set_config(const oru_generator_config& config) {
    pimpl_->config_ = config;
}

std::string oru_generator::encode_report_text(
    std::string_view text,
    const hl7_encoding_characters& encoding) {
    return impl::encode_report_text(text, encoding);
}

std::string oru_generator::decode_report_text(
    std::string_view encoded_text,
    const hl7_encoding_characters& encoding) {
    return impl::decode_report_text(encoded_text, encoding);
}

}  // namespace pacs::bridge::hl7
