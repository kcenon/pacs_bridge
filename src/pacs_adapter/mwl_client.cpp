/**
 * @file mwl_client.cpp
 * @brief Implementation of Modality Worklist client using mwl_adapter
 *
 * Refactored to use mwl_adapter interface which provides:
 * - memory_mwl_adapter: In-memory storage (standalone builds)
 * - pacs_mwl_adapter: pacs_system index_database integration
 *
 * This removes conditional compilation and provides consistent interface.
 */

#include "pacs/bridge/pacs_adapter/mwl_client.h"
#include "pacs/bridge/integration/mwl_adapter.h"
#include "pacs/bridge/monitoring/bridge_metrics.h"
#include "pacs/bridge/tracing/trace_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <thread>

namespace pacs::bridge::pacs_adapter {

// Alias for convenience
using integration::mwl_adapter;
using integration::mwl_adapter_error;
using integration::mwl_query_filter;

// =============================================================================
// Error Conversion Utilities
// =============================================================================

namespace {

/**
 * @brief Convert mwl_adapter_error to mwl_error
 */
[[nodiscard]] mwl_error to_mwl_error(mwl_adapter_error adapter_error) {
    using integration::mwl_adapter_error;

    switch (adapter_error) {
        case mwl_adapter_error::not_found:
            return mwl_error::entry_not_found;
        case mwl_adapter_error::duplicate:
            return mwl_error::duplicate_entry;
        case mwl_adapter_error::invalid_data:
            return mwl_error::invalid_data;
        case mwl_adapter_error::query_failed:
            return mwl_error::query_failed;
        case mwl_adapter_error::add_failed:
            return mwl_error::add_failed;
        case mwl_adapter_error::update_failed:
            return mwl_error::update_failed;
        case mwl_adapter_error::delete_failed:
            return mwl_error::cancel_failed;
        case mwl_adapter_error::storage_unavailable:
        case mwl_adapter_error::init_failed:
            return mwl_error::connection_failed;
        default:
            return mwl_error::connection_failed;
    }
}

}  // namespace

// =============================================================================
// Implementation Class
// =============================================================================

class mwl_client::impl {
public:
    explicit impl(const mwl_client_config& config)
        : config_(config),
          adapter_(integration::create_mwl_adapter(config.pacs_database_path)) {}

    ~impl() { disconnect(true); }

    // =========================================================================
    // Connection Management
    // =========================================================================

    std::expected<void, mwl_error> connect() {
        std::unique_lock lock(mutex_);

        if (connected_) {
            return {};  // Already connected
        }

        stats_.connect_attempts++;

        // Verify adapter is available
        if (!adapter_) {
            return std::unexpected(mwl_error::connection_failed);
        }

        // Mark as connected (adapter handles actual storage connection)
        connected_ = true;
        stats_.connect_successes++;

        return {};
    }

    void disconnect(bool graceful) {
        std::unique_lock lock(mutex_);

        if (!connected_) {
            return;
        }

        if (graceful) {
            // Send association release in real implementation
        }

        connected_ = false;
    }

    bool is_connected() const noexcept {
        std::shared_lock lock(mutex_);
        return connected_;
    }

    std::expected<void, mwl_error> reconnect() {
        disconnect(true);
        stats_.reconnections++;
        return connect();
    }

    // =========================================================================
    // MWL Operations
    // =========================================================================

