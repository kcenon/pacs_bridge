/**
 * @file mwl_client.cpp
 * @brief Implementation of Modality Worklist client for pacs_system
 *
 * This file provides two implementations:
 * - Stub implementation (PACS_BRIDGE_STANDALONE_BUILD): In-memory storage for testing
 * - Real implementation (PACS_BRIDGE_HAS_PACS_SYSTEM): Uses pacs_system's index_database
 */

#include "pacs/bridge/pacs_adapter/mwl_client.h"
#include "pacs/bridge/monitoring/bridge_metrics.h"
#include "pacs/bridge/tracing/trace_manager.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <thread>

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
#include <pacs/storage/index_database.hpp>
#include <pacs/storage/worklist_record.hpp>
#endif

namespace pacs::bridge::pacs_adapter {

// =============================================================================
// Conversion Utilities (for pacs_system integration)
// =============================================================================

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
namespace {

/**
 * @brief Convert mwl_item to pacs_system worklist_item
 */
[[nodiscard]] pacs::storage::worklist_item
to_worklist_item(const mapping::mwl_item& item) {
    pacs::storage::worklist_item wl;

    // Patient information
    wl.patient_id = item.patient.patient_id;
    wl.patient_name = item.patient.patient_name;
    wl.birth_date = item.patient.patient_birth_date;
    wl.sex = item.patient.patient_sex;

    // Imaging service request
    wl.accession_no = item.imaging_service_request.accession_number;

    // Requested procedure
    wl.requested_proc_id = item.requested_procedure.requested_procedure_id;
    wl.study_uid = item.requested_procedure.study_instance_uid;
    wl.referring_phys = item.requested_procedure.referring_physician_name;
    wl.referring_phys_id = item.requested_procedure.referring_physician_id;

    // Scheduled procedure step (use first step if available)
    if (!item.scheduled_steps.empty()) {
        const auto& sps = item.scheduled_steps[0];
        wl.step_id = sps.scheduled_step_id;
        wl.station_ae = sps.scheduled_station_ae_title;
        wl.station_name = sps.scheduled_step_location;
        wl.modality = sps.modality;
        wl.procedure_desc = sps.scheduled_step_description;
        wl.step_status = sps.scheduled_step_status.empty()
                             ? "SCHEDULED"
                             : sps.scheduled_step_status;

        // Combine date and time for scheduled_datetime (YYYYMMDDHHMMSS)
        wl.scheduled_datetime = sps.scheduled_start_date;
        if (!sps.scheduled_start_time.empty()) {
            wl.scheduled_datetime += sps.scheduled_start_time;
        }

        // Protocol code (serialize as JSON-like string)
        if (!sps.protocol_code_value.empty()) {
            wl.protocol_code = "{\"value\":\"" + sps.protocol_code_value + "\","
                               "\"meaning\":\"" + sps.protocol_code_meaning + "\","
                               "\"scheme\":\"" + sps.protocol_coding_scheme + "\"}";
        }
    } else {
        // Generate default step_id if not provided
        wl.step_id = item.imaging_service_request.accession_number + "_SPS1";
        wl.step_status = "SCHEDULED";
    }

    return wl;
}

/**
 * @brief Convert pacs_system worklist_item to mwl_item
 */
[[nodiscard]] mapping::mwl_item
from_worklist_item(const pacs::storage::worklist_item& wl) {
    mapping::mwl_item item;

    // Patient information
    item.patient.patient_id = wl.patient_id;
    item.patient.patient_name = wl.patient_name;
    item.patient.patient_birth_date = wl.birth_date;
    item.patient.patient_sex = wl.sex;

    // Imaging service request
    item.imaging_service_request.accession_number = wl.accession_no;

    // Requested procedure
    item.requested_procedure.requested_procedure_id = wl.requested_proc_id;
    item.requested_procedure.study_instance_uid = wl.study_uid;
    item.requested_procedure.referring_physician_name = wl.referring_phys;
    item.requested_procedure.referring_physician_id = wl.referring_phys_id;

    // Scheduled procedure step
    mapping::dicom_scheduled_procedure_step sps;
    sps.scheduled_step_id = wl.step_id;
    sps.scheduled_station_ae_title = wl.station_ae;
    sps.scheduled_step_location = wl.station_name;
    sps.modality = wl.modality;
    sps.scheduled_step_description = wl.procedure_desc;
    sps.scheduled_step_status = wl.step_status;

    // Parse scheduled_datetime (YYYYMMDDHHMMSS)
    if (wl.scheduled_datetime.size() >= 8) {
        sps.scheduled_start_date = wl.scheduled_datetime.substr(0, 8);
        if (wl.scheduled_datetime.size() >= 14) {
            sps.scheduled_start_time = wl.scheduled_datetime.substr(8, 6);
        }
    }

    item.scheduled_steps.push_back(std::move(sps));

    return item;
}

/**
 * @brief Convert mwl_query_filter to pacs_system worklist_query
 */
[[nodiscard]] pacs::storage::worklist_query
to_worklist_query(const mwl_query_filter& filter) {
    pacs::storage::worklist_query query;

    if (filter.patient_id && !filter.patient_id->empty()) {
        query.patient_id = *filter.patient_id;
    }
    if (filter.accession_number && !filter.accession_number->empty()) {
        query.accession_no = *filter.accession_number;
    }
    if (filter.patient_name && !filter.patient_name->empty()) {
        query.patient_name = *filter.patient_name;
    }
    if (filter.modality && !filter.modality->empty()) {
        query.modality = *filter.modality;
    }
    if (filter.scheduled_station_ae && !filter.scheduled_station_ae->empty()) {
        query.station_ae = *filter.scheduled_station_ae;
    }
    if (filter.scheduled_date && !filter.scheduled_date->empty()) {
        query.scheduled_date_from = *filter.scheduled_date;
        query.scheduled_date_to = *filter.scheduled_date;
    }
    if (filter.scheduled_date_from && !filter.scheduled_date_from->empty()) {
        query.scheduled_date_from = *filter.scheduled_date_from;
    }
    if (filter.scheduled_date_to && !filter.scheduled_date_to->empty()) {
        query.scheduled_date_to = *filter.scheduled_date_to;
    }

    // Include all status or only scheduled
    query.include_all_status = false;

    if (filter.max_results > 0) {
        query.limit = filter.max_results;
    }

    return query;
}

/**
 * @brief Parse date string (YYYYMMDD) to system_clock time_point
 *
 * Converts a DICOM-format date string to a std::chrono time_point.
 * This enables precise date-based worklist cleanup operations.
 *
 * @param date_str Date string in YYYYMMDD format
 * @return time_point if parsing succeeds, nullopt otherwise
 */
[[nodiscard]] std::optional<std::chrono::system_clock::time_point>
parse_date_to_timepoint(std::string_view date_str) {
    // Validate length: YYYYMMDD (8 chars) or YYYY-MM-DD (10 chars)
    if (date_str.length() != 8 && date_str.length() != 10) {
        return std::nullopt;
    }

    std::tm tm = {};
    std::string normalized(date_str);

    // Handle YYYY-MM-DD format by removing dashes
    if (date_str.length() == 10) {
        normalized.erase(std::remove(normalized.begin(), normalized.end(), '-'),
                        normalized.end());
    }

    if (normalized.length() != 8) {
        return std::nullopt;
    }

    // Parse YYYYMMDD
    try {
        tm.tm_year = std::stoi(normalized.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(normalized.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(normalized.substr(6, 2));
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;  // Let mktime determine DST
    } catch (const std::exception&) {
        return std::nullopt;
    }

    // Validate parsed values
    if (tm.tm_year < 0 || tm.tm_mon < 0 || tm.tm_mon > 11 ||
        tm.tm_mday < 1 || tm.tm_mday > 31) {
        return std::nullopt;
    }

    auto time_t_val = std::mktime(&tm);
    if (time_t_val == -1) {
        return std::nullopt;
    }

    return std::chrono::system_clock::from_time_t(time_t_val);
}

}  // namespace
#endif  // PACS_BRIDGE_HAS_PACS_SYSTEM

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

        std::unique_lock lock(mutex_);

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
        // Real pacs_system integration: use index_database
        if (!db_) {
            span.set_error("Database not initialized");
            return std::unexpected(mwl_error::connection_failed);
        }

        // Convert mwl_item to worklist_item
        auto wl_item = to_worklist_item(item);

        // Check for duplicate by querying existing entry
        auto existing = db_->find_worklist_item(wl_item.step_id, wl_item.accession_no);
        if (existing.has_value()) {
            span.set_error("Duplicate entry");
            return std::unexpected(mwl_error::duplicate_entry);
        }

        // Add entry to database
        auto add_result = db_->add_worklist_item(wl_item);
        if (add_result.is_err()) {
            span.set_error("Database add failed: " + add_result.error().message);
            return std::unexpected(mwl_error::add_failed);
        }

        stats_.add_count++;
#else
        // Stub mode: use in-memory storage
        // Check for duplicate
        auto it = std::find_if(entries_.begin(), entries_.end(),
            [&item](const mapping::mwl_item& existing) {
                return existing.imaging_service_request.accession_number ==
                       item.imaging_service_request.accession_number;
            });

        if (it != entries_.end()) {
            span.set_error("Duplicate entry");
            return std::unexpected(mwl_error::duplicate_entry);
        }

        // Add entry to in-memory storage
        entries_.push_back(item);
        stats_.add_count++;
#endif

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

        std::unique_lock lock(mutex_);

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
        // Real pacs_system integration: use index_database
        if (!db_) {
            span.set_error("Database not initialized");
            return std::unexpected(mwl_error::connection_failed);
        }

        // Find existing entry by accession number using query
        pacs::storage::worklist_query query;
        query.accession_no = std::string(accession_number);
        query.include_all_status = true;

        auto query_result = db_->query_worklist(query);
        if (query_result.is_err() || query_result.value().empty()) {
            span.set_error("Entry not found");
            return std::unexpected(mwl_error::entry_not_found);
        }

        const auto& existing_wl = query_result.value()[0];

        // Convert updated item to worklist_item and merge
        auto wl_item = to_worklist_item(item);

        // Update status if the new status is provided
        std::string new_status = wl_item.step_status.empty()
                                     ? existing_wl.step_status
                                     : wl_item.step_status;

        auto update_result = db_->update_worklist_status(
            existing_wl.step_id, std::string(accession_number), new_status);

        if (update_result.is_err()) {
            span.set_error("Database update failed: " + update_result.error().message);
            return std::unexpected(mwl_error::update_failed);
        }

        stats_.update_count++;
#else
        // Stub mode: use in-memory storage
        auto it = std::find_if(entries_.begin(), entries_.end(),
            [accession_number](const mapping::mwl_item& existing) {
                return existing.imaging_service_request.accession_number ==
                       accession_number;
            });

        if (it == entries_.end()) {
            span.set_error("Entry not found");
            return std::unexpected(mwl_error::entry_not_found);
        }

        // Update entry with non-empty fields from item
        update_mwl_item(*it, item);
        stats_.update_count++;
#endif

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

        std::unique_lock lock(mutex_);

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
        // Real pacs_system integration: use index_database
        if (!db_) {
            span.set_error("Database not initialized");
            return std::unexpected(mwl_error::connection_failed);
        }

        // Find entry by accession number to get step_id
        pacs::storage::worklist_query query;
        query.accession_no = std::string(accession_number);
        query.include_all_status = true;

        auto query_result = db_->query_worklist(query);
        if (query_result.is_err() || query_result.value().empty()) {
            span.set_error("Entry not found");
            return std::unexpected(mwl_error::entry_not_found);
        }

        const auto& existing_wl = query_result.value()[0];

        // Delete entry from database
        auto delete_result = db_->delete_worklist_item(
            existing_wl.step_id, std::string(accession_number));

        if (delete_result.is_err()) {
            span.set_error("Database delete failed: " + delete_result.error().message);
            return std::unexpected(mwl_error::cancel_failed);
        }

        stats_.cancel_count++;
#else
        // Stub mode: use in-memory storage
        auto it = std::find_if(entries_.begin(), entries_.end(),
            [accession_number](const mapping::mwl_item& existing) {
                return existing.imaging_service_request.accession_number ==
                       accession_number;
            });

        if (it == entries_.end()) {
            span.set_error("Entry not found");
            return std::unexpected(mwl_error::entry_not_found);
        }

        entries_.erase(it);
        stats_.cancel_count++;
#endif

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

        std::shared_lock lock(mutex_);

        std::vector<mapping::mwl_item> results;

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
        // Real pacs_system integration: use index_database
        if (!db_) {
            span.set_error("Database not initialized");
            return std::unexpected(mwl_error::connection_failed);
        }

        // Convert filter to pacs_system worklist_query
        auto wl_query = to_worklist_query(filter);

        auto query_result = db_->query_worklist(wl_query);
        if (query_result.is_err()) {
            span.set_error("Database query failed: " + query_result.error().message);
            return std::unexpected(mwl_error::query_failed);
        }

        // Convert worklist_items to mwl_items
        for (const auto& wl_item : query_result.value()) {
            results.push_back(from_worklist_item(wl_item));
        }

        stats_.query_count++;
#else
        // Stub mode: use in-memory storage
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
#endif

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        // Record metrics for query duration (in nanoseconds)
        auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start_time);
        monitoring::bridge_metrics_collector::instance().record_mwl_query_duration(
            elapsed_ns);

