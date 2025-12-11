#ifndef PACS_BRIDGE_EMR_RESULT_TRACKER_H
#define PACS_BRIDGE_EMR_RESULT_TRACKER_H

/**
 * @file result_tracker.h
 * @brief Result tracking for posted DiagnosticReports
 *
 * Provides local tracking of posted results to enable updates
 * and duplicate detection without querying the EMR.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/105
 */

#include "result_poster.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::emr {

// =============================================================================
// Result Tracker Configuration
// =============================================================================

/**
 * @brief Configuration for result tracker
 */
struct result_tracker_config {
    /** Maximum number of tracked results */
    size_t max_entries{10000};

    /** TTL for tracked results */
    std::chrono::hours ttl{24 * 7};  // 1 week

    /** Enable automatic cleanup of expired entries */
    bool auto_cleanup{true};

    /** Cleanup interval */
    std::chrono::minutes cleanup_interval{60};
};

// =============================================================================
// Result Tracker Interface
// =============================================================================

/**
 * @brief Abstract interface for result tracking
 *
 * Implementations can use different backends (memory, database, etc.)
 */
class result_tracker {
public:
    virtual ~result_tracker() = default;

    /**
     * @brief Track a posted result
     *
     * @param result Posted result to track
     * @return true on success
     */
    [[nodiscard]] virtual bool track(const posted_result& result) = 0;

    /**
     * @brief Update a tracked result
     *
     * @param study_uid Study Instance UID
     * @param result Updated result data
     * @return true on success
     */
    [[nodiscard]] virtual bool update(std::string_view study_uid,
                                       const posted_result& result) = 0;

    /**
     * @brief Get tracked result by Study Instance UID
     *
     * @param study_uid Study Instance UID
     * @return Tracked result or nullopt if not found
     */
    [[nodiscard]] virtual std::optional<posted_result> get_by_study_uid(
        std::string_view study_uid) const = 0;

    /**
     * @brief Get tracked result by accession number
     *
     * @param accession_number Accession number
     * @return Tracked result or nullopt if not found
     */
    [[nodiscard]] virtual std::optional<posted_result> get_by_accession(
        std::string_view accession_number) const = 0;

    /**
     * @brief Get tracked result by report ID
     *
     * @param report_id DiagnosticReport resource ID
     * @return Tracked result or nullopt if not found
     */
    [[nodiscard]] virtual std::optional<posted_result> get_by_report_id(
        std::string_view report_id) const = 0;

    /**
     * @brief Check if a study is already tracked
     *
     * @param study_uid Study Instance UID
     * @return true if tracked
     */
    [[nodiscard]] virtual bool exists(std::string_view study_uid) const = 0;

    /**
     * @brief Remove a tracked result
     *
     * @param study_uid Study Instance UID
     * @return true if removed
     */
    [[nodiscard]] virtual bool remove(std::string_view study_uid) = 0;

    /**
     * @brief Clear all tracked results
     */
    virtual void clear() = 0;

    /**
     * @brief Get number of tracked results
     */
    [[nodiscard]] virtual size_t size() const = 0;

    /**
     * @brief Get all tracked Study Instance UIDs
     */
    [[nodiscard]] virtual std::vector<std::string> keys() const = 0;

    /**
     * @brief Cleanup expired entries
     *
     * @return Number of entries removed
     */
    [[nodiscard]] virtual size_t cleanup_expired() = 0;
};

// =============================================================================
// In-Memory Result Tracker
// =============================================================================

/**
 * @brief In-memory implementation of result tracker
 *
 * Thread-safe implementation using concurrent hash maps.
 * Suitable for single-instance deployments.
 *
 * @example Basic Usage
 * ```cpp
 * result_tracker_config config;
 * config.max_entries = 50000;
 * config.ttl = std::chrono::hours{24 * 30};  // 30 days
 *
 * in_memory_result_tracker tracker(config);
 *
 * // Track a result
 * posted_result result;
 * result.report_id = "report-123";
 * result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
 * result.status = result_status::final_report;
 * result.posted_at = std::chrono::system_clock::now();
 *
 * tracker.track(result);
 *
 * // Check if exists
 * if (tracker.exists("1.2.3.4.5.6.7.8.9")) {
 *     auto tracked = tracker.get_by_study_uid("1.2.3.4.5.6.7.8.9");
 *     std::cout << "Report ID: " << tracked->report_id << "\n";
 * }
 * ```
 */
class in_memory_result_tracker : public result_tracker {
public:
    /**
     * @brief Construct with configuration
     */
    explicit in_memory_result_tracker(
        const result_tracker_config& config = {});

    /**
     * @brief Destructor
     */
    ~in_memory_result_tracker() override;

    // Non-copyable, movable
    in_memory_result_tracker(const in_memory_result_tracker&) = delete;
    in_memory_result_tracker& operator=(const in_memory_result_tracker&) = delete;
    in_memory_result_tracker(in_memory_result_tracker&&) noexcept;
    in_memory_result_tracker& operator=(in_memory_result_tracker&&) noexcept;

    // =========================================================================
    // result_tracker Interface
    // =========================================================================

    [[nodiscard]] bool track(const posted_result& result) override;

    [[nodiscard]] bool update(std::string_view study_uid,
                              const posted_result& result) override;

    [[nodiscard]] std::optional<posted_result> get_by_study_uid(
        std::string_view study_uid) const override;

    [[nodiscard]] std::optional<posted_result> get_by_accession(
        std::string_view accession_number) const override;

    [[nodiscard]] std::optional<posted_result> get_by_report_id(
        std::string_view report_id) const override;

    [[nodiscard]] bool exists(std::string_view study_uid) const override;

    [[nodiscard]] bool remove(std::string_view study_uid) override;

    void clear() override;

    [[nodiscard]] size_t size() const override;

    [[nodiscard]] std::vector<std::string> keys() const override;

    [[nodiscard]] size_t cleanup_expired() override;

    // =========================================================================
    // Additional Methods
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const result_tracker_config& config() const noexcept;

    /**
     * @brief Update configuration
     *
     * @param config New configuration
     */
    void set_config(const result_tracker_config& config);

    /**
     * @brief Get tracker statistics
     */
    struct statistics {
        size_t total_tracked{0};
        size_t current_size{0};
        size_t expired_cleaned{0};
        size_t evictions{0};
    };

    [[nodiscard]] statistics get_statistics() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_RESULT_TRACKER_H