    std::expected<mwl_client::operation_result, mwl_error>
    add_entry(const mapping::mwl_item& item) {
        // Start tracing span for PACS update
        auto span = tracing::trace_manager::instance().start_span(
            "pacs_update", tracing::span_kind::client);
        span.set_attribute("pacs.operation", "mwl_add");
        span.set_attribute("pacs.accession_number",
                           item.imaging_service_request.accession_number);

        auto start_time = std::chrono::steady_clock::now();

        // Validate input
        if (item.imaging_service_request.accession_number.empty()) {
            span.set_error("Invalid data: empty accession number");
            return std::unexpected(mwl_error::invalid_data);
        }

        // Ensure connection
        if (!ensure_connected()) {
            span.set_error("Connection failed");
            return std::unexpected(mwl_error::connection_failed);
        }

        // Use adapter to add item
        auto result = adapter_->add_item(item);
        if (!result) {
            auto error = to_mwl_error(result.error());
            span.set_error(to_string(error));
            return std::unexpected(error);
        }

        // Update statistics
        {
            std::unique_lock lock(mutex_);
            stats_.add_count++;
        }

        // Record metrics for entry creation
        monitoring::bridge_metrics_collector::instance().record_mwl_entry_created();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        update_avg_operation_time(elapsed.count());

        span.set_attribute("pacs.duration_ms", static_cast<int64_t>(elapsed.count()));
        span.set_attribute("pacs.success", true);

        return mwl_client::operation_result{
            .elapsed_time = elapsed,
            .retry_count = 0,
            .dicom_status = 0x0000  // Success
        };
    }

    std::expected<mwl_client::operation_result, mwl_error>
    update_entry(std::string_view accession_number,
                 const mapping::mwl_item& item) {
        // Start tracing span for PACS update
        auto span = tracing::trace_manager::instance().start_span(
            "pacs_update", tracing::span_kind::client);
        span.set_attribute("pacs.operation", "mwl_update");
        span.set_attribute("pacs.accession_number", std::string(accession_number));

        auto start_time = std::chrono::steady_clock::now();

        if (accession_number.empty()) {
            span.set_error("Invalid data: empty accession number");
            return std::unexpected(mwl_error::invalid_data);
        }

        if (!ensure_connected()) {
            span.set_error("Connection failed");
            return std::unexpected(mwl_error::connection_failed);
        }

        // Use adapter to update item
        auto result = adapter_->update_item(accession_number, item);
        if (!result) {
            auto error = to_mwl_error(result.error());
            span.set_error(to_string(error));
            return std::unexpected(error);
        }

        // Update statistics
        {
            std::unique_lock lock(mutex_);
            stats_.update_count++;
        }

        // Record metrics for entry update
        monitoring::bridge_metrics_collector::instance().record_mwl_entry_updated();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        update_avg_operation_time(elapsed.count());

        span.set_attribute("pacs.duration_ms", static_cast<int64_t>(elapsed.count()));
        span.set_attribute("pacs.success", true);

        return mwl_client::operation_result{
            .elapsed_time = elapsed,
            .retry_count = 0,
            .dicom_status = 0x0000
        };
    }

    std::expected<mwl_client::operation_result, mwl_error>
    cancel_entry(std::string_view accession_number) {
        // Start tracing span for PACS update
        auto span = tracing::trace_manager::instance().start_span(
            "pacs_update", tracing::span_kind::client);
        span.set_attribute("pacs.operation", "mwl_cancel");
        span.set_attribute("pacs.accession_number", std::string(accession_number));

        auto start_time = std::chrono::steady_clock::now();

        if (accession_number.empty()) {
            span.set_error("Invalid data: empty accession number");
            return std::unexpected(mwl_error::invalid_data);
        }

        if (!ensure_connected()) {
            span.set_error("Connection failed");
            return std::unexpected(mwl_error::connection_failed);
        }

        // Use adapter to delete item
        auto result = adapter_->delete_item(accession_number);
        if (!result) {
            auto error = to_mwl_error(result.error());
            span.set_error(to_string(error));
            return std::unexpected(error);
        }

        // Update statistics
        {
            std::unique_lock lock(mutex_);
            stats_.cancel_count++;
        }

        // Record metrics for entry cancellation
        monitoring::bridge_metrics_collector::instance().record_mwl_entry_cancelled();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        update_avg_operation_time(elapsed.count());

        span.set_attribute("pacs.duration_ms", static_cast<int64_t>(elapsed.count()));
        span.set_attribute("pacs.success", true);

        return mwl_client::operation_result{
            .elapsed_time = elapsed,
            .retry_count = 0,
            .dicom_status = 0x0000
        };
    }

