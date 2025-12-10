/**
 * @file fhir_dicom_mapper.cpp
 * @brief FHIR-DICOM mapper implementation
 *
 * Implements bidirectional mapping between FHIR R4 resources and DICOM
 * data structures.
 *
 * @see include/pacs/bridge/mapping/fhir_dicom_mapper.h
 * @see https://github.com/kcenon/pacs_bridge/issues/35
 */

#include "pacs/bridge/mapping/fhir_dicom_mapper.h"

#include "pacs/bridge/fhir/patient_resource.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>

namespace pacs::bridge::mapping {

// =============================================================================
// JSON Utilities
// =============================================================================

namespace {

/**
 * @brief Escape a string for JSON output
 */
std::string json_escape(std::string_view input) {
    std::string result;
    result.reserve(input.size() + 10);

    for (char c : input) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x",
                             static_cast<unsigned int>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

/**
 * @brief Convert string to lowercase
 */
std::string to_lower(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

/**
 * @brief Simple JSON string extractor
 */
std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        return "";
    }

    pos += search.size();

    // Skip whitespace and colon
    while (pos < json.size() &&
           (std::isspace(static_cast<unsigned char>(json[pos])) ||
            json[pos] == ':')) {
        ++pos;
    }

    if (pos >= json.size() || json[pos] != '"') {
        return "";
    }
    ++pos;

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                default:
                    result += json[pos];
                    break;
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }

    return result;
}

/**
 * @brief Check if string is all digits
 */
bool is_all_digits(std::string_view str) {
    return !str.empty() &&
           std::all_of(str.begin(), str.end(), [](unsigned char c) {
               return std::isdigit(c);
           });
}

// LOINC to DICOM procedure code mapping (common radiology codes)
const std::unordered_map<std::string, fhir_coding> loinc_to_dicom_map = {
    {"24558-9", {"DCM", {}, "CT", "Computed Tomography"}},           // CT Chest
    {"24627-2", {"DCM", {}, "CT", "Computed Tomography"}},           // CT Abdomen
    {"30746-2", {"DCM", {}, "MR", "Magnetic Resonance"}},            // MRI Brain
    {"36813-4", {"DCM", {}, "MR", "Magnetic Resonance"}},            // MRI Spine
    {"36643-5", {"DCM", {}, "XA", "X-Ray Angiography"}},             // Angiography
    {"38269-7", {"DCM", {}, "US", "Ultrasound"}},                    // US Abdomen
    {"24725-4", {"DCM", {}, "CR", "Computed Radiography"}},          // Chest X-Ray
    {"36572-6", {"DCM", {}, "NM", "Nuclear Medicine"}},              // Nuclear Med
    {"44136-0", {"DCM", {}, "PT", "Positron Emission Tomography"}},  // PET Scan
    {"24566-2", {"DCM", {}, "MG", "Mammography"}},                   // Mammogram
};

// SNOMED to DICOM body site mapping
const std::unordered_map<std::string, fhir_coding> snomed_to_dicom_map = {
    {"51185008", {"DCM", {}, "CHEST", "Chest"}},
    {"818981001", {"DCM", {}, "ABDOMEN", "Abdomen"}},
    {"12738006", {"DCM", {}, "BRAIN", "Brain"}},
    {"421060004", {"DCM", {}, "SPINE", "Spine"}},
    {"302509004", {"DCM", {}, "HEAD", "Head"}},
    {"76752008", {"DCM", {}, "BREAST", "Breast"}},
    {"64033007", {"DCM", {}, "KIDNEY", "Kidney"}},
    {"10200004", {"DCM", {}, "LIVER", "Liver"}},
    {"80891009", {"DCM", {}, "HEART", "Heart"}},
};

}  // namespace

// =============================================================================
// Implementation Class
// =============================================================================

class fhir_dicom_mapper::impl {
public:
    explicit impl(const fhir_dicom_mapper_config& config)
        : config_(config), uid_counter_(0) {
        // Initialize random seed for UID generation
        std::random_device rd;
        random_engine_.seed(rd());
    }

