/**
 * @file mwl_adapter.cpp
 * @brief Implementation of MWL adapter interface and concrete adapters
 *
 * Provides two implementations:
 * - memory_mwl_adapter: In-memory storage for standalone/testing
 * - pacs_mwl_adapter: pacs_system index_database integration
 */

#include "pacs/bridge/integration/mwl_adapter.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
#include <pacs/storage/index_database.hpp>
#include <pacs/storage/worklist_record.hpp>
#endif

namespace pacs::bridge::integration {

// =============================================================================
// Memory MWL Adapter (for standalone mode and testing)
// =============================================================================

class memory_mwl_adapter final : public mwl_adapter {
public:
    memory_mwl_adapter() = default;
    ~memory_mwl_adapter() override = default;

    std::expected<void, mwl_adapter_error>
    add_item(const mapping::mwl_item& item) override {
        if (item.imaging_service_request.accession_number.empty()) {
            return std::unexpected(mwl_adapter_error::invalid_data);
        }

        std::unique_lock lock(mutex_);

        const auto& accession_number = item.imaging_service_request.accession_number;

        // Check for duplicate
        if (items_.contains(accession_number)) {
            return std::unexpected(mwl_adapter_error::duplicate);
        }

        items_[accession_number] = item;
        return {};
    }

    std::expected<void, mwl_adapter_error>
    update_item(std::string_view accession_number,
                const mapping::mwl_item& item) override {
        if (accession_number.empty()) {
            return std::unexpected(mwl_adapter_error::invalid_data);
        }

        std::unique_lock lock(mutex_);

        auto it = items_.find(std::string(accession_number));
        if (it == items_.end()) {
            return std::unexpected(mwl_adapter_error::not_found);
        }

        // Update existing item with non-empty fields
        update_fields(it->second, item);
        return {};
    }

    std::expected<void, mwl_adapter_error>
    delete_item(std::string_view accession_number) override {
        if (accession_number.empty()) {
            return std::unexpected(mwl_adapter_error::invalid_data);
        }

        std::unique_lock lock(mutex_);

        auto it = items_.find(std::string(accession_number));
        if (it == items_.end()) {
            return std::unexpected(mwl_adapter_error::not_found);
        }

        items_.erase(it);
        return {};
    }

    std::expected<std::vector<mapping::mwl_item>, mwl_adapter_error>
    query_items(const mwl_query_filter& filter) override {
        std::shared_lock lock(mutex_);

        std::vector<mapping::mwl_item> results;

        for (const auto& [accession, item] : items_) {
            if (matches_filter(item, filter)) {
                results.push_back(item);

                if (filter.max_results > 0 &&
                    results.size() >= filter.max_results) {
                    break;
                }
            }
        }

        return results;
    }

    std::expected<mapping::mwl_item, mwl_adapter_error>
    get_item(std::string_view accession_number) override {
        if (accession_number.empty()) {
            return std::unexpected(mwl_adapter_error::invalid_data);
        }

        std::shared_lock lock(mutex_);

        auto it = items_.find(std::string(accession_number));
        if (it == items_.end()) {
            return std::unexpected(mwl_adapter_error::not_found);
        }

        return it->second;
    }

    bool exists(std::string_view accession_number) override {
        if (accession_number.empty()) {
            return false;
        }

        std::shared_lock lock(mutex_);
        return items_.contains(std::string(accession_number));
    }

    std::expected<size_t, mwl_adapter_error>
    delete_items_before(std::string_view before_date) override {
        // Validate date format (YYYYMMDD)
        if (before_date.length() != 8) {
            return std::unexpected(mwl_adapter_error::invalid_data);
        }

        for (char c : before_date) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return std::unexpected(mwl_adapter_error::invalid_data);
            }
        }

        std::unique_lock lock(mutex_);

        std::string before_date_str(before_date);
        size_t deleted = 0;

        auto it = items_.begin();
        while (it != items_.end()) {
            bool should_delete = false;

            if (!it->second.scheduled_steps.empty()) {
                const auto& sps = it->second.scheduled_steps[0];
                if (!sps.scheduled_start_date.empty() &&
                    sps.scheduled_start_date < before_date_str) {
                    should_delete = true;
                }
            }

            if (should_delete) {
                it = items_.erase(it);
                deleted++;
            } else {
                ++it;
            }
        }

        return deleted;
    }

    const char* adapter_type() const noexcept override {
        return "memory";
    }

