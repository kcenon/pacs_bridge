/**
 * @file mpps_handler.cpp
 * @brief Implementation of MPPS event handler for pacs_system
 */

#include "pacs/bridge/pacs_adapter/mpps_handler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <thread>

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
// Implementation Class
// =============================================================================

class mpps_handler_impl : public mpps_handler {
public:
    explicit mpps_handler_impl(const mpps_handler_config& config)
        : config_(config)
        , running_(false)
        , connected_(false)
        , start_time_(std::chrono::steady_clock::now()) {}

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
            monitor_thread_ = std::thread(&mpps_handler_impl::monitor_connection, this);
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
        // In a real implementation, use logger_system for structured logging
        // For now, this is a placeholder for logging

        std::ostringstream oss;
        oss << "MPPS " << operation << ": "
            << "SOP=" << dataset.sop_instance_uid << ", "
            << "Accession=" << dataset.accession_number << ", "
            << "Status=" << to_string(dataset.status) << ", "
            << "Patient=" << dataset.patient_id;

        // Log using logger_system when integrated
        // logger_system::info(oss.str());
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
};

// =============================================================================
// Factory Function
// =============================================================================

std::unique_ptr<mpps_handler>
mpps_handler::create(const mpps_handler_config& config) {
    return std::make_unique<mpps_handler_impl>(config);
}

}  // namespace pacs::bridge::pacs_adapter
