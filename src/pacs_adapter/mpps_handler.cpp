/**
 * @file mpps_handler.cpp
 * @brief Unified MPPS event handler implementation
 *
 * Single implementation that delegates persistence to integration::mpps_adapter,
 * eliminating the previous dual-implementation (#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM).
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/334
 */

#include "pacs/bridge/pacs_adapter/mpps_handler.h"
#include "pacs/bridge/integration/pacs_adapter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <thread>

namespace integration = pacs::bridge::integration;

namespace pacs::bridge::pacs_adapter {

// =============================================================================
// IExecutor Job Implementations (when available)
// =============================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD

/**
 * @brief Job implementation for periodic monitor execution
 *
 * Wraps a single monitor iteration. Designed to be rescheduled
 * after completion for continuous monitoring.
 */
class mpps_monitor_job : public kcenon::common::interfaces::IJob {
public:
    explicit mpps_monitor_job(std::function<void()> monitor_func)
        : monitor_func_(std::move(monitor_func)) {}

    kcenon::common::VoidResult execute() override {
        if (monitor_func_) {
            monitor_func_();
        }
        return std::monostate{};
    }

    std::string get_name() const override { return "mpps_monitor"; }

private:
    std::function<void()> monitor_func_;
};

#endif  // PACS_BRIDGE_STANDALONE_BUILD

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
// mpps_dataset <-> integration::mpps_record Conversion Utilities
// =============================================================================

namespace {

/**
 * @brief Parse DICOM date+time strings into a time_point
 */
[[nodiscard]] std::optional<std::chrono::system_clock::time_point>
parse_dicom_datetime(const std::string& date, const std::string& time) {
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
}

/**
 * @brief Format a time_point into DICOM date (YYYYMMDD) and time (HHMMSS) strings
 */
void format_dicom_datetime(std::chrono::system_clock::time_point tp,
                           std::string& out_date,
                           std::string& out_time) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = {};
    localtime_r(&time_t_val, &tm);

