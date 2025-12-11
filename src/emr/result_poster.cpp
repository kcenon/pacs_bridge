/**
 * @file result_poster.cpp
 * @brief EMR Result Poster Implementation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/105
 */

#include "pacs/bridge/emr/result_poster.h"
#include "pacs/bridge/emr/diagnostic_report_builder.h"
#include "pacs/bridge/emr/result_tracker.h"

#include <atomic>
#include <mutex>

namespace pacs::bridge::emr {

// =============================================================================
// Helper Functions
// =============================================================================

std::optional<result_status> parse_result_status(
    std::string_view status_str) noexcept {
    if (status_str == "registered") return result_status::registered;
    if (status_str == "partial") return result_status::partial;
    if (status_str == "preliminary") return result_status::preliminary;
    if (status_str == "final") return result_status::final_report;
    if (status_str == "amended") return result_status::amended;
    if (status_str == "corrected") return result_status::corrected;
    if (status_str == "appended") return result_status::appended;
    if (status_str == "cancelled") return result_status::cancelled;
    if (status_str == "entered-in-error") return result_status::entered_in_error;
    if (status_str == "unknown") return result_status::unknown;
    return std::nullopt;
}

namespace {

/**
 * @brief Extract resource ID from Location header or resource JSON
 */
std::optional<std::string> extract_resource_id(
    const fhir_result<fhir_resource_wrapper>& result) {
    // First try to get from resource wrapper
    if (result.value.id) {
        return result.value.id;
    }

    // Try to extract from Location header
    if (result.location) {
        const auto& loc = *result.location;
        // Location format: [base]/DiagnosticReport/[id]/_history/[vid]
        auto report_pos = loc.find("DiagnosticReport/");
        if (report_pos != std::string::npos) {
            auto id_start = report_pos + 17;  // strlen("DiagnosticReport/")
            auto id_end = loc.find('/', id_start);
            if (id_end == std::string::npos) {
                id_end = loc.size();
            }
            return loc.substr(id_start, id_end - id_start);
        }
    }

    return std::nullopt;
}

}  // namespace

// =============================================================================
// emr_result_poster Implementation
// =============================================================================

class emr_result_poster::impl {
public:
    explicit impl(std::shared_ptr<fhir_client> client,
                  const result_poster_config& config)
        : client_(std::move(client))
        , config_(config)
        , tracker_(config.enable_tracking
                       ? std::make_shared<in_memory_result_tracker>()
                       : nullptr) {}

    auto post_result(const study_result& result)
        -> std::expected<posted_result, result_error> {
        // Validate input
        if (!result.is_valid()) {
            return std::unexpected(result_error::invalid_data);
        }

        // Check for duplicates if enabled
        if (config_.check_duplicates) {
            if (tracker_ && tracker_->exists(result.study_instance_uid)) {
                ++stats_.duplicate_skips;
                return std::unexpected(result_error::duplicate);
            }

            // Also check EMR for existing report
            auto existing = find_by_study_uid_impl(result.study_instance_uid);
            if (existing && *existing) {
                ++stats_.duplicate_skips;
                return std::unexpected(result_error::duplicate);
            }
        }

        // Build DiagnosticReport JSON
        auto json = diagnostic_report_builder::from_study_result(result)
                        .build_validated();
        if (!json) {
            return std::unexpected(result_error::build_failed);
        }

        auto start_time = std::chrono::steady_clock::now();

        // Post to EMR
        auto post_result = client_->create("DiagnosticReport", *json);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        std::lock_guard lock(stats_mutex_);
        stats_.total_post_time += elapsed;
        ++stats_.total_posts;

        if (!post_result) {
            ++stats_.failed_posts;
            return std::unexpected(result_error::post_failed);
        }

        ++stats_.successful_posts;

        // Extract resource ID
        auto resource_id = extract_resource_id(*post_result);
        if (!resource_id) {
            return std::unexpected(result_error::post_failed);
        }

        // Create posted result reference
        posted_result posted;
        posted.report_id = *resource_id;
        posted.study_instance_uid = result.study_instance_uid;
        posted.accession_number = result.accession_number;
        posted.status = result.status;
        posted.etag = post_result->etag;
        posted.posted_at = std::chrono::system_clock::now();

        // Track the result
        if (tracker_) {
            tracker_->track(posted);
        }

        return posted;
    }

