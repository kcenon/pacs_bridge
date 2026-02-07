/**
 * @file pacs_adapter.cpp
 * @brief Implementation of PACS adapter for pacs_bridge
 *
 * @see include/pacs/bridge/integration/pacs_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/283
 */

#include "pacs/bridge/integration/pacs_adapter.h"

#include <algorithm>
#include <cassert>
#include <mutex>
#include <unordered_map>

namespace pacs::bridge::integration {

// =============================================================================
// Error Handling
// =============================================================================

std::string_view to_string(pacs_error error) noexcept {
    switch (error) {
        case pacs_error::connection_failed:
            return "Connection to PACS server failed";
        case pacs_error::query_failed:
            return "Query execution failed";
        case pacs_error::store_failed:
            return "Store operation failed";
        case pacs_error::invalid_dataset:
            return "Invalid or malformed DICOM dataset";
        case pacs_error::association_failed:
            return "DICOM association failed";
        case pacs_error::timeout:
            return "Operation timeout";
        case pacs_error::not_found:
            return "Resource not found";
        case pacs_error::duplicate_entry:
            return "Duplicate entry detected";
        case pacs_error::validation_failed:
            return "Validation failed";
        case pacs_error::mpps_create_failed:
            return "MPPS N-CREATE failed";
        case pacs_error::mpps_update_failed:
            return "MPPS N-SET failed";
        case pacs_error::mwl_query_failed:
            return "MWL query failed";
        case pacs_error::storage_failed:
            return "DICOM storage failed";
        case pacs_error::invalid_sop_uid:
            return "Invalid SOP Instance UID";
        default:
            return "Unknown PACS error";
    }
}

// =============================================================================
// dicom_dataset Implementation
// =============================================================================

std::optional<std::string> dicom_dataset::get_string(uint32_t tag) const {
    auto it = attributes.find(tag);
    if (it != attributes.end()) {
        return it->second;
    }
    return std::nullopt;
}

void dicom_dataset::set_string(uint32_t tag, std::string_view value) {
    attributes[tag] = std::string(value);
}

bool dicom_dataset::has_tag(uint32_t tag) const {
    return attributes.find(tag) != attributes.end();
}

void dicom_dataset::remove_tag(uint32_t tag) {
    attributes.erase(tag);
}

void dicom_dataset::clear() {
    sop_class_uid.clear();
    sop_instance_uid.clear();
    attributes.clear();
}

// =============================================================================
// mpps_record Implementation
// =============================================================================

bool mpps_record::is_valid() const {
    // Check required fields
    if (sop_instance_uid.empty()) return false;
    if (status.empty()) return false;

    // Require at least one identifier for tracking
    if (scheduled_procedure_step_id.empty() && accession_number.empty()) {
        return false;
    }

    // Validate status
    if (status != "IN PROGRESS" && status != "COMPLETED" && status != "DISCONTINUED") {
        return false;
    }

    // COMPLETED status must have end_datetime
    if (status == "COMPLETED" && !end_datetime.has_value()) {
        return false;
    }

    return true;
}

// =============================================================================
// mwl_item Implementation
// =============================================================================

bool mwl_item::is_valid() const {
    // Check required fields
    if (accession_number.empty()) return false;
    if (scheduled_procedure_step_id.empty()) return false;
    if (patient_id.empty()) return false;
    if (patient_name.empty()) return false;
    if (modality.empty()) return false;

    return true;
}

// =============================================================================
// Stub MPPS Adapter
// =============================================================================

/**
 * @brief In-memory implementation of MPPS adapter (standalone mode)
 *
 * Provides thread-safe in-memory storage for MPPS records,
 * enabling standalone testing and operation without pacs_system.
 */
class stub_mpps_adapter : public mpps_adapter {
public:
    std::expected<void, pacs_error> create_mpps(const mpps_record& record) override {
        if (!record.is_valid()) {
            return std::unexpected(pacs_error::validation_failed);
        }

        std::lock_guard lock(mutex_);
        auto [it, inserted] = records_.emplace(record.sop_instance_uid, record);
        if (!inserted) {
            return std::unexpected(pacs_error::duplicate_entry);
        }
        return {};
    }

    std::expected<void, pacs_error> update_mpps(const mpps_record& record) override {
        if (!record.is_valid()) {
            return std::unexpected(pacs_error::validation_failed);
        }

        std::lock_guard lock(mutex_);
        auto it = records_.find(record.sop_instance_uid);
        if (it == records_.end()) {
            return std::unexpected(pacs_error::not_found);
        }
        it->second = record;
        return {};
    }

