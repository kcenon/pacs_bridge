/**
 * @file generic_fhir_adapter.cpp
 * @brief Generic FHIR R4 EMR adapter implementation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/107
 */

#include "pacs/bridge/emr/adapters/generic_fhir_adapter.h"
#include "pacs/bridge/emr/encounter_context.h"
#include "pacs/bridge/emr/patient_lookup.h"
#include "pacs/bridge/emr/result_poster.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace pacs::bridge::emr {

// =============================================================================
// Implementation Class
// =============================================================================

class generic_fhir_adapter::impl {
public:
    explicit impl(const emr_adapter_config& config)
        : config_(config)
        , initialized_(false)
        , connected_(false) {}

    impl(std::shared_ptr<fhir_client> client, const emr_adapter_config& config)
        : config_(config)
        , fhir_client_(std::move(client))
        , initialized_(false)
        , connected_(false) {}

    ~impl() = default;

    // Non-copyable, non-movable (contains mutex and atomic)
    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;
    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    // Configuration
    emr_adapter_config config_;

    // Services
    std::shared_ptr<fhir_client> fhir_client_;
    std::unique_ptr<emr_patient_lookup> patient_lookup_;
    std::unique_ptr<emr_result_poster> result_poster_;
    std::unique_ptr<encounter_context_provider> encounter_provider_;

    // State
    std::atomic<bool> initialized_;
    std::atomic<bool> connected_;
    adapter_health_status health_status_;
    emr_adapter::statistics stats_;

    // Synchronization
    mutable std::mutex mutex_;
    mutable std::mutex stats_mutex_;
};

// =============================================================================
// Constructor / Destructor
// =============================================================================

generic_fhir_adapter::generic_fhir_adapter(const emr_adapter_config& config)
    : impl_(std::make_unique<impl>(config)) {}

generic_fhir_adapter::generic_fhir_adapter(
    std::shared_ptr<fhir_client> client,
    const emr_adapter_config& config)
    : impl_(std::make_unique<impl>(std::move(client), config)) {}

generic_fhir_adapter::~generic_fhir_adapter() {
    shutdown();
}

generic_fhir_adapter::generic_fhir_adapter(generic_fhir_adapter&&) noexcept = default;
generic_fhir_adapter& generic_fhir_adapter::operator=(generic_fhir_adapter&&) noexcept = default;

// =============================================================================
// Identification
// =============================================================================

emr_vendor generic_fhir_adapter::vendor() const noexcept {
    return emr_vendor::generic_fhir;
}

std::string_view generic_fhir_adapter::vendor_name() const noexcept {
    return "Generic FHIR R4";
}

std::string_view generic_fhir_adapter::version() const noexcept {
    return "1.0.0";
}

adapter_features generic_fhir_adapter::features() const noexcept {
    adapter_features f;
    f.patient_lookup = true;
    f.patient_search = true;
    f.result_posting = true;
    f.result_updates = true;
    f.encounter_context = true;
    f.imaging_study = true;
    f.service_request = true;
    f.bulk_export = false;  // Not yet implemented
    f.smart_on_fhir = true;
    f.oauth2_client_credentials = true;
    f.basic_auth = true;
    return f;
}

// =============================================================================
// Connection Management
// =============================================================================

std::expected<void, adapter_error> generic_fhir_adapter::initialize() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    if (impl_->initialized_) {
        return {};  // Already initialized
    }

    // Validate configuration
    if (!impl_->config_.is_valid()) {
        return std::unexpected(adapter_error::invalid_configuration);
    }

    // Create FHIR client if not provided
    if (!impl_->fhir_client_) {
        auto result = create_fhir_client();
        if (!result) {
            return std::unexpected(result.error());
        }
        impl_->fhir_client_ = *result;
    }

    // Create patient lookup service
    patient_lookup_config lookup_config;
    lookup_config.enable_cache = true;
    if (impl_->config_.mrn_system.has_value()) {
        lookup_config.default_identifier_system = *impl_->config_.mrn_system;
    }
    impl_->patient_lookup_ = std::make_unique<emr_patient_lookup>(
        impl_->fhir_client_, lookup_config);

    // Create result poster service
    result_poster_config poster_config;
    poster_config.check_duplicates = true;
    poster_config.enable_tracking = true;
    if (impl_->config_.organization_id.has_value()) {
        poster_config.issuing_organization = *impl_->config_.organization_id;
    }
    impl_->result_poster_ = std::make_unique<emr_result_poster>(
        impl_->fhir_client_, poster_config);

    // Create encounter provider
    encounter_context_config encounter_config;
    encounter_config.client = impl_->fhir_client_;
    encounter_config.include_location = true;
    encounter_config.include_participants = true;
    impl_->encounter_provider_ = std::make_unique<encounter_context_provider>(
        encounter_config);

    impl_->initialized_ = true;
    impl_->connected_ = true;

    return {};
}

