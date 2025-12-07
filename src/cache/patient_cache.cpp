/**
 * @file patient_cache.cpp
 * @brief Thread-safe patient data cache implementation
 */

#include "pacs/bridge/cache/patient_cache.h"

#include <algorithm>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace pacs::bridge::cache {

// =============================================================================
// lru_cache Implementation
// =============================================================================

template <typename K, typename V>
class lru_cache<K, V>::impl {
public:
    struct entry {
        K key;
        V value;
        std::chrono::system_clock::time_point created_at;
        std::chrono::seconds ttl;

        [[nodiscard]] bool is_expired() const noexcept {
            auto now = std::chrono::system_clock::now();
            return (now - created_at) > ttl;
        }
    };

    using list_type = std::list<entry>;
    using list_iterator = typename list_type::iterator;

    size_t capacity_;
    std::chrono::seconds default_ttl_;
    list_type entries_;
    std::unordered_map<K, list_iterator> index_;
    mutable std::shared_mutex mutex_;

    impl(size_t capacity, std::chrono::seconds default_ttl)
        : capacity_(capacity), default_ttl_(default_ttl) {}

    void put(const K& key, const V& value, std::optional<std::chrono::seconds> ttl) {
        std::unique_lock lock(mutex_);

        auto it = index_.find(key);
        if (it != index_.end()) {
            // Update existing entry
            entries_.erase(it->second);
            index_.erase(it);
        }

        // Evict if at capacity
        while (entries_.size() >= capacity_ && !entries_.empty()) {
            auto& oldest = entries_.back();
            index_.erase(oldest.key);
            entries_.pop_back();
        }

        // Add new entry at front (most recently used)
        entry e;
        e.key = key;
        e.value = value;
        e.created_at = std::chrono::system_clock::now();
        e.ttl = ttl.value_or(default_ttl_);

        entries_.push_front(std::move(e));
        index_[key] = entries_.begin();
    }

    std::optional<V> get(const K& key) {
        std::unique_lock lock(mutex_);

        auto it = index_.find(key);
        if (it == index_.end()) {
            return std::nullopt;
        }

        // Check expiration
        if (it->second->is_expired()) {
            entries_.erase(it->second);
            index_.erase(it);
            return std::nullopt;
        }

        // Move to front (most recently used)
        if (it->second != entries_.begin()) {
            entries_.splice(entries_.begin(), entries_, it->second);
        }

        return it->second->value;
    }

    bool contains(const K& key) const noexcept {
        std::shared_lock lock(mutex_);

        auto it = index_.find(key);
        if (it == index_.end()) {
            return false;
        }

        return !it->second->is_expired();
    }

    bool remove(const K& key) {
        std::unique_lock lock(mutex_);

        auto it = index_.find(key);
        if (it == index_.end()) {
            return false;
        }

        entries_.erase(it->second);
        index_.erase(it);
        return true;
    }

    void clear() {
        std::unique_lock lock(mutex_);
        entries_.clear();
        index_.clear();
    }

    size_t size() const noexcept {
        std::shared_lock lock(mutex_);
        return entries_.size();
    }

    size_t evict_expired() {
        std::unique_lock lock(mutex_);

        size_t count = 0;
        auto it = entries_.begin();
        while (it != entries_.end()) {
            if (it->is_expired()) {
                index_.erase(it->key);
                it = entries_.erase(it);
                ++count;
            } else {
                ++it;
            }
        }
        return count;
    }
};

template <typename K, typename V>
lru_cache<K, V>::lru_cache(size_t capacity, std::chrono::seconds default_ttl)
    : pimpl_(std::make_unique<impl>(capacity, default_ttl)) {}

template <typename K, typename V>
lru_cache<K, V>::~lru_cache() = default;

template <typename K, typename V>
void lru_cache<K, V>::put(const K& key, const V& value,
                           std::optional<std::chrono::seconds> ttl) {
    pimpl_->put(key, value, ttl);
}