private:
    bool matches_filter(const mapping::mwl_item& item,
                       const mwl_query_filter& filter) const {
        // Patient ID filter
        if (filter.patient_id && !filter.patient_id->empty()) {
            if (item.patient.patient_id != *filter.patient_id) {
                return false;
            }
        }

        // Accession number filter
        if (filter.accession_number && !filter.accession_number->empty()) {
            if (item.imaging_service_request.accession_number !=
                *filter.accession_number) {
                return false;
            }
        }

        // Patient name filter (supports wildcards)
        if (filter.patient_name && !filter.patient_name->empty()) {
            if (!matches_wildcard(item.patient.patient_name,
                                  *filter.patient_name)) {
                return false;
            }
        }

        // Referring physician filter
        if (filter.referring_physician && !filter.referring_physician->empty()) {
            if (!matches_wildcard(
                    item.requested_procedure.referring_physician_name,
                    *filter.referring_physician)) {
                return false;
            }
        }

        // Scheduled procedure step filters
        if (!item.scheduled_steps.empty()) {
            const auto& sps = item.scheduled_steps[0];

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

    void update_fields(mapping::mwl_item& existing,
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

    std::unordered_map<std::string, mapping::mwl_item> items_;
    mutable std::shared_mutex mutex_;
};

// =============================================================================
// PACS MWL Adapter (for pacs_system integration)
// =============================================================================

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM

namespace {

// Convert mwl_item to pacs_system worklist_item
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

// Convert pacs_system worklist_item to mwl_item
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

// Convert mwl_query_filter to pacs_system worklist_query
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

    query.include_all_status = false;

    if (filter.max_results > 0) {
        query.limit = filter.max_results;
    }

    return query;
}

// Parse date string to time_point
[[nodiscard]] std::optional<std::chrono::system_clock::time_point>
parse_date_to_timepoint(std::string_view date_str) {
    if (date_str.length() != 8 && date_str.length() != 10) {
        return std::nullopt;
    }

    std::tm tm = {};
    std::string normalized(date_str);

    if (date_str.length() == 10) {
        normalized.erase(std::remove(normalized.begin(), normalized.end(), '-'),
                        normalized.end());
    }

    if (normalized.length() != 8) {
        return std::nullopt;
    }

    try {
        tm.tm_year = std::stoi(normalized.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(normalized.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(normalized.substr(6, 2));
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;
    } catch (const std::exception&) {
        return std::nullopt;
    }

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

class pacs_mwl_adapter final : public mwl_adapter {
public:
    explicit pacs_mwl_adapter(const std::string& database_path) {
        auto db_result = pacs::storage::index_database::open(database_path);
        if (db_result.is_err()) {
            // Database failed to open - will return errors on all operations
            return;
        }
        db_ = std::move(db_result.value());
    }

    ~pacs_mwl_adapter() override = default;

    std::expected<void, mwl_adapter_error>
    add_item(const mapping::mwl_item& item) override {
        if (!db_ || !db_->is_open()) {
            return std::unexpected(mwl_adapter_error::storage_unavailable);
        }

        if (item.imaging_service_request.accession_number.empty()) {
            return std::unexpected(mwl_adapter_error::invalid_data);
        }

        auto wl_item = to_worklist_item(item);

        // Check for duplicate
        auto existing = db_->find_worklist_item(wl_item.step_id, wl_item.accession_no);
        if (existing.has_value()) {
            return std::unexpected(mwl_adapter_error::duplicate);
        }

        auto result = db_->add_worklist_item(wl_item);
        if (result.is_err()) {
            return std::unexpected(mwl_adapter_error::add_failed);
        }

        return {};
    }

    std::expected<void, mwl_adapter_error>
    update_item(std::string_view accession_number,
                const mapping::mwl_item& item) override {
        if (!db_ || !db_->is_open()) {
            return std::unexpected(mwl_adapter_error::storage_unavailable);
        }

        if (accession_number.empty()) {
            return std::unexpected(mwl_adapter_error::invalid_data);
        }

        // Find existing entry
        pacs::storage::worklist_query query;
        query.accession_no = std::string(accession_number);
        query.include_all_status = true;

        auto query_result = db_->query_worklist(query);
        if (query_result.is_err() || query_result.value().empty()) {
            return std::unexpected(mwl_adapter_error::not_found);
        }

        const auto& existing_wl = query_result.value()[0];

        // Convert updated item
        auto wl_item = to_worklist_item(item);

        // Update status
        std::string new_status = wl_item.step_status.empty()
                                     ? existing_wl.step_status
                                     : wl_item.step_status;

        auto update_result = db_->update_worklist_status(
            existing_wl.step_id, std::string(accession_number), new_status);

        if (update_result.is_err()) {
            return std::unexpected(mwl_adapter_error::update_failed);
        }

        return {};
    }

    std::expected<void, mwl_adapter_error>
    delete_item(std::string_view accession_number) override {
        if (!db_ || !db_->is_open()) {
            return std::unexpected(mwl_adapter_error::storage_unavailable);
        }

        if (accession_number.empty()) {
            return std::unexpected(mwl_adapter_error::invalid_data);
        }

        // Find entry to get step_id
        pacs::storage::worklist_query query;
        query.accession_no = std::string(accession_number);
        query.include_all_status = true;

        auto query_result = db_->query_worklist(query);
        if (query_result.is_err() || query_result.value().empty()) {
            return std::unexpected(mwl_adapter_error::not_found);
        }

        const auto& existing_wl = query_result.value()[0];

        auto delete_result = db_->delete_worklist_item(
            existing_wl.step_id, std::string(accession_number));

        if (delete_result.is_err()) {
            return std::unexpected(mwl_adapter_error::delete_failed);
        }

        return {};
    }

    std::expected<std::vector<mapping::mwl_item>, mwl_adapter_error>
    query_items(const mwl_query_filter& filter) override {
        if (!db_ || !db_->is_open()) {
            return std::unexpected(mwl_adapter_error::storage_unavailable);
        }

        auto wl_query = to_worklist_query(filter);

        auto query_result = db_->query_worklist(wl_query);
        if (query_result.is_err()) {
            return std::unexpected(mwl_adapter_error::query_failed);
        }

        std::vector<mapping::mwl_item> results;
        for (const auto& wl_item : query_result.value()) {
            results.push_back(from_worklist_item(wl_item));
        }

        return results;
    }

    std::expected<mapping::mwl_item, mwl_adapter_error>
    get_item(std::string_view accession_number) override {
        if (!db_ || !db_->is_open()) {
            return std::unexpected(mwl_adapter_error::storage_unavailable);
        }

        if (accession_number.empty()) {
            return std::unexpected(mwl_adapter_error::invalid_data);
        }

        pacs::storage::worklist_query query;
        query.accession_no = std::string(accession_number);
        query.include_all_status = true;
        query.limit = 1;

        auto query_result = db_->query_worklist(query);
        if (query_result.is_err() || query_result.value().empty()) {
            return std::unexpected(mwl_adapter_error::not_found);
        }

        return from_worklist_item(query_result.value()[0]);
    }

    bool exists(std::string_view accession_number) override {
        if (!db_ || !db_->is_open()) {
            return false;
        }

        if (accession_number.empty()) {
            return false;
        }

        pacs::storage::worklist_query query;
        query.accession_no = std::string(accession_number);
        query.include_all_status = true;
        query.limit = 1;

        auto query_result = db_->query_worklist(query);
        return query_result.is_ok() && !query_result.value().empty();
    }

    std::expected<size_t, mwl_adapter_error>
    delete_items_before(std::string_view before_date) override {
        if (!db_ || !db_->is_open()) {
            return std::unexpected(mwl_adapter_error::storage_unavailable);
        }

        auto before_time = parse_date_to_timepoint(before_date);
        if (!before_time) {
            return std::unexpected(mwl_adapter_error::invalid_data);
        }

        auto cleanup_result = db_->cleanup_worklist_items_before(*before_time);
        if (cleanup_result.is_err()) {
            return std::unexpected(mwl_adapter_error::delete_failed);
        }

        return cleanup_result.value();
    }

    const char* adapter_type() const noexcept override {
        return "pacs_system";
    }

private:
    std::unique_ptr<pacs::storage::index_database> db_;
};

#endif  // PACS_BRIDGE_HAS_PACS_SYSTEM

// =============================================================================
// Factory Function
// =============================================================================

std::shared_ptr<mwl_adapter>
create_mwl_adapter(const std::string& database_path) {
#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
    return std::make_shared<pacs_mwl_adapter>(database_path);
#else
    (void)database_path;  // Unused in standalone mode
    return std::make_shared<memory_mwl_adapter>();
#endif
}

}  // namespace pacs::bridge::integration