    char date_buf[9];
    char time_buf[7];
    std::snprintf(date_buf, sizeof(date_buf), "%04d%02d%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    std::snprintf(time_buf, sizeof(time_buf), "%02d%02d%02d",
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    out_date = date_buf;
    out_time = time_buf;
}

/**
 * @brief Convert mpps_dataset to integration::mpps_record
 */
[[nodiscard]] integration::mpps_record
to_integration_record(const mpps_dataset& dataset) {
    integration::mpps_record record;

    record.sop_instance_uid = dataset.sop_instance_uid;
    record.scheduled_procedure_step_id = dataset.scheduled_procedure_step_id;
    record.performed_procedure_step_id = dataset.performed_procedure_step_id;
    record.performed_station_ae_title = dataset.station_ae_title;
    record.performed_station_name = dataset.station_name;
    record.status = to_string(dataset.status);
    record.study_instance_uid = dataset.study_instance_uid;
    record.patient_id = dataset.patient_id;
    record.patient_name = dataset.patient_name;

    // Extended fields
    record.accession_number = dataset.accession_number;
    record.modality = dataset.modality;
    record.performed_procedure_description = dataset.performed_procedure_description;
    record.referring_physician = dataset.referring_physician;
    record.requested_procedure_id = dataset.requested_procedure_id;
    record.discontinuation_reason = dataset.discontinuation_reason;

    // Timing
    auto start_tp = parse_dicom_datetime(dataset.start_date, dataset.start_time);
    if (start_tp) {
        record.start_datetime = *start_tp;
    }

    if (!dataset.end_date.empty() && !dataset.end_time.empty()) {
        record.end_datetime = parse_dicom_datetime(dataset.end_date, dataset.end_time);
    }

    // Series UIDs
    for (const auto& series : dataset.performed_series) {
        record.series_instance_uids.push_back(series.series_instance_uid);
    }

    return record;
}

/**
 * @brief Convert integration::mpps_record back to mpps_dataset
 */
[[nodiscard]] mpps_dataset
from_integration_record(const integration::mpps_record& record) {
    mpps_dataset dataset;

    dataset.sop_instance_uid = record.sop_instance_uid;
    dataset.scheduled_procedure_step_id = record.scheduled_procedure_step_id;
    dataset.performed_procedure_step_id = record.performed_procedure_step_id;
    dataset.station_ae_title = record.performed_station_ae_title;
    dataset.station_name = record.performed_station_name;
    dataset.study_instance_uid = record.study_instance_uid;
    dataset.patient_id = record.patient_id;
    dataset.patient_name = record.patient_name;

    // Extended fields
    dataset.accession_number = record.accession_number;
    dataset.modality = record.modality;
    dataset.performed_procedure_description = record.performed_procedure_description;
    dataset.referring_physician = record.referring_physician;
    dataset.requested_procedure_id = record.requested_procedure_id;
    dataset.discontinuation_reason = record.discontinuation_reason;

    // Parse status
    auto status_opt = parse_mpps_status(record.status);
    dataset.status = status_opt.value_or(mpps_event::in_progress);

    // Timing
    format_dicom_datetime(record.start_datetime, dataset.start_date, dataset.start_time);

    if (record.end_datetime) {
        format_dicom_datetime(*record.end_datetime, dataset.end_date, dataset.end_time);
    }

    // Series (as performed series stubs with UIDs)
    for (const auto& uid : record.series_instance_uids) {
        mpps_performed_series series;
        series.series_instance_uid = uid;
        dataset.performed_series.push_back(std::move(series));
    }

    return dataset;
}

/**
 * @brief Convert mpps_handler::mpps_query_params to integration::mpps_query_params
 */
[[nodiscard]] integration::mpps_query_params
to_integration_query(const mpps_handler::mpps_query_params& params) {
    integration::mpps_query_params query;

    if (params.status) {
        query.status = to_string(*params.status);
    }
    if (params.station_ae_title) {
        query.station_ae_title = *params.station_ae_title;
    }
    if (params.modality) {
        query.modality = *params.modality;
    }
    if (params.study_instance_uid) {
        query.study_instance_uid = *params.study_instance_uid;
    }
    if (params.accession_number) {
        query.accession_number = *params.accession_number;
    }
    if (params.limit > 0) {
        query.max_results = params.limit;
    }

    return query;
}

}  // anonymous namespace

// =============================================================================
// Unified MPPS Handler Implementation
// =============================================================================

/**
 * @brief Unified MPPS handler implementation
 *
 * Delegates all persistence operations to integration::mpps_adapter,
 * removing the need for separate real/stub implementations.
 */
class mpps_handler_impl : public mpps_handler {
public:
    explicit mpps_handler_impl(const mpps_handler_config& config)
        : config_(config)
        , running_(false)
        , connected_(false)
        , start_time_(std::chrono::steady_clock::now()) {
        // Use provided adapter or create default
        if (config_.mpps_adapter) {
            adapter_ = config_.mpps_adapter;
        } else {
            auto pacs = integration::create_pacs_adapter(integration::pacs_config{});
            adapter_ = pacs->get_mpps_adapter();
        }
    }