    std::expected<std::vector<mpps_record>, pacs_error> query_mpps(
        const mpps_query_params& params) override {
        std::lock_guard lock(mutex_);
        std::vector<mpps_record> results;

        for (const auto& [uid, record] : records_) {
            bool match = true;
            if (params.patient_id && *params.patient_id != record.patient_id) {
                match = false;
            }
            if (params.study_instance_uid && *params.study_instance_uid != record.study_instance_uid) {
                match = false;
            }
            if (params.status && *params.status != record.status) {
                match = false;
            }
            if (params.station_ae_title && *params.station_ae_title != record.performed_station_ae_title) {
                match = false;
            }
            if (params.modality && *params.modality != record.modality) {
                match = false;
            }
            if (params.accession_number && *params.accession_number != record.accession_number) {
                match = false;
            }
            if (match) {
                results.push_back(record);
                if (results.size() >= params.max_results) {
                    break;
                }
            }
        }

        return results;
    }

    std::expected<mpps_record, pacs_error> get_mpps(
        std::string_view sop_instance_uid) override {
        if (sop_instance_uid.empty()) {
            return std::unexpected(pacs_error::invalid_sop_uid);
        }

        std::lock_guard lock(mutex_);
        auto it = records_.find(std::string(sop_instance_uid));
        if (it == records_.end()) {
            return std::unexpected(pacs_error::not_found);
        }
        return it->second;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, mpps_record> records_;
};

// =============================================================================
// Stub MWL Adapter
// =============================================================================

/**
 * @brief Stub implementation of MWL adapter (standalone mode)
 *
 * Provides no-op implementations for testing and standalone usage.
 */
class stub_mwl_adapter : public mwl_adapter {
public:
    std::expected<std::vector<mwl_item>, pacs_error> query_mwl(
        const mwl_query_params& params) override {
        // Stub: return empty result
        return std::vector<mwl_item>{};
    }

    std::expected<mwl_item, pacs_error> get_mwl_item(
        std::string_view accession_number) override {
        if (accession_number.empty()) {
            return std::unexpected(pacs_error::validation_failed);
        }
        // Stub: not found
        return std::unexpected(pacs_error::not_found);
    }
};

// =============================================================================
// Stub Storage Adapter
// =============================================================================

/**
 * @brief Stub implementation of storage adapter (standalone mode)
 *
 * Provides no-op implementations for testing and standalone usage.
 */
class stub_storage_adapter : public storage_adapter {
public:
    std::expected<void, pacs_error> store(const dicom_dataset& dataset) override {
        if (dataset.sop_instance_uid.empty()) {
            return std::unexpected(pacs_error::invalid_dataset);
        }
        // Stub: no-op implementation
        return {};
    }

    std::expected<dicom_dataset, pacs_error> retrieve(
        std::string_view sop_instance_uid) override {
        if (sop_instance_uid.empty()) {
            return std::unexpected(pacs_error::invalid_sop_uid);
        }
        // Stub: not found
        return std::unexpected(pacs_error::not_found);
    }

    bool exists(std::string_view sop_instance_uid) const override {
        // Stub: always return false
        return false;
    }
};

// =============================================================================
// Stub PACS Adapter
// =============================================================================

/**
 * @brief Stub implementation of PACS adapter (standalone mode)
 *
 * Provides a complete stub implementation for standalone usage
 * without pacs_system integration.
 */
class stub_pacs_adapter : public pacs_adapter {
public:
    explicit stub_pacs_adapter(const pacs_config& config)
        : config_(config)
        , mpps_adapter_(std::make_shared<stub_mpps_adapter>())
        , mwl_adapter_(std::make_shared<stub_mwl_adapter>())
        , storage_adapter_(std::make_shared<stub_storage_adapter>())
        , connected_(false) {}

    std::shared_ptr<mpps_adapter> get_mpps_adapter() override {
        return mpps_adapter_;
    }

    std::shared_ptr<mwl_adapter> get_mwl_adapter() override {
        return mwl_adapter_;
    }

    std::shared_ptr<storage_adapter> get_storage_adapter() override {
        return storage_adapter_;
    }

    std::expected<void, pacs_error> connect() override {
        // Stub: simulate successful connection
        connected_ = true;
        return {};
    }