    auto update_result(std::string_view report_id, const study_result& result)
        -> std::expected<void, result_error> {
        if (!result.is_valid()) {
            return std::unexpected(result_error::invalid_data);
        }

        // Build updated DiagnosticReport JSON
        auto json = diagnostic_report_builder::from_study_result(result)
                        .build_validated();
        if (!json) {
            return std::unexpected(result_error::build_failed);
        }

        // Update in EMR
        auto update_result =
            client_->update("DiagnosticReport", report_id, *json);

        if (!update_result) {
            return std::unexpected(result_error::update_failed);
        }

        // Update tracking
        if (tracker_) {
            posted_result updated;
            updated.report_id = std::string(report_id);
            updated.study_instance_uid = result.study_instance_uid;
            updated.accession_number = result.accession_number;
            updated.status = result.status;
            updated.etag = update_result->etag;
            updated.updated_at = std::chrono::system_clock::now();

            tracker_->update(result.study_instance_uid, updated);
        }

        std::lock_guard lock(stats_mutex_);
        ++stats_.updates;

        return {};
    }

    auto update_status(std::string_view report_id, result_status new_status)
        -> std::expected<void, result_error> {
        // Read current resource
        auto read_result = client_->read("DiagnosticReport", report_id);
        if (!read_result) {
            return std::unexpected(result_error::not_found);
        }

        // For now, we need to parse and rebuild - in a full implementation
        // we would use PATCH operation or modify the JSON directly
        // This is a simplified implementation

        return std::unexpected(result_error::update_failed);
    }

    auto find_by_accession(std::string_view accession_number)
        -> std::expected<std::optional<std::string>, result_error> {
        // Check local tracker first
        if (tracker_) {
            auto tracked = tracker_->get_by_accession(accession_number);
            if (tracked) {
                return tracked->report_id;
            }
        }

        // Search EMR
        auto params = search_params{};
        params.add("identifier", std::string(accession_number));

        auto search_result = client_->search("DiagnosticReport", params);
        if (!search_result) {
            return std::unexpected(result_error::post_failed);
        }

        const auto& bundle = search_result->value;
        if (bundle.entries.empty()) {
            return std::nullopt;
        }

        // Return first match
        if (bundle.entries[0].resource_id) {
            return *bundle.entries[0].resource_id;
        }

        return std::nullopt;
    }

    auto find_by_study_uid(std::string_view study_uid)
        -> std::expected<std::optional<std::string>, result_error> {
        return find_by_study_uid_impl(study_uid);
    }

    auto get_result(std::string_view report_id)
        -> std::expected<posted_result, result_error> {
        // Check local tracker first
        if (tracker_) {
            auto tracked = tracker_->get_by_report_id(report_id);
            if (tracked) {
                return *tracked;
            }
        }

        // Read from EMR
        auto read_result = client_->read("DiagnosticReport", report_id);
        if (!read_result) {
            return std::unexpected(result_error::not_found);
        }

        // Parse response - simplified, would need full parsing in production
        posted_result result;
        result.report_id = std::string(report_id);
        result.etag = read_result->etag;
        result.posted_at = std::chrono::system_clock::now();

        return result;
    }

    std::optional<posted_result> get_tracked_result(
        std::string_view study_uid) const {
        if (tracker_) {
            return tracker_->get_by_study_uid(study_uid);
        }
        return std::nullopt;
    }

    void clear_tracking() {
        if (tracker_) {
            tracker_->clear();
        }
    }

