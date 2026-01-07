/**
 * @file diagnostic_report_builder.cpp
 * @brief FHIR DiagnosticReport Builder Implementation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/105
 */

#include "pacs/bridge/emr/diagnostic_report_builder.h"
#include "pacs/bridge/emr/emr_types.h"

#include <sstream>

namespace pacs::bridge::emr {

// =============================================================================
// Helper Functions for JSON Generation
// =============================================================================

namespace {

/**
 * @brief Escape a string for JSON
 */
std::string escape_json(std::string_view str) {
    std::string result;
    result.reserve(str.size() + 10);

    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x",
                             static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
        }
    }

    return result;
}

/**
 * @brief Generate JSON for fhir_coding
 */
std::string coding_to_json(const fhir_coding& coding) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"system\":\"" << escape_json(coding.system) << "\",";
    if (coding.version) {
        oss << "\"version\":\"" << escape_json(*coding.version) << "\",";
    }
    oss << "\"code\":\"" << escape_json(coding.code) << "\"";
    if (coding.display) {
        oss << ",\"display\":\"" << escape_json(*coding.display) << "\"";
    }
    oss << "}";
    return oss.str();
}

/**
 * @brief Generate JSON for fhir_codeable_concept
 */
std::string codeable_concept_to_json(const fhir_codeable_concept& cc) {
    std::ostringstream oss;
    oss << "{";
    if (!cc.coding.empty()) {
        oss << "\"coding\":[";
        bool first = true;
        for (const auto& coding : cc.coding) {
            if (!first) oss << ",";
            oss << coding_to_json(coding);
            first = false;
        }
        oss << "]";
    }
    if (cc.text) {
        if (!cc.coding.empty()) oss << ",";
        oss << "\"text\":\"" << escape_json(*cc.text) << "\"";
    }
    oss << "}";
    return oss.str();
}

/**
 * @brief Generate JSON for fhir_reference
 */
std::string reference_to_json(const fhir_reference& ref) {
    std::ostringstream oss;
    oss << "{";
    bool has_field = false;
    if (ref.reference) {
        oss << "\"reference\":\"" << escape_json(*ref.reference) << "\"";
        has_field = true;
    }
    if (ref.type) {
        if (has_field) oss << ",";
        oss << "\"type\":\"" << escape_json(*ref.type) << "\"";
        has_field = true;
    }
    if (ref.display) {
        if (has_field) oss << ",";
        oss << "\"display\":\"" << escape_json(*ref.display) << "\"";
    }
    oss << "}";
    return oss.str();
}

/**
 * @brief Generate JSON for fhir_identifier
 */
std::string identifier_to_json(const fhir_identifier& ident) {
    std::ostringstream oss;
    oss << "{";
    bool has_field = false;
    if (ident.use) {
        oss << "\"use\":\"" << escape_json(*ident.use) << "\"";
        has_field = true;
    }
    if (ident.system) {
        if (has_field) oss << ",";
        oss << "\"system\":\"" << escape_json(*ident.system) << "\"";
        has_field = true;
    }
    if (has_field) oss << ",";
    oss << "\"value\":\"" << escape_json(ident.value) << "\"";
    if (ident.type) {
        oss << ",\"type\":" << codeable_concept_to_json(*ident.type);
    }
    oss << "}";
    return oss.str();
}

}  // namespace

// =============================================================================
// diagnostic_report_builder Implementation
// =============================================================================

class diagnostic_report_builder::impl {
public:
    impl() = default;

    void set_status(result_status value) { status_ = value; }

    void set_code(fhir_codeable_concept cc) { code_ = std::move(cc); }

    void set_subject(fhir_reference ref) { subject_ = std::move(ref); }

    void add_category(fhir_codeable_concept cc) {
        categories_.push_back(std::move(cc));
    }

    void set_effective_datetime(std::string datetime) {
        effective_datetime_ = std::move(datetime);
    }

    void set_effective_period(std::string start, std::string end) {
        effective_period_start_ = std::move(start);
        effective_period_end_ = std::move(end);
    }

    void set_issued(std::string datetime) { issued_ = std::move(datetime); }

    void add_performer(fhir_reference ref) {
        performers_.push_back(std::move(ref));
    }

    void add_results_interpreter(fhir_reference ref) {
        results_interpreters_.push_back(std::move(ref));
    }

    void set_based_on(fhir_reference ref) { based_on_ = std::move(ref); }