    void disconnect() override {
        // Stub: simulate disconnection
        connected_ = false;
    }

    bool is_connected() const override {
        return connected_;
    }

    bool is_healthy() const override {
        // Stub: always healthy if connected
        return connected_;
    }

private:
    pacs_config config_;
    std::shared_ptr<stub_mpps_adapter> mpps_adapter_;
    std::shared_ptr<stub_mwl_adapter> mwl_adapter_;
    std::shared_ptr<stub_storage_adapter> storage_adapter_;
    bool connected_;
};

// =============================================================================
// pacs_system Integration (when pacs_system is available)
// =============================================================================

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM

#include <pacs/storage/index_database.hpp>
#include <pacs/storage/instance_record.hpp>
#include <pacs/storage/mpps_record.hpp>
#include <pacs/storage/worklist_record.hpp>

namespace {

// =============================================================================
// DateTime Conversion Helpers
// =============================================================================

/**
 * @brief Format time_point as DICOM datetime string (YYYYMMDDHHMMSS)
 */
std::string format_datetime_string(std::chrono::system_clock::time_point tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = {};
#ifdef _WIN32
    localtime_s(&tm, &time_t_val);
#else
    localtime_r(&time_t_val, &tm);
#endif
    char buf[15];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
}

/**
 * @brief Parse DICOM datetime string (YYYYMMDD or YYYYMMDDHHMMSS) to time_point
 */
std::optional<std::chrono::system_clock::time_point>
parse_datetime_string(std::string_view dt_str) {
    if (dt_str.length() < 8) {
        return std::nullopt;
    }

    std::tm tm = {};
    try {
        std::string s(dt_str);
        tm.tm_year = std::stoi(s.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(s.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(s.substr(6, 2));
        if (s.length() >= 14) {
            tm.tm_hour = std::stoi(s.substr(8, 2));
            tm.tm_min = std::stoi(s.substr(10, 2));
            tm.tm_sec = std::stoi(s.substr(12, 2));
        }
        tm.tm_isdst = -1;
    } catch (const std::exception&) {
        return std::nullopt;
    }

    auto time_t_val = std::mktime(&tm);
    if (time_t_val == -1) {
        return std::nullopt;
    }

    return std::chrono::system_clock::from_time_t(time_t_val);
}

// =============================================================================
// MPPS Status Mapping
// =============================================================================

pacs::storage::mpps_status map_status_to_pacs(std::string_view status) {
    if (status == "COMPLETED") {
        return pacs::storage::mpps_status::completed;
    }
    if (status == "DISCONTINUED") {
        return pacs::storage::mpps_status::discontinued;
    }
    return pacs::storage::mpps_status::in_progress;
}

std::string map_status_from_pacs(pacs::storage::mpps_status status) {
    switch (status) {
        case pacs::storage::mpps_status::completed:
            return "COMPLETED";
        case pacs::storage::mpps_status::discontinued:
            return "DISCONTINUED";
        default:
            return "IN PROGRESS";
    }
}

// =============================================================================
// MPPS Record Conversion
// =============================================================================

pacs::storage::mpps_record
to_pacs_mpps_record(const mpps_record& record) {
    pacs::storage::mpps_record pacs_rec;

    pacs_rec.mpps_uid = record.sop_instance_uid;
    pacs_rec.status = map_status_to_pacs(record.status);
    pacs_rec.start_datetime = format_datetime_string(record.start_datetime);

    if (record.end_datetime) {
        pacs_rec.end_datetime = format_datetime_string(*record.end_datetime);
    }

    pacs_rec.station_ae = record.performed_station_ae_title;
    pacs_rec.station_name = record.performed_station_name;
    pacs_rec.modality = record.modality;
    pacs_rec.study_uid = record.study_instance_uid;
    pacs_rec.accession_no = record.accession_number;
    pacs_rec.scheduled_step_id = record.scheduled_procedure_step_id;
    pacs_rec.requested_proc_id = record.requested_procedure_id;

    // Convert series UIDs to performed_series_info
    for (const auto& uid : record.series_instance_uids) {
        pacs::storage::performed_series_info info;
        info.series_uid = uid;
        pacs_rec.performed_series.push_back(std::move(info));
    }

    return pacs_rec;
}

mpps_record
from_pacs_mpps_record(const pacs::storage::mpps_record& pacs_rec) {
    mpps_record record;

    record.sop_instance_uid = pacs_rec.mpps_uid;
    record.status = map_status_from_pacs(pacs_rec.status);

    auto start_tp = parse_datetime_string(pacs_rec.start_datetime);
    if (start_tp) {
        record.start_datetime = *start_tp;
    }

    if (!pacs_rec.end_datetime.empty()) {
        record.end_datetime = parse_datetime_string(pacs_rec.end_datetime);
    }

    record.performed_station_ae_title = pacs_rec.station_ae;
    record.performed_station_name = pacs_rec.station_name;
    record.modality = pacs_rec.modality;
    record.study_instance_uid = pacs_rec.study_uid;
    record.accession_number = pacs_rec.accession_no;
    record.scheduled_procedure_step_id = pacs_rec.scheduled_step_id;
    record.requested_procedure_id = pacs_rec.requested_proc_id;

    for (const auto& info : pacs_rec.performed_series) {
        record.series_instance_uids.push_back(info.series_uid);
    }

    return record;
}

// =============================================================================
// MPPS Query Conversion
// =============================================================================

pacs::storage::mpps_query
to_pacs_mpps_query(const mpps_query_params& params) {
    pacs::storage::mpps_query query;

    if (params.study_instance_uid) {
        query.study_uid = *params.study_instance_uid;
    }
    if (params.status) {
        query.status = map_status_to_pacs(*params.status);
    }
    if (params.station_ae_title) {
        query.station_ae = *params.station_ae_title;
    }
    if (params.modality) {
        query.modality = *params.modality;
    }
    if (params.accession_number) {
        query.accession_no = *params.accession_number;
    }
    if (params.from_datetime) {
        query.start_date_from = format_datetime_string(*params.from_datetime);
    }
    if (params.to_datetime) {
        query.start_date_to = format_datetime_string(*params.to_datetime);
    }

    query.limit = params.max_results;

    return query;
}

// =============================================================================
// MWL Conversion
// =============================================================================

mwl_item from_pacs_worklist_item(const pacs::storage::worklist_item& wl) {
    mwl_item item;

    item.accession_number = wl.accession_no;
    item.scheduled_procedure_step_id = wl.step_id;
    item.requested_procedure_id = wl.requested_proc_id;
    item.scheduled_station_ae_title = wl.station_ae;
    item.modality = wl.modality;
    item.patient_id = wl.patient_id;
    item.patient_name = wl.patient_name;
    item.study_instance_uid = wl.study_uid;

    if (!wl.scheduled_datetime.empty()) {
        auto tp = parse_datetime_string(wl.scheduled_datetime);
        if (tp) {
            item.scheduled_datetime = *tp;
        }
    }

    return item;
}

pacs::storage::worklist_query
to_pacs_worklist_query(const mwl_query_params& params) {
    pacs::storage::worklist_query query;

    if (params.patient_id) {
        query.patient_id = *params.patient_id;
    }
    if (params.accession_number) {
        query.accession_no = *params.accession_number;
    }
    if (params.modality) {
        query.modality = *params.modality;
    }
    if (params.scheduled_date) {
        auto date_str = format_datetime_string(*params.scheduled_date);
        // Use date portion only (YYYYMMDD)
        query.scheduled_date_from = date_str.substr(0, 8);
        query.scheduled_date_to = date_str.substr(0, 8);
    }

    query.include_all_status = false;

    if (params.max_results > 0) {
        query.limit = params.max_results;
    }

    return query;
}

}  // anonymous namespace

// =============================================================================
// pacs_system MPPS Adapter
// =============================================================================

/**
 * @brief MPPS adapter backed by pacs_system index_database
 *
 * Provides full MPPS persistence through pacs_system's SQLite-backed
 * index database, supporting create, update, query, and retrieval operations.
 */
class pacs_system_mpps_adapter : public mpps_adapter {
public:
    explicit pacs_system_mpps_adapter(
        std::shared_ptr<pacs::storage::index_database> db)
        : db_(std::move(db)) {}

    std::expected<void, pacs_error>
    create_mpps(const mpps_record& record) override {
        if (!record.is_valid()) {
            return std::unexpected(pacs_error::validation_failed);
        }
        if (!db_) {
            return std::unexpected(pacs_error::connection_failed);
        }

        auto pacs_rec = to_pacs_mpps_record(record);
        auto result = db_->create_mpps(pacs_rec);
        if (result.is_err()) {
            return std::unexpected(pacs_error::mpps_create_failed);
        }

        return {};
    }

    std::expected<void, pacs_error>
    update_mpps(const mpps_record& record) override {
        if (!record.is_valid()) {
            return std::unexpected(pacs_error::validation_failed);
        }
        if (!db_) {
            return std::unexpected(pacs_error::connection_failed);
        }

        auto pacs_rec = to_pacs_mpps_record(record);
        auto result = db_->update_mpps(pacs_rec);
        if (result.is_err()) {
            return std::unexpected(pacs_error::mpps_update_failed);
        }

        return {};
    }

    std::expected<std::vector<mpps_record>, pacs_error>
    query_mpps(const mpps_query_params& params) override {
        if (!db_) {
            return std::unexpected(pacs_error::connection_failed);
        }

        auto pacs_query = to_pacs_mpps_query(params);
        auto result = db_->search_mpps(pacs_query);
        if (result.is_err()) {
            return std::unexpected(pacs_error::query_failed);
        }

        std::vector<mpps_record> records;
        records.reserve(result.value().size());
        for (const auto& pacs_rec : result.value()) {
            records.push_back(from_pacs_mpps_record(pacs_rec));
        }

        return records;
    }

    std::expected<mpps_record, pacs_error>
    get_mpps(std::string_view sop_instance_uid) override {
        if (sop_instance_uid.empty()) {
            return std::unexpected(pacs_error::invalid_sop_uid);
        }
        if (!db_) {
            return std::unexpected(pacs_error::connection_failed);
        }

        auto result = db_->find_mpps(std::string(sop_instance_uid));
        if (result.is_err()) {
            return std::unexpected(pacs_error::not_found);
        }

        return from_pacs_mpps_record(result.value());
    }

private:
    std::shared_ptr<pacs::storage::index_database> db_;
};

// =============================================================================
// pacs_system MWL Adapter
// =============================================================================

/**
 * @brief MWL adapter backed by pacs_system index_database
 *
 * Provides worklist query operations through pacs_system's index database.
 * For the full MWL adapter with add/update/delete, see mwl_adapter.cpp.
 */
class pacs_system_mwl_adapter : public mwl_adapter {
public:
    explicit pacs_system_mwl_adapter(
        std::shared_ptr<pacs::storage::index_database> db)
        : db_(std::move(db)) {}

    std::expected<std::vector<mwl_item>, pacs_error>
    query_mwl(const mwl_query_params& params) override {
        if (!db_) {
            return std::unexpected(pacs_error::connection_failed);
        }

        auto wl_query = to_pacs_worklist_query(params);
        auto result = db_->query_worklist(wl_query);
        if (result.is_err()) {
            return std::unexpected(pacs_error::mwl_query_failed);
        }

        std::vector<mwl_item> items;
        items.reserve(result.value().size());
        for (const auto& wl : result.value()) {
            items.push_back(from_pacs_worklist_item(wl));
        }

        return items;
    }

    std::expected<mwl_item, pacs_error>
    get_mwl_item(std::string_view accession_number) override {
        if (accession_number.empty()) {
            return std::unexpected(pacs_error::validation_failed);
        }
        if (!db_) {
            return std::unexpected(pacs_error::connection_failed);
        }

        // Query by accession number with limit 1
        pacs::storage::worklist_query query;
        query.accession_no = std::string(accession_number);
        query.include_all_status = true;
        query.limit = 1;

        auto result = db_->query_worklist(query);
        if (result.is_err() || result.value().empty()) {
            return std::unexpected(pacs_error::not_found);
        }

        return from_pacs_worklist_item(result.value()[0]);
    }

private:
    std::shared_ptr<pacs::storage::index_database> db_;
};

// =============================================================================
// pacs_system Storage Adapter
// =============================================================================

/**
 * @brief Storage adapter backed by pacs_system index_database
 *
 * Provides DICOM instance metadata operations through pacs_system's
 * index database. Stores instance metadata (SOP UIDs, class information)
 * as database records.
 *
 * @note This adapter manages metadata only. Actual DICOM file storage
 * requires additional infrastructure (storage SCP/SCU services).
 */
class pacs_system_storage_adapter : public storage_adapter {
public:
    explicit pacs_system_storage_adapter(
        std::shared_ptr<pacs::storage::index_database> db)
        : db_(std::move(db)) {}

    std::expected<void, pacs_error>
    store(const dicom_dataset& dataset) override {
        if (dataset.sop_instance_uid.empty()) {
            return std::unexpected(pacs_error::invalid_dataset);
        }
        if (!db_) {
            return std::unexpected(pacs_error::connection_failed);
        }

        pacs::storage::instance_record instance;
        instance.sop_uid = dataset.sop_instance_uid;
        instance.sop_class_uid = dataset.sop_class_uid;

        // Use a synthetic file_path for metadata-only storage
        instance.file_path = "db://" + dataset.sop_instance_uid;

        auto result = db_->upsert_instance(instance);
        if (result.is_err()) {
            return std::unexpected(pacs_error::store_failed);
        }

        return {};
    }

    std::expected<dicom_dataset, pacs_error>
    retrieve(std::string_view sop_instance_uid) override {
        if (sop_instance_uid.empty()) {
            return std::unexpected(pacs_error::invalid_sop_uid);
        }
        if (!db_) {
            return std::unexpected(pacs_error::connection_failed);
        }

        auto result = db_->find_instance(std::string(sop_instance_uid));
        if (result.is_err()) {
            return std::unexpected(pacs_error::not_found);
        }

        dicom_dataset dataset;
        dataset.sop_instance_uid = result.value().sop_uid;
        dataset.sop_class_uid = result.value().sop_class_uid;

        return dataset;
    }

    bool exists(std::string_view sop_instance_uid) const override {
        if (sop_instance_uid.empty() || !db_) {
            return false;
        }

        auto result = db_->find_instance(std::string(sop_instance_uid));
        return result.is_ok();
    }

private:
    std::shared_ptr<pacs::storage::index_database> db_;
};

// =============================================================================
// pacs_system Combined Adapter
// =============================================================================

/**
 * @brief Combined PACS adapter backed by pacs_system index_database
 *
 * Provides unified access to MPPS, MWL, and storage services through
 * a shared index_database instance. Connection state reflects database
 * availability.
 */
class pacs_system_adapter : public pacs_adapter {
public:
    explicit pacs_system_adapter(
        std::shared_ptr<pacs::storage::index_database> db)
        : db_(std::move(db))
        , mpps_adapter_(std::make_shared<pacs_system_mpps_adapter>(db_))
        , mwl_adapter_(std::make_shared<pacs_system_mwl_adapter>(db_))
        , storage_adapter_(std::make_shared<pacs_system_storage_adapter>(db_))
        , connected_(false) {}

    std::shared_ptr<mpps_adapter> get_mpps_adapter() override {
        return mpps_adapter_;
    }

    std::shared_ptr<mwl_adapter> get_mwl_adapter() override {
        return mwl_adapter_;
    }

    std::shared_ptr<storage_adapter> get_storage_adapter() override {
        return storage_adapter_;
    }

    std::expected<void, pacs_error> connect() override {
        if (db_ && db_->is_open()) {
            connected_ = true;
            return {};
        }
        return std::unexpected(pacs_error::connection_failed);
    }

    void disconnect() override {
        connected_ = false;
    }

    bool is_connected() const override {
        return connected_;
    }

    bool is_healthy() const override {
        return connected_ && db_ && db_->is_open();
    }

private:
    std::shared_ptr<pacs::storage::index_database> db_;
    std::shared_ptr<pacs_system_mpps_adapter> mpps_adapter_;
    std::shared_ptr<pacs_system_mwl_adapter> mwl_adapter_;
    std::shared_ptr<pacs_system_storage_adapter> storage_adapter_;
    bool connected_;
};

#endif  // PACS_BRIDGE_HAS_PACS_SYSTEM

// =============================================================================
// Factory Functions
// =============================================================================

std::shared_ptr<pacs_adapter> create_pacs_adapter(const pacs_config& config) {
#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
    // When pacs_system is available and database_path is configured,
    // create a pacs_system-backed adapter for full DICOM integration.
    if (!config.database_path.empty()) {
        auto db_result =
            pacs::storage::index_database::open(config.database_path);
        if (db_result.is_ok()) {
            auto db = std::shared_ptr<pacs::storage::index_database>(
                db_result.value().release());
            return std::make_shared<pacs_system_adapter>(std::move(db));
        }
        // Database open failed - fall through to stub adapter
    }
#endif
    // Fallback: standalone stub adapter for testing and standalone mode
    return std::make_shared<stub_pacs_adapter>(config);
}

}  // namespace pacs::bridge::integration