template <typename K, typename V>
std::optional<V> lru_cache<K, V>::get(const K& key) {
    return pimpl_->get(key);
}

template <typename K, typename V>
bool lru_cache<K, V>::contains(const K& key) const noexcept {
    return pimpl_->contains(key);
}

template <typename K, typename V>
bool lru_cache<K, V>::remove(const K& key) {
    return pimpl_->remove(key);
}

template <typename K, typename V>
void lru_cache<K, V>::clear() {
    pimpl_->clear();
}

template <typename K, typename V>
size_t lru_cache<K, V>::size() const noexcept {
    return pimpl_->size();
}

template <typename K, typename V>
size_t lru_cache<K, V>::evict_expired() {
    return pimpl_->evict_expired();
}

// Explicit template instantiations
template class lru_cache<std::string, mapping::dicom_patient>;

// =============================================================================
// patient_cache Implementation
// =============================================================================

class patient_cache::impl {
public:
    struct cache_entry {
        mapping::dicom_patient patient;
        cache_entry_metadata metadata;
    };

    using list_type = std::list<std::string>;  // Keys in LRU order
    using list_iterator = list_type::iterator;

    patient_cache_config config_;
    std::unordered_map<std::string, cache_entry> entries_;
    std::unordered_map<std::string, std::string> aliases_;  // alias -> primary key
    list_type lru_order_;
    std::unordered_map<std::string, list_iterator> lru_index_;
    mutable std::shared_mutex mutex_;
    mutable statistics stats_;

    explicit impl(const patient_cache_config& config) : config_(config) {}

    void update_lru(const std::string& key) {
        auto it = lru_index_.find(key);
        if (it != lru_index_.end()) {
            lru_order_.erase(it->second);
        }
        lru_order_.push_front(key);
        lru_index_[key] = lru_order_.begin();
    }

    void remove_from_lru(const std::string& key) {
        auto it = lru_index_.find(key);
        if (it != lru_index_.end()) {
            lru_order_.erase(it->second);
            lru_index_.erase(it);
        }
    }

    std::string resolve_key(std::string_view key) const {
        std::string key_str(key);
        auto alias_it = aliases_.find(key_str);
        if (alias_it != aliases_.end()) {
            return alias_it->second;
        }
        return key_str;
    }

    void evict_lru_if_needed() {
        while (entries_.size() >= config_.max_entries && !lru_order_.empty()) {
            const std::string& oldest_key = lru_order_.back();
            entries_.erase(oldest_key);
            lru_index_.erase(oldest_key);
            lru_order_.pop_back();
            ++stats_.eviction_count;
        }
    }
};

patient_cache::patient_cache() : patient_cache(patient_cache_config{}) {}