    std::expected<mwl_client::query_result, mwl_error>
    query(const mwl_query_filter& filter) {
        // Start tracing span for PACS query
        auto span = tracing::trace_manager::instance().start_span(
            "pacs_update", tracing::span_kind::client);
        span.set_attribute("pacs.operation", "mwl_query");

        auto start_time = std::chrono::steady_clock::now();

        if (!ensure_connected()) {
            span.set_error("Connection failed");
            return std::unexpected(mwl_error::connection_failed);
        }

        // Use adapter to query items
        auto result = adapter_->query_items(filter);
        if (!result) {
            auto error = to_mwl_error(result.error());
            span.set_error(to_string(error));
            return std::unexpected(error);
        }

        // Update statistics
        {
            std::unique_lock lock(mutex_);
            stats_.query_count++;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        // Record metrics for query duration (in nanoseconds)
        auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start_time);
        monitoring::bridge_metrics_collector::instance().record_mwl_query_duration(
            elapsed_ns);

        update_avg_operation_time(elapsed.count());

        span.set_attribute("pacs.duration_ms", static_cast<int64_t>(elapsed.count()));
        span.set_attribute("pacs.result_count", static_cast<int64_t>(result->size()));
        span.set_attribute("pacs.success", true);

        return mwl_client::query_result{
            .items = std::move(*result),
            .elapsed_time = elapsed,
            .has_more = false,
            .total_count = std::nullopt
        };
    }

    std::expected<mwl_client::query_result, mwl_error>
    query(const mapping::mwl_item& query_item) {
        // Convert mwl_item to filter
        mwl_query_filter filter;

        if (!query_item.patient.patient_id.empty()) {
            filter.patient_id = query_item.patient.patient_id;
        }
        if (!query_item.imaging_service_request.accession_number.empty()) {
            filter.accession_number =
                query_item.imaging_service_request.accession_number;
        }
        if (!query_item.patient.patient_name.empty()) {
            filter.patient_name = query_item.patient.patient_name;
        }
        if (!query_item.scheduled_steps.empty()) {
            const auto& sps = query_item.scheduled_steps[0];
            if (!sps.modality.empty()) {
                filter.modality = sps.modality;
            }
            if (!sps.scheduled_station_ae_title.empty()) {
                filter.scheduled_station_ae = sps.scheduled_station_ae_title;
            }
            if (!sps.scheduled_start_date.empty()) {
                filter.scheduled_date = sps.scheduled_start_date;
            }
        }

        return query(filter);
    }

    bool exists(std::string_view accession_number) {
        if (accession_number.empty()) {
            return false;
        }

        if (!ensure_connected()) {
            return false;
        }

        return adapter_->exists(accession_number);
    }

    std::expected<mapping::mwl_item, mwl_error>
    get_entry(std::string_view accession_number) {
        if (accession_number.empty()) {
            return std::unexpected(mwl_error::invalid_data);
        }

        if (!ensure_connected()) {
            return std::unexpected(mwl_error::connection_failed);
        }

        auto result = adapter_->get_item(accession_number);
        if (!result) {
            return std::unexpected(to_mwl_error(result.error()));
        }

        return *result;
    }

    // =========================================================================
    // Bulk Operations
    // =========================================================================

    std::expected<size_t, mwl_error>
    add_entries(const std::vector<mapping::mwl_item>& items,
                bool continue_on_error) {
        size_t success_count = 0;

        for (const auto& item : items) {
            auto result = add_entry(item);
            if (result) {
                success_count++;
            } else if (!continue_on_error) {
                return std::unexpected(result.error());
            } else {
                stats_.error_count++;
            }
        }

        return success_count;
    }

    std::expected<size_t, mwl_error>
    cancel_entries_before(std::string_view before_date) {
        if (before_date.empty()) {
            return std::unexpected(mwl_error::invalid_data);
        }

        if (!ensure_connected()) {
            return std::unexpected(mwl_error::connection_failed);
        }

        // Use adapter to delete items before date
        auto result = adapter_->delete_items_before(before_date);
        if (!result) {
            return std::unexpected(to_mwl_error(result.error()));
        }

        size_t cancelled = *result;

        // Update statistics for each cancelled entry
        {
            std::unique_lock lock(mutex_);
            for (size_t i = 0; i < cancelled; ++i) {
                stats_.cancel_count++;
                monitoring::bridge_metrics_collector::instance()
                    .record_mwl_entry_cancelled();
            }
        }

        return cancelled;
    }

