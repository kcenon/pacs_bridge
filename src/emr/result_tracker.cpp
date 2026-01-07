/**
 * @file result_tracker.cpp
 * @brief Result Tracker Implementation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/105
 */

#include "pacs/bridge/emr/result_tracker.h"

#include <algorithm>
#include <shared_mutex>

namespace pacs::bridge::emr {

// =============================================================================
// in_memory_result_tracker Implementation
// =============================================================================

class in_memory_result_tracker::impl {
public:
    explicit impl(const result_tracker_config& config) : config_(config) {}

    [[nodiscard]] VoidResult track(const posted_result& result) {
        std::unique_lock lock(mutex_);

        // Check capacity
        if (results_by_uid_.size() >= config_.max_entries) {
            // Evict oldest entry
            evict_oldest();
            ++stats_.evictions;
        }

        // Store by Study Instance UID (primary key)
        results_by_uid_[result.study_instance_uid] = result;

        // Index by accession number
        if (result.accession_number) {
            accession_to_uid_[*result.accession_number] =
                result.study_instance_uid;
        }

        // Index by report ID
        report_id_to_uid_[result.report_id] = result.study_instance_uid;

        ++stats_.total_tracked;

        return VoidResult{std::monostate{}};
    }

    [[nodiscard]] VoidResult update(std::string_view study_uid,
                                    const posted_result& result) {
        std::unique_lock lock(mutex_);

        auto it = results_by_uid_.find(std::string(study_uid));
        if (it == results_by_uid_.end()) {
            return VoidResult{to_error_info(
                tracker_error::not_found,
                std::string("Study UID: ") + std::string(study_uid))};
        }

        // Update indices if accession number changed
        if (it->second.accession_number != result.accession_number) {
            if (it->second.accession_number) {
                accession_to_uid_.erase(*it->second.accession_number);
            }
            if (result.accession_number) {
                accession_to_uid_[*result.accession_number] =
                    std::string(study_uid);
            }
        }

        // Update report ID index if changed
        if (it->second.report_id != result.report_id) {
            report_id_to_uid_.erase(it->second.report_id);
            report_id_to_uid_[result.report_id] = std::string(study_uid);
        }

        it->second = result;
        return VoidResult{std::monostate{}};
    }

