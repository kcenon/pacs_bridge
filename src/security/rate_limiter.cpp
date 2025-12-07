/**
 * @file rate_limiter.cpp
 * @brief Implementation of request rate limiting
 *
 * @see include/pacs/bridge/security/rate_limiter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include "pacs/bridge/security/rate_limiter.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace pacs::bridge::security {

// =============================================================================
// Sliding Window Counter
// =============================================================================

namespace {

struct sliding_window_counter {
    std::deque<std::chrono::steady_clock::time_point> timestamps;
    size_t count = 0;

    void add(std::chrono::steady_clock::time_point now,
             std::chrono::seconds window) {
        // Remove expired entries
        auto cutoff = now - window;
        while (!timestamps.empty() && timestamps.front() < cutoff) {
            timestamps.pop_front();
        }
        timestamps.push_back(now);
        count = timestamps.size();
    }

    size_t get_count(std::chrono::steady_clock::time_point now,
                     std::chrono::seconds window) const {
        auto cutoff = now - window;
        size_t valid_count = 0;
        for (const auto& ts : timestamps) {
            if (ts >= cutoff) ++valid_count;
        }
        return valid_count;
    }

    std::chrono::milliseconds time_until_slot(
        std::chrono::steady_clock::time_point now,
        std::chrono::seconds window,
        size_t max_requests) const {

        if (timestamps.size() < max_requests) {
            return std::chrono::milliseconds{0};
        }

        // Find when the oldest request in window will expire
        auto cutoff = now - window;
        for (const auto& ts : timestamps) {
            if (ts >= cutoff) {
                auto wait = ts + window - now;
                return std::chrono::duration_cast<std::chrono::milliseconds>(wait);
            }
        }
        return std::chrono::milliseconds{0};
    }
};

struct token_bucket {
    double tokens;
    std::chrono::steady_clock::time_point last_update;
    double refill_rate;
    size_t max_tokens;

    void refill(std::chrono::steady_clock::time_point now) {
        auto elapsed = std::chrono::duration<double>(now - last_update).count();
        tokens = std::min(static_cast<double>(max_tokens),
                          tokens + elapsed * refill_rate);
        last_update = now;
    }

    bool try_consume(size_t n = 1) {
        if (tokens >= static_cast<double>(n)) {
            tokens -= static_cast<double>(n);
            return true;
        }
        return false;
    }

    std::chrono::milliseconds time_until_available(size_t n = 1) const {
        if (tokens >= static_cast<double>(n)) {
            return std::chrono::milliseconds{0};
        }
        double needed = static_cast<double>(n) - tokens;
        double seconds = needed / refill_rate;
        return std::chrono::milliseconds{static_cast<long long>(seconds * 1000)};
    }
};

}  // namespace

// =============================================================================
// Implementation Class
// =============================================================================

class rate_limiter::impl {
public:
    explicit impl(const rate_limit_config& config) : config_(config) {}

    rate_limit_result check_request(std::string_view ip_address,
                                     std::string_view application) {
        if (!config_.enabled) {
            return rate_limit_result::allow(0, 0, std::chrono::milliseconds{0});
        }

        // Check exemptions
        if (is_exempt_ip(ip_address)) {
            return rate_limit_result::allow(0, 0, std::chrono::milliseconds{0});
        }
        if (!application.empty() && is_exempt_app(application)) {
            return rate_limit_result::allow(0, 0, std::chrono::milliseconds{0});
        }

        auto now = std::chrono::steady_clock::now();
        std::unique_lock lock(mutex_);

        // Check per-IP limit
        if (config_.per_ip_limit.enabled) {
            auto result = check_limit(ip_counters_, std::string(ip_address),
                                      config_.per_ip_limit, now);
            if (!result.allowed) {
                result.limit_key = "per_ip:" + std::string(ip_address);
                ++stats_.denied_by_ip;
                ++stats_.denied_requests;
                ++stats_.total_requests;
                return result;
            }
        }

        // Check per-application limit
        if (config_.per_app_limit.enabled && !application.empty()) {
            auto result = check_limit(app_counters_, std::string(application),
                                      config_.per_app_limit, now);
            if (!result.allowed) {
                result.limit_key = "per_app:" + std::string(application);
                ++stats_.denied_by_app;
                ++stats_.denied_requests;
                ++stats_.total_requests;
                return result;
            }
        }

        // Check global limit
        if (config_.global_limit.enabled) {
            auto result = check_global_limit_impl(now);
            if (!result.allowed) {
                result.limit_key = "global";
                ++stats_.denied_by_global;
                ++stats_.denied_requests;
                ++stats_.total_requests;
                return result;
            }
        }

        // Request allowed - record it
        record_request(std::string(ip_address), std::string(application), now);

        ++stats_.allowed_requests;
        ++stats_.total_requests;

        auto& counter = ip_counters_[std::string(ip_address)];
        auto window = config_.per_ip_limit.window_duration;
        auto reset_after = std::chrono::duration_cast<std::chrono::milliseconds>(window);

        return rate_limit_result::allow(
            counter.get_count(now, window),
            config_.per_ip_limit.max_requests,
            reset_after);
    }

    rate_limit_result check_limit(
        std::unordered_map<std::string, sliding_window_counter>& counters,
        const std::string& key,
        const rate_limit_tier& tier,
        std::chrono::steady_clock::time_point now) {

        auto& counter = counters[key];
        size_t current = counter.get_count(now, tier.window_duration);

        if (current >= tier.max_requests) {
            auto retry = counter.time_until_slot(now, tier.window_duration,
                                                  tier.max_requests);
            return rate_limit_result::deny(current, tier.max_requests, retry);
        }

        auto reset = std::chrono::duration_cast<std::chrono::milliseconds>(
            tier.window_duration);
        return rate_limit_result::allow(current, tier.max_requests, reset);
    }

    rate_limit_result check_global_limit_impl(std::chrono::steady_clock::time_point now) {
        size_t current = global_counter_.get_count(now,
            config_.global_limit.window_duration);

        if (current >= config_.global_limit.max_requests) {
            auto retry = global_counter_.time_until_slot(
                now, config_.global_limit.window_duration,
                config_.global_limit.max_requests);
            return rate_limit_result::deny(current,
                config_.global_limit.max_requests, retry);
        }

        auto reset = std::chrono::duration_cast<std::chrono::milliseconds>(
            config_.global_limit.window_duration);
        return rate_limit_result::allow(current,
            config_.global_limit.max_requests, reset);
    }

    void record_request(const std::string& ip,
                        const std::string& app,
                        std::chrono::steady_clock::time_point now) {
        if (config_.per_ip_limit.enabled) {
            ip_counters_[ip].add(now, config_.per_ip_limit.window_duration);
        }
        if (config_.per_app_limit.enabled && !app.empty()) {
            app_counters_[app].add(now, config_.per_app_limit.window_duration);
        }
        if (config_.global_limit.enabled) {
            global_counter_.add(now, config_.global_limit.window_duration);
        }
    }

    bool is_exempt_ip(std::string_view ip) const {
        return std::find(config_.exempt_ips.begin(), config_.exempt_ips.end(),
                         std::string(ip)) != config_.exempt_ips.end();
    }

    bool is_exempt_app(std::string_view app) const {
        return std::find(config_.exempt_apps.begin(), config_.exempt_apps.end(),
                         std::string(app)) != config_.exempt_apps.end();
    }

    rate_limit_config config_;
    mutable std::shared_mutex mutex_;

    std::unordered_map<std::string, sliding_window_counter> ip_counters_;
    std::unordered_map<std::string, sliding_window_counter> app_counters_;
    sliding_window_counter global_counter_;

    std::unordered_map<std::string, size_t> bytes_counters_;
    size_t total_bytes_ = 0;

    std::unordered_map<std::string, double> penalties_;

    statistics stats_{};
    violation_callback violation_callback_;
};

// =============================================================================
// Constructor / Destructor
// =============================================================================

rate_limiter::rate_limiter(const rate_limit_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

rate_limiter::~rate_limiter() = default;

rate_limiter::rate_limiter(rate_limiter&&) noexcept = default;
rate_limiter& rate_limiter::operator=(rate_limiter&&) noexcept = default;

// =============================================================================
// Rate Limit Checking
// =============================================================================

rate_limit_result rate_limiter::check_request(std::string_view ip_address,
                                               std::string_view application) {
    auto result = pimpl_->check_request(ip_address, application);
    if (!result.allowed && pimpl_->violation_callback_) {
        pimpl_->violation_callback_(ip_address, result);
    }
    return result;
}

rate_limit_result rate_limiter::peek(std::string_view ip_address,
                                      std::string_view application) const {
    if (!pimpl_->config_.enabled) {
        return rate_limit_result::allow(0, 0, std::chrono::milliseconds{0});
    }

    auto now = std::chrono::steady_clock::now();
    std::shared_lock lock(pimpl_->mutex_);

    // Check per-IP limit
    if (pimpl_->config_.per_ip_limit.enabled) {
        auto it = pimpl_->ip_counters_.find(std::string(ip_address));
        if (it != pimpl_->ip_counters_.end()) {
            size_t current = it->second.get_count(now,
                pimpl_->config_.per_ip_limit.window_duration);
            if (current >= pimpl_->config_.per_ip_limit.max_requests) {
                auto retry = it->second.time_until_slot(
                    now, pimpl_->config_.per_ip_limit.window_duration,
                    pimpl_->config_.per_ip_limit.max_requests);
                return rate_limit_result::deny(current,
                    pimpl_->config_.per_ip_limit.max_requests, retry);
            }
        }
    }

    return rate_limit_result::allow(0,
        pimpl_->config_.per_ip_limit.max_requests,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            pimpl_->config_.per_ip_limit.window_duration));
}

rate_limit_result rate_limiter::check_connection(std::string_view ip_address) {
    if (!pimpl_->config_.connection_limits.enabled) {
        return rate_limit_result::allow(0, 0, std::chrono::milliseconds{0});
    }

    // Connection limiting uses a stricter short window
    auto now = std::chrono::steady_clock::now();
    std::unique_lock lock(pimpl_->mutex_);

    auto& counter = pimpl_->ip_counters_[std::string(ip_address)];
    size_t current = counter.get_count(now,
        pimpl_->config_.connection_limits.window_duration);

    if (current >= pimpl_->config_.connection_limits.max_connections_per_ip) {
        ++pimpl_->stats_.denied_by_connection;
        auto retry = counter.time_until_slot(
            now, pimpl_->config_.connection_limits.window_duration,
            pimpl_->config_.connection_limits.max_connections_per_ip);
        return rate_limit_result::deny(current,
            pimpl_->config_.connection_limits.max_connections_per_ip, retry);
    }

    counter.add(now, pimpl_->config_.connection_limits.window_duration);
    return rate_limit_result::allow(current + 1,
        pimpl_->config_.connection_limits.max_connections_per_ip,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            pimpl_->config_.connection_limits.window_duration));
}

rate_limit_result rate_limiter::check_ip_limit(std::string_view ip_address) {
    auto now = std::chrono::steady_clock::now();
    std::shared_lock lock(pimpl_->mutex_);

    auto it = pimpl_->ip_counters_.find(std::string(ip_address));
    if (it == pimpl_->ip_counters_.end()) {
        return rate_limit_result::allow(0,
            pimpl_->config_.per_ip_limit.max_requests,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                pimpl_->config_.per_ip_limit.window_duration));
    }

    size_t current = it->second.get_count(now,
        pimpl_->config_.per_ip_limit.window_duration);
    auto reset = std::chrono::duration_cast<std::chrono::milliseconds>(
        pimpl_->config_.per_ip_limit.window_duration);

    if (current >= pimpl_->config_.per_ip_limit.max_requests) {
        auto retry = it->second.time_until_slot(
            now, pimpl_->config_.per_ip_limit.window_duration,
            pimpl_->config_.per_ip_limit.max_requests);
        return rate_limit_result::deny(current,
            pimpl_->config_.per_ip_limit.max_requests, retry);
    }

    return rate_limit_result::allow(current,
        pimpl_->config_.per_ip_limit.max_requests, reset);
}

rate_limit_result rate_limiter::check_app_limit(std::string_view application) {
    auto now = std::chrono::steady_clock::now();
    std::shared_lock lock(pimpl_->mutex_);

    auto it = pimpl_->app_counters_.find(std::string(application));
    if (it == pimpl_->app_counters_.end()) {
        return rate_limit_result::allow(0,
            pimpl_->config_.per_app_limit.max_requests,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                pimpl_->config_.per_app_limit.window_duration));
    }

    size_t current = it->second.get_count(now,
        pimpl_->config_.per_app_limit.window_duration);
    auto reset = std::chrono::duration_cast<std::chrono::milliseconds>(
        pimpl_->config_.per_app_limit.window_duration);

    if (current >= pimpl_->config_.per_app_limit.max_requests) {
        auto retry = it->second.time_until_slot(
            now, pimpl_->config_.per_app_limit.window_duration,
            pimpl_->config_.per_app_limit.max_requests);
        return rate_limit_result::deny(current,
            pimpl_->config_.per_app_limit.max_requests, retry);
    }

    return rate_limit_result::allow(current,
        pimpl_->config_.per_app_limit.max_requests, reset);
}

rate_limit_result rate_limiter::check_global_limit() {
    auto now = std::chrono::steady_clock::now();
    std::shared_lock lock(pimpl_->mutex_);
    return pimpl_->check_global_limit_impl(now);
}

// =============================================================================
// Size-Based Limiting
// =============================================================================

rate_limit_result rate_limiter::check_bytes(std::string_view ip_address,
                                             size_t bytes) {
    if (!pimpl_->config_.size_limits.enabled) {
        return rate_limit_result::allow(0, 0, std::chrono::milliseconds{0});
    }

    std::unique_lock lock(pimpl_->mutex_);

    auto& ip_bytes = pimpl_->bytes_counters_[std::string(ip_address)];
    size_t new_total = ip_bytes + bytes;

    if (new_total > pimpl_->config_.size_limits.max_bytes_per_ip) {
        ++pimpl_->stats_.denied_by_size;
        return rate_limit_result::deny(ip_bytes,
            pimpl_->config_.size_limits.max_bytes_per_ip,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                pimpl_->config_.size_limits.window_duration));
    }

    ip_bytes = new_total;
    pimpl_->total_bytes_ += bytes;
    pimpl_->stats_.total_bytes += bytes;

    return rate_limit_result::allow(new_total,
        pimpl_->config_.size_limits.max_bytes_per_ip,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            pimpl_->config_.size_limits.window_duration));
}

void rate_limiter::record_bytes(std::string_view ip_address, size_t bytes) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->bytes_counters_[std::string(ip_address)] += bytes;
    pimpl_->total_bytes_ += bytes;
    pimpl_->stats_.total_bytes += bytes;
}

size_t rate_limiter::get_bytes_transferred(std::string_view ip_address) const {
    std::shared_lock lock(pimpl_->mutex_);
    auto it = pimpl_->bytes_counters_.find(std::string(ip_address));
    return it != pimpl_->bytes_counters_.end() ? it->second : 0;
}

size_t rate_limiter::get_total_bytes_transferred() const {
    std::shared_lock lock(pimpl_->mutex_);
    return pimpl_->total_bytes_;
}

// =============================================================================
// Penalty Management
// =============================================================================

void rate_limiter::apply_penalty(std::string_view key, double multiplier) {
    std::unique_lock lock(pimpl_->mutex_);
    double penalty_mult = multiplier > 0 ? multiplier : pimpl_->config_.penalty_multiplier;
    pimpl_->penalties_[std::string(key)] *= penalty_mult;
    ++pimpl_->stats_.active_penalties;
}

void rate_limiter::reset_penalty(std::string_view key) {
    std::unique_lock lock(pimpl_->mutex_);
    if (pimpl_->penalties_.erase(std::string(key)) > 0) {
        if (pimpl_->stats_.active_penalties > 0) {
            --pimpl_->stats_.active_penalties;
        }
    }
}

double rate_limiter::get_penalty(std::string_view key) const {
    std::shared_lock lock(pimpl_->mutex_);
    auto it = pimpl_->penalties_.find(std::string(key));
    return it != pimpl_->penalties_.end() ? it->second : 1.0;
}

// =============================================================================
// Exemptions
// =============================================================================

void rate_limiter::add_exempt_ip(std::string_view ip_address) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_.exempt_ips.emplace_back(ip_address);
}

void rate_limiter::remove_exempt_ip(std::string_view ip_address) {
    std::unique_lock lock(pimpl_->mutex_);
    auto& ips = pimpl_->config_.exempt_ips;
    ips.erase(std::remove(ips.begin(), ips.end(), std::string(ip_address)),
              ips.end());
}

bool rate_limiter::is_exempt_ip(std::string_view ip_address) const {
    std::shared_lock lock(pimpl_->mutex_);
    return pimpl_->is_exempt_ip(ip_address);
}

void rate_limiter::add_exempt_app(std::string_view application) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_.exempt_apps.emplace_back(application);
}

void rate_limiter::remove_exempt_app(std::string_view application) {
    std::unique_lock lock(pimpl_->mutex_);
    auto& apps = pimpl_->config_.exempt_apps;
    apps.erase(std::remove(apps.begin(), apps.end(), std::string(application)),
               apps.end());
}

bool rate_limiter::is_exempt_app(std::string_view application) const {
    std::shared_lock lock(pimpl_->mutex_);
    return pimpl_->is_exempt_app(application);
}

// =============================================================================
// Configuration
// =============================================================================

void rate_limiter::set_config(const rate_limit_config& config) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_ = config;
}

const rate_limit_config& rate_limiter::config() const noexcept {
    return pimpl_->config_;
}

void rate_limiter::set_enabled(bool enabled) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_.enabled = enabled;
}

bool rate_limiter::is_enabled() const noexcept {
    return pimpl_->config_.enabled;
}

// =============================================================================
// Callbacks
// =============================================================================

void rate_limiter::set_violation_callback(violation_callback callback) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->violation_callback_ = std::move(callback);
}

// =============================================================================
// Maintenance
// =============================================================================

void rate_limiter::cleanup() {
    std::unique_lock lock(pimpl_->mutex_);
    // Clear old counters periodically
    pimpl_->bytes_counters_.clear();
    pimpl_->total_bytes_ = 0;
}

void rate_limiter::reset() {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->ip_counters_.clear();
    pimpl_->app_counters_.clear();
    pimpl_->global_counter_ = {};
    pimpl_->bytes_counters_.clear();
    pimpl_->total_bytes_ = 0;
    pimpl_->penalties_.clear();
}

void rate_limiter::reset_ip(std::string_view ip_address) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->ip_counters_.erase(std::string(ip_address));
    pimpl_->bytes_counters_.erase(std::string(ip_address));
    pimpl_->penalties_.erase(std::string(ip_address));
}

// =============================================================================
// Statistics
// =============================================================================

rate_limiter::statistics rate_limiter::get_statistics() const {
    std::shared_lock lock(pimpl_->mutex_);
    auto stats = pimpl_->stats_;
    stats.tracked_ips = pimpl_->ip_counters_.size();
    stats.tracked_apps = pimpl_->app_counters_.size();
    return stats;
}

void rate_limiter::reset_statistics() {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->stats_ = {};
}

rate_limiter::client_status rate_limiter::get_client_status(
    std::string_view ip_address) const {

    std::shared_lock lock(pimpl_->mutex_);
    auto now = std::chrono::steady_clock::now();

    client_status status;
    status.ip_address = std::string(ip_address);
    status.is_exempt = pimpl_->is_exempt_ip(ip_address);

    auto ip_it = pimpl_->ip_counters_.find(std::string(ip_address));
    if (ip_it != pimpl_->ip_counters_.end()) {
        status.requests_in_window = ip_it->second.get_count(
            now, pimpl_->config_.per_ip_limit.window_duration);
    }
    status.requests_limit = pimpl_->config_.per_ip_limit.max_requests;

    auto bytes_it = pimpl_->bytes_counters_.find(std::string(ip_address));
    if (bytes_it != pimpl_->bytes_counters_.end()) {
        status.bytes_in_window = bytes_it->second;
    }
    status.bytes_limit = pimpl_->config_.size_limits.max_bytes_per_ip;

    auto penalty_it = pimpl_->penalties_.find(std::string(ip_address));
    if (penalty_it != pimpl_->penalties_.end()) {
        status.penalty_multiplier = penalty_it->second;
    }

    status.window_reset = std::chrono::duration_cast<std::chrono::milliseconds>(
        pimpl_->config_.per_ip_limit.window_duration);

    return status;
}

std::vector<rate_limiter::client_status>
rate_limiter::get_all_client_statuses() const {
    std::shared_lock lock(pimpl_->mutex_);
    std::vector<client_status> statuses;
    statuses.reserve(pimpl_->ip_counters_.size());

    for (const auto& [ip, _] : pimpl_->ip_counters_) {
        lock.unlock();
        statuses.push_back(get_client_status(ip));
        lock.lock();
    }

    return statuses;
}

// =============================================================================
// HTTP Header Helpers
// =============================================================================

std::unordered_map<std::string, std::string>
make_rate_limit_headers(const rate_limit_result& result) {
    std::unordered_map<std::string, std::string> headers;

    // Standard rate limit headers (draft-ietf-httpapi-ratelimit-headers)
    headers["RateLimit-Limit"] = std::to_string(result.limit);
    headers["RateLimit-Remaining"] = std::to_string(result.remaining);
    headers["RateLimit-Reset"] = std::to_string(result.reset_after.count() / 1000);

    if (!result.allowed) {
        headers["Retry-After"] = std::to_string(result.retry_after.count() / 1000);
    }

    // Legacy headers for compatibility
    headers["X-RateLimit-Limit"] = std::to_string(result.limit);
    headers["X-RateLimit-Remaining"] = std::to_string(result.remaining);

    return headers;
}

}  // namespace pacs::bridge::security