    const result_poster_config& config() const noexcept { return config_; }

    void set_config(const result_poster_config& config) { config_ = config; }

    void set_tracker(std::shared_ptr<result_tracker> tracker) {
        tracker_ = std::move(tracker);
    }

    statistics get_statistics() const noexcept {
        std::lock_guard lock(stats_mutex_);
        return stats_;
    }

    void reset_statistics() noexcept {
        std::lock_guard lock(stats_mutex_);
        stats_ = {};
    }

private:
    auto find_by_study_uid_impl(std::string_view study_uid)
        -> std::expected<std::optional<std::string>, result_error> {
        // Check local tracker first
        if (tracker_) {
            auto tracked = tracker_->get_by_study_uid(study_uid);
            if (tracked) {
                return tracked->report_id;
            }
        }

        // Search EMR by study UID identifier
        auto params = search_params{};
        params.add("identifier", "urn:dicom:uid|" + std::string(study_uid));

        auto search_result = client_->search("DiagnosticReport", params);
        if (!search_result) {
            return std::unexpected(result_error::post_failed);
        }

        const auto& bundle = search_result->value;
        if (bundle.entries.empty()) {
            return std::nullopt;
        }

        if (bundle.entries[0].resource_id) {
            return *bundle.entries[0].resource_id;
        }

        return std::nullopt;
    }

    std::shared_ptr<fhir_client> client_;
    result_poster_config config_;
    std::shared_ptr<result_tracker> tracker_;

    mutable std::mutex stats_mutex_;
    statistics stats_;
};

// =============================================================================
// emr_result_poster Public Interface
// =============================================================================

emr_result_poster::emr_result_poster(std::shared_ptr<fhir_client> client,
                                     const result_poster_config& config)
    : impl_(std::make_unique<impl>(std::move(client), config)) {}

emr_result_poster::~emr_result_poster() = default;

emr_result_poster::emr_result_poster(emr_result_poster&&) noexcept = default;
emr_result_poster& emr_result_poster::operator=(emr_result_poster&&) noexcept =
    default;

auto emr_result_poster::post_result(const study_result& result)
    -> std::expected<posted_result, result_error> {
    return impl_->post_result(result);
}

auto emr_result_poster::update_result(std::string_view report_id,
                                      const study_result& result)
    -> std::expected<void, result_error> {
    return impl_->update_result(report_id, result);
}

auto emr_result_poster::update_status(std::string_view report_id,
                                      result_status new_status)
    -> std::expected<void, result_error> {
    return impl_->update_status(report_id, new_status);
}

auto emr_result_poster::find_by_accession(std::string_view accession_number)
    -> std::expected<std::optional<std::string>, result_error> {
    return impl_->find_by_accession(accession_number);
}

auto emr_result_poster::find_by_study_uid(std::string_view study_uid)
    -> std::expected<std::optional<std::string>, result_error> {
    return impl_->find_by_study_uid(study_uid);
}

auto emr_result_poster::get_result(std::string_view report_id)
    -> std::expected<posted_result, result_error> {
    return impl_->get_result(report_id);
}

std::optional<posted_result> emr_result_poster::get_tracked_result(
    std::string_view study_uid) const {
    return impl_->get_tracked_result(study_uid);
}

void emr_result_poster::clear_tracking() { impl_->clear_tracking(); }

const result_poster_config& emr_result_poster::config() const noexcept {
    return impl_->config();
}

void emr_result_poster::set_config(const result_poster_config& config) {
    impl_->set_config(config);
}

void emr_result_poster::set_tracker(std::shared_ptr<result_tracker> tracker) {
    impl_->set_tracker(std::move(tracker));
}

emr_result_poster::statistics emr_result_poster::get_statistics() const noexcept {
    return impl_->get_statistics();
}

void emr_result_poster::reset_statistics() noexcept {
    impl_->reset_statistics();
}

}  // namespace pacs::bridge::emr
