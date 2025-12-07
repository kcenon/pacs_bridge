#ifndef PACS_BRIDGE_CACHE_PATIENT_CACHE_H
#define PACS_BRIDGE_CACHE_PATIENT_CACHE_H

/**
 * @file patient_cache.h
 * @brief Thread-safe patient data cache with TTL and LRU eviction
 *
 * Provides an in-memory cache for patient demographic data to reduce
 * repeated lookups to source systems. Features include:
 *   - Time-based expiration (TTL)
 *   - LRU eviction when capacity is reached
 *   - Thread-safe operations
 *   - Cache statistics
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/18
 */

#include "pacs/bridge/mapping/hl7_dicom_mapper.h"

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::cache {

// =============================================================================
// Cache Error Codes (-920 to -929)
// =============================================================================

/**
 * @brief Cache specific error codes
 *
 * Allocated range: -920 to -929
 */
enum class cache_error : int {
    /** Entry not found in cache */
    not_found = -920,

    /** Entry has expired */
    expired = -921,

    /** Cache capacity reached and eviction failed */
    capacity_exceeded = -922,

    /** Invalid key format */
    invalid_key = -923,

    /** Serialization error */
    serialization_error = -924,

    /** Cache is disabled */
    cache_disabled = -925
};

/**
 * @brief Convert cache_error to error code
 */
[[nodiscard]] constexpr int to_error_code(cache_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(cache_error error) noexcept {
    switch (error) {
        case cache_error::not_found:
            return "Entry not found in cache";
        case cache_error::expired:
            return "Cache entry has expired";
        case cache_error::capacity_exceeded:
            return "Cache capacity exceeded";
        case cache_error::invalid_key:
            return "Invalid cache key format";
        case cache_error::serialization_error:
            return "Cache serialization error";
        case cache_error::cache_disabled:
            return "Cache is disabled";
        default:
            return "Unknown cache error";
    }
}

// =============================================================================
// Cache Configuration
// =============================================================================

/**
 * @brief Patient cache configuration
 */
struct patient_cache_config {
    /** Maximum number of entries */
    size_t max_entries = 10000;

    /** Default TTL for entries */
    std::chrono::seconds default_ttl{3600};  // 1 hour

    /** Enable cache */
    bool enabled = true;

    /** Automatically evict expired entries periodically */
    bool auto_evict = true;

    /** Interval for automatic eviction */
    std::chrono::seconds eviction_interval{60};

    /** Use LRU eviction when capacity is reached */
    bool lru_eviction = true;

    /** Enable cache statistics */
    bool enable_statistics = true;
};

// =============================================================================
// Cache Entry
// =============================================================================

/**
 * @brief Cache entry metadata
 */
struct cache_entry_metadata {
    /** When entry was created */
    std::chrono::system_clock::time_point created_at;

    /** When entry was last accessed */
    std::chrono::system_clock::time_point last_accessed;

    /** Entry TTL */
    std::chrono::seconds ttl;

    /** Number of times accessed */
    size_t access_count = 0;

    /**
     * @brief Check if entry has expired
     */
    [[nodiscard]] bool is_expired() const noexcept {
        auto now = std::chrono::system_clock::now();
        return (now - created_at) > ttl;
    }

    /**
     * @brief Get time remaining until expiration
     */
    [[nodiscard]] std::chrono::seconds time_remaining() const noexcept {
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - created_at);
        if (age >= ttl) return std::chrono::seconds{0};
        return ttl - age;
    }
};

// =============================================================================
// Patient Cache
// =============================================================================

/**
 * @brief Thread-safe patient data cache
 *
 * Caches patient demographic data for quick lookup without querying
 * source systems repeatedly. Supports multiple lookup keys per patient.
 *
 * @example Basic Usage
 * ```cpp
 * patient_cache cache;
 *
 * // Add patient
 * dicom_patient patient;
 * patient.patient_id = "12345";
 * patient.patient_name = "DOE^JOHN";
 * cache.put("12345", patient);
 *
 * // Retrieve patient
 * auto result = cache.get("12345");
 * if (result) {
 *     std::cout << "Found: " << result->patient_name << std::endl;
 * }
 * ```
 *
 * @example With Custom TTL
 * ```cpp
 * // Cache with 30-minute TTL
 * cache.put("12345", patient, std::chrono::minutes{30});
 * ```
 *
 * @example Multiple Keys
 * ```cpp
 * // Add secondary lookup key
 * cache.put("12345", patient);
 * cache.add_alias("SSN:123-45-6789", "12345");
 *
 * // Both keys work
 * auto p1 = cache.get("12345");
 * auto p2 = cache.get("SSN:123-45-6789");
 * ```
 */
class patient_cache {
public:
    /**
     * @brief Construct cache with default configuration
     */
    patient_cache();

    /**
     * @brief Construct cache with custom configuration
     */
    explicit patient_cache(const patient_cache_config& config);

    /**
     * @brief Destructor
     */
    ~patient_cache();

    // Non-copyable, movable
    patient_cache(const patient_cache&) = delete;
    patient_cache& operator=(const patient_cache&) = delete;
    patient_cache(patient_cache&&) noexcept;
    patient_cache& operator=(patient_cache&&) noexcept;

    // =========================================================================
    // Cache Operations
    // =========================================================================

    /**
     * @brief Add or update patient in cache
     *
     * @param key Primary lookup key (usually patient ID)
     * @param patient Patient data
     * @param ttl Custom TTL (optional, uses default if not specified)
     */
    void put(std::string_view key, const mapping::dicom_patient& patient,
             std::optional<std::chrono::seconds> ttl = std::nullopt);

    /**
     * @brief Get patient from cache
     *
     * @param key Lookup key
     * @return Patient data or error
     */
    [[nodiscard]] std::expected<mapping::dicom_patient, cache_error> get(
        std::string_view key) const;

    /**
     * @brief Get patient without updating access time
     *
     * @param key Lookup key
     * @return Patient data or error
     */
    [[nodiscard]] std::expected<mapping::dicom_patient, cache_error> peek(
        std::string_view key) const;

    /**
     * @brief Check if key exists in cache
     *
     * @param key Lookup key
     * @return true if exists and not expired
     */
    [[nodiscard]] bool contains(std::string_view key) const noexcept;

    /**
     * @brief Remove entry from cache
     *
     * @param key Lookup key
     * @return true if removed
     */
    bool remove(std::string_view key);

    /**
     * @brief Add alias key for existing entry
     *
     * @param alias New lookup key
     * @param primary_key Existing primary key
     * @return true if alias added
     */
    bool add_alias(std::string_view alias, std::string_view primary_key);

    /**
     * @brief Remove alias
     *
     * @param alias Alias to remove
     * @return true if removed
     */
    bool remove_alias(std::string_view alias);

    /**
     * @brief Get or load patient
     *
     * If patient is not in cache, calls loader function to fetch it
     * and adds it to cache.
     *
     * @param key Lookup key
     * @param loader Function to load patient if not cached
     * @return Patient data or error
     */
    [[nodiscard]] std::expected<mapping::dicom_patient, cache_error> get_or_load(
        std::string_view key,
        std::function<std::optional<mapping::dicom_patient>()> loader);

    // =========================================================================
    // Bulk Operations
    // =========================================================================

    /**
     * @brief Get multiple patients
     *
     * @param keys List of lookup keys
     * @return Map of key to patient (only found entries)
     */
    [[nodiscard]] std::unordered_map<std::string, mapping::dicom_patient> get_many(
        const std::vector<std::string>& keys) const;

    /**
     * @brief Add multiple patients
     *
     * @param entries Map of key to patient
     */
    void put_many(
        const std::unordered_map<std::string, mapping::dicom_patient>& entries);

    // =========================================================================
    // Cache Management
    // =========================================================================

    /**
     * @brief Clear all entries
     */
    void clear();

    /**
     * @brief Evict expired entries
     *
     * @return Number of entries evicted
     */
    size_t evict_expired();

    /**
     * @brief Get current entry count
     */
    [[nodiscard]] size_t size() const noexcept;

    /**
     * @brief Check if cache is empty
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Get cache configuration
     */
    [[nodiscard]] const patient_cache_config& config() const noexcept;

    /**
     * @brief Set cache enabled/disabled
     */
    void set_enabled(bool enabled);

    /**
     * @brief Get entry metadata
     */
    [[nodiscard]] std::optional<cache_entry_metadata> get_metadata(
        std::string_view key) const;

    /**
     * @brief Get all keys in cache
     */
    [[nodiscard]] std::vector<std::string> keys() const;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Cache statistics
     */
    struct statistics {
        /** Total get requests */
        size_t get_count = 0;

        /** Cache hits */
        size_t hit_count = 0;

        /** Cache misses */
        size_t miss_count = 0;

        /** Expired entry accesses */
        size_t expired_count = 0;

        /** Put operations */
        size_t put_count = 0;

        /** Remove operations */
        size_t remove_count = 0;

        /** Eviction operations */
        size_t eviction_count = 0;

        /** Current entry count */
        size_t current_size = 0;

        /** Maximum entries ever stored */
        size_t max_size_reached = 0;

        /**
         * @brief Calculate hit rate
         */
        [[nodiscard]] double hit_rate() const noexcept {
            if (get_count == 0) return 0.0;
            return static_cast<double>(hit_count) / static_cast<double>(get_count);
        }
    };

    /**
     * @brief Get cache statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Generic LRU Cache
// =============================================================================

/**
 * @brief Generic thread-safe LRU cache
 *
 * Template-based cache that can be used for any key-value pair.
 * Used internally by patient_cache but also available for other uses.
 *
 * @tparam K Key type
 * @tparam V Value type
 */
template <typename K, typename V>
class lru_cache {
public:
    /**
     * @brief Constructor
     *
     * @param capacity Maximum number of entries
     * @param default_ttl Default TTL for entries
     */
    explicit lru_cache(size_t capacity,
                       std::chrono::seconds default_ttl = std::chrono::seconds{3600});

    /**
     * @brief Destructor
     */
    ~lru_cache();

    /**
     * @brief Add or update entry
     */
    void put(const K& key, const V& value,
             std::optional<std::chrono::seconds> ttl = std::nullopt);

    /**
     * @brief Get entry
     */
    [[nodiscard]] std::optional<V> get(const K& key);

    /**
     * @brief Check if key exists
     */
    [[nodiscard]] bool contains(const K& key) const noexcept;

    /**
     * @brief Remove entry
     */
    bool remove(const K& key);

    /**
     * @brief Clear cache
     */
    void clear();

    /**
     * @brief Get size
     */
    [[nodiscard]] size_t size() const noexcept;

    /**
     * @brief Evict expired entries
     */
    size_t evict_expired();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::cache

#endif  // PACS_BRIDGE_CACHE_PATIENT_CACHE_H
