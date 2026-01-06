#ifndef PACS_BRIDGE_EMR_RESULT_POSTER_H
#define PACS_BRIDGE_EMR_RESULT_POSTER_H

/**
 * @file result_poster.h
 * @brief EMR Result Posting Interface
 *
 * Provides automatic posting of imaging results (DiagnosticReport) to external
 * EMR systems when studies are completed. Closes the loop in the imaging
 * workflow by notifying EMR of study completion and availability.
 *
 * Features:
 *   - MPPS completion to DiagnosticReport conversion
 *   - Automatic posting to EMR FHIR endpoint
 *   - Status update support (preliminary → final)
 *   - Duplicate detection and handling
 *   - Result tracking for updates
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/105
 * @see https://www.hl7.org/fhir/diagnosticreport.html
 */

#include "emr_types.h"
#include "fhir_client.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace pacs::bridge::emr {

// Forward declarations
class diagnostic_report_builder;
class result_tracker;

// =============================================================================
// Result Status
// =============================================================================

/**
 * @brief DiagnosticReport status codes
 *
 * Maps to FHIR DiagnosticReport.status value set.
 * @see https://www.hl7.org/fhir/valueset-diagnostic-report-status.html
 */
enum class result_status {
    /** Report has been registered but not yet started */
    registered,

    /** Some results are available but not complete */
    partial,

    /** Preliminary report - may be subject to change */
    preliminary,

    /** Final report - complete and verified */
    final_report,

    /** Report has been modified after being finalized */
    amended,

    /** Report was corrected after being finalized */
    corrected,

    /** Report is appended to a prior report */
    appended,

    /** Report was cancelled */
    cancelled,

    /** Report was entered in error */
    entered_in_error,

    /** Status is unknown */
    unknown
};

/**
 * @brief Convert result_status to FHIR status code string
 */
[[nodiscard]] constexpr std::string_view to_string(result_status status) noexcept {
    switch (status) {
        case result_status::registered: return "registered";
        case result_status::partial: return "partial";
        case result_status::preliminary: return "preliminary";
        case result_status::final_report: return "final";
        case result_status::amended: return "amended";
        case result_status::corrected: return "corrected";
        case result_status::appended: return "appended";
        case result_status::cancelled: return "cancelled";
        case result_status::entered_in_error: return "entered-in-error";
        case result_status::unknown: return "unknown";
    }
    return "unknown";
}

/**
 * @brief Parse result_status from FHIR status code string
 */
[[nodiscard]] std::optional<result_status> parse_result_status(
    std::string_view status_str) noexcept;

// =============================================================================
// Result Posting Error Codes (-1060 to -1079)
// =============================================================================

/**
 * @brief Result posting specific error codes
 *
 * Allocated range: -1060 to -1079
 * @see docs/api/error-codes.md for error code allocation
 */
enum class result_error : int {
    /** Failed to post result to EMR */
    post_failed = -1060,

    /** Failed to update existing result */
    update_failed = -1061,

    /** Duplicate result detected */
    duplicate = -1062,

    /** Invalid result data */
    invalid_data = -1063,

    /** EMR rejected the result */
    rejected = -1064,

    /** Result not found for update */
    not_found = -1065,

    /** Invalid status transition */
    invalid_status_transition = -1066,

    /** Missing required reference (patient, study, etc.) */
    missing_reference = -1067,

    /** Failed to build DiagnosticReport */
    build_failed = -1068,

    /** Tracker operation failed */
    tracker_error = -1069
};

/**
 * @brief Convert result_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(result_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of result error
 */
[[nodiscard]] constexpr const char* to_string(result_error error) noexcept {
    switch (error) {
        case result_error::post_failed:
            return "Failed to post result to EMR";
        case result_error::update_failed:
            return "Failed to update existing result";
        case result_error::duplicate:
            return "Duplicate result detected";
        case result_error::invalid_data:
            return "Invalid result data";
        case result_error::rejected:
            return "EMR rejected the result";
        case result_error::not_found:
            return "Result not found";
        case result_error::invalid_status_transition:
            return "Invalid status transition";
        case result_error::missing_reference:
            return "Missing required reference";
        case result_error::build_failed:
            return "Failed to build DiagnosticReport";
        case result_error::tracker_error:
            return "Result tracker operation failed";
    }
    return "Unknown result error";
}

/**
 * @brief Convert result_error to error_info for Result<T>
 *
 * @param error Result error code
 * @param details Optional additional details
 * @return error_info for use with Result<T>
 */
[[nodiscard]] inline error_info to_error_info(
    result_error error,
    const std::string& details = "") {
    return error_info{
        static_cast<int>(error),
        to_string(error),
        "emr.result",
        details
    };
}