    void set_encounter(fhir_reference ref) { encounter_ = std::move(ref); }

    void add_imaging_study(fhir_reference ref) {
        imaging_studies_.push_back(std::move(ref));
    }

    void add_identifier(fhir_identifier ident) {
        identifiers_.push_back(std::move(ident));
    }

    void set_conclusion(std::string text) { conclusion_ = std::move(text); }

    void add_conclusion_code(fhir_coding coding) {
        conclusion_codes_.push_back(std::move(coding));
    }

    void add_result(fhir_reference ref) { results_.push_back(std::move(ref)); }

    [[nodiscard]] bool is_valid() const {
        return status_.has_value() && code_.has_value() && subject_.has_value();
    }

    [[nodiscard]] std::vector<std::string> validation_errors() const {
        std::vector<std::string> errors;
        if (!status_) {
            errors.push_back("status is required");
        }
        if (!code_) {
            errors.push_back("code is required");
        }
        if (!subject_) {
            errors.push_back("subject is required");
        }
        return errors;
    }

    [[nodiscard]] std::optional<std::string> build() const {
        if (!is_valid()) {
            return std::nullopt;
        }

        std::ostringstream oss;
        oss << "{";
        oss << "\"resourceType\":\"DiagnosticReport\"";

        // Identifiers
        if (!identifiers_.empty()) {
            oss << ",\"identifier\":[";
            bool first = true;
            for (const auto& ident : identifiers_) {
                if (!first) oss << ",";
                oss << identifier_to_json(ident);
                first = false;
            }
            oss << "]";
        }

        // Status
        oss << ",\"status\":\"" << to_string(*status_) << "\"";

        // Category
        if (!categories_.empty()) {
            oss << ",\"category\":[";
            bool first = true;
            for (const auto& cat : categories_) {
                if (!first) oss << ",";
                oss << codeable_concept_to_json(cat);
                first = false;
            }
            oss << "]";
        }

        // Code
        oss << ",\"code\":" << codeable_concept_to_json(*code_);

        // Subject
        oss << ",\"subject\":" << reference_to_json(*subject_);

        // Encounter
        if (encounter_) {
            oss << ",\"encounter\":" << reference_to_json(*encounter_);
        }

        // Effective[x]
        if (effective_datetime_) {
            oss << ",\"effectiveDateTime\":\"" << escape_json(*effective_datetime_)
                << "\"";
        } else if (effective_period_start_) {
            oss << ",\"effectivePeriod\":{";
            oss << "\"start\":\"" << escape_json(*effective_period_start_) << "\"";
            if (effective_period_end_) {
                oss << ",\"end\":\"" << escape_json(*effective_period_end_) << "\"";
            }
            oss << "}";
        }

        // Issued
        if (issued_) {
            oss << ",\"issued\":\"" << escape_json(*issued_) << "\"";
        }

        // Performer
        if (!performers_.empty()) {
            oss << ",\"performer\":[";
            bool first = true;
            for (const auto& perf : performers_) {
                if (!first) oss << ",";
                oss << reference_to_json(perf);
                first = false;
            }
            oss << "]";
        }

        // Results interpreter
        if (!results_interpreters_.empty()) {
            oss << ",\"resultsInterpreter\":[";
            bool first = true;
            for (const auto& interp : results_interpreters_) {
                if (!first) oss << ",";
                oss << reference_to_json(interp);
                first = false;
            }
            oss << "]";
        }

        // Based on
        if (based_on_) {
            oss << ",\"basedOn\":[" << reference_to_json(*based_on_) << "]";
        }

        // Imaging study
        if (!imaging_studies_.empty()) {
            oss << ",\"imagingStudy\":[";
            bool first = true;
            for (const auto& study : imaging_studies_) {
                if (!first) oss << ",";
                oss << reference_to_json(study);
                first = false;
            }
            oss << "]";
        }

        // Result observations
        if (!results_.empty()) {
            oss << ",\"result\":[";
            bool first = true;
            for (const auto& res : results_) {
                if (!first) oss << ",";
                oss << reference_to_json(res);
                first = false;
            }
            oss << "]";
        }

        // Conclusion
        if (conclusion_) {
            oss << ",\"conclusion\":\"" << escape_json(*conclusion_) << "\"";
        }

        // Conclusion codes
        if (!conclusion_codes_.empty()) {
            oss << ",\"conclusionCode\":[{\"coding\":[";
            bool first = true;
            for (const auto& code : conclusion_codes_) {
                if (!first) oss << ",";
                oss << coding_to_json(code);
                first = false;
            }
            oss << "]}]";
        }

        oss << "}";
        return oss.str();
    }