        update_avg_operation_time(elapsed.count());

        span.set_attribute("pacs.duration_ms", static_cast<int64_t>(elapsed.count()));
        span.set_attribute("pacs.result_count", static_cast<int64_t>(results.size()));
        span.set_attribute("pacs.success", true);

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

        if (!ensure_connected()) {
            return false;
        }

        std::shared_lock lock(mutex_);

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
        // Real pacs_system integration: use index_database
        if (!db_) {
            return false;
        }

        pacs::storage::worklist_query query;
        query.accession_no = std::string(accession_number);
        query.include_all_status = true;
        query.limit = 1;

        auto query_result = db_->query_worklist(query);
        return query_result.is_ok() && !query_result.value().empty();
#else
        // Stub mode: use in-memory storage
        return std::any_of(entries_.begin(), entries_.end(),
            [accession_number](const mapping::mwl_item& entry) {
                return entry.imaging_service_request.accession_number ==
                       accession_number;
            });
#endif
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

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
        // Real pacs_system integration: use index_database
        if (!db_) {
            return std::unexpected(mwl_error::connection_failed);
        }

        pacs::storage::worklist_query query;
        query.accession_no = std::string(accession_number);
        query.include_all_status = true;
        query.limit = 1;

        auto query_result = db_->query_worklist(query);
        if (query_result.is_err() || query_result.value().empty()) {
            return std::unexpected(mwl_error::entry_not_found);
        }