    // =========================================================================
    // Statistics
    // =========================================================================

    mwl_client::statistics get_statistics() const {
        std::shared_lock lock(mutex_);
        return stats_;
    }

    void reset_statistics() {
        std::unique_lock lock(mutex_);
        stats_ = mwl_client::statistics{};
    }

    const mwl_client_config& config() const noexcept { return config_; }

private:
    // =========================================================================
    // Helper Methods
    // =========================================================================

    bool ensure_connected() {
        if (is_connected()) {
            return true;
        }

        auto result = connect();
        return result.has_value();
    }

    void update_avg_operation_time(double elapsed_ms) {
        // Running average calculation
        auto total_ops = stats_.add_count + stats_.update_count +
                         stats_.cancel_count + stats_.query_count;

        if (total_ops > 0) {
            stats_.avg_operation_ms =
                (stats_.avg_operation_ms * (total_ops - 1) + elapsed_ms) /
                total_ops;
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    mwl_client_config config_;
    std::shared_ptr<mwl_adapter> adapter_;
    mutable std::shared_mutex mutex_;
    std::atomic<bool> connected_{false};
    mwl_client::statistics stats_;
};

// =============================================================================
// MWL Client Public Interface
// =============================================================================

mwl_client::mwl_client(const mwl_client_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

mwl_client::~mwl_client() = default;

mwl_client::mwl_client(mwl_client&&) noexcept = default;
mwl_client& mwl_client::operator=(mwl_client&&) noexcept = default;

// Connection Management
std::expected<void, mwl_error> mwl_client::connect() {
    return pimpl_->connect();
}

void mwl_client::disconnect(bool graceful) {
    pimpl_->disconnect(graceful);
}

bool mwl_client::is_connected() const noexcept {
    return pimpl_->is_connected();
}

std::expected<void, mwl_error> mwl_client::reconnect() {
    return pimpl_->reconnect();
}

// MWL Operations
std::expected<mwl_client::operation_result, mwl_error>
mwl_client::add_entry(const mapping::mwl_item& item) {
    return pimpl_->add_entry(item);
}

std::expected<mwl_client::operation_result, mwl_error>
mwl_client::update_entry(std::string_view accession_number,
                         const mapping::mwl_item& item) {
    return pimpl_->update_entry(accession_number, item);
}

std::expected<mwl_client::operation_result, mwl_error>
mwl_client::cancel_entry(std::string_view accession_number) {
    return pimpl_->cancel_entry(accession_number);
}

std::expected<mwl_client::query_result, mwl_error>
mwl_client::query(const mwl_query_filter& filter) {
    return pimpl_->query(filter);
}

std::expected<mwl_client::query_result, mwl_error>
mwl_client::query(const mapping::mwl_item& query_item) {
    return pimpl_->query(query_item);
}

bool mwl_client::exists(std::string_view accession_number) {
    return pimpl_->exists(accession_number);
}

std::expected<mapping::mwl_item, mwl_error>
mwl_client::get_entry(std::string_view accession_number) {
    return pimpl_->get_entry(accession_number);
}

// Bulk Operations
std::expected<size_t, mwl_error>
mwl_client::add_entries(const std::vector<mapping::mwl_item>& items,
                        bool continue_on_error) {
    return pimpl_->add_entries(items, continue_on_error);
}

std::expected<size_t, mwl_error>
mwl_client::cancel_entries_before(std::string_view before_date) {
    return pimpl_->cancel_entries_before(before_date);
}

// Statistics
mwl_client::statistics mwl_client::get_statistics() const {
    return pimpl_->get_statistics();
}

void mwl_client::reset_statistics() {
    pimpl_->reset_statistics();
}

const mwl_client_config& mwl_client::config() const noexcept {
    return pimpl_->config();
}

}  // namespace pacs::bridge::pacs_adapter