    void reset() {
        status_ = std::nullopt;
        code_ = std::nullopt;
        subject_ = std::nullopt;
        categories_.clear();
        effective_datetime_ = std::nullopt;
        effective_period_start_ = std::nullopt;
        effective_period_end_ = std::nullopt;
        issued_ = std::nullopt;
        performers_.clear();
        results_interpreters_.clear();
        based_on_ = std::nullopt;
        encounter_ = std::nullopt;
        imaging_studies_.clear();
        identifiers_.clear();
        conclusion_ = std::nullopt;
        conclusion_codes_.clear();
        results_.clear();
    }

private:
    std::optional<result_status> status_;
    std::optional<fhir_codeable_concept> code_;
    std::optional<fhir_reference> subject_;
    std::vector<fhir_codeable_concept> categories_;
    std::optional<std::string> effective_datetime_;
    std::optional<std::string> effective_period_start_;
    std::optional<std::string> effective_period_end_;
    std::optional<std::string> issued_;
    std::vector<fhir_reference> performers_;
    std::vector<fhir_reference> results_interpreters_;
    std::optional<fhir_reference> based_on_;
    std::optional<fhir_reference> encounter_;
    std::vector<fhir_reference> imaging_studies_;
    std::vector<fhir_identifier> identifiers_;
    std::optional<std::string> conclusion_;
    std::vector<fhir_coding> conclusion_codes_;
    std::vector<fhir_reference> results_;
};

// =============================================================================
// diagnostic_report_builder Public Interface
// =============================================================================

diagnostic_report_builder::diagnostic_report_builder()
    : impl_(std::make_unique<impl>()) {}

diagnostic_report_builder::~diagnostic_report_builder() = default;

diagnostic_report_builder::diagnostic_report_builder(
    diagnostic_report_builder&&) noexcept = default;
diagnostic_report_builder& diagnostic_report_builder::operator=(
    diagnostic_report_builder&&) noexcept = default;

diagnostic_report_builder diagnostic_report_builder::from_study_result(
    const study_result& result) {
    diagnostic_report_builder builder;

    // Status
    builder.status(result.status);

    // Category - Radiology
    builder.category_radiology();

    // Code - Diagnostic imaging study
    builder.code_imaging_study();

    // Subject
    if (result.patient_reference) {
        builder.subject(*result.patient_reference);
    } else {
        builder.subject("Patient/" + result.patient_id);
    }

    // Effective datetime
    builder.effective_datetime(result.study_datetime);

    // Issued (current time if not specified)
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now_t));
    builder.issued(buf);

    // Performer
    if (result.performer_reference) {
        builder.performer(*result.performer_reference);
    } else if (result.performing_physician) {
        fhir_reference perf;
        perf.display = result.performing_physician;
        builder.impl_->add_performer(perf);
    }

    // Based on (order reference)
    if (result.based_on_reference) {
        builder.based_on(*result.based_on_reference);
    }

    // Encounter
    if (result.encounter_reference) {
        builder.encounter(*result.encounter_reference);
    }

    // Imaging study reference
    if (result.imaging_study_reference) {
        builder.imaging_study(*result.imaging_study_reference);
    }

    // Study Instance UID identifier
    builder.study_instance_uid(result.study_instance_uid);

    // Accession number
    if (result.accession_number) {
        builder.accession_number(*result.accession_number);
    }

    // Conclusion
    if (result.conclusion) {
        builder.conclusion(*result.conclusion);
    }

    // Conclusion code
    if (result.conclusion_code) {
        builder.conclusion_code_snomed(*result.conclusion_code, "");
    }

    return builder;
}

