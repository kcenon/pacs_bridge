/**
 * @file access_control.cpp
 * @brief Implementation of IP-based access control
 *
 * @see include/pacs/bridge/security/access_control.h
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include "pacs/bridge/security/access_control.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>

namespace pacs::bridge::security {

// =============================================================================
// IP Range Implementation
// =============================================================================

std::optional<ip_range> ip_range::from_cidr(std::string_view cidr,
                                             std::string_view desc) {
    ip_range result;
    result.description = std::string(desc);
    result.added_at = std::chrono::system_clock::now();

    // Parse CIDR notation
    auto slash_pos = cidr.find('/');
    if (slash_pos != std::string_view::npos) {
        result.address = std::string(cidr.substr(0, slash_pos));
        auto prefix_str = cidr.substr(slash_pos + 1);

        auto [ptr, ec] = std::from_chars(
            prefix_str.data(), prefix_str.data() + prefix_str.size(),
            result.prefix_length);

        if (ec != std::errc()) {
            return std::nullopt;
        }
    } else {
        result.address = std::string(cidr);
        // Default prefix length based on IP version
        result.prefix_length = cidr.find(':') != std::string_view::npos ? 128 : 32;
    }

    // Validate IP address
    if (!is_valid_ip(result.address)) {
        return std::nullopt;
    }

    return result;
}

bool ip_range::matches(std::string_view ip) const {
    if (prefix_length == 32 && ip.find(':') == std::string_view::npos) {
        // Exact match for IPv4
        return address == ip;
    }
    if (prefix_length == 128 && ip.find(':') != std::string_view::npos) {
        // Exact match for IPv6
        return address == ip;
    }

    // CIDR matching (simplified for IPv4)
    if (ip.find(':') == std::string_view::npos &&
        address.find(':') == std::string_view::npos) {
        // Parse IPv4 addresses
        auto parse_ipv4 = [](std::string_view addr) -> std::optional<uint32_t> {
            uint32_t result = 0;
            size_t start = 0;
            int octet_count = 0;

            for (size_t i = 0; i <= addr.size(); ++i) {
                if (i == addr.size() || addr[i] == '.') {
                    if (octet_count >= 4) return std::nullopt;

                    auto octet_str = addr.substr(start, i - start);
                    uint8_t octet;
                    auto [ptr, ec] = std::from_chars(
                        octet_str.data(), octet_str.data() + octet_str.size(), octet);
                    if (ec != std::errc()) return std::nullopt;

                    result = (result << 8) | octet;
                    start = i + 1;
                    ++octet_count;
                }
            }

            return octet_count == 4 ? std::make_optional(result) : std::nullopt;
        };

        auto addr_val = parse_ipv4(address);
        auto ip_val = parse_ipv4(ip);

        if (!addr_val || !ip_val) {
            return false;
        }

        uint32_t mask = prefix_length == 0 ? 0 :
            ~((1U << (32 - prefix_length)) - 1);

        return (*addr_val & mask) == (*ip_val & mask);
    }

    return false;
}

bool ip_range::is_expired() const noexcept {
    if (!expires_at) return false;
    return std::chrono::system_clock::now() > *expires_at;
}

std::string ip_range::to_cidr() const {
    if ((prefix_length == 32 && address.find(':') == std::string_view::npos) ||
        (prefix_length == 128 && address.find(':') != std::string_view::npos)) {
        return address;
    }
    return address + "/" + std::to_string(prefix_length);
}

// =============================================================================
// Utility Functions Implementation
// =============================================================================

bool is_valid_ip(std::string_view ip) {
    if (ip.empty()) return false;

    // IPv6 check (contains ':')
    if (ip.find(':') != std::string_view::npos) {
        // Basic IPv6 validation
        int colon_count = 0;
        for (char c : ip) {
            if (c == ':') ++colon_count;
            else if (!std::isxdigit(c)) return false;
        }
        return colon_count >= 2 && colon_count <= 7;
    }

    // IPv4 validation
    int dot_count = 0;
    size_t start = 0;

    for (size_t i = 0; i <= ip.size(); ++i) {
        if (i == ip.size() || ip[i] == '.') {
            if (i == start) return false;

            auto octet_str = ip.substr(start, i - start);
            if (octet_str.size() > 3) return false;

            int octet = 0;
            auto [ptr, ec] = std::from_chars(
                octet_str.data(), octet_str.data() + octet_str.size(), octet);
            if (ec != std::errc() || octet < 0 || octet > 255) return false;

            if (i < ip.size()) ++dot_count;
            start = i + 1;
        } else if (!std::isdigit(ip[i])) {
            return false;
        }
    }

    return dot_count == 3;
}

bool is_private_ip(std::string_view ip) {
    // Parse first octets for IPv4
    if (ip.find(':') != std::string_view::npos) {
        // IPv6 - check for link-local (fe80::) and unique local (fc00::)
        return ip.substr(0, 4) == "fe80" ||
               ip.substr(0, 2) == "fc" ||
               ip.substr(0, 2) == "fd";
    }

    auto first_dot = ip.find('.');
    if (first_dot == std::string_view::npos) return false;

    int first_octet = 0;
    auto [ptr1, ec1] = std::from_chars(ip.data(), ip.data() + first_dot, first_octet);
    if (ec1 != std::errc()) return false;

    // 10.x.x.x
    if (first_octet == 10) return true;

    // 127.x.x.x (loopback)
    if (first_octet == 127) return true;

    // 172.16-31.x.x
    if (first_octet == 172) {
        auto second_dot = ip.find('.', first_dot + 1);
        if (second_dot != std::string_view::npos) {
            int second_octet = 0;
            auto [ptr2, ec2] = std::from_chars(
                ip.data() + first_dot + 1,
                ip.data() + second_dot,
                second_octet);
            if (ec2 == std::errc() && second_octet >= 16 && second_octet <= 31) {
                return true;
            }
        }
    }

    // 192.168.x.x
    if (first_octet == 192) {
        auto second_dot = ip.find('.', first_dot + 1);
        if (second_dot != std::string_view::npos) {
            int second_octet = 0;
            auto [ptr2, ec2] = std::from_chars(
                ip.data() + first_dot + 1,
                ip.data() + second_dot,
                second_octet);
            if (ec2 == std::errc() && second_octet == 168) {
                return true;
            }
        }
    }

    // 169.254.x.x (link-local)
    if (first_octet == 169) {
        auto second_dot = ip.find('.', first_dot + 1);
        if (second_dot != std::string_view::npos) {
            int second_octet = 0;
            auto [ptr2, ec2] = std::from_chars(
                ip.data() + first_dot + 1,
                ip.data() + second_dot,
                second_octet);
            if (ec2 == std::errc() && second_octet == 254) {
                return true;
            }
        }
    }

    return false;
}

bool is_localhost(std::string_view ip) {
    if (ip == "127.0.0.1" || ip == "localhost") return true;
    if (ip == "::1") return true;

    // 127.x.x.x range
    if (ip.size() > 4 && ip.substr(0, 4) == "127.") return true;

    return false;
}

std::optional<std::pair<std::string, uint8_t>> parse_cidr(std::string_view cidr) {
    auto slash_pos = cidr.find('/');
    if (slash_pos == std::string_view::npos) {
        if (is_valid_ip(cidr)) {
            uint8_t prefix = cidr.find(':') != std::string_view::npos ? 128 : 32;
            return std::make_pair(std::string(cidr), prefix);
        }
        return std::nullopt;
    }

    auto address = std::string(cidr.substr(0, slash_pos));
    auto prefix_str = cidr.substr(slash_pos + 1);

    uint8_t prefix;
    auto [ptr, ec] = std::from_chars(
        prefix_str.data(), prefix_str.data() + prefix_str.size(), prefix);

    if (ec != std::errc() || !is_valid_ip(address)) {
        return std::nullopt;
    }

    return std::make_pair(address, prefix);
}

// =============================================================================
// Implementation Class
// =============================================================================

class access_controller::impl {
public:
    explicit impl(const access_control_config& config) : config_(config) {}

    access_result check(std::string_view ip_address) const {
        std::shared_lock lock(mutex_);

        if (!config_.enabled) {
            return access_result::allow("access_control_disabled");
        }

        // Always allow localhost if configured
        if (config_.always_allow_localhost && is_localhost(ip_address)) {
            return access_result::allow("localhost_always_allowed");
        }

        // Check temporary blocks first
        auto block_it = temp_blocks_.find(std::string(ip_address));
        if (block_it != temp_blocks_.end()) {
            auto now = std::chrono::system_clock::now();
            if (now < block_it->second) {
                auto result = access_result::deny(
                    access_error::blacklisted, "temporarily_blocked");
                result.block_remaining = std::chrono::duration_cast<std::chrono::seconds>(
                    block_it->second - now);
                return result;
            }
        }

        // Check blacklist
        for (const auto& range : config_.blacklist) {
            if (!range.is_expired() && range.matches(ip_address)) {
                return access_result::deny(
                    access_error::blacklisted, range.description);
            }
        }

        // Check whitelist mode
        if (config_.mode == access_mode::whitelist_only ||
            config_.mode == access_mode::whitelist_and_blacklist) {

            bool whitelisted = false;
            std::string matched_rule;

            for (const auto& range : config_.whitelist) {
                if (!range.is_expired() && range.matches(ip_address)) {
                    whitelisted = true;
                    matched_rule = range.description;
                    break;
                }
            }

            if (!whitelisted) {
                return access_result::deny(
                    access_error::not_whitelisted, "not_in_whitelist");
            }

            return access_result::allow(matched_rule);
        }

        // Allow all mode or blacklist only (passed blacklist check)
        return access_result::allow("default_allow");
    }

    access_result check_and_record(std::string_view ip_address) {
        auto result = check(ip_address);

        if (result.allowed) {
            std::unique_lock lock(mutex_);
            auto& count = connection_counts_[std::string(ip_address)];
            ++count;
            result.connection_count = count;

            // Check connection rate limit
            if (config_.max_connections_per_ip > 0 &&
                count > config_.max_connections_per_ip) {
                result.allowed = false;
                result.error = access_error::rate_limited;
                result.matched_rule = "max_connections_per_ip";
                ++stats_.denied_rate_limited;
                ++stats_.denied_count;
            } else {
                ++stats_.allowed_count;
            }
        } else {
            std::unique_lock lock(mutex_);
            ++stats_.denied_count;
        }

        ++stats_.total_checks;
        unique_ips_.insert(std::string(ip_address));
        stats_.unique_ips = unique_ips_.size();

        return result;
    }

    void record_failure(std::string_view ip_address) {
        std::unique_lock lock(mutex_);
        auto& count = failure_counts_[std::string(ip_address)];
        ++count;

        // Auto-block after too many failures
        if (config_.block_after_failures > 0 &&
            count >= config_.block_after_failures) {
            auto expiry = std::chrono::system_clock::now() +
                          config_.block_duration;
            temp_blocks_[std::string(ip_address)] = expiry;
            ++stats_.currently_blocked;
            ++stats_.denied_too_many_failures;
        }
    }

    void reset_failures(std::string_view ip_address) {
        std::unique_lock lock(mutex_);
        failure_counts_.erase(std::string(ip_address));
    }

    access_control_config config_;
    mutable std::shared_mutex mutex_;

    std::unordered_map<std::string, size_t> connection_counts_;
    std::unordered_map<std::string, size_t> failure_counts_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> temp_blocks_;
    std::unordered_set<std::string> unique_ips_;

    statistics stats_{};
    access_callback access_callback_;
};

// =============================================================================
// Constructor / Destructor
// =============================================================================

access_controller::access_controller(const access_control_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

access_controller::~access_controller() = default;

access_controller::access_controller(access_controller&&) noexcept = default;
access_controller& access_controller::operator=(access_controller&&) noexcept = default;

// =============================================================================
// Access Checking
// =============================================================================

access_result access_controller::check(std::string_view ip_address) const {
    auto result = pimpl_->check(ip_address);
    if (pimpl_->access_callback_) {
        pimpl_->access_callback_(ip_address, result);
    }
    return result;
}

access_result access_controller::check_and_record(std::string_view ip_address) {
    auto result = pimpl_->check_and_record(ip_address);
    if (pimpl_->access_callback_) {
        pimpl_->access_callback_(ip_address, result);
    }
    return result;
}

void access_controller::record_failure(std::string_view ip_address) {
    pimpl_->record_failure(ip_address);
}

void access_controller::reset_failures(std::string_view ip_address) {
    pimpl_->reset_failures(ip_address);
}

// =============================================================================
// Whitelist Management
// =============================================================================

bool access_controller::add_to_whitelist(std::string_view cidr,
                                          std::string_view description) {
    auto range = ip_range::from_cidr(cidr, description);
    if (!range) return false;

    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_.whitelist.push_back(*range);
    return true;
}

bool access_controller::remove_from_whitelist(std::string_view cidr) {
    std::unique_lock lock(pimpl_->mutex_);
    auto& whitelist = pimpl_->config_.whitelist;
    auto it = std::remove_if(whitelist.begin(), whitelist.end(),
        [cidr](const ip_range& range) { return range.to_cidr() == cidr; });
    if (it != whitelist.end()) {
        whitelist.erase(it, whitelist.end());
        return true;
    }
    return false;
}

std::vector<ip_range> access_controller::get_whitelist() const {
    std::shared_lock lock(pimpl_->mutex_);
    return pimpl_->config_.whitelist;
}

void access_controller::clear_whitelist() {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_.whitelist.clear();
}

// =============================================================================
// Blacklist Management
// =============================================================================

bool access_controller::add_to_blacklist(std::string_view cidr,
                                          std::string_view description) {
    auto range = ip_range::from_cidr(cidr, description);
    if (!range) return false;

    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_.blacklist.push_back(*range);
    return true;
}

bool access_controller::remove_from_blacklist(std::string_view cidr) {
    std::unique_lock lock(pimpl_->mutex_);
    auto& blacklist = pimpl_->config_.blacklist;
    auto it = std::remove_if(blacklist.begin(), blacklist.end(),
        [cidr](const ip_range& range) { return range.to_cidr() == cidr; });
    if (it != blacklist.end()) {
        blacklist.erase(it, blacklist.end());
        return true;
    }
    return false;
}

std::vector<ip_range> access_controller::get_blacklist() const {
    std::shared_lock lock(pimpl_->mutex_);
    return pimpl_->config_.blacklist;
}

void access_controller::clear_blacklist() {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_.blacklist.clear();
}

// =============================================================================
// Temporary Blocking
// =============================================================================

void access_controller::block(std::string_view ip_address,
                               std::chrono::minutes duration,
                               std::string_view reason) {
    std::unique_lock lock(pimpl_->mutex_);
    auto expiry = std::chrono::system_clock::now() + duration;
    pimpl_->temp_blocks_[std::string(ip_address)] = expiry;
    ++pimpl_->stats_.currently_blocked;
}

void access_controller::unblock(std::string_view ip_address) {
    std::unique_lock lock(pimpl_->mutex_);
    if (pimpl_->temp_blocks_.erase(std::string(ip_address)) > 0) {
        if (pimpl_->stats_.currently_blocked > 0) {
            --pimpl_->stats_.currently_blocked;
        }
    }
}

std::vector<std::pair<std::string, std::chrono::system_clock::time_point>>
access_controller::get_blocked_ips() const {
    std::shared_lock lock(pimpl_->mutex_);
    std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> result;
    result.reserve(pimpl_->temp_blocks_.size());
    for (const auto& [ip, expiry] : pimpl_->temp_blocks_) {
        result.emplace_back(ip, expiry);
    }
    return result;
}

void access_controller::cleanup_expired_blocks() {
    std::unique_lock lock(pimpl_->mutex_);
    auto now = std::chrono::system_clock::now();
    for (auto it = pimpl_->temp_blocks_.begin(); it != pimpl_->temp_blocks_.end();) {
        if (now >= it->second) {
            it = pimpl_->temp_blocks_.erase(it);
            if (pimpl_->stats_.currently_blocked > 0) {
                --pimpl_->stats_.currently_blocked;
            }
        } else {
            ++it;
        }
    }
}

// =============================================================================
// Configuration
// =============================================================================

void access_controller::set_config(const access_control_config& config) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_ = config;
}

const access_control_config& access_controller::config() const noexcept {
    return pimpl_->config_;
}

void access_controller::set_mode(access_mode mode) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_.mode = mode;
}

void access_controller::set_enabled(bool enabled) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->config_.enabled = enabled;
}

bool access_controller::is_enabled() const noexcept {
    return pimpl_->config_.enabled;
}

// =============================================================================
// Callbacks
// =============================================================================

void access_controller::set_access_callback(access_callback callback) {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->access_callback_ = std::move(callback);
}

// =============================================================================
// Statistics
// =============================================================================

access_controller::statistics access_controller::get_statistics() const {
    std::shared_lock lock(pimpl_->mutex_);
    return pimpl_->stats_;
}

void access_controller::reset_statistics() {
    std::unique_lock lock(pimpl_->mutex_);
    pimpl_->stats_ = {};
    pimpl_->connection_counts_.clear();
    pimpl_->unique_ips_.clear();
}

}  // namespace pacs::bridge::security