void generic_fhir_adapter::shutdown() noexcept {
    if (!impl_) {
        return;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex_);

    impl_->encounter_provider_.reset();
    impl_->result_poster_.reset();
    impl_->patient_lookup_.reset();
    impl_->fhir_client_.reset();

    impl_->initialized_ = false;
    impl_->connected_ = false;
}

bool generic_fhir_adapter::is_initialized() const noexcept {
    return impl_ && impl_->initialized_;
}

bool generic_fhir_adapter::is_connected() const noexcept {
    return impl_ && impl_->connected_;
}

// =============================================================================
// Health Check
// =============================================================================

std::expected<adapter_health_status, adapter_error>
generic_fhir_adapter::health_check() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    if (!impl_->initialized_) {
        return std::unexpected(adapter_error::not_initialized);
    }

    auto start = std::chrono::steady_clock::now();
    adapter_health_status status;

    // Try to get capabilities from FHIR server
    auto cap_result = impl_->fhir_client_->capabilities();
    auto end = std::chrono::steady_clock::now();

    status.response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start);
    status.last_check = std::chrono::system_clock::now();

    if (cap_result) {
        status.healthy = true;
        status.connected = true;
        status.authenticated = true;
        impl_->connected_ = true;
    } else {
        status.healthy = false;
        status.connected = false;
        status.error_message = to_string(cap_result.error());
        impl_->connected_ = false;
    }

    impl_->health_status_ = status;
    return status;
}

adapter_health_status generic_fhir_adapter::get_health_status() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->health_status_;
}

// =============================================================================
// Patient Operations
// =============================================================================

std::expected<patient_record, patient_error>
generic_fhir_adapter::query_patient(const patient_query& query) {
    auto start = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        if (!impl_->initialized_) {
            return std::unexpected(patient_error::query_failed);
        }
    }

    auto result = impl_->patient_lookup_->find_patient(query);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
        impl_->stats_.total_requests++;
        impl_->stats_.patient_queries++;
        impl_->stats_.total_request_time += duration;
        if (result) {
            impl_->stats_.successful_requests++;
        } else {
            impl_->stats_.failed_requests++;
        }
    }

    return result;
}

std::expected<std::vector<patient_match>, patient_error>
generic_fhir_adapter::search_patients(const patient_query& query) {
    auto start = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        if (!impl_->initialized_) {
            return std::unexpected(patient_error::query_failed);
        }
    }

    auto result = impl_->patient_lookup_->search_patients(query);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
        impl_->stats_.total_requests++;
        impl_->stats_.patient_queries++;
        impl_->stats_.total_request_time += duration;
        if (result) {
            impl_->stats_.successful_requests++;
        } else {
            impl_->stats_.failed_requests++;
        }
    }

    return result;
}

// =============================================================================
// Result Operations
// =============================================================================

std::expected<posted_result, result_error>
generic_fhir_adapter::post_result(const study_result& result) {
    auto start = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        if (!impl_->initialized_) {
            return std::unexpected(result_error::post_failed);
        }
    }

    auto post_result = impl_->result_poster_->post_result(result);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
        impl_->stats_.total_requests++;
        impl_->stats_.result_posts++;
        impl_->stats_.total_request_time += duration;
        if (post_result) {
            impl_->stats_.successful_requests++;
        } else {
            impl_->stats_.failed_requests++;
        }
    }

    return post_result;
}

std::expected<void, result_error>
generic_fhir_adapter::update_result(std::string_view report_id,
                                    const study_result& result) {
    auto start = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        if (!impl_->initialized_) {
            return std::unexpected(result_error::update_failed);
        }
    }

    auto update_result = impl_->result_poster_->update_result(report_id, result);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
        impl_->stats_.total_requests++;
        impl_->stats_.total_request_time += duration;
        if (update_result) {
            impl_->stats_.successful_requests++;
        } else {
            impl_->stats_.failed_requests++;
        }
    }

    return update_result;
}