        return from_worklist_item(query_result.value()[0]);
#else
        // Stub mode: use in-memory storage
        auto it = std::find_if(entries_.begin(), entries_.end(),
            [accession_number](const mapping::mwl_item& entry) {
                return entry.imaging_service_request.accession_number ==
                       accession_number;
            });

        if (it == entries_.end()) {
            return std::unexpected(mwl_error::entry_not_found);
        }

        return *it;
#endif
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

        size_t cancelled = 0;

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
        // Real pacs_system integration: use index_database
        if (!db_) {
            return std::unexpected(mwl_error::connection_failed);
        }

        // Parse date string (YYYYMMDD format) to time_point
        // This provides exact date-based cleanup without timezone ambiguities
        auto before_time = parse_date_to_timepoint(before_date);
        if (!before_time) {
            return std::unexpected(mwl_error::invalid_data);
        }

        // Use pacs_system's date-based cleanup API for precise cleanup
        auto cleanup_result = db_->cleanup_worklist_items_before(*before_time);
        if (cleanup_result.is_err()) {
            return std::unexpected(mwl_error::cancel_failed);
        }

        cancelled = cleanup_result.value();

        // Update statistics for each cancelled entry
        for (size_t i = 0; i < cancelled; ++i) {
            stats_.cancel_count++;
            monitoring::bridge_metrics_collector::instance()
                .record_mwl_entry_cancelled();
        }
#else
        // Stub mode: use in-memory storage
        // Validate date format (YYYYMMDD - 8 chars)
        if (before_date.length() != 8) {
            return std::unexpected(mwl_error::invalid_data);
        }

        // Check if all characters are digits
        for (char c : before_date) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return std::unexpected(mwl_error::invalid_data);
            }
        }

        std::string before_date_str(before_date);

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
#endif

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

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
            // Real pacs_system integration: open index_database
            if (!db_) {
                auto db_result = pacs::storage::index_database::open(
                    config_.pacs_database_path);

                if (db_result.is_err()) {
                    // Log error and retry
                    continue;
                }

                db_ = std::move(db_result.value());
            }

            // Verify database is open and accessible
            if (db_ && db_->is_open()) {
                return {};
            }
#else
            // Stub mode: Connection always succeeds (in-memory storage)
            // In real implementation:
            // - Create DICOM association
            // - Negotiate presentation contexts for MWL
            // - Verify AE titles
            return {};
#endif
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

#ifndef PACS_BRIDGE_HAS_PACS_SYSTEM
    // Stub mode helper methods
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
#endif  // !PACS_BRIDGE_HAS_PACS_SYSTEM

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
    mwl_client::statistics stats_;

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
    // Real pacs_system integration: use index_database for persistent storage
    std::unique_ptr<pacs::storage::index_database> db_;
#else
    // Stub mode: use in-memory storage
    std::vector<mapping::mwl_item> entries_;
#endif
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
