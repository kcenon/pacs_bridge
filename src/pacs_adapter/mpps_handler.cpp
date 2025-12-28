/**
 * @file mpps_handler.cpp
 * @brief Implementation of MPPS event handler for pacs_system
 *
 * This file provides two implementations:
 *   - Real implementation when PACS_BRIDGE_HAS_PACS_SYSTEM is defined
 *   - Stub implementation for standalone builds
 */

#include "pacs/bridge/pacs_adapter/mpps_handler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

// =============================================================================
// pacs_system Integration Headers (conditional)
// =============================================================================

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
#include <pacs/core/dicom_dataset.hpp>
#include <pacs/core/dicom_tag_constants.hpp>
#include <pacs/services/mpps_scp.hpp>
#include <pacs/storage/index_database.hpp>
#include <pacs/storage/mpps_record.hpp>
#endif  // PACS_BRIDGE_HAS_PACS_SYSTEM

namespace pacs::bridge::pacs_adapter {

// =============================================================================
// Utility Function Implementations
// =============================================================================

std::expected<void, mpps_error> validate_mpps_dataset(const mpps_dataset& dataset) {
    // Check required identification attributes
    if (dataset.sop_instance_uid.empty()) {
        return std::unexpected(mpps_error::missing_attribute);
    }

    // Check required relationship attributes
    if (dataset.accession_number.empty() && dataset.scheduled_procedure_step_id.empty()) {
        // At least one of these should be present for tracking
        return std::unexpected(mpps_error::missing_attribute);
    }

    // For completed/discontinued, check timing
    if (dataset.status != mpps_event::in_progress) {
        if (dataset.end_date.empty() || dataset.end_time.empty()) {
            // End timing is required for completed/discontinued
            // This is a warning, not an error - some systems may not provide it
        }
    }

    return {};
}

std::optional<std::chrono::seconds>
calculate_procedure_duration(const mpps_dataset& dataset) {
    if (dataset.start_date.empty() || dataset.start_time.empty() ||
        dataset.end_date.empty() || dataset.end_time.empty()) {
        return std::nullopt;
    }

    // Parse DICOM date/time format: YYYYMMDD and HHMMSS
    auto parse_datetime = [](const std::string& date, const std::string& time)
        -> std::optional<std::chrono::system_clock::time_point> {
        if (date.size() < 8 || time.size() < 6) {
            return std::nullopt;
        }

        std::tm tm = {};
        tm.tm_year = std::stoi(date.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(date.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(date.substr(6, 2));
        tm.tm_hour = std::stoi(time.substr(0, 2));
        tm.tm_min = std::stoi(time.substr(2, 2));
        tm.tm_sec = std::stoi(time.substr(4, 2));

        auto time_t_val = std::mktime(&tm);
        if (time_t_val == -1) {
            return std::nullopt;
        }

        return std::chrono::system_clock::from_time_t(time_t_val);
    };

    auto start = parse_datetime(dataset.start_date, dataset.start_time);
    auto end = parse_datetime(dataset.end_date, dataset.end_time);

    if (!start || !end) {
        return std::nullopt;
    }

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(*end - *start);

    // Sanity check - duration should be positive and reasonable
    if (duration.count() < 0 || duration.count() > 86400 * 7) {  // Max 7 days
        return std::nullopt;
    }

    return duration;
}

// =============================================================================
// pacs_system Integration (when PACS_BRIDGE_HAS_PACS_SYSTEM is defined)
// =============================================================================

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM

namespace {

// DICOM tag definitions for MPPS attributes
constexpr pacs::core::dicom_tag tag_sop_instance_uid{0x0008, 0x0018};
constexpr pacs::core::dicom_tag tag_study_instance_uid{0x0020, 0x000D};
constexpr pacs::core::dicom_tag tag_accession_number{0x0008, 0x0050};
constexpr pacs::core::dicom_tag tag_patient_id{0x0010, 0x0020};
constexpr pacs::core::dicom_tag tag_patient_name{0x0010, 0x0010};
constexpr pacs::core::dicom_tag tag_modality{0x0008, 0x0060};
constexpr pacs::core::dicom_tag tag_referring_physician{0x0008, 0x0090};
constexpr pacs::core::dicom_tag tag_station_name{0x0008, 0x1010};
constexpr pacs::core::dicom_tag tag_series_description{0x0008, 0x103E};
constexpr pacs::core::dicom_tag tag_performing_physician{0x0008, 0x1050};
constexpr pacs::core::dicom_tag tag_protocol_name{0x0018, 0x1030};
constexpr pacs::core::dicom_tag tag_series_instance_uid{0x0020, 0x000E};

// MPPS-specific tags (Group 0x0040)
constexpr pacs::core::dicom_tag tag_scheduled_procedure_step_id{0x0040, 0x0009};
constexpr pacs::core::dicom_tag tag_requested_procedure_id{0x0040, 0x1001};
constexpr pacs::core::dicom_tag tag_performed_station_ae_title{0x0040, 0x0241};
constexpr pacs::core::dicom_tag tag_performed_station_name{0x0040, 0x0242};
constexpr pacs::core::dicom_tag tag_performed_procedure_step_start_date{0x0040, 0x0244};
constexpr pacs::core::dicom_tag tag_performed_procedure_step_start_time{0x0040, 0x0245};
constexpr pacs::core::dicom_tag tag_performed_procedure_step_end_date{0x0040, 0x0250};
constexpr pacs::core::dicom_tag tag_performed_procedure_step_end_time{0x0040, 0x0251};
constexpr pacs::core::dicom_tag tag_performed_procedure_step_status{0x0040, 0x0252};
constexpr pacs::core::dicom_tag tag_performed_procedure_step_id{0x0040, 0x0253};
constexpr pacs::core::dicom_tag tag_performed_procedure_step_description{0x0040, 0x0254};
constexpr pacs::core::dicom_tag tag_scheduled_step_attributes_sequence{0x0040, 0x0270};
constexpr pacs::core::dicom_tag tag_performed_series_sequence{0x0040, 0x0340};

/**
 * @brief Convert pacs_system mpps_status to pacs_bridge mpps_event
 */
mpps_event convert_status(pacs::services::mpps_status status) {
    switch (status) {
        case pacs::services::mpps_status::in_progress:
            return mpps_event::in_progress;
        case pacs::services::mpps_status::completed:
            return mpps_event::completed;
        case pacs::services::mpps_status::discontinued:
            return mpps_event::discontinued;
        default:
            return mpps_event::in_progress;
    }
}

/**
 * @brief Extract performed series from DICOM dataset
 *
 * Extracts series information from the Performed Series Sequence (0040,0340).
 */
std::vector<mpps_performed_series> extract_performed_series(
    [[maybe_unused]] const pacs::core::dicom_dataset& data) {
    std::vector<mpps_performed_series> series_list;

    // Note: Sequence extraction requires pacs_system sequence support
    // For now, return empty list - full implementation depends on
    // pacs_system's sequence element API
    // TODO: Implement when pacs_system provides sequence iteration API

    return series_list;
}

/**
 * @brief Convert pacs_system mpps_instance to pacs_bridge mpps_dataset
 *
 * Extracts all relevant DICOM attributes from the pacs_system MPPS instance
 * and populates a pacs_bridge mpps_dataset structure.
 */
mpps_dataset convert_to_mpps_dataset(
    const pacs::services::mpps_instance& instance) {
    mpps_dataset dataset;

    // SOP Instance Identification
    dataset.sop_instance_uid = instance.sop_instance_uid;

    // Status
    dataset.status = convert_status(instance.status);

    // Extract attributes from DICOM dataset
    const auto& data = instance.data;

    // Study/Procedure relationship
    dataset.study_instance_uid = data.get_string(tag_study_instance_uid);
    dataset.accession_number = data.get_string(tag_accession_number);
    dataset.scheduled_procedure_step_id = data.get_string(tag_scheduled_procedure_step_id);
    dataset.performed_procedure_step_id = data.get_string(tag_performed_procedure_step_id);
    dataset.requested_procedure_id = data.get_string(tag_requested_procedure_id);

    // Patient information
    dataset.patient_id = data.get_string(tag_patient_id);
    dataset.patient_name = data.get_string(tag_patient_name);

    // Procedure step description
    dataset.performed_procedure_description =
        data.get_string(tag_performed_procedure_step_description);

    // Timing information
    dataset.start_date = data.get_string(tag_performed_procedure_step_start_date);
    dataset.start_time = data.get_string(tag_performed_procedure_step_start_time);
    dataset.end_date = data.get_string(tag_performed_procedure_step_end_date);
    dataset.end_time = data.get_string(tag_performed_procedure_step_end_time);

    // Modality and station
    dataset.modality = data.get_string(tag_modality);
    dataset.station_ae_title = instance.station_ae;
    if (dataset.station_ae_title.empty()) {
        dataset.station_ae_title = data.get_string(tag_performed_station_ae_title);
    }
    dataset.station_name = data.get_string(tag_performed_station_name);
    if (dataset.station_name.empty()) {
        dataset.station_name = data.get_string(tag_station_name);
    }

    // Additional information
    dataset.referring_physician = data.get_string(tag_referring_physician);

    // Performed series (if available)
    dataset.performed_series = extract_performed_series(data);

    return dataset;
}

/**
 * @brief Convert N-SET modifications and status to mpps_dataset
 */
mpps_dataset convert_n_set_to_mpps_dataset(
    const std::string& sop_instance_uid,
    const pacs::core::dicom_dataset& modifications,
    pacs::services::mpps_status new_status) {
    mpps_dataset dataset;

    // SOP Instance Identification
    dataset.sop_instance_uid = sop_instance_uid;

    // Status from N-SET
    dataset.status = convert_status(new_status);

    // Extract attributes from modifications
    dataset.study_instance_uid = modifications.get_string(tag_study_instance_uid);
    dataset.accession_number = modifications.get_string(tag_accession_number);
    dataset.scheduled_procedure_step_id = modifications.get_string(tag_scheduled_procedure_step_id);
    dataset.performed_procedure_step_id = modifications.get_string(tag_performed_procedure_step_id);
    dataset.requested_procedure_id = modifications.get_string(tag_requested_procedure_id);

    // Patient information
    dataset.patient_id = modifications.get_string(tag_patient_id);
    dataset.patient_name = modifications.get_string(tag_patient_name);

    // Procedure step description
    dataset.performed_procedure_description =
        modifications.get_string(tag_performed_procedure_step_description);

    // Timing information (N-SET typically provides end time)
    dataset.start_date = modifications.get_string(tag_performed_procedure_step_start_date);
    dataset.start_time = modifications.get_string(tag_performed_procedure_step_start_time);
    dataset.end_date = modifications.get_string(tag_performed_procedure_step_end_date);
    dataset.end_time = modifications.get_string(tag_performed_procedure_step_end_time);

    // Modality and station
    dataset.modality = modifications.get_string(tag_modality);
    dataset.station_ae_title = modifications.get_string(tag_performed_station_ae_title);
    dataset.station_name = modifications.get_string(tag_performed_station_name);

    // Additional information
    dataset.referring_physician = modifications.get_string(tag_referring_physician);

    // Performed series
    dataset.performed_series = extract_performed_series(modifications);

    return dataset;
}

// =============================================================================
// mpps_dataset <-> mpps_record Conversion Utilities
// =============================================================================

/**
 * @brief Convert mpps_dataset to pacs_system mpps_record for persistence
 */
[[nodiscard]] pacs::storage::mpps_record
to_mpps_record(const mpps_dataset& dataset) {
    pacs::storage::mpps_record record;

    record.mpps_uid = dataset.sop_instance_uid;
    record.status = to_string(dataset.status);

    // Combine date and time for start_datetime
    record.start_datetime = dataset.start_date;
    if (!dataset.start_time.empty()) {
        record.start_datetime += dataset.start_time;
    }

    // Combine date and time for end_datetime
    record.end_datetime = dataset.end_date;
    if (!dataset.end_time.empty()) {
        record.end_datetime += dataset.end_time;
    }

    record.station_ae = dataset.station_ae_title;
    record.station_name = dataset.station_name;
    record.modality = dataset.modality;
    record.study_uid = dataset.study_instance_uid;
    record.accession_no = dataset.accession_number;
    record.scheduled_step_id = dataset.scheduled_procedure_step_id;
    record.requested_proc_id = dataset.requested_procedure_id;

    // Serialize performed series to JSON
    if (!dataset.performed_series.empty()) {
        std::ostringstream oss;
        oss << "[";
        bool first = true;
        for (const auto& series : dataset.performed_series) {
            if (!first) {
                oss << ",";
            }
            first = false;
            oss << "{\"uid\":\"" << series.series_instance_uid << "\","
                << "\"protocol\":\"" << series.protocol_name << "\","
                << "\"instances\":" << series.number_of_instances << "}";
        }
        oss << "]";
        record.performed_series = oss.str();
    }

    return record;
}

/**
 * @brief Convert pacs_system mpps_record to mpps_dataset
 */
[[nodiscard]] mpps_dataset
from_mpps_record(const pacs::storage::mpps_record& record) {
    mpps_dataset dataset;

    dataset.sop_instance_uid = record.mpps_uid;

    // Parse status
    auto status_opt = parse_mpps_status(record.status);
    dataset.status = status_opt.value_or(mpps_event::in_progress);

    // Parse start_datetime (YYYYMMDDHHMMSS)
    if (record.start_datetime.size() >= 8) {
        dataset.start_date = record.start_datetime.substr(0, 8);
        if (record.start_datetime.size() >= 14) {
            dataset.start_time = record.start_datetime.substr(8, 6);
        }
    }

    // Parse end_datetime
    if (record.end_datetime.size() >= 8) {
        dataset.end_date = record.end_datetime.substr(0, 8);
        if (record.end_datetime.size() >= 14) {
            dataset.end_time = record.end_datetime.substr(8, 6);
        }
    }

    dataset.station_ae_title = record.station_ae;
    dataset.station_name = record.station_name;
    dataset.modality = record.modality;
    dataset.study_instance_uid = record.study_uid;
    dataset.accession_number = record.accession_no;
    dataset.scheduled_procedure_step_id = record.scheduled_step_id;
    dataset.requested_procedure_id = record.requested_proc_id;

    // Note: performed_series JSON parsing could be added here if needed

    return dataset;
}

/**
 * @brief Convert mpps_handler::mpps_query_params to pacs_system mpps_query
 */
[[nodiscard]] pacs::storage::mpps_query
to_mpps_query(const mpps_handler::mpps_query_params& params) {
    pacs::storage::mpps_query query;

    if (params.sop_instance_uid) {
        query.mpps_uid = *params.sop_instance_uid;
    }
    if (params.status) {
        query.status = to_string(*params.status);
    }
    if (params.station_ae_title) {
        query.station_ae = *params.station_ae_title;
    }
    if (params.modality) {
        query.modality = *params.modality;
    }
    if (params.study_instance_uid) {
        query.study_uid = *params.study_instance_uid;
    }
    if (params.accession_number) {
        query.accession_no = *params.accession_number;
    }
    if (params.limit > 0) {
        query.limit = params.limit;
    }

    return query;
}

}  // anonymous namespace

/**
 * @brief Real MPPS handler implementation using pacs_system
 *
 * This implementation uses pacs_system's mpps_scp service to receive
 * actual MPPS N-CREATE and N-SET events from modalities.
 */
class mpps_handler_real_impl : public mpps_handler {
public:
    explicit mpps_handler_real_impl(const mpps_handler_config& config)
        : config_(config)
        , running_(false)
        , connected_(false)
        , start_time_(std::chrono::steady_clock::now())
        , mpps_scp_(std::make_unique<pacs::services::mpps_scp>()) {
        setup_mpps_handlers();
        initialize_database();
    }

    ~mpps_handler_real_impl() override {
        stop(true);
    }

    // =========================================================================
    // Callback Management
    // =========================================================================

    void set_callback(mpps_callback callback) override {
        std::unique_lock lock(callback_mutex_);
        callback_ = std::move(callback);
    }

    void clear_callback() override {
        std::unique_lock lock(callback_mutex_);
        callback_ = nullptr;
    }

    bool has_callback() const noexcept override {
        std::shared_lock lock(callback_mutex_);
        return callback_ != nullptr;
    }

    // =========================================================================
    // Lifecycle Management
    // =========================================================================

    std::expected<void, mpps_error> start() override {
        std::unique_lock lock(state_mutex_);

        if (running_) {
            return std::unexpected(mpps_error::already_registered);
        }

        stats_.connect_attempts++;

        // Note: In pacs_system integration, the mpps_scp is typically
        // registered with a DICOM server that handles association management.
        // The bridge acts as a listener through the registered handlers.

        running_ = true;
        connected_ = true;
        stats_.connect_successes++;
        start_time_ = std::chrono::steady_clock::now();

        // Start monitor thread for connection health
        if (config_.auto_reconnect) {
            stop_requested_ = false;
            monitor_thread_ = std::thread(&mpps_handler_real_impl::monitor_connection, this);
        }

        return {};
    }

    void stop(bool graceful) override {
        {
            std::unique_lock lock(state_mutex_);

            if (!running_) {
                return;
            }

            running_ = false;
        }

        stop_requested_ = true;

        if (graceful) {
            std::unique_lock lock(pending_mutex_);
            // Wait for any pending event processing
        }

        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }

        connected_ = false;
    }

    bool is_running() const noexcept override {
        std::shared_lock lock(state_mutex_);
        return running_;
    }

    bool is_connected() const noexcept override {
        std::shared_lock lock(state_mutex_);
        return connected_;
    }

    // =========================================================================
    // Event Handlers (public interface for testing)
    // =========================================================================

    std::expected<void, mpps_error>
    on_n_create(const mpps_dataset& dataset) override {
        auto validation = validate_mpps_dataset(dataset);
        if (!validation) {
            update_stats_error();
            return validation;
        }

        mpps_dataset processed_dataset = dataset;
        if (processed_dataset.status != mpps_event::in_progress) {
            processed_dataset.status = mpps_event::in_progress;
        }

        // Persist MPPS record to database
        auto persist_result = persist_mpps_create(processed_dataset);
        if (!persist_result) {
            // Log warning but continue - persistence failure is not fatal
            update_persistence_error();
        }

        {
            std::unique_lock lock(stats_mutex_);
            stats_.n_create_count++;
            stats_.in_progress_count++;
            stats_.last_event_time = std::chrono::system_clock::now();
        }

        auto callback_result = invoke_callback(mpps_event::in_progress, processed_dataset);
        if (!callback_result) {
            return callback_result;
        }

        if (config_.verbose_logging) {
            log_mpps_event("N-CREATE", processed_dataset);
        }

        return {};
    }

    std::expected<void, mpps_error>
    on_n_set(const mpps_dataset& dataset) override {
        auto validation = validate_mpps_dataset(dataset);
        if (!validation) {
            update_stats_error();
            return validation;
        }

        mpps_event event = dataset.status;

        // Update MPPS record in database
        auto persist_result = persist_mpps_update(dataset);
        if (!persist_result) {
            // Log warning but continue - persistence failure is not fatal
            update_persistence_error();
        }

        {
            std::unique_lock lock(stats_mutex_);
            stats_.n_set_count++;

            switch (event) {
                case mpps_event::in_progress:
                    stats_.in_progress_count++;
                    break;
                case mpps_event::completed:
                    stats_.completed_count++;
                    break;
                case mpps_event::discontinued:
                    stats_.discontinued_count++;
                    break;
            }

            stats_.last_event_time = std::chrono::system_clock::now();
        }

        auto callback_result = invoke_callback(event, dataset);
        if (!callback_result) {
            return callback_result;
        }

        if (config_.verbose_logging) {
            log_mpps_event("N-SET", dataset);
        }

        return {};
    }

    // =========================================================================
    // Statistics
    // =========================================================================

    statistics get_statistics() const override {
        std::shared_lock lock(stats_mutex_);
        statistics stats = stats_;

        auto now = std::chrono::steady_clock::now();
        stats.uptime = std::chrono::duration_cast<std::chrono::seconds>(
            now - start_time_);

        return stats;
    }

    void reset_statistics() override {
        std::unique_lock lock(stats_mutex_);
        stats_ = statistics{};
        start_time_ = std::chrono::steady_clock::now();
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    const mpps_handler_config& config() const noexcept override {
        return config_;
    }

    // =========================================================================
    // Persistence Operations (public interface)
    // =========================================================================

    bool is_persistence_enabled() const noexcept override {
        return db_ != nullptr;
    }

    std::expected<std::optional<mpps_dataset>, mpps_error>
    query_mpps(std::string_view sop_instance_uid) const override {
        if (!db_) {
            return std::unexpected(mpps_error::persistence_disabled);
        }

        auto record = db_->find_mpps(sop_instance_uid);
        if (!record) {
            return std::nullopt;
        }

        return from_mpps_record(*record);
    }

    std::expected<std::vector<mpps_dataset>, mpps_error>
    query_mpps(const mpps_query_params& params) const override {
        if (!db_) {
            return std::unexpected(mpps_error::persistence_disabled);
        }

        auto query = to_mpps_query(params);
        auto result = db_->search_mpps(query);

        if (result.is_err()) {
            return std::unexpected(mpps_error::database_error);
        }

        std::vector<mpps_dataset> datasets;
        datasets.reserve(result.value().size());

        for (const auto& record : result.value()) {
            datasets.push_back(from_mpps_record(record));
        }

        return datasets;
    }

    std::expected<std::vector<mpps_dataset>, mpps_error>
    get_active_mpps() const override {
        if (!db_) {
            return std::unexpected(mpps_error::persistence_disabled);
        }

        pacs::storage::mpps_query query;
        query.status = "IN PROGRESS";

        auto result = db_->search_mpps(query);

        if (result.is_err()) {
            return std::unexpected(mpps_error::database_error);
        }

        std::vector<mpps_dataset> datasets;
        datasets.reserve(result.value().size());

        for (const auto& record : result.value()) {
            datasets.push_back(from_mpps_record(record));
        }

        return datasets;
    }

    std::expected<std::vector<mpps_dataset>, mpps_error>
    get_pending_mpps_for_station(std::string_view station_ae_title) const override {
        if (!db_) {
            return std::unexpected(mpps_error::persistence_disabled);
        }

        auto result = db_->list_active_mpps(station_ae_title);

        if (result.is_err()) {
            return std::unexpected(mpps_error::database_error);
        }

        std::vector<mpps_dataset> datasets;
        datasets.reserve(result.value().size());

        for (const auto& record : result.value()) {
            datasets.push_back(from_mpps_record(record));
        }

        return datasets;
    }

    persistence_stats get_persistence_stats() const override {
        std::shared_lock lock(persist_stats_mutex_);
        return persist_stats_;
    }

    // =========================================================================
    // pacs_system Integration
    // =========================================================================

    /**
     * @brief Get the underlying pacs_system mpps_scp service
     *
     * This allows the bridge_server to register the MPPS SCP with
     * the DICOM server for handling incoming MPPS requests.
     */
    pacs::services::mpps_scp& get_mpps_scp() noexcept {
        return *mpps_scp_;
    }

private:
    /**
     * @brief Setup MPPS handlers to bridge pacs_system events to callbacks
     */
    void setup_mpps_handlers() {
        // Set N-CREATE handler
        mpps_scp_->set_create_handler(
            [this](const pacs::services::mpps_instance& instance)
                -> pacs::network::Result<std::monostate> {
                auto dataset = convert_to_mpps_dataset(instance);
                auto result = this->on_n_create(dataset);
                if (!result) {
                    return pacs::network::Result<std::monostate>::err(
                        static_cast<int>(result.error()),
                        to_string(result.error()));
                }
                return pacs::network::Result<std::monostate>::ok({});
            });

        // Set N-SET handler
        mpps_scp_->set_set_handler(
            [this](const std::string& sop_instance_uid,
                   const pacs::core::dicom_dataset& modifications,
                   pacs::services::mpps_status new_status)
                -> pacs::network::Result<std::monostate> {
                auto dataset = convert_n_set_to_mpps_dataset(
                    sop_instance_uid, modifications, new_status);
                auto result = this->on_n_set(dataset);
                if (!result) {
                    return pacs::network::Result<std::monostate>::err(
                        static_cast<int>(result.error()),
                        to_string(result.error()));
                }
                return pacs::network::Result<std::monostate>::ok({});
            });
    }

    void monitor_connection() {
        while (!stop_requested_) {
            std::this_thread::sleep_for(config_.reconnect_delay);

            if (stop_requested_) {
                break;
            }

            std::shared_lock lock(state_mutex_);
            // Connection monitoring logic
            // In real integration, check DICOM server health
        }
    }

    std::expected<void, mpps_error>
    invoke_callback(mpps_event event, const mpps_dataset& dataset) {
        std::shared_lock lock(callback_mutex_);

        if (!callback_) {
            return {};
        }

        try {
            callback_(event, dataset);
            return {};
        } catch (const std::exception&) {
            std::unique_lock stats_lock(stats_mutex_);
            stats_.callback_error_count++;
            return std::unexpected(mpps_error::callback_failed);
        } catch (...) {
            std::unique_lock stats_lock(stats_mutex_);
            stats_.callback_error_count++;
            return std::unexpected(mpps_error::callback_failed);
        }
    }

    void update_stats_error() {
        std::unique_lock lock(stats_mutex_);
        stats_.parse_error_count++;
    }

    void log_mpps_event(const char* operation, const mpps_dataset& dataset) {
        std::ostringstream oss;
        oss << "MPPS " << operation << " [REAL]: "
            << "SOP=" << dataset.sop_instance_uid << ", "
            << "Accession=" << dataset.accession_number << ", "
            << "Status=" << to_string(dataset.status) << ", "
            << "Patient=" << dataset.patient_id;

        // Use logger_system when integrated
        // logger_system::info(oss.str());
    }

    // =========================================================================
    // Persistence Methods
    // =========================================================================

    void initialize_database() {
        if (!config_.enable_persistence) {
            return;
        }

        std::string db_path = config_.database_path;
        if (db_path.empty()) {
            // Use default path based on configuration
            db_path = "mpps_bridge.db";
        }

        auto db_result = pacs::storage::index_database::open(db_path);
        if (db_result.is_err()) {
            // Log warning - persistence will be disabled
            return;
        }

        db_ = std::move(db_result.value());

        // Recover pending MPPS if configured
        if (config_.recover_on_startup) {
            recover_pending_mpps();
        }
    }

    std::expected<void, mpps_error>
    persist_mpps_create(const mpps_dataset& dataset) {
        if (!db_) {
            return std::unexpected(mpps_error::persistence_disabled);
        }

        auto record = to_mpps_record(dataset);
        auto result = db_->create_mpps(record);

        if (result.is_err()) {
            return std::unexpected(mpps_error::database_error);
        }

        {
            std::unique_lock lock(persist_stats_mutex_);
            persist_stats_.total_persisted++;
            persist_stats_.in_progress_count++;
        }

        return {};
    }

    std::expected<void, mpps_error>
    persist_mpps_update(const mpps_dataset& dataset) {
        if (!db_) {
            return std::unexpected(mpps_error::persistence_disabled);
        }

        auto record = to_mpps_record(dataset);
        auto result = db_->update_mpps(record);

        if (result.is_err()) {
            return std::unexpected(mpps_error::database_error);
        }

        {
            std::unique_lock lock(persist_stats_mutex_);
            if (dataset.status == mpps_event::completed) {
                persist_stats_.completed_count++;
                if (persist_stats_.in_progress_count > 0) {
                    persist_stats_.in_progress_count--;
                }
            } else if (dataset.status == mpps_event::discontinued) {
                persist_stats_.discontinued_count++;
                if (persist_stats_.in_progress_count > 0) {
                    persist_stats_.in_progress_count--;
                }
            }
        }

        return {};
    }

    void recover_pending_mpps() {
        if (!db_) {
            return;
        }

        pacs::storage::mpps_query query;
        query.status = "IN PROGRESS";

        auto result = db_->search_mpps(query);
        if (result.is_err()) {
            return;
        }

        const auto& records = result.value();

        // Filter by max_recovery_age if configured
        auto now = std::chrono::system_clock::now();
        size_t recovered = 0;

        for (const auto& record : records) {
            if (config_.max_recovery_age.count() > 0) {
                auto age = std::chrono::duration_cast<std::chrono::hours>(
                    now - record.created_at);
                if (age > config_.max_recovery_age) {
                    continue;
                }
            }
            recovered++;
        }

        {
            std::unique_lock lock(persist_stats_mutex_);
            persist_stats_.recovered_count = recovered;
            persist_stats_.in_progress_count = recovered;
        }
    }

    void update_persistence_error() {
        std::unique_lock lock(persist_stats_mutex_);
        persist_stats_.persistence_failures++;
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    mpps_handler_config config_;

    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    std::atomic<bool> stop_requested_{false};
    mutable std::shared_mutex state_mutex_;

    mpps_callback callback_;
    mutable std::shared_mutex callback_mutex_;

    std::mutex pending_mutex_;
    std::thread monitor_thread_;

    statistics stats_;
    mutable std::shared_mutex stats_mutex_;
    std::chrono::steady_clock::time_point start_time_;

    // pacs_system MPPS SCP service
    std::unique_ptr<pacs::services::mpps_scp> mpps_scp_;

    // Database for persistence
    std::unique_ptr<pacs::storage::index_database> db_;
    persistence_stats persist_stats_;
    mutable std::shared_mutex persist_stats_mutex_;
};

#endif  // PACS_BRIDGE_HAS_PACS_SYSTEM

// =============================================================================
// Stub Implementation Class (standalone build)
// =============================================================================

class mpps_handler_stub_impl : public mpps_handler {
public:
    explicit mpps_handler_stub_impl(const mpps_handler_config& config)
        : config_(config)
        , running_(false)
        , connected_(false)
        , start_time_(std::chrono::steady_clock::now()) {}

    ~mpps_handler_stub_impl() override {
        stop(true);
    }

    // =========================================================================
    // Callback Management
    // =========================================================================

    void set_callback(mpps_callback callback) override {
        std::unique_lock lock(callback_mutex_);
        callback_ = std::move(callback);
    }

    void clear_callback() override {
        std::unique_lock lock(callback_mutex_);
        callback_ = nullptr;
    }

    bool has_callback() const noexcept override {
        std::shared_lock lock(callback_mutex_);
        return callback_ != nullptr;
    }

    // =========================================================================
    // Lifecycle Management
    // =========================================================================

    std::expected<void, mpps_error> start() override {
        std::unique_lock lock(state_mutex_);

        if (running_) {
            return std::unexpected(mpps_error::already_registered);
        }

        stats_.connect_attempts++;

        // Attempt to connect to pacs_system MPPS SCP
        auto connect_result = connect_to_pacs();
        if (!connect_result) {
            return connect_result;
        }

        // Register as MPPS event listener
        auto register_result = register_as_listener();
        if (!register_result) {
            disconnect_from_pacs();
            return register_result;
        }

        running_ = true;
        connected_ = true;
        stats_.connect_successes++;
        start_time_ = std::chrono::steady_clock::now();

        // Start event processing thread
        if (config_.auto_reconnect) {
            monitor_thread_ = std::thread(&mpps_handler_stub_impl::monitor_connection, this);
        }

        return {};
    }

    void stop(bool graceful) override {
        {
            std::unique_lock lock(state_mutex_);

            if (!running_) {
                return;
            }

            running_ = false;
        }

        // Signal monitor thread to stop
        stop_requested_ = true;

        // Wait for pending events if graceful
        if (graceful) {
            std::unique_lock lock(pending_mutex_);
            // In a real implementation, wait for pending events to complete
        }

        // Stop monitor thread
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }

        // Unregister and disconnect
        unregister_listener();
        disconnect_from_pacs();

        connected_ = false;
    }

    bool is_running() const noexcept override {
        std::shared_lock lock(state_mutex_);
        return running_;
    }

    bool is_connected() const noexcept override {
        std::shared_lock lock(state_mutex_);
        return connected_;
    }

    // =========================================================================
    // Event Handlers
    // =========================================================================

    std::expected<void, mpps_error>
    on_n_create(const mpps_dataset& dataset) override {
        // Validate dataset
        auto validation = validate_mpps_dataset(dataset);
        if (!validation) {
            update_stats_error();
            return validation;
        }

        // N-CREATE should always be IN PROGRESS
        mpps_dataset processed_dataset = dataset;
        if (processed_dataset.status != mpps_event::in_progress) {
            // Log warning but continue processing
            processed_dataset.status = mpps_event::in_progress;
        }

        // Cache MPPS for in-memory persistence
        cache_mpps(processed_dataset);

        // Update statistics
        {
            std::unique_lock lock(stats_mutex_);
            stats_.n_create_count++;
            stats_.in_progress_count++;
            stats_.last_event_time = std::chrono::system_clock::now();
        }

        // Invoke callback
        auto callback_result = invoke_callback(mpps_event::in_progress, processed_dataset);
        if (!callback_result) {
            return callback_result;
        }

        // Log event if verbose
        if (config_.verbose_logging) {
            log_mpps_event("N-CREATE", processed_dataset);
        }

        return {};
    }

    std::expected<void, mpps_error>
    on_n_set(const mpps_dataset& dataset) override {
        // Validate dataset
        auto validation = validate_mpps_dataset(dataset);
        if (!validation) {
            update_stats_error();
            return validation;
        }

        // Update MPPS cache for in-memory persistence
        cache_mpps(dataset);

        // Determine event type from status
        mpps_event event = dataset.status;
        if (event == mpps_event::in_progress) {
            // N-SET with IN PROGRESS is unusual but valid (status update)
            // Some systems send this for intermediate updates
        }

        // Update statistics
        {
            std::unique_lock lock(stats_mutex_);
            stats_.n_set_count++;

            switch (event) {
                case mpps_event::in_progress:
                    stats_.in_progress_count++;
                    break;
                case mpps_event::completed:
                    stats_.completed_count++;
                    break;
                case mpps_event::discontinued:
                    stats_.discontinued_count++;
                    break;
            }

            stats_.last_event_time = std::chrono::system_clock::now();
        }

        // Invoke callback
        auto callback_result = invoke_callback(event, dataset);
        if (!callback_result) {
            return callback_result;
        }

        // Log event if verbose
        if (config_.verbose_logging) {
            log_mpps_event("N-SET", dataset);
        }

        return {};
    }

    // =========================================================================
    // Statistics
    // =========================================================================

    statistics get_statistics() const override {
        std::shared_lock lock(stats_mutex_);
        statistics stats = stats_;

        // Calculate uptime
        auto now = std::chrono::steady_clock::now();
        stats.uptime = std::chrono::duration_cast<std::chrono::seconds>(
            now - start_time_);

        return stats;
    }

    void reset_statistics() override {
        std::unique_lock lock(stats_mutex_);
        stats_ = statistics{};
        start_time_ = std::chrono::steady_clock::now();
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    const mpps_handler_config& config() const noexcept override {
        return config_;
    }

    // =========================================================================
    // Persistence Operations (stub - in-memory only for standalone build)
    // =========================================================================

    bool is_persistence_enabled() const noexcept override {
        // Stub always returns true for testing
        return true;
    }

    std::expected<std::optional<mpps_dataset>, mpps_error>
    query_mpps(std::string_view sop_instance_uid) const override {
        std::shared_lock lock(mpps_cache_mutex_);
        auto it = mpps_cache_.find(std::string(sop_instance_uid));
        if (it != mpps_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::expected<std::vector<mpps_dataset>, mpps_error>
    query_mpps(const mpps_query_params& params) const override {
        std::shared_lock lock(mpps_cache_mutex_);
        std::vector<mpps_dataset> results;

        for (const auto& [uid, dataset] : mpps_cache_) {
            bool match = true;

            if (params.sop_instance_uid && *params.sop_instance_uid != uid) {
                match = false;
            }
            if (params.status && *params.status != dataset.status) {
                match = false;
            }
            if (params.station_ae_title && *params.station_ae_title != dataset.station_ae_title) {
                match = false;
            }
            if (params.modality && *params.modality != dataset.modality) {
                match = false;
            }

            if (match) {
                results.push_back(dataset);
                if (params.limit > 0 && results.size() >= params.limit) {
                    break;
                }
            }
        }

        return results;
    }

    std::expected<std::vector<mpps_dataset>, mpps_error>
    get_active_mpps() const override {
        std::shared_lock lock(mpps_cache_mutex_);
        std::vector<mpps_dataset> results;

        for (const auto& [uid, dataset] : mpps_cache_) {
            if (dataset.status == mpps_event::in_progress) {
                results.push_back(dataset);
            }
        }

        return results;
    }

    std::expected<std::vector<mpps_dataset>, mpps_error>
    get_pending_mpps_for_station(std::string_view station_ae_title) const override {
        std::shared_lock lock(mpps_cache_mutex_);
        std::vector<mpps_dataset> results;

        for (const auto& [uid, dataset] : mpps_cache_) {
            if (dataset.status == mpps_event::in_progress &&
                dataset.station_ae_title == station_ae_title) {
                results.push_back(dataset);
            }
        }

        return results;
    }

    persistence_stats get_persistence_stats() const override {
        std::shared_lock lock(persist_stats_mutex_);
        return persist_stats_;
    }

private:
    // =========================================================================
    // Connection Helpers
    // =========================================================================

    std::expected<void, mpps_error> connect_to_pacs() {
        // In a real implementation, this would:
        // 1. Create TCP connection to pacs_host:pacs_port
        // 2. Perform DICOM association negotiation
        // 3. Verify MPPS SOP Class support

        // Simulated connection delay
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // For now, simulate successful connection
        // In real implementation, check actual connection result
        return {};
    }

    void disconnect_from_pacs() {
        // In a real implementation, this would:
        // 1. Send Association Release
        // 2. Close TCP connection

        connected_ = false;
    }

    std::expected<void, mpps_error> register_as_listener() {
        // In a real implementation, this would register with pacs_system's
        // MPPS SCP to receive N-CREATE and N-SET notifications

        // pacs_system would call our on_n_create/on_n_set methods
        // when MPPS operations occur

        return {};
    }

    void unregister_listener() {
        // In a real implementation, unregister from pacs_system
    }

    void monitor_connection() {
        while (!stop_requested_) {
            std::this_thread::sleep_for(config_.reconnect_delay);

            if (stop_requested_) {
                break;
            }

            // Check connection health
            std::shared_lock lock(state_mutex_);
            if (running_ && !connected_) {
                // Attempt reconnection
                lock.unlock();
                attempt_reconnection();
            }
        }
    }

    void attempt_reconnection() {
        if (config_.max_reconnect_attempts > 0 &&
            stats_.reconnections >= config_.max_reconnect_attempts) {
            // Max attempts reached
            return;
        }

        auto result = connect_to_pacs();
        if (result) {
            auto reg_result = register_as_listener();
            if (reg_result) {
                std::unique_lock lock(state_mutex_);
                connected_ = true;
                stats_.reconnections++;
            }
        }
    }

    // =========================================================================
    // Callback Invocation
    // =========================================================================

    std::expected<void, mpps_error>
    invoke_callback(mpps_event event, const mpps_dataset& dataset) {
        std::shared_lock lock(callback_mutex_);

        if (!callback_) {
            // No callback registered - not an error
            return {};
        }

        try {
            callback_(event, dataset);
            return {};
        } catch (const std::exception& e) {
            // Callback threw an exception
            std::unique_lock stats_lock(stats_mutex_);
            stats_.callback_error_count++;
            return std::unexpected(mpps_error::callback_failed);
        } catch (...) {
            std::unique_lock stats_lock(stats_mutex_);
            stats_.callback_error_count++;
            return std::unexpected(mpps_error::callback_failed);
        }
    }

    // =========================================================================
    // Statistics Helpers
    // =========================================================================

    void update_stats_error() {
        std::unique_lock lock(stats_mutex_);
        stats_.parse_error_count++;
    }

    // =========================================================================
    // Logging Helpers
    // =========================================================================

    void log_mpps_event(const char* operation, const mpps_dataset& dataset) {
        // Stub implementation logging - not connected to real pacs_system
        std::ostringstream oss;
        oss << "MPPS " << operation << " [STUB]: "
            << "SOP=" << dataset.sop_instance_uid << ", "
            << "Accession=" << dataset.accession_number << ", "
            << "Status=" << to_string(dataset.status) << ", "
            << "Patient=" << dataset.patient_id;

        // Log using logger_system when integrated
        // logger_system::info(oss.str());
    }

    // =========================================================================
    // Persistence Helpers (stub in-memory storage)
    // =========================================================================

    void cache_mpps(const mpps_dataset& dataset) {
        std::unique_lock lock(mpps_cache_mutex_);
        mpps_cache_[dataset.sop_instance_uid] = dataset;

        std::unique_lock stats_lock(persist_stats_mutex_);
        if (dataset.status == mpps_event::in_progress) {
            persist_stats_.total_persisted++;
            persist_stats_.in_progress_count++;
        } else if (dataset.status == mpps_event::completed) {
            persist_stats_.completed_count++;
            if (persist_stats_.in_progress_count > 0) {
                persist_stats_.in_progress_count--;
            }
        } else if (dataset.status == mpps_event::discontinued) {
            persist_stats_.discontinued_count++;
            if (persist_stats_.in_progress_count > 0) {
                persist_stats_.in_progress_count--;
            }
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    mpps_handler_config config_;

    // State
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    std::atomic<bool> stop_requested_{false};
    mutable std::shared_mutex state_mutex_;

    // Callback
    mpps_callback callback_;
    mutable std::shared_mutex callback_mutex_;

    // Pending events
    std::mutex pending_mutex_;

    // Monitor thread
    std::thread monitor_thread_;

    // Statistics
    statistics stats_;
    mutable std::shared_mutex stats_mutex_;
    std::chrono::steady_clock::time_point start_time_;

    // Persistence (stub - in-memory cache)
    std::unordered_map<std::string, mpps_dataset> mpps_cache_;
    mutable std::shared_mutex mpps_cache_mutex_;
    persistence_stats persist_stats_;
    mutable std::shared_mutex persist_stats_mutex_;
};

// =============================================================================
// Factory Function
// =============================================================================

std::unique_ptr<mpps_handler>
mpps_handler::create(const mpps_handler_config& config) {
#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
    // Use real pacs_system integration
    return std::make_unique<mpps_handler_real_impl>(config);
#else
    // Use stub implementation for standalone builds
    return std::make_unique<mpps_handler_stub_impl>(config);
#endif
}

}  // namespace pacs::bridge::pacs_adapter