// =============================================================================
// Encounter Operations
// =============================================================================

std::expected<encounter_info, encounter_error>
generic_fhir_adapter::get_encounter(std::string_view encounter_id) {
    auto start = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        if (!impl_->initialized_) {
            return std::unexpected(encounter_error::query_failed);
        }
    }

    auto result = impl_->encounter_provider_->get_encounter(encounter_id);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
        impl_->stats_.total_requests++;
        impl_->stats_.encounter_queries++;
        impl_->stats_.total_request_time += duration;
    }

    // Convert variant to expected
    if (auto* enc = std::get_if<encounter_info>(&result)) {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
        impl_->stats_.successful_requests++;
        return *enc;
    } else {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
        impl_->stats_.failed_requests++;
        return std::unexpected(std::get<encounter_error>(result));
    }
}

std::expected<std::optional<encounter_info>, encounter_error>
generic_fhir_adapter::find_active_encounter(std::string_view patient_id) {
    auto start = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        if (!impl_->initialized_) {
            return std::unexpected(encounter_error::query_failed);
        }
    }

    auto result = impl_->encounter_provider_->find_active_encounter(patient_id);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
        impl_->stats_.total_requests++;
        impl_->stats_.encounter_queries++;
        impl_->stats_.total_request_time += duration;
    }

    // Convert variant to expected
    if (auto* opt_enc = std::get_if<std::optional<encounter_info>>(&result)) {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
        impl_->stats_.successful_requests++;
        return *opt_enc;
    } else {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
        impl_->stats_.failed_requests++;
        return std::unexpected(std::get<encounter_error>(result));
    }
}

// =============================================================================
// Configuration
// =============================================================================

const emr_adapter_config& generic_fhir_adapter::config() const noexcept {
    return impl_->config_;
}

std::expected<void, adapter_error>
generic_fhir_adapter::set_config(const emr_adapter_config& config) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    if (!config.is_valid()) {
        return std::unexpected(adapter_error::invalid_configuration);
    }

    // If already initialized, need to reinitialize
    bool was_initialized = impl_->initialized_;
    if (was_initialized) {
        impl_->initialized_ = false;
        impl_->connected_ = false;
        impl_->fhir_client_.reset();
        impl_->patient_lookup_.reset();
        impl_->result_poster_.reset();
        impl_->encounter_provider_.reset();
    }

    impl_->config_ = config;

    // Reinitialize if was running
    if (was_initialized) {
        // Unlock before calling initialize to avoid deadlock
        // But we need to ensure atomicity - use a flag
        impl_->mutex_.unlock();
        auto result = initialize();
        impl_->mutex_.lock();
        if (!result) {
            return std::unexpected(result.error());
        }
    }

    return {};
}

// =============================================================================
// Statistics
// =============================================================================

emr_adapter::statistics generic_fhir_adapter::get_statistics() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->stats_mutex_);

    auto stats = impl_->stats_;
    if (stats.total_requests > 0) {
        stats.avg_response_time = std::chrono::milliseconds(
            stats.total_request_time.count() / stats.total_requests);
    }
    return stats;
}

void generic_fhir_adapter::reset_statistics() noexcept {
    std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
    impl_->stats_ = {};
}

// =============================================================================
// Generic FHIR Specific Methods
// =============================================================================

std::shared_ptr<fhir_client>
generic_fhir_adapter::get_fhir_client() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->fhir_client_;
}

std::expected<std::shared_ptr<fhir_client>, adapter_error>
generic_fhir_adapter::create_fhir_client() {
    fhir_client_config fhir_config;
    fhir_config.base_url = impl_->config_.base_url;
    fhir_config.timeout = impl_->config_.timeout;
    fhir_config.retry = impl_->config_.retry;

    try {
        return std::make_shared<fhir_client>(fhir_config);
    } catch (const std::exception&) {
        return std::unexpected(adapter_error::connection_failed);
    }
}

void generic_fhir_adapter::record_request(bool success,
                                          std::chrono::milliseconds duration) {
    std::lock_guard<std::mutex> lock(impl_->stats_mutex_);
    impl_->stats_.total_requests++;
    impl_->stats_.total_request_time += duration;
    if (success) {
        impl_->stats_.successful_requests++;
    } else {
        impl_->stats_.failed_requests++;
    }
}

}  // namespace pacs::bridge::emr