    ~mpps_handler_impl() override {
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

        running_ = true;
        connected_ = true;
        stats_.connect_successes++;
        start_time_ = std::chrono::steady_clock::now();

        // Start monitor for connection health
        if (config_.auto_reconnect) {
            stop_requested_ = false;
#ifndef PACS_BRIDGE_STANDALONE_BUILD
            if (config_.executor) {
                schedule_monitor_job();
            } else {
                monitor_thread_ = std::thread(&mpps_handler_impl::monitor_connection, this);
            }
#else
            monitor_thread_ = std::thread(&mpps_handler_impl::monitor_connection, this);
#endif
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
        }

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        if (config_.executor && monitor_future_.valid()) {
            monitor_future_.wait_for(std::chrono::seconds{5});
        }
#endif

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
    // Event Handlers
    // =========================================================================

    std::expected<void, mpps_error>
    on_n_create(const mpps_dataset& dataset) override {
        auto validation = validate_mpps_dataset(dataset);
        if (!validation) {
            update_stats_error();
            return validation;
        }

        // N-CREATE should always be IN PROGRESS
        mpps_dataset processed_dataset = dataset;
        if (processed_dataset.status != mpps_event::in_progress) {
            processed_dataset.status = mpps_event::in_progress;
        }

        // Persist via adapter
        auto record = to_integration_record(processed_dataset);
        auto persist_result = adapter_->create_mpps(record);
        if (!persist_result) {
            update_persistence_error();
        }

        {
            std::unique_lock lock(stats_mutex_);
            stats_.n_create_count++;
            stats_.in_progress_count++;
            stats_.last_event_time = std::chrono::system_clock::now();
        }

        {
            std::unique_lock lock(persist_stats_mutex_);
            persist_stats_.total_persisted++;
            persist_stats_.in_progress_count++;
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

        // Check if record exists via adapter
        auto existing = adapter_->get_mpps(dataset.sop_instance_uid);
        if (!existing.has_value()) {
            update_stats_error();
            return std::unexpected(mpps_error::record_not_found);
        }

        // Check state transition validity - COMPLETED/DISCONTINUED are final
        if (existing->status == "COMPLETED" || existing->status == "DISCONTINUED") {
            update_stats_error();
            return std::unexpected(mpps_error::invalid_state_transition);
        }

        // Update via adapter
        auto record = to_integration_record(dataset);
        auto persist_result = adapter_->update_mpps(record);
        if (!persist_result) {
            update_persistence_error();
        }

        mpps_event event = dataset.status;

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

        {
            std::unique_lock lock(persist_stats_mutex_);
            if (event == mpps_event::completed) {
                persist_stats_.completed_count++;
                if (persist_stats_.in_progress_count > 0) {
                    persist_stats_.in_progress_count--;
                }
            } else if (event == mpps_event::discontinued) {
                persist_stats_.discontinued_count++;
                if (persist_stats_.in_progress_count > 0) {
                    persist_stats_.in_progress_count--;
                }
            }
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
    // Persistence Operations (delegated to adapter)
    // =========================================================================

    bool is_persistence_enabled() const noexcept override {
        return adapter_ != nullptr;
    }

    std::expected<std::optional<mpps_dataset>, mpps_error>
    query_mpps(std::string_view sop_instance_uid) const override {
        if (!adapter_) {
            return std::unexpected(mpps_error::persistence_disabled);
        }

        auto result = adapter_->get_mpps(sop_instance_uid);
        if (!result.has_value()) {
            if (result.error() == integration::pacs_error::not_found) {
                return std::nullopt;
            }
            return std::unexpected(mpps_error::database_error);
        }

        return from_integration_record(*result);
    }

    std::expected<std::vector<mpps_dataset>, mpps_error>
    query_mpps(const mpps_query_params& params) const override {
        if (!adapter_) {
            return std::unexpected(mpps_error::persistence_disabled);
        }

        auto query = to_integration_query(params);

        // Handle SOP Instance UID direct lookup
        if (params.sop_instance_uid) {
            auto single = adapter_->get_mpps(*params.sop_instance_uid);
            if (!single.has_value()) {
                return std::vector<mpps_dataset>{};
            }
            auto dataset = from_integration_record(*single);
            // Apply additional filters
            bool match = true;
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
                return std::vector<mpps_dataset>{dataset};
            }
            return std::vector<mpps_dataset>{};
        }

        auto result = adapter_->query_mpps(query);
        if (!result.has_value()) {
            return std::unexpected(mpps_error::database_error);
        }

        std::vector<mpps_dataset> datasets;
        datasets.reserve(result->size());
        for (const auto& record : *result) {
            datasets.push_back(from_integration_record(record));
        }

        return datasets;
    }

    std::expected<std::vector<mpps_dataset>, mpps_error>
    get_active_mpps() const override {
        if (!adapter_) {
            return std::unexpected(mpps_error::persistence_disabled);
        }

        integration::mpps_query_params query;
        query.status = "IN PROGRESS";

        auto result = adapter_->query_mpps(query);
        if (!result.has_value()) {
            return std::unexpected(mpps_error::database_error);
        }

        std::vector<mpps_dataset> datasets;
        datasets.reserve(result->size());
        for (const auto& record : *result) {
            datasets.push_back(from_integration_record(record));
        }

        return datasets;
    }

    std::expected<std::vector<mpps_dataset>, mpps_error>
    get_pending_mpps_for_station(std::string_view station_ae_title) const override {
        if (!adapter_) {
            return std::unexpected(mpps_error::persistence_disabled);
        }

        integration::mpps_query_params query;
        query.status = "IN PROGRESS";
        query.station_ae_title = std::string(station_ae_title);

        auto result = adapter_->query_mpps(query);
        if (!result.has_value()) {
            return std::unexpected(mpps_error::database_error);
        }

        std::vector<mpps_dataset> datasets;
        datasets.reserve(result->size());
        for (const auto& record : *result) {
            datasets.push_back(from_integration_record(record));
        }

        return datasets;
    }

    persistence_stats get_persistence_stats() const override {
        std::shared_lock lock(persist_stats_mutex_);
        return persist_stats_;
    }

private:
    // =========================================================================
    // Monitor Thread
    // =========================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    void schedule_monitor_job() {
        if (stop_requested_ || !config_.executor) {
            return;
        }

        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            config_.reconnect_delay);

        auto job = std::make_unique<mpps_monitor_job>([this]() {
            if (stop_requested_) {
                return;
            }

            {
                std::shared_lock lock(state_mutex_);
            }

            schedule_monitor_job();
        });

        auto result = config_.executor->execute_delayed(std::move(job), delay);
        if (result.is_ok()) {
            monitor_future_ = std::move(result.value());
        }
    }
#endif  // PACS_BRIDGE_STANDALONE_BUILD

    void monitor_connection() {
        while (!stop_requested_) {
            std::this_thread::sleep_for(config_.reconnect_delay);

            if (stop_requested_) {
                break;
            }

            std::shared_lock lock(state_mutex_);
        }
    }

    // =========================================================================
    // Callback Invocation
    // =========================================================================

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

    // =========================================================================
    // Statistics Helpers
    // =========================================================================

    void update_stats_error() {
        std::unique_lock lock(stats_mutex_);
        stats_.parse_error_count++;
    }

    void update_persistence_error() {
        std::unique_lock lock(persist_stats_mutex_);
        persist_stats_.persistence_failures++;
    }

    void log_mpps_event(const char* operation, const mpps_dataset& dataset) {
        std::ostringstream oss;
        oss << "MPPS " << operation << ": "
            << "SOP=" << dataset.sop_instance_uid << ", "
            << "Accession=" << dataset.accession_number << ", "
            << "Status=" << to_string(dataset.status) << ", "
            << "Patient=" << dataset.patient_id;
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    mpps_handler_config config_;

    // Persistence adapter
    std::shared_ptr<integration::mpps_adapter> adapter_;

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

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    std::future<void> monitor_future_;
#endif

    // Statistics
    statistics stats_;
    mutable std::shared_mutex stats_mutex_;
    std::chrono::steady_clock::time_point start_time_;

    // Persistence statistics
    persistence_stats persist_stats_;
    mutable std::shared_mutex persist_stats_mutex_;
};

// =============================================================================
// Factory Function
// =============================================================================

std::unique_ptr<mpps_handler>
mpps_handler::create(const mpps_handler_config& config) {
    return std::make_unique<mpps_handler_impl>(config);
}

}  // namespace pacs::bridge::pacs_adapter