diagnostic_report_builder& diagnostic_report_builder::status(
    result_status value) {
    impl_->set_status(value);
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::code(
    fhir_codeable_concept codeable_concept) {
    impl_->set_code(std::move(codeable_concept));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::code_imaging_study() {
    fhir_codeable_concept cc;
    cc.add_coding(fhir_coding::loinc("18748-4", "Diagnostic imaging study"));
    impl_->set_code(std::move(cc));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::code_loinc(
    std::string_view loinc_code, std::string_view display) {
    fhir_codeable_concept cc;
    cc.add_coding(
        fhir_coding::loinc(std::string(loinc_code), std::string(display)));
    impl_->set_code(std::move(cc));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::subject(
    std::string_view reference) {
    impl_->set_subject(fhir_reference::from_string(std::string(reference)));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::subject(
    std::string_view reference, std::string_view display) {
    fhir_reference ref = fhir_reference::from_string(std::string(reference));
    ref.display = std::string(display);
    impl_->set_subject(std::move(ref));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::category(
    fhir_codeable_concept codeable_concept) {
    impl_->add_category(std::move(codeable_concept));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::category_radiology() {
    fhir_codeable_concept cc;
    cc.add_coding(fhir_coding::hl7v2("0074", "RAD", "Radiology"));
    impl_->add_category(std::move(cc));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::effective_datetime(
    std::string_view datetime) {
    impl_->set_effective_datetime(std::string(datetime));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::effective_period(
    std::string_view start, std::string_view end) {
    impl_->set_effective_period(std::string(start), std::string(end));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::issued(
    std::string_view datetime) {
    impl_->set_issued(std::string(datetime));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::performer(
    std::string_view reference) {
    impl_->add_performer(fhir_reference::from_string(std::string(reference)));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::performer(
    std::string_view reference, std::string_view display) {
    fhir_reference ref = fhir_reference::from_string(std::string(reference));
    ref.display = std::string(display);
    impl_->add_performer(std::move(ref));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::results_interpreter(
    std::string_view reference) {
    impl_->add_results_interpreter(
        fhir_reference::from_string(std::string(reference)));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::based_on(
    std::string_view reference) {
    impl_->set_based_on(fhir_reference::from_string(std::string(reference)));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::encounter(
    std::string_view reference) {
    impl_->set_encounter(fhir_reference::from_string(std::string(reference)));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::imaging_study(
    std::string_view reference) {
    impl_->add_imaging_study(fhir_reference::from_string(std::string(reference)));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::identifier(
    const fhir_identifier& ident) {
    impl_->add_identifier(ident);
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::accession_number(
    std::string_view value, std::string_view system) {
    fhir_identifier ident;
    ident.use = "official";
    if (!system.empty()) {
        ident.system = std::string(system);
    }
    ident.value = std::string(value);

    // Add type coding for accession number
    fhir_codeable_concept type;
    type.add_coding(fhir_coding::hl7v2("0203", "ACSN", "Accession ID"));
    ident.type = std::move(type);

    impl_->add_identifier(std::move(ident));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::study_instance_uid(
    std::string_view uid) {
    fhir_identifier ident;
    ident.system = "urn:dicom:uid";
    ident.value = std::string(uid);
    impl_->add_identifier(std::move(ident));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::conclusion(
    std::string_view text) {
    impl_->set_conclusion(std::string(text));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::conclusion_code(
    fhir_coding coding) {
    impl_->add_conclusion_code(std::move(coding));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::conclusion_code_snomed(
    std::string_view code, std::string_view display) {
    impl_->add_conclusion_code(
        fhir_coding::snomed(std::string(code), std::string(display)));
    return *this;
}

diagnostic_report_builder& diagnostic_report_builder::result(
    std::string_view reference) {
    impl_->add_result(fhir_reference::from_string(std::string(reference)));
    return *this;
}

std::optional<std::string> diagnostic_report_builder::build() const {
    return impl_->build();
}

Result<std::string>
diagnostic_report_builder::build_validated() const {
    auto errors = impl_->validation_errors();
    if (!errors.empty()) {
        std::string error_msg = "Validation failed: ";
        bool first = true;
        for (const auto& err : errors) {
            if (!first) error_msg += ", ";
            error_msg += err;
            first = false;
        }
        return error_info{
            static_cast<int>(result_error::build_failed),
            error_msg,
            "emr::diagnostic_report_builder::build_validated"};
    }

    auto json = impl_->build();
    if (!json) {
        return error_info{
            static_cast<int>(result_error::build_failed),
            "Failed to build JSON",
            "emr::diagnostic_report_builder::build_validated"};
    }

    return *json;
}

bool diagnostic_report_builder::is_valid() const {
    return impl_->is_valid();
}

std::vector<std::string> diagnostic_report_builder::validation_errors() const {
    return impl_->validation_errors();
}

void diagnostic_report_builder::reset() {
    impl_->reset();
}

}  // namespace pacs::bridge::emr
