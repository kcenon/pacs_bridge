/**
 * @file mwl_client.cpp
 * @brief Implementation of Modality Worklist client for pacs_system
 */

#include "pacs/bridge/pacs_adapter/mwl_client.h"
#include "pacs/bridge/monitoring/bridge_metrics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace pacs::bridge::pacs_adapter {

// =============================================================================
// Implementation Class
// =============================================================================

class mwl_client::impl {
public:
    explicit impl(const mwl_client_config& config) : config_(config) {}

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

        // Simulate connection to pacs_system
        // In real implementation, this would establish DICOM association
        auto result = try_connect_with_retry();
        if (result) {
            connected_ = true;
            stats_.connect_successes++;
        }

        return result;
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
        auto start_time = std::chrono::steady_clock::now();

        // Validate input
        if (item.imaging_service_request.accession_number.empty()) {
            return std::unexpected(mwl_error::invalid_data);
        }

        // Ensure connection
        if (!ensure_connected()) {
            return std::unexpected(mwl_error::connection_failed);
        }

        std::unique_lock lock(mutex_);

        // Check for duplicate
        auto it = std::find_if(entries_.begin(), entries_.end(),
            [&item](const mapping::mwl_item& existing) {
                return existing.imaging_service_request.accession_number ==
                       item.imaging_service_request.accession_number;
            });

        if (it != entries_.end()) {
            return std::unexpected(mwl_error::duplicate_entry);
        }

        // Add entry (in real implementation, send to pacs_system via DICOM)
        entries_.push_back(item);
        stats_.add_count++;

        // Record metrics for entry creation
        monitoring::bridge_metrics_collector::instance().record_mwl_entry_created();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        update_avg_operation_time(elapsed.count());

