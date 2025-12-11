#ifndef PACS_BRIDGE_EMR_ENCOUNTER_CONTEXT_H
#define PACS_BRIDGE_EMR_ENCOUNTER_CONTEXT_H

/**
 * @file encounter_context.h
 * @brief Encounter context retrieval from EMR
 *
 * Implements encounter (visit) context retrieval from EMR to link imaging
 * studies with patient visits. This enables proper billing, clinical context,
 * and continuity of care by associating images with the correct hospital
 * encounter.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/106
 * @see https://www.hl7.org/fhir/encounter.html
 */

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pacs::bridge::emr {

// Forward declarations
class fhir_client;

// =============================================================================
// Encounter Error Codes (-1080 to -1099)
// =============================================================================

/**
 * @brief Encounter query specific error codes
 *
 * Allocated range: -1080 to -1099
 */
enum class encounter_error : int {
    /** Encounter not found in EMR */
    not_found = -1080,

    /** Encounter query failed */
    query_failed = -1081,

    /** Multiple active encounters found */
    multiple_active = -1082,

    /** Encounter has ended */
    encounter_ended = -1083,

    /** Invalid encounter data in response */
    invalid_data = -1084,

    /** Visit number not found */
    visit_not_found = -1085,

    /** Invalid encounter status */
    invalid_status = -1086,

    /** Location not found */
    location_not_found = -1087,

    /** Practitioner not found */
    practitioner_not_found = -1088,

    /** Parse error */
    parse_failed = -1089
};

/**
 * @brief Convert encounter_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(encounter_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of encounter error
 */
[[nodiscard]] constexpr const char* to_string(encounter_error error) noexcept {
    switch (error) {
        case encounter_error::not_found:
            return "Encounter not found in EMR";
        case encounter_error::query_failed:
            return "Encounter query failed";
        case encounter_error::multiple_active:
            return "Multiple active encounters found";
        case encounter_error::encounter_ended:
            return "Encounter has ended";
        case encounter_error::invalid_data:
            return "Invalid encounter data in response";
        case encounter_error::visit_not_found:
            return "Visit number not found";
        case encounter_error::invalid_status:
            return "Invalid encounter status";
        case encounter_error::location_not_found:
            return "Location not found";
        case encounter_error::practitioner_not_found:
            return "Practitioner not found";
        case encounter_error::parse_failed:
            return "Encounter data parsing failed";
        default:
            return "Unknown encounter error";
    }
}

// =============================================================================
// Encounter Status
// =============================================================================

/**
 * @brief Encounter status codes (FHIR EncounterStatus)
 */
enum class encounter_status {
    /** Encounter is being planned */
    planned,

    /** Patient has arrived but encounter has not started */
    arrived,

    /** Patient is on their way to facility */
    triaged,

    /** Encounter is in progress */
    in_progress,

    /** Patient has left facility but encounter is not finished */
    on_leave,

    /** Encounter is finished */
    finished,

    /** Encounter was cancelled */
    cancelled,

    /** Patient did not show up */
    entered_in_error,

    /** Unknown status */
    unknown
};

/**
 * @brief Convert encounter_status to FHIR status string
 */
[[nodiscard]] constexpr std::string_view to_string(
    encounter_status status) noexcept {
    switch (status) {
        case encounter_status::planned:
            return "planned";
        case encounter_status::arrived:
            return "arrived";
        case encounter_status::triaged:
            return "triaged";
        case encounter_status::in_progress:
            return "in-progress";
        case encounter_status::on_leave:
            return "onleave";
        case encounter_status::finished:
            return "finished";
        case encounter_status::cancelled:
            return "cancelled";
        case encounter_status::entered_in_error:
            return "entered-in-error";
        case encounter_status::unknown:
        default:
            return "unknown";
    }
}

/**
 * @brief Parse encounter status from FHIR string
 */
[[nodiscard]] encounter_status parse_encounter_status(
    std::string_view status) noexcept;

/**
 * @brief Check if encounter is active
 */
[[nodiscard]] constexpr bool is_active(encounter_status status) noexcept {
    return status == encounter_status::planned ||
           status == encounter_status::arrived ||
           status == encounter_status::triaged ||
           status == encounter_status::in_progress ||
           status == encounter_status::on_leave;
}

// =============================================================================
// Encounter Class
// =============================================================================

/**
 * @brief Encounter class codes (ActCode)
 */
enum class encounter_class {
    /** Inpatient admission */
    inpatient,

    /** Outpatient visit */
    outpatient,

    /** Emergency room visit */
    emergency,

    /** Home health visit */
    home_health,

    /** Virtual encounter */
    virtual_visit,

    /** Pre-admission */
    preadmission,

    /** Short stay (observation) */
    short_stay,

    /** Unknown class */
    unknown
};

/**
 * @brief Convert encounter_class to ActCode string
 */
[[nodiscard]] constexpr std::string_view to_code(
    encounter_class enc_class) noexcept {
    switch (enc_class) {
        case encounter_class::inpatient:
            return "IMP";
        case encounter_class::outpatient:
            return "AMB";
        case encounter_class::emergency:
            return "EMER";
        case encounter_class::home_health:
            return "HH";
        case encounter_class::virtual_visit:
            return "VR";
        case encounter_class::preadmission:
            return "PRENC";
        case encounter_class::short_stay:
            return "SS";
        case encounter_class::unknown:
        default:
            return "UNK";
    }
}

/**
 * @brief Convert encounter_class to display string
 */
[[nodiscard]] constexpr std::string_view to_display(
    encounter_class enc_class) noexcept {
    switch (enc_class) {
        case encounter_class::inpatient:
            return "inpatient encounter";
        case encounter_class::outpatient:
            return "ambulatory";
        case encounter_class::emergency:
            return "emergency";
        case encounter_class::home_health:
            return "home health";
        case encounter_class::virtual_visit:
            return "virtual";
        case encounter_class::preadmission:
            return "pre-admission";
        case encounter_class::short_stay:
            return "short stay";
        case encounter_class::unknown:
        default:
            return "unknown";
    }
}

/**
 * @brief Parse encounter class from ActCode string
 */
[[nodiscard]] encounter_class parse_encounter_class(
    std::string_view code) noexcept;

// =============================================================================
// Location Info
// =============================================================================

/**
 * @brief Location information from encounter
 */
struct location_info {
    /** Location resource ID */
    std::string id;

    /** Location display name */
    std::string display;

    /** Location type (e.g., "ward", "room", "bed") */
    std::string type;

    /** Location status (active, planned, reserved) */
    std::string status;

    /** Physical type (e.g., "ro" for room, "bd" for bed) */
    std::string physical_type;

    /** Period of stay at this location */
    std::optional<std::chrono::system_clock::time_point> start_time;
    std::optional<std::chrono::system_clock::time_point> end_time;
};

// =============================================================================
// Practitioner Info
// =============================================================================

/**
 * @brief Practitioner information from encounter
 */
struct practitioner_info {
    /** Practitioner resource ID */
    std::string id;

    /** Practitioner display name */
    std::string display;

    /** Practitioner type (attending, consulting, admitting, etc.) */
    std::string type;

    /** Period of involvement */
    std::optional<std::chrono::system_clock::time_point> start_time;
    std::optional<std::chrono::system_clock::time_point> end_time;
};

// =============================================================================
// Encounter Info
// =============================================================================

/**
 * @brief Encounter information retrieved from EMR
 */
struct encounter_info {
    /** Encounter resource ID */
    std::string id;

    /** Visit/Encounter number */
    std::string visit_number;

    /** Encounter status */
    encounter_status status = encounter_status::unknown;

    /** Encounter class (inpatient, outpatient, emergency) */
    encounter_class enc_class = encounter_class::unknown;

    /** Encounter class display text */
    std::string class_display;

    /** Encounter type codes */
    std::vector<std::string> type_codes;

    /** Encounter type display text */
    std::string type_display;

    /** Patient reference (e.g., "Patient/123") */
    std::string patient_reference;

    /** Encounter period start */
    std::optional<std::chrono::system_clock::time_point> start_time;

    /** Encounter period end */
    std::optional<std::chrono::system_clock::time_point> end_time;

    /** Locations during encounter */
    std::vector<location_info> locations;

    /** Participants (practitioners) */
    std::vector<practitioner_info> participants;

    /** Service provider organization reference */
    std::string service_provider;

    /** Service provider display name */
    std::string service_provider_display;

    /** Reason for encounter (text) */
    std::string reason_text;

    /** Diagnosis references */
    std::vector<std::string> diagnosis_references;

    // Convenience methods

    /**
     * @brief Get FHIR reference string (e.g., "Encounter/123")
     */
    [[nodiscard]] std::string to_reference() const;

    /**
     * @brief Check if encounter is currently active
     */
    [[nodiscard]] bool is_active() const noexcept;

    /**
     * @brief Get current location (if any)
     */
    [[nodiscard]] std::optional<location_info> current_location() const;

    /**
     * @brief Get attending physician (if any)
     */
    [[nodiscard]] std::optional<practitioner_info> attending_physician() const;

    /**
     * @brief Get performing physician (if any)
     */
    [[nodiscard]] std::optional<practitioner_info> performing_physician() const;
};

// =============================================================================
// Encounter Context Provider
// =============================================================================

/**
 * @brief Configuration for encounter context provider
 */
struct encounter_context_config {
    /** FHIR client to use for queries */
    std::shared_ptr<fhir_client> client;

    /** Include location details in encounter queries */
    bool include_location = true;

    /** Include participant details in encounter queries */
    bool include_participants = true;

    /** Cache encounter data (TTL in seconds) */
    std::chrono::seconds cache_ttl{300};

    /** Maximum number of cached encounters */
    size_t max_cache_size = 1000;
};

/**
 * @brief Encounter context provider for retrieving visit information
 *
 * Retrieves encounter/visit context from EMR systems via FHIR API.
 * Supports querying by encounter ID, visit number, or patient ID.
 *
 * Example usage:
 * @code
 * encounter_context_config config;
 * config.client = fhir_client;
 *
 * encounter_context_provider provider(config);
 *
 * // Get encounter by ID
 * auto result = provider.get_encounter("enc-12345");
 * if (result.has_value()) {
 *     auto& encounter = result.value();
 *     std::cout << "Visit: " << encounter.visit_number << std::endl;
 * }
 *
 * // Find active encounter for patient
 * auto active = provider.find_active_encounter("patient-123");
 * @endcode
 */
class encounter_context_provider {
public:
    /**
     * @brief Result type for encounter operations
     */
    template <typename T>
    using result = std::variant<T, encounter_error>;

    /**
     * @brief Constructor with configuration
     */
    explicit encounter_context_provider(encounter_context_config config);

    /**
     * @brief Default destructor
     */
    ~encounter_context_provider();

    // Non-copyable, movable
    encounter_context_provider(const encounter_context_provider&) = delete;
    encounter_context_provider& operator=(const encounter_context_provider&) =
        delete;
    encounter_context_provider(encounter_context_provider&&) noexcept;
    encounter_context_provider& operator=(
        encounter_context_provider&&) noexcept;

    // ==========================================================================
    // Query Operations
    // ==========================================================================

    /**
     * @brief Get encounter by FHIR ID
     *
     * @param encounter_id FHIR Encounter resource ID
     * @return encounter_info or error
     */
    [[nodiscard]] result<encounter_info> get_encounter(
        std::string_view encounter_id);

    /**
     * @brief Find encounter by visit number
     *
     * @param visit_number Visit/account number
     * @param system Optional identifier system URI
     * @return encounter_info or error
     */
    [[nodiscard]] result<encounter_info> find_by_visit_number(
        std::string_view visit_number,
        std::optional<std::string_view> system = std::nullopt);

    /**
     * @brief Find active encounter for patient
     *
     * @param patient_id Patient FHIR ID or reference
     * @return encounter_info (if found) or nullopt, or error
     */
    [[nodiscard]] result<std::optional<encounter_info>> find_active_encounter(
        std::string_view patient_id);

    /**
     * @brief Find encounters for patient
     *
     * @param patient_id Patient FHIR ID or reference
     * @param status_filter Optional status filter
     * @param max_results Maximum number of results
     * @return Vector of encounters or error
     */
    [[nodiscard]] result<std::vector<encounter_info>> find_encounters(
        std::string_view patient_id,
        std::optional<encounter_status> status_filter = std::nullopt,
        size_t max_results = 10);

    // ==========================================================================
    // Cache Operations
    // ==========================================================================

    /**
     * @brief Clear encounter cache
     */
    void clear_cache();

    /**
     * @brief Get cache statistics
     */
    struct cache_stats {
        size_t total_entries;
        size_t cache_hits;
        size_t cache_misses;
    };
    [[nodiscard]] cache_stats get_cache_stats() const;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// FHIR Encounter Parser
// =============================================================================

/**
 * @brief Parse encounter_info from FHIR JSON
 *
 * @param json FHIR Encounter resource JSON
 * @return encounter_info or error
 */
[[nodiscard]] std::variant<encounter_info, encounter_error>
parse_encounter_json(std::string_view json);

/**
 * @brief Parse encounter_info from JSON object (nlohmann::json-like)
 *
 * @param json_obj JSON object containing Encounter resource
 * @return encounter_info or error
 */
template <typename JsonType>
[[nodiscard]] std::variant<encounter_info, encounter_error>
parse_encounter_from_json(const JsonType& json_obj);

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_ENCOUNTER_CONTEXT_H