// =============================================================================
// Study Result Data
// =============================================================================

/**
 * @brief Study result data for posting to EMR
 *
 * Contains all information needed to create a DiagnosticReport resource.
 */
struct study_result {
    /** DICOM Study Instance UID (required) */
    std::string study_instance_uid;

    /** Patient ID / MRN (required) */
    std::string patient_id;

    /** Patient FHIR reference (e.g., "Patient/123") */
    std::optional<std::string> patient_reference;

    /** Accession number */
    std::optional<std::string> accession_number;

    /** Modality (e.g., "CT", "MR", "US") */
    std::string modality;

    /** Study description */
    std::optional<std::string> study_description;

    /** Performing physician name */
    std::optional<std::string> performing_physician;

    /** Performing physician FHIR reference */
    std::optional<std::string> performer_reference;

    /** Study date/time (ISO 8601 format) */
    std::string study_datetime;

    /** Report status */
    result_status status{result_status::final_report};

    /** Clinical conclusion / findings */
    std::optional<std::string> conclusion;

    /** Conclusion code (SNOMED CT) */
    std::optional<std::string> conclusion_code;

    /** ImagingStudy FHIR reference */
    std::optional<std::string> imaging_study_reference;

    /** ServiceRequest/Order FHIR reference */
    std::optional<std::string> based_on_reference;

    /** Encounter FHIR reference */
    std::optional<std::string> encounter_reference;

    /**
     * @brief Validate required fields
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !study_instance_uid.empty() &&
               !patient_id.empty() &&
               !modality.empty() &&
               !study_datetime.empty();
    }
};

// =============================================================================
// Posted Result Reference
// =============================================================================

/**
 * @brief Reference to a posted result
 *
 * Contains identifiers and metadata for tracking posted results.
 */
struct posted_result {
    /** DiagnosticReport resource ID */
    std::string report_id;

    /** Study Instance UID this report is for */
    std::string study_instance_uid;

    /** Accession number */
    std::optional<std::string> accession_number;

    /** Current report status */
    result_status status{result_status::final_report};

    /** ETag/version for optimistic locking */
    std::optional<std::string> etag;

    /** Time when result was posted */
    std::chrono::system_clock::time_point posted_at;

    /** Time when result was last updated */
    std::optional<std::chrono::system_clock::time_point> updated_at;
};

// =============================================================================
// Result Poster Configuration
// =============================================================================

/**
 * @brief Configuration for EMR result poster
 */
struct result_poster_config {
    /** Enable duplicate checking before posting */
    bool check_duplicates{true};

    /** Enable result tracking for updates */
    bool enable_tracking{true};

    /** Auto-create ImagingStudy reference if not provided */
    bool auto_create_imaging_study_ref{false};

    /** Auto-lookup patient reference if not provided */
    bool auto_lookup_patient{true};

    /** Default LOINC code for imaging studies */
    std::string default_loinc_code{"18748-4"};

    /** Default LOINC display text */
    std::string default_loinc_display{"Diagnostic imaging study"};

    /** Organization identifier for issued reports */
    std::optional<std::string> issuing_organization;

    /** Retry policy for failed posts */
    retry_policy retry;

    /** Timeout for post operations */
    std::chrono::seconds post_timeout{30};
};

// =============================================================================
// EMR Result Poster
// =============================================================================

/**
 * @brief EMR Result Poster Service
 *
 * Posts imaging results (DiagnosticReport) to external EMR systems.
 * Handles the complete workflow from MPPS completion to EMR notification.
 *
 * Thread-safe: All operations are thread-safe for concurrent use.
 *
 * @example Basic Usage
 * ```cpp
 * // Create FHIR client
 * fhir_client_config fhir_config;
 * fhir_config.base_url = "https://emr.hospital.local/fhir";
 *
 * auto client = std::make_shared<fhir_client>(fhir_config);
 *
 * // Create result poster
 * result_poster_config poster_config;
 * poster_config.check_duplicates = true;
 *
 * emr_result_poster poster(client, poster_config);
 *
 * // Post a result
 * study_result result;
 * result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
 * result.patient_id = "MRN12345";
 * result.modality = "CT";
 * result.study_datetime = "2025-01-15T10:30:00Z";
 * result.status = result_status::final_report;
 * result.conclusion = "No acute findings.";
 *
 * auto post_result = poster.post_result(result);
 * if (post_result) {
 *     std::cout << "Posted: " << post_result->report_id << "\n";
 * } else {
 *     std::cerr << "Error: " << to_string(post_result.error()) << "\n";
 * }
 * ```
 *
 * @example Update Existing Result
 * ```cpp
 * // Update status from preliminary to final
 * study_result updated_result;
 * updated_result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
 * updated_result.patient_id = "MRN12345";
 * updated_result.modality = "CT";
 * updated_result.study_datetime = "2025-01-15T10:30:00Z";
 * updated_result.status = result_status::final_report;
 * updated_result.conclusion = "Final: No acute findings.";
 *
 * auto update_result = poster.update_result("report-123", updated_result);
 * if (update_result) {
 *     std::cout << "Updated successfully\n";
 * }
 * ```
 */
class emr_result_poster {
public:
    /**
     * @brief Construct with FHIR client
     *
     * @param client FHIR client for EMR communication
     * @param config Poster configuration
     */
    explicit emr_result_poster(
        std::shared_ptr<fhir_client> client,
        const result_poster_config& config = {});