    fhir_dicom_mapper_config config_;
    fhir_dicom_mapper::patient_lookup_function patient_lookup_;
    mutable std::shared_mutex mutex_;
    mutable std::atomic<uint64_t> uid_counter_;
    mutable std::mt19937_64 random_engine_;
};

// =============================================================================
// Constructor/Destructor
// =============================================================================

fhir_dicom_mapper::fhir_dicom_mapper()
    : pimpl_(std::make_unique<impl>(fhir_dicom_mapper_config{})) {}

fhir_dicom_mapper::fhir_dicom_mapper(const fhir_dicom_mapper_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

fhir_dicom_mapper::~fhir_dicom_mapper() = default;

fhir_dicom_mapper::fhir_dicom_mapper(fhir_dicom_mapper&&) noexcept = default;

fhir_dicom_mapper& fhir_dicom_mapper::operator=(fhir_dicom_mapper&&) noexcept =
    default;

// =============================================================================
// ServiceRequest to MWL Mapping
// =============================================================================

std::expected<mwl_item, fhir_dicom_error> fhir_dicom_mapper::service_request_to_mwl(
    const fhir_service_request& request,
    const dicom_patient& patient) const {
    // Validate required fields
    auto validation_errors = validate_service_request(request);
    if (!validation_errors.empty() && pimpl_->config_.validate_output) {
        return std::unexpected(fhir_dicom_error::validation_failed);
    }

    mwl_item mwl;

    // Copy patient data
    mwl.patient = patient;

    // Set character set
    mwl.specific_character_set = pimpl_->config_.specific_character_set;

    // Map Imaging Service Request
    auto& isr = mwl.imaging_service_request;

    // Find accession number from identifiers
    for (const auto& [system, value] : request.identifiers) {
        if (system.find("accession") != std::string::npos ||
            system.find("ACSN") != std::string::npos) {
            isr.accession_number = value;
            break;
        }
    }

    // If no accession number found, generate one
    if (isr.accession_number.empty()) {
        isr.accession_number = generate_uid("ACSN");
    }

    // Map requester to requesting physician
    if (request.requester.has_value()) {
        if (request.requester->display.has_value()) {
            isr.requesting_physician = *request.requester->display;
        } else if (request.requester->reference.has_value()) {
            // Extract name from reference
            isr.requesting_physician = *request.requester->reference;
        }
    }

    // Map placer order number from identifiers
    for (const auto& [system, value] : request.identifiers) {
        if (system.find("placer") != std::string::npos) {
            isr.placer_order_number = value;
            break;
        }
    }

    // Map Requested Procedure
    auto& rp = mwl.requested_procedure;

    // Generate or map requested procedure ID
    rp.requested_procedure_id = request.id.empty() ? generate_uid("RP") : request.id;

    // Map procedure code
    if (!request.code.coding.empty()) {
        const auto& coding = request.code.coding[0];
        rp.procedure_code_value = coding.code;
        rp.procedure_code_meaning = coding.display;
        rp.procedure_coding_scheme = coding.system;
        rp.requested_procedure_description = coding.display;
    }
    if (request.code.text.has_value()) {
        rp.requested_procedure_description = *request.code.text;
    }

    // Generate Study Instance UID
    if (pimpl_->config_.auto_generate_study_uid) {
        rp.study_instance_uid = generate_uid();
    }

    // Map reason for procedure
    if (request.reason_code.has_value()) {
        rp.reason_for_procedure = *request.reason_code;
    }

    // Map priority
    rp.requested_procedure_priority = fhir_priority_to_dicom(request.priority);

    // Map referring physician (same as requester in many cases)
    if (request.requester.has_value() && request.requester->display.has_value()) {
        rp.referring_physician_name = *request.requester->display;
    }

    // Map Scheduled Procedure Step
    dicom_scheduled_procedure_step sps;

    // Generate SPS ID
    if (pimpl_->config_.auto_generate_sps_id) {
        sps.scheduled_step_id = generate_uid("SPS");
    }

    // Map scheduled start date/time
    if (request.occurrence_date_time.has_value()) {
        auto datetime_result = fhir_datetime_to_dicom(*request.occurrence_date_time);
        if (datetime_result) {
            sps.scheduled_start_date = datetime_result->first;
            sps.scheduled_start_time = datetime_result->second;
        }
    }

    // Map modality from code system translation
    sps.modality = pimpl_->config_.default_modality;
    if (!request.code.coding.empty()) {
        const auto& coding = request.code.coding[0];
        if (pimpl_->config_.enable_loinc_mapping &&
            coding.system.find("loinc") != std::string::npos) {
            auto dicom_code = loinc_to_dicom(coding.code);
            if (dicom_code) {
                sps.modality = dicom_code->code;
            }
        }
    }

    // Map scheduled station AE title from performer
    if (!request.performer.empty() && request.performer[0].reference.has_value()) {
        sps.scheduled_station_ae_title = *request.performer[0].reference;
    } else {
        sps.scheduled_station_ae_title = pimpl_->config_.default_station_ae_title;
    }

    // Map performing physician
    if (!request.performer.empty() && request.performer[0].display.has_value()) {
        sps.scheduled_performing_physician = *request.performer[0].display;
    }

    // Map procedure step description
    if (!request.code.coding.empty()) {
        sps.scheduled_step_description = request.code.coding[0].display;
    }

    // Map protocol code (same as procedure code)
    if (!request.code.coding.empty()) {
        const auto& coding = request.code.coding[0];
        sps.protocol_code_value = coding.code;
        sps.protocol_code_meaning = coding.display;
        sps.protocol_coding_scheme = coding.system;
    }

    // Map notes to comments
    if (request.note.has_value()) {
        sps.comments = *request.note;
    }

    // Set initial status
    sps.scheduled_step_status = "SCHEDULED";

    mwl.scheduled_steps.push_back(std::move(sps));

    // Validate output if enabled
    if (pimpl_->config_.validate_output) {
        auto mwl_errors = validate_mwl(mwl);
        if (!mwl_errors.empty()) {
            return std::unexpected(fhir_dicom_error::validation_failed);
        }
    }

    return mwl;
}

std::expected<mwl_item, fhir_dicom_error> fhir_dicom_mapper::service_request_to_mwl(
    const fhir_service_request& request) const {
    // Resolve patient reference
    if (!request.subject.reference.has_value()) {
        return std::unexpected(fhir_dicom_error::missing_required_field);
    }

    if (!pimpl_->patient_lookup_) {
        return std::unexpected(fhir_dicom_error::patient_not_found);
    }

    auto patient_result = pimpl_->patient_lookup_(*request.subject.reference);
    if (!patient_result) {
        return std::unexpected(patient_result.error());
    }

    return service_request_to_mwl(request, *patient_result);
}

// =============================================================================
// DICOM Study to ImagingStudy Mapping
// =============================================================================

std::expected<fhir_imaging_study, fhir_dicom_error>
fhir_dicom_mapper::study_to_imaging_study(
    const dicom_study& study,
    const std::optional<std::string>& patient_reference) const {
    fhir_imaging_study result;

    // Set resource ID (use Study Instance UID as base)
    if (!study.study_instance_uid.empty()) {
        // Create a URL-safe ID from UID
        result.id = "study-" + study.study_instance_uid;
        std::replace(result.id.begin(), result.id.end(), '.', '-');
    }

    // Add Study Instance UID as identifier
    result.identifiers.push_back({"urn:dicom:uid", study.study_instance_uid});
    result.study_instance_uid = study.study_instance_uid;

    // Add Accession Number as identifier
    if (!study.accession_number.empty()) {
        result.identifiers.push_back({"http://hospital.local/accession",
                                      study.accession_number});
    }

    // Set status
    result.status = study.status.empty() ? "available" : to_lower(study.status);

    // Set subject reference
    if (patient_reference.has_value()) {
        result.subject.reference = *patient_reference;
    } else if (!study.patient_id.empty()) {
        result.subject.reference = "Patient/" + study.patient_id;
    }

    // Convert started date/time
    if (!study.study_date.empty()) {
        auto datetime_result = dicom_datetime_to_fhir(study.study_date,
                                                       study.study_time);
        if (datetime_result) {
            result.started = *datetime_result;
        }
    }

    // Set referrer
    if (!study.referring_physician_name.empty()) {
        result.referrer = fhir_reference{};
        result.referrer->display = study.referring_physician_name;
    }

    // Set counts
    result.number_of_series = study.number_of_series;
    result.number_of_instances = study.number_of_instances;

    // Set description
    if (!study.study_description.empty()) {
        result.description = study.study_description;
    }

    // Map series
    for (const auto& dicom_series_item : study.series) {
        fhir_imaging_series fhir_series;
        fhir_series.uid = dicom_series_item.series_instance_uid;
        fhir_series.number = dicom_series_item.series_number;

        // Set modality
        fhir_series.modality.system = "http://dicom.nema.org/resources/ontology/DCM";
        fhir_series.modality.code = dicom_series_item.modality;
        fhir_series.modality.display = dicom_series_item.modality;

        if (!dicom_series_item.series_description.empty()) {
            fhir_series.description = dicom_series_item.series_description;
        }

        fhir_series.number_of_instances = dicom_series_item.number_of_instances;

        // Map body site if available
        if (!dicom_series_item.body_part_examined.empty()) {
            fhir_series.body_site = fhir_coding{};
            fhir_series.body_site->system = "http://snomed.info/sct";
            fhir_series.body_site->code = dicom_series_item.body_part_examined;
            fhir_series.body_site->display = dicom_series_item.body_part_examined;
        }

        fhir_series.instance_uids = dicom_series_item.instance_uids;

        result.series.push_back(std::move(fhir_series));
    }

    return result;
}

// =============================================================================
// Patient Mapping
// =============================================================================

std::expected<std::unique_ptr<fhir::patient_resource>, fhir_dicom_error>
fhir_dicom_mapper::dicom_to_fhir_patient(const dicom_patient& dicom_patient) const {
    // Use the utility function from patient_resource.h
    auto patient = fhir::dicom_to_fhir_patient(dicom_patient);
    return patient;
}

std::expected<dicom_patient, fhir_dicom_error>
fhir_dicom_mapper::fhir_to_dicom_patient(const fhir::patient_resource& patient) const {
    dicom_patient result;

    // Get primary identifier
    const auto& identifiers = patient.identifiers();
    if (!identifiers.empty()) {
        result.patient_id = identifiers[0].value;
        if (identifiers[0].system.has_value()) {
            result.issuer_of_patient_id = *identifiers[0].system;
        }

        // Additional identifiers
        for (size_t i = 1; i < identifiers.size(); ++i) {
            result.other_patient_ids.push_back(identifiers[i].value);
        }
    }

    // Convert name
    const auto& names = patient.names();
    if (!names.empty()) {
        result.patient_name = fhir::fhir_name_to_dicom(names[0]);
    }

    // Convert birth date
    if (patient.birth_date().has_value()) {
        result.patient_birth_date = fhir::fhir_date_to_dicom(*patient.birth_date());
    }

    // Convert gender
    if (patient.gender().has_value()) {
        result.patient_sex = fhir::fhir_gender_to_dicom_sex(*patient.gender());
    }

    return result;
}

// =============================================================================
// Code System Translation
// =============================================================================

std::optional<fhir_coding> fhir_dicom_mapper::loinc_to_dicom(
    const std::string& loinc_code) const {
    auto it = loinc_to_dicom_map.find(loinc_code);
    if (it != loinc_to_dicom_map.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<fhir_coding> fhir_dicom_mapper::snomed_to_dicom(
    const std::string& snomed_code) const {
    auto it = snomed_to_dicom_map.find(snomed_code);
    if (it != snomed_to_dicom_map.end()) {
        return it->second;
    }
    return std::nullopt;
}

// =============================================================================
// Utility Functions
// =============================================================================

std::string fhir_dicom_mapper::generate_uid(const std::string& suffix) const {
    std::unique_lock lock(pimpl_->mutex_);

    // Get timestamp component
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();

    // Get counter component
    uint64_t counter = pimpl_->uid_counter_.fetch_add(1);

    // Get random component
    uint64_t random = pimpl_->random_engine_();

    std::ostringstream uid;
    uid << pimpl_->config_.uid_root;
    uid << "." << millis;
    uid << "." << counter;
    uid << "." << (random % 10000);

    if (!suffix.empty()) {
        uid << "." << suffix;
    }

    return uid.str();
}

std::expected<std::pair<std::string, std::string>, fhir_dicom_error>
fhir_dicom_mapper::fhir_datetime_to_dicom(std::string_view fhir_datetime) {
    // FHIR format: YYYY-MM-DDTHH:MM:SS[.SSS][Z|+/-HH:MM]
    // DICOM Date: YYYYMMDD
    // DICOM Time: HHMMSS[.FFFFFF]

    if (fhir_datetime.size() < 10) {
        return std::unexpected(fhir_dicom_error::datetime_conversion_failed);
    }

    // Extract date part (YYYY-MM-DD)
    std::string date;
    if (fhir_datetime.size() >= 4 &&
        fhir_datetime.size() >= 7 && fhir_datetime[4] == '-' &&
        fhir_datetime.size() >= 10 && fhir_datetime[7] == '-') {
        date = std::string(fhir_datetime.substr(0, 4));   // YYYY
        date += std::string(fhir_datetime.substr(5, 2));  // MM
        date += std::string(fhir_datetime.substr(8, 2));  // DD
    } else {
        return std::unexpected(fhir_dicom_error::datetime_conversion_failed);
    }

    // Extract time part if present
    std::string time;
    if (fhir_datetime.size() > 11 && fhir_datetime[10] == 'T') {
        // Find end of time (before timezone)
        size_t time_end = 11;
        while (time_end < fhir_datetime.size() &&
               fhir_datetime[time_end] != 'Z' &&
               fhir_datetime[time_end] != '+' &&
               fhir_datetime[time_end] != '-') {
            ++time_end;
        }

        std::string_view time_part = fhir_datetime.substr(11, time_end - 11);

        // HH:MM:SS -> HHMMSS
        if (time_part.size() >= 8 && time_part[2] == ':' && time_part[5] == ':') {
            time = std::string(time_part.substr(0, 2));   // HH
            time += std::string(time_part.substr(3, 2));  // MM
            time += std::string(time_part.substr(6, 2));  // SS

            // Handle fractional seconds
            if (time_part.size() > 8 && time_part[8] == '.') {
                time += '.';
                time += std::string(time_part.substr(9));
            }
        } else if (time_part.size() >= 5 && time_part[2] == ':') {
            // HH:MM format
            time = std::string(time_part.substr(0, 2));   // HH
            time += std::string(time_part.substr(3, 2));  // MM
            time += "00";
        }
    }

    return std::make_pair(date, time);
}

std::expected<std::string, fhir_dicom_error>
fhir_dicom_mapper::dicom_datetime_to_fhir(std::string_view dicom_date,
                                          std::string_view dicom_time) {
    // DICOM Date: YYYYMMDD -> FHIR: YYYY-MM-DD
    // DICOM Time: HHMMSS[.FFFFFF] -> FHIR: THH:MM:SS[.SSS]

    if (dicom_date.size() != 8) {
        return std::unexpected(fhir_dicom_error::datetime_conversion_failed);
    }

    // Validate date is all digits
    if (!is_all_digits(dicom_date)) {
        return std::unexpected(fhir_dicom_error::datetime_conversion_failed);
    }

    std::string result;
    result.reserve(30);

    // Format date
    result += dicom_date.substr(0, 4);  // YYYY
    result += '-';
    result += dicom_date.substr(4, 2);  // MM
    result += '-';
    result += dicom_date.substr(6, 2);  // DD

    // Format time if present
    if (!dicom_time.empty() && dicom_time.size() >= 6) {
        result += 'T';
        result += dicom_time.substr(0, 2);  // HH
        result += ':';
        result += dicom_time.substr(2, 2);  // MM
        result += ':';
        result += dicom_time.substr(4, 2);  // SS

        // Handle fractional seconds
        if (dicom_time.size() > 6 && dicom_time[6] == '.') {
            // Take up to 3 decimal places for FHIR
            size_t frac_len = std::min(static_cast<size_t>(4),
                                       dicom_time.size() - 6);
            result += dicom_time.substr(6, frac_len);
        }
    }

    return result;
}

std::string fhir_dicom_mapper::fhir_priority_to_dicom(std::string_view fhir_priority) {
    std::string lower = to_lower(fhir_priority);
    if (lower == "stat") {
        return "STAT";
    }
    if (lower == "asap") {
        return "HIGH";
    }
    if (lower == "urgent") {
        return "HIGH";
    }
    if (lower == "routine") {
        return "MEDIUM";
    }
    return "MEDIUM";  // Default
}

std::string fhir_dicom_mapper::dicom_priority_to_fhir(std::string_view dicom_priority) {
    std::string upper;
    upper.reserve(dicom_priority.size());
    for (char c : dicom_priority) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    if (upper == "STAT") {
        return "stat";
    }
    if (upper == "HIGH") {
        return "urgent";
    }
    if (upper == "MEDIUM") {
        return "routine";
    }
    if (upper == "LOW") {
        return "routine";
    }
    return "routine";  // Default
}

std::optional<std::string> fhir_dicom_mapper::parse_patient_reference(
    const std::string& reference) {
    // Format: "Patient/{id}" or absolute URL
    const std::string prefix = "Patient/";

    // Try to find Patient/ anywhere in the URL
    auto pos = reference.find(prefix);
    if (pos != std::string::npos) {
        std::string id = reference.substr(pos + prefix.size());
        // Remove any trailing path, query, or fragment
        auto end = id.find_first_of("/?#");
        if (end != std::string::npos) {
            id = id.substr(0, end);
        }
        return id;
    }

    return std::nullopt;
}

// =============================================================================
// Configuration
// =============================================================================

const fhir_dicom_mapper_config& fhir_dicom_mapper::config() const noexcept {
    return pimpl_->config_;
}

void fhir_dicom_mapper::set_config(const fhir_dicom_mapper_config& config) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_ = config;
}

void fhir_dicom_mapper::set_patient_lookup(patient_lookup_function lookup) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->patient_lookup_ = std::move(lookup);
}

// =============================================================================
// Validation
// =============================================================================

std::vector<std::string> fhir_dicom_mapper::validate_service_request(
    const fhir_service_request& request) const {
    std::vector<std::string> errors;

    // Check required fields
    if (request.code.coding.empty() && !request.code.text.has_value()) {
        errors.push_back("ServiceRequest.code is required");
    }

    if (!request.subject.reference.has_value() &&
        !request.subject.identifier.has_value()) {
        errors.push_back("ServiceRequest.subject is required");
    }

    // Validate status
    if (request.status != "draft" && request.status != "active" &&
        request.status != "completed" && request.status != "cancelled" &&
        request.status != "entered-in-error" && request.status != "unknown") {
        errors.push_back("ServiceRequest.status has invalid value: " + request.status);
    }

    // Validate intent
    if (request.intent != "proposal" && request.intent != "plan" &&
        request.intent != "order" && request.intent != "original-order" &&
        request.intent != "reflex-order" && request.intent != "filler-order" &&
        request.intent != "instance-order" && request.intent != "option") {
        errors.push_back("ServiceRequest.intent has invalid value: " + request.intent);
    }

    return errors;
}

std::vector<std::string> fhir_dicom_mapper::validate_mwl(const mwl_item& item) const {
    std::vector<std::string> errors;

    // Patient validation
    if (item.patient.patient_id.empty()) {
        errors.push_back("Patient ID is required");
    }

    // Study Instance UID
    if (item.requested_procedure.study_instance_uid.empty()) {
        errors.push_back("Study Instance UID is required");
    }

    // Scheduled Procedure Step validation
    if (item.scheduled_steps.empty()) {
        errors.push_back("At least one Scheduled Procedure Step is required");
    } else {
        for (size_t i = 0; i < item.scheduled_steps.size(); ++i) {
            const auto& sps = item.scheduled_steps[i];
            if (sps.modality.empty()) {
                errors.push_back("SPS[" + std::to_string(i) + "]: Modality is required");
            }
        }
    }

    return errors;
}

// =============================================================================
// JSON Serialization
// =============================================================================

std::string imaging_study_to_json(const fhir_imaging_study& study) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"resourceType\": \"ImagingStudy\"";

    if (!study.id.empty()) {
        json << ",\n  \"id\": \"" << json_escape(study.id) << "\"";
    }

    // Identifiers
    if (!study.identifiers.empty()) {
        json << ",\n  \"identifier\": [\n";
        for (size_t i = 0; i < study.identifiers.size(); ++i) {
            const auto& [system, value] = study.identifiers[i];
            json << "    {\n";
            json << "      \"system\": \"" << json_escape(system) << "\",\n";
            json << "      \"value\": \"" << json_escape(value) << "\"\n";
            json << "    }";
            if (i < study.identifiers.size() - 1) {
                json << ",";
            }
            json << "\n";
        }
        json << "  ]";
    }

    // Status
    json << ",\n  \"status\": \"" << json_escape(study.status) << "\"";

    // Subject
    if (study.subject.reference.has_value()) {
        json << ",\n  \"subject\": {\n";
        json << "    \"reference\": \"" << json_escape(*study.subject.reference) << "\"";
        if (study.subject.display.has_value()) {
            json << ",\n    \"display\": \"" << json_escape(*study.subject.display) << "\"";
        }
        json << "\n  }";
    }

    // Started
    if (study.started.has_value()) {
        json << ",\n  \"started\": \"" << json_escape(*study.started) << "\"";
    }

    // Number of series/instances
    if (study.number_of_series.has_value()) {
        json << ",\n  \"numberOfSeries\": " << *study.number_of_series;
    }
    if (study.number_of_instances.has_value()) {
        json << ",\n  \"numberOfInstances\": " << *study.number_of_instances;
    }

    // Description
    if (study.description.has_value()) {
        json << ",\n  \"description\": \"" << json_escape(*study.description) << "\"";
    }

    // Series
    if (!study.series.empty()) {
        json << ",\n  \"series\": [\n";
        for (size_t i = 0; i < study.series.size(); ++i) {
            const auto& series = study.series[i];
            json << "    {\n";
            json << "      \"uid\": \"" << json_escape(series.uid) << "\"";

            if (series.number.has_value()) {
                json << ",\n      \"number\": " << *series.number;
            }

            json << ",\n      \"modality\": {\n";
            json << "        \"system\": \"" << json_escape(series.modality.system) << "\",\n";
            json << "        \"code\": \"" << json_escape(series.modality.code) << "\"";
            if (!series.modality.display.empty()) {
                json << ",\n        \"display\": \"" << json_escape(series.modality.display) << "\"";
            }
            json << "\n      }";

            if (series.description.has_value()) {
                json << ",\n      \"description\": \"" << json_escape(*series.description) << "\"";
            }

            if (series.number_of_instances.has_value()) {
                json << ",\n      \"numberOfInstances\": " << *series.number_of_instances;
            }

            json << "\n    }";
            if (i < study.series.size() - 1) {
                json << ",";
            }
            json << "\n";
        }
        json << "  ]";
    }

    json << "\n}";
    return json.str();
}