    [[nodiscard]] std::optional<posted_result> get_by_study_uid(
        std::string_view study_uid) const {
        std::shared_lock lock(mutex_);

        auto it = results_by_uid_.find(std::string(study_uid));
        if (it != results_by_uid_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<posted_result> get_by_accession(
        std::string_view accession_number) const {
        std::shared_lock lock(mutex_);

        auto uid_it = accession_to_uid_.find(std::string(accession_number));
        if (uid_it == accession_to_uid_.end()) {
            return std::nullopt;
        }

        auto it = results_by_uid_.find(uid_it->second);
        if (it != results_by_uid_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<posted_result> get_by_report_id(
        std::string_view report_id) const {
        std::shared_lock lock(mutex_);

        auto uid_it = report_id_to_uid_.find(std::string(report_id));
        if (uid_it == report_id_to_uid_.end()) {
            return std::nullopt;
        }

        auto it = results_by_uid_.find(uid_it->second);
        if (it != results_by_uid_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool exists(std::string_view study_uid) const {
        std::shared_lock lock(mutex_);
        return results_by_uid_.contains(std::string(study_uid));
    }

    [[nodiscard]] VoidResult remove(std::string_view study_uid) {
        std::unique_lock lock(mutex_);

        auto it = results_by_uid_.find(std::string(study_uid));
        if (it == results_by_uid_.end()) {
            return VoidResult{to_error_info(
                tracker_error::not_found,
                std::string("Study UID: ") + std::string(study_uid))};
        }

        // Remove from indices
        if (it->second.accession_number) {
            accession_to_uid_.erase(*it->second.accession_number);
        }
        report_id_to_uid_.erase(it->second.report_id);

        results_by_uid_.erase(it);
        return VoidResult{std::monostate{}};
    }

    void clear() {
        std::unique_lock lock(mutex_);
        results_by_uid_.clear();
        accession_to_uid_.clear();
        report_id_to_uid_.clear();
    }

    [[nodiscard]] size_t size() const {
        std::shared_lock lock(mutex_);
        return results_by_uid_.size();
    }

    [[nodiscard]] std::vector<std::string> keys() const {
        std::shared_lock lock(mutex_);

        std::vector<std::string> result;
        result.reserve(results_by_uid_.size());

        for (const auto& [uid, _] : results_by_uid_) {
            result.push_back(uid);
        }

        return result;
    }

    [[nodiscard]] size_t cleanup_expired() {
        std::unique_lock lock(mutex_);

        auto now = std::chrono::system_clock::now();
        size_t removed = 0;

        std::vector<std::string> to_remove;

        for (const auto& [uid, result] : results_by_uid_) {
            auto age = now - result.posted_at;
            if (age > config_.ttl) {
                to_remove.push_back(uid);
            }
        }

        for (const auto& uid : to_remove) {
            auto it = results_by_uid_.find(uid);
            if (it != results_by_uid_.end()) {
                if (it->second.accession_number) {
                    accession_to_uid_.erase(*it->second.accession_number);
                }
                report_id_to_uid_.erase(it->second.report_id);
                results_by_uid_.erase(it);
                ++removed;
            }
        }

        stats_.expired_cleaned += removed;
        return removed;
    }

    [[nodiscard]] const result_tracker_config& config() const noexcept {
        return config_;
    }

    void set_config(const result_tracker_config& config) {
        std::unique_lock lock(mutex_);
        config_ = config;
    }

    [[nodiscard]] in_memory_result_tracker::statistics get_statistics()
        const noexcept {
        std::shared_lock lock(mutex_);
        statistics s = stats_;
        s.current_size = results_by_uid_.size();
        return s;
    }

private:
    void evict_oldest() {
        // Find oldest entry
        auto oldest = results_by_uid_.begin();
        for (auto it = results_by_uid_.begin(); it != results_by_uid_.end();
             ++it) {
            if (it->second.posted_at < oldest->second.posted_at) {
                oldest = it;
            }
        }

        if (oldest != results_by_uid_.end()) {
            if (oldest->second.accession_number) {
                accession_to_uid_.erase(*oldest->second.accession_number);
            }
            report_id_to_uid_.erase(oldest->second.report_id);
            results_by_uid_.erase(oldest);
        }
    }

    result_tracker_config config_;
    mutable std::shared_mutex mutex_;

    // Primary storage: Study Instance UID -> posted_result
    std::unordered_map<std::string, posted_result> results_by_uid_;

    // Secondary indices
    std::unordered_map<std::string, std::string> accession_to_uid_;
    std::unordered_map<std::string, std::string> report_id_to_uid_;

    mutable statistics stats_;
};

// =============================================================================
// in_memory_result_tracker Public Interface
// =============================================================================

in_memory_result_tracker::in_memory_result_tracker(
    const result_tracker_config& config)
    : impl_(std::make_unique<impl>(config)) {}

in_memory_result_tracker::~in_memory_result_tracker() = default;

in_memory_result_tracker::in_memory_result_tracker(
    in_memory_result_tracker&&) noexcept = default;
in_memory_result_tracker& in_memory_result_tracker::operator=(
    in_memory_result_tracker&&) noexcept = default;

VoidResult in_memory_result_tracker::track(const posted_result& result) {
    return impl_->track(result);
}

VoidResult in_memory_result_tracker::update(std::string_view study_uid,
                                            const posted_result& result) {
    return impl_->update(study_uid, result);
}

std::optional<posted_result> in_memory_result_tracker::get_by_study_uid(
    std::string_view study_uid) const {
    return impl_->get_by_study_uid(study_uid);
}

std::optional<posted_result> in_memory_result_tracker::get_by_accession(
    std::string_view accession_number) const {
    return impl_->get_by_accession(accession_number);
}

std::optional<posted_result> in_memory_result_tracker::get_by_report_id(
    std::string_view report_id) const {
    return impl_->get_by_report_id(report_id);
}

bool in_memory_result_tracker::exists(std::string_view study_uid) const {
    return impl_->exists(study_uid);
}

VoidResult in_memory_result_tracker::remove(std::string_view study_uid) {
    return impl_->remove(study_uid);
}

void in_memory_result_tracker::clear() {
    impl_->clear();
}

size_t in_memory_result_tracker::size() const {
    return impl_->size();
}

std::vector<std::string> in_memory_result_tracker::keys() const {
    return impl_->keys();
}

size_t in_memory_result_tracker::cleanup_expired() {
    return impl_->cleanup_expired();
}

const result_tracker_config& in_memory_result_tracker::config() const noexcept {
    return impl_->config();
}

void in_memory_result_tracker::set_config(const result_tracker_config& config) {
    impl_->set_config(config);
}

in_memory_result_tracker::statistics in_memory_result_tracker::get_statistics()
    const noexcept {
    return impl_->get_statistics();
}

}  // namespace pacs::bridge::emr