    /**
     * @brief Destructor
     */
    ~emr_result_poster();

    // Non-copyable, movable
    emr_result_poster(const emr_result_poster&) = delete;
    emr_result_poster& operator=(const emr_result_poster&) = delete;
    emr_result_poster(emr_result_poster&&) noexcept;
    emr_result_poster& operator=(emr_result_poster&&) noexcept;

    // =========================================================================
    // Post Operations
    // =========================================================================

    /**
     * @brief Post a new result to EMR
     *
     * Creates a DiagnosticReport resource and posts it to the EMR.
     * If duplicate checking is enabled, checks for existing reports first.
     *
     * @param result Study result data
     * @return Posted result reference or error
     */
    [[nodiscard]] auto post_result(const study_result& result)
        -> Result<posted_result>;

    /**
     * @brief Update an existing result
     *
     * Updates a previously posted DiagnosticReport with new data.
     * Uses optimistic locking if ETag is available.
     *
     * @param report_id DiagnosticReport resource ID
     * @param result Updated result data
     * @return Success or error
     */
    [[nodiscard]] auto update_result(std::string_view report_id,
                                     const study_result& result)
        -> VoidResult;

    /**
     * @brief Update result status only
     *
     * Updates just the status of a DiagnosticReport without changing
     * other fields.
     *
     * @param report_id DiagnosticReport resource ID
     * @param new_status New status value
     * @return Success or error
     */
    [[nodiscard]] auto update_status(std::string_view report_id,
                                     result_status new_status)
        -> VoidResult;

    // =========================================================================
    // Query Operations
    // =========================================================================

    /**
     * @brief Find existing DiagnosticReport by accession number
     *
     * Searches for an existing DiagnosticReport that matches
     * the given accession number.
     *
     * @param accession_number Accession number to search
     * @return Report ID if found, nullopt if not found, or error
     */
    [[nodiscard]] auto find_by_accession(std::string_view accession_number)
        -> Result<std::optional<std::string>>;

    /**
     * @brief Find existing DiagnosticReport by Study Instance UID
     *
     * @param study_uid Study Instance UID to search
     * @return Report ID if found, nullopt if not found, or error
     */
    [[nodiscard]] auto find_by_study_uid(std::string_view study_uid)
        -> Result<std::optional<std::string>>;

    /**
     * @brief Get a posted result by report ID
     *
     * @param report_id DiagnosticReport resource ID
     * @return Posted result data or error
     */
    [[nodiscard]] auto get_result(std::string_view report_id)
        -> Result<posted_result>;

    // =========================================================================
    // Tracking
    // =========================================================================

    /**
     * @brief Get tracked result by Study Instance UID
     *
     * Retrieves a locally tracked result without querying the EMR.
     *
     * @param study_uid Study Instance UID
     * @return Posted result if tracked, nullopt otherwise
     */
    [[nodiscard]] std::optional<posted_result> get_tracked_result(
        std::string_view study_uid) const;

    /**
     * @brief Clear tracking data
     */
    void clear_tracking();

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const result_poster_config& config() const noexcept;

    /**
     * @brief Update configuration
     *
     * @param config New configuration
     */
    void set_config(const result_poster_config& config);

    /**
     * @brief Set custom result tracker
     *
     * @param tracker Result tracker implementation
     */
    void set_tracker(std::shared_ptr<result_tracker> tracker);

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Posting statistics
     */
    struct statistics {
        size_t total_posts{0};
        size_t successful_posts{0};
        size_t failed_posts{0};
        size_t duplicate_skips{0};
        size_t updates{0};
        std::chrono::milliseconds total_post_time{0};
    };

    /**
     * @brief Get posting statistics
     */
    [[nodiscard]] statistics get_statistics() const noexcept;

    /**
     * @brief Reset statistics
     */
    void reset_statistics() noexcept;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_RESULT_POSTER_H