std::expected<fhir_service_request, fhir_dicom_error>
service_request_from_json(const std::string& json) {
    // Check resourceType
    std::string resource_type = extract_json_string(json, "resourceType");
    if (resource_type != "ServiceRequest") {
        return std::unexpected(fhir_dicom_error::unsupported_resource_type);
    }

    fhir_service_request result;

    // Extract basic fields
    result.id = extract_json_string(json, "id");
    result.status = extract_json_string(json, "status");
    if (result.status.empty()) {
        result.status = "active";
    }

    result.intent = extract_json_string(json, "intent");
    if (result.intent.empty()) {
        result.intent = "order";
    }

    result.priority = extract_json_string(json, "priority");
    if (result.priority.empty()) {
        result.priority = "routine";
    }

    // Extract occurrenceDateTime
    std::string occurrence = extract_json_string(json, "occurrenceDateTime");
    if (!occurrence.empty()) {
        result.occurrence_date_time = occurrence;
    }

    // Note: Full JSON parsing for nested objects (code, subject, etc.)
    // would require a proper JSON parser. This is a simplified implementation.

    return result;
}

std::string service_request_to_json(const fhir_service_request& request) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"resourceType\": \"ServiceRequest\"";

    if (!request.id.empty()) {
        json << ",\n  \"id\": \"" << json_escape(request.id) << "\"";
    }

    // Identifiers
    if (!request.identifiers.empty()) {
        json << ",\n  \"identifier\": [\n";
        for (size_t i = 0; i < request.identifiers.size(); ++i) {
            const auto& [system, value] = request.identifiers[i];
            json << "    {\n";
            json << "      \"system\": \"" << json_escape(system) << "\",\n";
            json << "      \"value\": \"" << json_escape(value) << "\"\n";
            json << "    }";
            if (i < request.identifiers.size() - 1) {
                json << ",";
            }
            json << "\n";
        }
        json << "  ]";
    }

    json << ",\n  \"status\": \"" << json_escape(request.status) << "\"";
    json << ",\n  \"intent\": \"" << json_escape(request.intent) << "\"";

    // Code
    if (!request.code.coding.empty() || request.code.text.has_value()) {
        json << ",\n  \"code\": {\n";
        bool first = true;

        if (!request.code.coding.empty()) {
            json << "    \"coding\": [\n";
            for (size_t i = 0; i < request.code.coding.size(); ++i) {
                const auto& coding = request.code.coding[i];
                json << "      {\n";
                json << "        \"system\": \"" << json_escape(coding.system) << "\",\n";
                json << "        \"code\": \"" << json_escape(coding.code) << "\",\n";
                json << "        \"display\": \"" << json_escape(coding.display) << "\"\n";
                json << "      }";
                if (i < request.code.coding.size() - 1) {
                    json << ",";
                }
                json << "\n";
            }
            json << "    ]";
            first = false;
        }

        if (request.code.text.has_value()) {
            if (!first) {
                json << ",\n";
            }
            json << "    \"text\": \"" << json_escape(*request.code.text) << "\"";
        }
        json << "\n  }";
    }

    // Subject
    if (request.subject.reference.has_value()) {
        json << ",\n  \"subject\": {\n";
        json << "    \"reference\": \"" << json_escape(*request.subject.reference) << "\"";
        if (request.subject.display.has_value()) {
            json << ",\n    \"display\": \"" << json_escape(*request.subject.display) << "\"";
        }
        json << "\n  }";
    }

    // OccurrenceDateTime
    if (request.occurrence_date_time.has_value()) {
        json << ",\n  \"occurrenceDateTime\": \"" << json_escape(*request.occurrence_date_time) << "\"";
    }

    // Priority
    json << ",\n  \"priority\": \"" << json_escape(request.priority) << "\"";

    json << "\n}";
    return json.str();
}

}  // namespace pacs::bridge::mapping