patient_cache::patient_cache(const patient_cache_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

patient_cache::~patient_cache() = default;

patient_cache::patient_cache(patient_cache&&) noexcept = default;
patient_cache& patient_cache::operator=(patient_cache&&) noexcept = default;

void patient_cache::put(std::string_view key, const mapping::dicom_patient& patient,
                         std::optional<std::chrono::seconds> ttl) {
    if (!pimpl_->config_.enabled) {
        return;
    }

    std::unique_lock lock(pimpl_->mutex_);

    std::string key_str(key);

    // Evict if needed
    if (pimpl_->config_.lru_eviction) {
        pimpl_->evict_lru_if_needed();
    }

    auto now = std::chrono::system_clock::now();

    impl::cache_entry entry;
    entry.patient = patient;
    entry.metadata.created_at = now;
    entry.metadata.last_accessed = now;
    entry.metadata.ttl = ttl.value_or(pimpl_->config_.default_ttl);
    entry.metadata.access_count = 0;

    pimpl_->entries_[key_str] = std::move(entry);
    pimpl_->update_lru(key_str);

    ++pimpl_->stats_.put_count;
    pimpl_->stats_.current_size = pimpl_->entries_.size();
    if (pimpl_->stats_.current_size > pimpl_->stats_.max_size_reached) {
        pimpl_->stats_.max_size_reached = pimpl_->stats_.current_size;
    }
}

std::expected<mapping::dicom_patient, cache_error> patient_cache::get(
    std::string_view key) const {
    if (!pimpl_->config_.enabled) {
        return std::unexpected(cache_error::cache_disabled);
    }

    std::unique_lock lock(pimpl_->mutex_);

    ++pimpl_->stats_.get_count;

    std::string resolved_key = pimpl_->resolve_key(key);
    auto it = pimpl_->entries_.find(resolved_key);

    if (it == pimpl_->entries_.end()) {
        ++pimpl_->stats_.miss_count;
        return std::unexpected(cache_error::not_found);
    }

    if (it->second.metadata.is_expired()) {
        ++pimpl_->stats_.expired_count;
        ++pimpl_->stats_.miss_count;
        return std::unexpected(cache_error::expired);
    }

    // Update access metadata
    it->second.metadata.last_accessed = std::chrono::system_clock::now();
    ++it->second.metadata.access_count;
    pimpl_->update_lru(resolved_key);

    ++pimpl_->stats_.hit_count;
    return it->second.patient;
}

std::expected<mapping::dicom_patient, cache_error> patient_cache::peek(
    std::string_view key) const {
    if (!pimpl_->config_.enabled) {
        return std::unexpected(cache_error::cache_disabled);
    }

    std::shared_lock lock(pimpl_->mutex_);

    std::string resolved_key = pimpl_->resolve_key(key);
    auto it = pimpl_->entries_.find(resolved_key);

    if (it == pimpl_->entries_.end()) {
        return std::unexpected(cache_error::not_found);
    }

    if (it->second.metadata.is_expired()) {
        return std::unexpected(cache_error::expired);
    }

    return it->second.patient;
}

bool patient_cache::contains(std::string_view key) const noexcept {
    if (!pimpl_->config_.enabled) {
        return false;
    }

    std::shared_lock lock(pimpl_->mutex_);

    std::string resolved_key = pimpl_->resolve_key(key);
    auto it = pimpl_->entries_.find(resolved_key);

    if (it == pimpl_->entries_.end()) {
        return false;
    }

    return !it->second.metadata.is_expired();
}

bool patient_cache::remove(std::string_view key) {
    std::unique_lock lock(pimpl_->mutex_);

    std::string key_str(key);

    // Check if it's an alias
    auto alias_it = pimpl_->aliases_.find(key_str);
    if (alias_it != pimpl_->aliases_.end()) {
        key_str = alias_it->second;
    }

    auto it = pimpl_->entries_.find(key_str);
    if (it == pimpl_->entries_.end()) {
        return false;
    }

    pimpl_->entries_.erase(it);
    pimpl_->remove_from_lru(key_str);

    // Remove all aliases pointing to this key
    for (auto alias_it = pimpl_->aliases_.begin(); alias_it != pimpl_->aliases_.end();) {
        if (alias_it->second == key_str) {
            alias_it = pimpl_->aliases_.erase(alias_it);
        } else {
            ++alias_it;
        }
    }

    ++pimpl_->stats_.remove_count;
    pimpl_->stats_.current_size = pimpl_->entries_.size();

    return true;
}

bool patient_cache::add_alias(std::string_view alias, std::string_view primary_key) {
    std::unique_lock lock(pimpl_->mutex_);

    std::string primary_str(primary_key);

    // Check if primary key exists
    if (pimpl_->entries_.find(primary_str) == pimpl_->entries_.end()) {
        return false;
    }

    pimpl_->aliases_[std::string(alias)] = primary_str;
    return true;
}

bool patient_cache::remove_alias(std::string_view alias) {
    std::unique_lock lock(pimpl_->mutex_);
    return pimpl_->aliases_.erase(std::string(alias)) > 0;
}

std::expected<mapping::dicom_patient, cache_error> patient_cache::get_or_load(
    std::string_view key,
    std::function<std::optional<mapping::dicom_patient>()> loader) {
    // First try to get from cache
    auto cached = get(key);
    if (cached.has_value()) {
        return cached;
    }

    // Cache miss - try to load
    auto loaded = loader();
    if (!loaded.has_value()) {
        return std::unexpected(cache_error::not_found);
    }

    // Store in cache
    put(key, *loaded);

    return *loaded;
}

std::unordered_map<std::string, mapping::dicom_patient> patient_cache::get_many(
    const std::vector<std::string>& keys) const {
    std::unordered_map<std::string, mapping::dicom_patient> results;

    for (const auto& key : keys) {
        auto result = get(key);
        if (result.has_value()) {
            results[key] = *result;
        }
    }

    return results;
}

void patient_cache::put_many(
    const std::unordered_map<std::string, mapping::dicom_patient>& entries) {
    for (const auto& [key, patient] : entries) {
        put(key, patient);
    }
}

void patient_cache::clear() {
    std::unique_lock lock(pimpl_->mutex_);

    pimpl_->entries_.clear();
    pimpl_->aliases_.clear();
    pimpl_->lru_order_.clear();
    pimpl_->lru_index_.clear();
    pimpl_->stats_.current_size = 0;
}

size_t patient_cache::evict_expired() {
    std::unique_lock lock(pimpl_->mutex_);

    size_t count = 0;
    std::vector<std::string> expired_keys;

    for (const auto& [key, entry] : pimpl_->entries_) {
        if (entry.metadata.is_expired()) {
            expired_keys.push_back(key);
        }
    }

    for (const auto& key : expired_keys) {
        pimpl_->entries_.erase(key);
        pimpl_->remove_from_lru(key);
        ++count;
    }

    // Remove aliases pointing to expired entries
    for (auto it = pimpl_->aliases_.begin(); it != pimpl_->aliases_.end();) {
        if (pimpl_->entries_.find(it->second) == pimpl_->entries_.end()) {
            it = pimpl_->aliases_.erase(it);
        } else {
            ++it;
        }
    }

    pimpl_->stats_.eviction_count += count;
    pimpl_->stats_.current_size = pimpl_->entries_.size();

    return count;
}

size_t patient_cache::size() const noexcept {
    std::shared_lock lock(pimpl_->mutex_);
    return pimpl_->entries_.size();
}

bool patient_cache::empty() const noexcept {
    std::shared_lock lock(pimpl_->mutex_);
    return pimpl_->entries_.empty();
}

const patient_cache_config& patient_cache::config() const noexcept {
    return pimpl_->config_;
}

void patient_cache::set_enabled(bool enabled) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_.enabled = enabled;
}

std::optional<cache_entry_metadata> patient_cache::get_metadata(
    std::string_view key) const {
    std::shared_lock lock(pimpl_->mutex_);

    std::string resolved_key = pimpl_->resolve_key(key);
    auto it = pimpl_->entries_.find(resolved_key);

    if (it == pimpl_->entries_.end()) {
        return std::nullopt;
    }

    return it->second.metadata;
}

std::vector<std::string> patient_cache::keys() const {
    std::shared_lock lock(pimpl_->mutex_);

    std::vector<std::string> result;
    result.reserve(pimpl_->entries_.size());

    for (const auto& [key, _] : pimpl_->entries_) {
        result.push_back(key);
    }

    return result;
}

patient_cache::statistics patient_cache::get_statistics() const {
    std::shared_lock lock(pimpl_->mutex_);
    return pimpl_->stats_;
}

void patient_cache::reset_statistics() {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->stats_ = statistics{};
    pimpl_->stats_.current_size = pimpl_->entries_.size();
}

}  // namespace pacs::bridge::cache