        return mwl_client::operation_result{
            .elapsed_time = elapsed,
            .retry_count = 0,
            .dicom_status = 0x0000  // Success
        };
    }

    std::expected<mwl_client::operation_result, mwl_error>
    update_entry(std::string_view accession_number,
                 const mapping::mwl_item& item) {
        auto start_time = std::chrono::steady_clock::now();

        if (accession_number.empty()) {
            return std::unexpected(mwl_error::invalid_data);
        }

        if (!ensure_connected()) {
            return std::unexpected(mwl_error::connection_failed);
        }

        std::unique_lock lock(mutex_);

        auto it = std::find_if(entries_.begin(), entries_.end(),
            [accession_number](const mapping::mwl_item& existing) {
                return existing.imaging_service_request.accession_number ==
                       accession_number;
            });

        if (it == entries_.end()) {
            return std::unexpected(mwl_error::entry_not_found);
        }

        // Update entry with non-empty fields from item
        update_mwl_item(*it, item);
        stats_.update_count++;

        // Record metrics for entry update
        monitoring::bridge_metrics_collector::instance().record_mwl_entry_updated();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        update_avg_operation_time(elapsed.count());

        return mwl_client::operation_result{
            .elapsed_time = elapsed,
            .retry_count = 0,
            .dicom_status = 0x0000
        };
    }

    std::expected<mwl_client::operation_result, mwl_error>
    cancel_entry(std::string_view accession_number) {
        auto start_time = std::chrono::steady_clock::now();

        if (accession_number.empty()) {
            return std::unexpected(mwl_error::invalid_data);
        }

        if (!ensure_connected()) {
            return std::unexpected(mwl_error::connection_failed);
        }

        std::unique_lock lock(mutex_);

        auto it = std::find_if(entries_.begin(), entries_.end(),
            [accession_number](const mapping::mwl_item& existing) {
                return existing.imaging_service_request.accession_number ==
                       accession_number;
            });

        if (it == entries_.end()) {
            return std::unexpected(mwl_error::entry_not_found);
        }

        entries_.erase(it);
        stats_.cancel_count++;

        // Record metrics for entry cancellation
        monitoring::bridge_metrics_collector::instance().record_mwl_entry_cancelled();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        update_avg_operation_time(elapsed.count());

        return mwl_client::operation_result{
            .elapsed_time = elapsed,
            .retry_count = 0,
            .dicom_status = 0x0000
        };
    }

    std::expected<mwl_client::query_result, mwl_error>
    query(const mwl_query_filter& filter) {
        auto start_time = std::chrono::steady_clock::now();

        if (!ensure_connected()) {
            return std::unexpected(mwl_error::connection_failed);
        }

        std::shared_lock lock(mutex_);

        std::vector<mapping::mwl_item> results;

        for (const auto& entry : entries_) {
            if (matches_filter(entry, filter)) {
                results.push_back(entry);

                if (filter.max_results > 0 &&
                    results.size() >= filter.max_results) {
                    break;
                }
            }
        }

        stats_.query_count++;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        // Record metrics for query duration (in nanoseconds)
        auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start_time);
        monitoring::bridge_metrics_collector::instance().record_mwl_query_duration(
            elapsed_ns);

        update_avg_operation_time(elapsed.count());

        return mwl_client::query_result{
            .items = std::move(results),
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

        std::shared_lock lock(mutex_);

        return std::any_of(entries_.begin(), entries_.end(),
            [accession_number](const mapping::mwl_item& entry) {
                return entry.imaging_service_request.accession_number ==
                       accession_number;
            });
    }

    std::expected<mapping::mwl_item, mwl_error>
    get_entry(std::string_view accession_number) {
        if (accession_number.empty()) {
            return std::unexpected(mwl_error::invalid_data);
        }

        if (!ensure_connected()) {
            return std::unexpected(mwl_error::connection_failed);
        }

        std::shared_lock lock(mutex_);

        auto it = std::find_if(entries_.begin(), entries_.end(),
            [accession_number](const mapping::mwl_item& entry) {
                return entry.imaging_service_request.accession_number ==
                       accession_number;
            });

        if (it == entries_.end()) {
            return std::unexpected(mwl_error::entry_not_found);
        }

        return *it;
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

        std::unique_lock lock(mutex_);

        std::string before_date_str(before_date);
        size_t cancelled = 0;

        auto it = entries_.begin();
        while (it != entries_.end()) {
            bool should_cancel = false;

            if (!it->scheduled_steps.empty()) {
                const auto& sps = it->scheduled_steps[0];
                if (!sps.scheduled_start_date.empty() &&
                    sps.scheduled_start_date < before_date_str) {
                    should_cancel = true;
                }
            }

            if (should_cancel) {
                it = entries_.erase(it);
                cancelled++;
                stats_.cancel_count++;

                // Record metrics for each entry cancellation
                monitoring::bridge_metrics_collector::instance()
                    .record_mwl_entry_cancelled();
            } else {
                ++it;
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

    std::expected<void, mwl_error> try_connect_with_retry() {
        for (size_t attempt = 0; attempt <= config_.max_retries; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(config_.retry_delay);
            }

            // Simulate connection (always succeeds in stub implementation)
            // In real implementation:
            // - Create DICOM association
            // - Negotiate presentation contexts for MWL
            // - Verify AE titles

            // Stub: Connection always succeeds
            return {};
        }

        return std::unexpected(mwl_error::connection_failed);
    }

    bool ensure_connected() {
        if (is_connected()) {
            return true;
        }

        auto result = connect();
        return result.has_value();
    }

    bool matches_filter(const mapping::mwl_item& entry,
                       const mwl_query_filter& filter) const {
        // Patient ID filter
        if (filter.patient_id && !filter.patient_id->empty()) {
            if (entry.patient.patient_id != *filter.patient_id) {
                return false;
            }
        }

        // Accession number filter
        if (filter.accession_number && !filter.accession_number->empty()) {
            if (entry.imaging_service_request.accession_number !=
                *filter.accession_number) {
                return false;
            }
        }

        // Patient name filter (supports wildcards with *)
        if (filter.patient_name && !filter.patient_name->empty()) {
            if (!matches_wildcard(entry.patient.patient_name,
                                  *filter.patient_name)) {
                return false;
            }
        }

        // Referring physician filter
        if (filter.referring_physician && !filter.referring_physician->empty()) {
            if (!matches_wildcard(
                    entry.requested_procedure.referring_physician_name,
                    *filter.referring_physician)) {
                return false;
            }
        }

        // Scheduled procedure step filters
        if (!entry.scheduled_steps.empty()) {
            const auto& sps = entry.scheduled_steps[0];

            // Modality filter
            if (filter.modality && !filter.modality->empty()) {
                if (sps.modality != *filter.modality) {
                    return false;
                }
            }

            // Scheduled station AE filter
            if (filter.scheduled_station_ae &&
                !filter.scheduled_station_ae->empty()) {
                if (sps.scheduled_station_ae_title !=
                    *filter.scheduled_station_ae) {
                    return false;
                }
            }

            // Scheduled date filter (exact match)
            if (filter.scheduled_date && !filter.scheduled_date->empty()) {
                if (sps.scheduled_start_date != *filter.scheduled_date) {
                    return false;
                }
            }

            // Scheduled date range filter
            if (filter.scheduled_date_from &&
                !filter.scheduled_date_from->empty()) {
                if (sps.scheduled_start_date < *filter.scheduled_date_from) {
                    return false;
                }
            }

            if (filter.scheduled_date_to && !filter.scheduled_date_to->empty()) {
                if (sps.scheduled_start_date > *filter.scheduled_date_to) {
                    return false;
                }
            }

            // SPS status filter
            if (filter.sps_status && !filter.sps_status->empty()) {
                if (sps.scheduled_step_status != *filter.sps_status) {
                    return false;
                }
            }
        }

        return true;
    }

    bool matches_wildcard(std::string_view text,
                         std::string_view pattern) const {
        // Simple wildcard matching with * as wildcard
        if (pattern.empty()) {
            return true;
        }

        if (pattern == "*") {
            return true;
        }

        // Check for prefix match (pattern ends with *)
        if (pattern.back() == '*') {
            auto prefix = pattern.substr(0, pattern.size() - 1);
            return text.substr(0, prefix.size()) == prefix;
        }

        // Check for suffix match (pattern starts with *)
        if (pattern.front() == '*') {
            auto suffix = pattern.substr(1);
            if (text.size() < suffix.size()) {
                return false;
            }
            return text.substr(text.size() - suffix.size()) == suffix;
        }

        // Exact match
        return text == pattern;
    }

    void update_mwl_item(mapping::mwl_item& existing,
                        const mapping::mwl_item& updates) {
        // Update patient fields if non-empty
        if (!updates.patient.patient_id.empty()) {
            existing.patient.patient_id = updates.patient.patient_id;
        }
        if (!updates.patient.patient_name.empty()) {
            existing.patient.patient_name = updates.patient.patient_name;
        }
        if (!updates.patient.patient_birth_date.empty()) {
            existing.patient.patient_birth_date =
                updates.patient.patient_birth_date;
        }
        if (!updates.patient.patient_sex.empty()) {
            existing.patient.patient_sex = updates.patient.patient_sex;
        }

        // Update imaging service request fields
        if (!updates.imaging_service_request.requesting_physician.empty()) {
            existing.imaging_service_request.requesting_physician =
                updates.imaging_service_request.requesting_physician;
        }
        if (!updates.imaging_service_request.requesting_service.empty()) {
            existing.imaging_service_request.requesting_service =
                updates.imaging_service_request.requesting_service;
        }

        // Update requested procedure fields
        if (!updates.requested_procedure.requested_procedure_description
                 .empty()) {
            existing.requested_procedure.requested_procedure_description =
                updates.requested_procedure.requested_procedure_description;
        }
        if (!updates.requested_procedure.referring_physician_name.empty()) {
            existing.requested_procedure.referring_physician_name =
                updates.requested_procedure.referring_physician_name;
        }

        // Update scheduled procedure steps
        if (!updates.scheduled_steps.empty()) {
            if (existing.scheduled_steps.empty()) {
                existing.scheduled_steps = updates.scheduled_steps;
            } else {
                auto& existing_sps = existing.scheduled_steps[0];
                const auto& update_sps = updates.scheduled_steps[0];

                if (!update_sps.scheduled_start_date.empty()) {
                    existing_sps.scheduled_start_date =
                        update_sps.scheduled_start_date;
                }
                if (!update_sps.scheduled_start_time.empty()) {
                    existing_sps.scheduled_start_time =
                        update_sps.scheduled_start_time;
                }
                if (!update_sps.modality.empty()) {
                    existing_sps.modality = update_sps.modality;
                }
                if (!update_sps.scheduled_station_ae_title.empty()) {
                    existing_sps.scheduled_station_ae_title =
                        update_sps.scheduled_station_ae_title;
                }
                if (!update_sps.scheduled_performing_physician.empty()) {
                    existing_sps.scheduled_performing_physician =
                        update_sps.scheduled_performing_physician;
                }
            }
        }
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
    mutable std::shared_mutex mutex_;
    std::atomic<bool> connected_{false};
    std::vector<mapping::mwl_item> entries_;
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
