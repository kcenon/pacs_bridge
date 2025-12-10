/**
 * @file config_manager.cpp
 * @brief Configuration manager implementation with hot-reload support
 *
 * Provides runtime configuration management with signal handling,
 * file watching, and component notification for configuration changes.
 *
 * @see include/pacs/bridge/config/config_manager.h
 * @see https://github.com/kcenon/pacs_bridge/issues/39
 */

#include "pacs/bridge/config/config_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <variant>

namespace pacs::bridge::config {

// =============================================================================
// Global Signal State
// =============================================================================

namespace {

// Global atomic flag for signal handling
std::atomic<bool> g_sighup_received{false};

// Pointer to the active config_manager for signal handling
std::atomic<config_manager::impl*> g_active_manager{nullptr};

#ifndef _WIN32
// Previous signal handler to restore
struct sigaction g_old_handler;
#endif

}  // namespace

// =============================================================================
// Configuration Manager Implementation
// =============================================================================

class config_manager::impl {
public:
    explicit impl(const bridge_config& initial_config,
                  const std::filesystem::path& config_path)
        : config_(initial_config),
          config_path_(config_path),
          last_load_time_(std::chrono::system_clock::now()) {
        update_file_mtime();
    }

    explicit impl(const std::filesystem::path& config_path)
        : config_path_(config_path) {
        auto result = config_loader::load(config_path);
        if (!result) {
            throw std::runtime_error("Failed to load configuration: " +
                                     result.error().to_string());
        }
        config_ = std::move(result.value());
        last_load_time_ = std::chrono::system_clock::now();
        update_file_mtime();
    }

    ~impl() {
        disable_file_watcher();
        disable_signal_handler();
    }

    // Non-copyable
    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    // =========================================================================
    // Configuration Access
    // =========================================================================

    [[nodiscard]] const bridge_config& get() const {
        std::shared_lock lock(config_mutex_);
        return config_;
    }

    [[nodiscard]] bridge_config get_copy() const {
        std::shared_lock lock(config_mutex_);
        return config_;
    }

    [[nodiscard]] const std::filesystem::path& config_path() const noexcept {
        return config_path_;
    }

    // =========================================================================
    // Reload Operations
    // =========================================================================

    [[nodiscard]] reload_result reload() { return reload_from(config_path_); }

    [[nodiscard]] reload_result reload_from(const std::filesystem::path& path) {
        reload_result result;
        auto start_time = std::chrono::steady_clock::now();

        increment_stat(&stats_.reload_attempts);
        stats_.last_reload_time = std::chrono::system_clock::now();

        // Load configuration from file
        auto load_result = config_loader::load(path);
        if (!load_result) {
            result.success = false;
            result.error_message = load_result.error().to_string();
            result.validation_errors = load_result.error().validation_errors;
            stats_.last_error = result.error_message;
            increment_stat(&stats_.reload_failures);

            auto end_time = std::chrono::steady_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time);
            return result;
        }

        return apply_internal(load_result.value(), start_time);
    }

    [[nodiscard]] reload_result apply(const bridge_config& new_config) {
        return apply_internal(new_config, std::chrono::steady_clock::now());
    }

    [[nodiscard]] bool has_file_changed() const {
        try {
            auto current_mtime = std::filesystem::last_write_time(config_path_);
            return current_mtime > last_file_mtime_;
        } catch (...) {
            return false;
        }
    }

    // =========================================================================
    // Change Detection
    // =========================================================================

    [[nodiscard]] static config_diff compare(const bridge_config& old_config,
                                             const bridge_config& new_config) {
        config_diff diff;

        // Check server name
        if (old_config.name != new_config.name) {
            diff.changed_fields.push_back("name");
        }

        // Check HL7 configuration
        compare_hl7(old_config.hl7, new_config.hl7, diff);

        // Check FHIR configuration
        compare_fhir(old_config.fhir, new_config.fhir, diff);

        // Check PACS configuration
        compare_pacs(old_config.pacs, new_config.pacs, diff);

        // Check routing rules (reloadable)
        if (!compare_routing_rules(old_config.routing_rules,
                                   new_config.routing_rules)) {
            diff.changed_fields.push_back("routing_rules");
        }

        // Check mapping configuration (reloadable)
        if (!compare_mapping(old_config.mapping, new_config.mapping)) {
            diff.changed_fields.push_back("mapping");
        }

        // Check queue configuration
        compare_queue(old_config.queue, new_config.queue, diff);

        // Check patient cache configuration
        if (old_config.patient_cache.max_entries !=
                new_config.patient_cache.max_entries ||
            old_config.patient_cache.ttl != new_config.patient_cache.ttl) {
            diff.changed_fields.push_back("patient_cache");
        }

        // Check logging configuration (partially reloadable)
        compare_logging(old_config.logging, new_config.logging, diff);

        return diff;
    }

    [[nodiscard]] static bool is_reloadable(std::string_view field_path) {
        // Reloadable fields
        static const std::vector<std::string> reloadable_prefixes = {
            "routing_rules",
            "hl7.outbound_destinations",
            "mapping",
            "logging.level",
            "logging.format",
            "logging.include_source_location",
            "patient_cache.ttl",
            "patient_cache.evict_on_full"};

        for (const auto& prefix : reloadable_prefixes) {
            if (field_path.starts_with(prefix)) {
                return true;
            }
        }
        return false;
    }

    // =========================================================================
    // Callback Registration
    // =========================================================================

    size_t on_reload(reload_callback callback) {
        std::lock_guard lock(callbacks_mutex_);
        size_t handle = next_callback_handle_++;
        callbacks_.emplace_back(handle, std::move(callback));
        return handle;
    }

    size_t on_reload(reload_callback_with_diff callback) {
        std::lock_guard lock(callbacks_mutex_);
        size_t handle = next_callback_handle_++;
        callbacks_with_diff_.emplace_back(handle, std::move(callback));
        return handle;
    }

    bool remove_callback(size_t handle) {
        std::lock_guard lock(callbacks_mutex_);

        auto it = std::find_if(callbacks_.begin(), callbacks_.end(),
                               [handle](const auto& pair) {
                                   return pair.first == handle;
                               });
        if (it != callbacks_.end()) {
            callbacks_.erase(it);
            return true;
        }

        auto it2 = std::find_if(callbacks_with_diff_.begin(),
                                callbacks_with_diff_.end(),
                                [handle](const auto& pair) {
                                    return pair.first == handle;
                                });
        if (it2 != callbacks_with_diff_.end()) {
            callbacks_with_diff_.erase(it2);
            return true;
        }

        return false;
    }

    void clear_callbacks() {
        std::lock_guard lock(callbacks_mutex_);
        callbacks_.clear();
        callbacks_with_diff_.clear();
    }

    // =========================================================================
    // Signal Handling
    // =========================================================================

    bool enable_signal_handler() {
#ifdef _WIN32
        // Windows doesn't support SIGHUP
        return false;
#else
        // Set up SIGHUP handler
        struct sigaction sa {};
        sa.sa_handler = [](int) { g_sighup_received.store(true); };
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;

        if (sigaction(SIGHUP, &sa, &g_old_handler) != 0) {
            return false;
        }

        g_active_manager.store(this);
        signal_handler_enabled_ = true;
        return true;
#endif
    }

    void disable_signal_handler() {
#ifndef _WIN32
        if (signal_handler_enabled_) {
            sigaction(SIGHUP, &g_old_handler, nullptr);
            g_active_manager.store(nullptr);
            signal_handler_enabled_ = false;
        }
#endif
    }

    [[nodiscard]] bool is_signal_handler_enabled() const noexcept {
        return signal_handler_enabled_;
    }

    bool process_pending_signal() {
        if (g_sighup_received.exchange(false)) {
            reload();
            return true;
        }
        return false;
    }

    // =========================================================================
    // File Watching
    // =========================================================================

    bool enable_file_watcher(std::chrono::seconds check_interval) {
        if (file_watcher_running_) {
            return true;
        }

        file_watcher_running_ = true;
        file_watcher_thread_ = std::thread([this, check_interval] {
            while (file_watcher_running_) {
                {
                    std::unique_lock lock(file_watcher_mutex_);
                    if (file_watcher_cv_.wait_for(lock, check_interval,
                                                  [this] {
                                                      return !file_watcher_running_;
                                                  })) {
                        break;  // Watcher was stopped
                    }
                }

                if (has_file_changed()) {
                    reload();
                }
            }
        });

        return true;
    }

    void disable_file_watcher() {
        if (!file_watcher_running_) {
            return;
        }

        {
            std::lock_guard lock(file_watcher_mutex_);
            file_watcher_running_ = false;
        }
        file_watcher_cv_.notify_all();

        if (file_watcher_thread_.joinable()) {
            file_watcher_thread_.join();
        }
    }

    [[nodiscard]] bool is_file_watcher_enabled() const noexcept {
        return file_watcher_running_;
    }

    // =========================================================================
    // Statistics
    // =========================================================================

    [[nodiscard]] statistics get_statistics() const {
        std::lock_guard lock(stats_mutex_);
        auto stats = stats_;
        stats.callback_count = callbacks_.size() + callbacks_with_diff_.size();
        return stats;
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    [[nodiscard]] reload_result
    apply_internal(const bridge_config& new_config,
                   std::chrono::steady_clock::time_point start_time) {
        reload_result result;

        // Validate new configuration
        auto validation_errors = config_loader::validate(new_config);
        if (!validation_errors.empty()) {
            result.success = false;
            result.error_message = "Configuration validation failed";
            result.validation_errors = std::move(validation_errors);
            stats_.last_error = result.error_message;
            increment_stat(&stats_.reload_failures);

            auto end_time = std::chrono::steady_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time);
            return result;
        }

        // Compare configurations
        config_diff diff;
        {
            std::shared_lock lock(config_mutex_);
            diff = compare(config_, new_config);
        }

        // Check for non-reloadable changes
        if (diff.requires_restart) {
            result.success = false;
            result.error_message =
                "Configuration contains non-reloadable changes: " +
                join_strings(diff.non_reloadable_changes, ", ");
            stats_.last_error = result.error_message;
            increment_stat(&stats_.reload_failures);

            auto end_time = std::chrono::steady_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time);
            return result;
        }

        // Apply new configuration
        {
            std::unique_lock lock(config_mutex_);
            config_ = new_config;
        }

        last_load_time_ = std::chrono::system_clock::now();
        update_file_mtime();

        // Notify callbacks
        result.components_notified = notify_callbacks(new_config, diff);

        result.success = true;
        increment_stat(&stats_.reload_successes);
        stats_.last_successful_reload_time = std::chrono::system_clock::now();
        stats_.last_error.reset();

        auto end_time = std::chrono::steady_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);

        return result;
    }

    size_t notify_callbacks(const bridge_config& config,
                            const config_diff& diff) {
        std::lock_guard lock(callbacks_mutex_);
        size_t count = 0;

        for (const auto& [handle, callback] : callbacks_) {
            try {
                callback(config);
                count++;
            } catch (...) {
                // Log error but continue with other callbacks
            }
        }

        for (const auto& [handle, callback] : callbacks_with_diff_) {
            try {
                callback(config, diff);
                count++;
            } catch (...) {
                // Log error but continue with other callbacks
            }
        }

        return count;
    }

    void update_file_mtime() {
        try {
            last_file_mtime_ = std::filesystem::last_write_time(config_path_);
        } catch (...) {
            // Ignore errors
        }
    }

    void increment_stat(size_t* stat) {
        std::lock_guard lock(stats_mutex_);
        (*stat)++;
    }

    static std::string join_strings(const std::vector<std::string>& strings,
                                    const std::string& delimiter) {
        std::string result;
        for (size_t i = 0; i < strings.size(); ++i) {
            if (i > 0) result += delimiter;
            result += strings[i];
        }
        return result;
    }

    // =========================================================================
    // Comparison Helpers
    // =========================================================================

    static void compare_hl7(const hl7_config& old_cfg,
                            const hl7_config& new_cfg, config_diff& diff) {
        // Listener port changes require restart
        if (old_cfg.listener.port != new_cfg.listener.port) {
            diff.changed_fields.push_back("hl7.listener.port");
            diff.non_reloadable_changes.push_back("hl7.listener.port");
            diff.requires_restart = true;
        }

        if (old_cfg.listener.bind_address != new_cfg.listener.bind_address) {
            diff.changed_fields.push_back("hl7.listener.bind_address");
            diff.non_reloadable_changes.push_back("hl7.listener.bind_address");
            diff.requires_restart = true;
        }

        // TLS changes require restart
        if (old_cfg.listener.tls.enabled != new_cfg.listener.tls.enabled ||
            old_cfg.listener.tls.cert_path != new_cfg.listener.tls.cert_path ||
            old_cfg.listener.tls.key_path != new_cfg.listener.tls.key_path) {
            diff.changed_fields.push_back("hl7.listener.tls");
            diff.non_reloadable_changes.push_back("hl7.listener.tls");
            diff.requires_restart = true;
        }

        // Other listener settings are reloadable
        if (old_cfg.listener.max_connections != new_cfg.listener.max_connections ||
            old_cfg.listener.idle_timeout != new_cfg.listener.idle_timeout) {
            diff.changed_fields.push_back("hl7.listener.settings");
        }

        // Outbound destinations are reloadable
        if (old_cfg.outbound_destinations.size() !=
            new_cfg.outbound_destinations.size()) {
            diff.changed_fields.push_back("hl7.outbound_destinations");
        }
    }

    static void compare_fhir(const fhir_config& old_cfg,
                             const fhir_config& new_cfg, config_diff& diff) {
        if (old_cfg.enabled != new_cfg.enabled) {
            diff.changed_fields.push_back("fhir.enabled");
            if (new_cfg.enabled) {
                diff.non_reloadable_changes.push_back("fhir.enabled");
                diff.requires_restart = true;
            }
        }

        if (old_cfg.server.port != new_cfg.server.port) {
            diff.changed_fields.push_back("fhir.server.port");
            diff.non_reloadable_changes.push_back("fhir.server.port");
            diff.requires_restart = true;
        }
    }

    static void compare_pacs(const pacs_config& old_cfg,
                             const pacs_config& new_cfg, config_diff& diff) {
        if (old_cfg.host != new_cfg.host || old_cfg.port != new_cfg.port ||
            old_cfg.ae_title != new_cfg.ae_title ||
            old_cfg.called_ae != new_cfg.called_ae) {
            diff.changed_fields.push_back("pacs");
        }
    }

    static bool compare_routing_rules(const std::vector<routing_rule>& old_rules,
                                      const std::vector<routing_rule>& new_rules) {
        if (old_rules.size() != new_rules.size()) return false;

        for (size_t i = 0; i < old_rules.size(); ++i) {
            const auto& old_r = old_rules[i];
            const auto& new_r = new_rules[i];
            if (old_r.name != new_r.name ||
                old_r.message_type_pattern != new_r.message_type_pattern ||
                old_r.source_pattern != new_r.source_pattern ||
                old_r.destination != new_r.destination ||
                old_r.priority != new_r.priority ||
                old_r.enabled != new_r.enabled) {
                return false;
            }
        }
        return true;
    }

    static bool compare_mapping(const mapping_config& old_map,
                                const mapping_config& new_map) {
        return old_map.modality_ae_titles == new_map.modality_ae_titles &&
               old_map.procedure_to_modality == new_map.procedure_to_modality &&
               old_map.custom_field_mappings == new_map.custom_field_mappings &&
               old_map.default_patient_id_issuer ==
                   new_map.default_patient_id_issuer;
    }

    static void compare_queue(const queue_config& old_cfg,
                              const queue_config& new_cfg, config_diff& diff) {
        if (old_cfg.database_path != new_cfg.database_path) {
            diff.changed_fields.push_back("queue.database_path");
            diff.non_reloadable_changes.push_back("queue.database_path");
            diff.requires_restart = true;
        }

        if (old_cfg.max_queue_size != new_cfg.max_queue_size ||
            old_cfg.worker_count != new_cfg.worker_count) {
            diff.changed_fields.push_back("queue");
        }
    }

    static void compare_logging(const logging_config& old_cfg,
                                const logging_config& new_cfg,
                                config_diff& diff) {
        // Level and format are reloadable
        if (old_cfg.level != new_cfg.level) {
            diff.changed_fields.push_back("logging.level");
        }
        if (old_cfg.format != new_cfg.format) {
            diff.changed_fields.push_back("logging.format");
        }

        // File path changes require restart
        if (old_cfg.file != new_cfg.file) {
            diff.changed_fields.push_back("logging.file");
            diff.non_reloadable_changes.push_back("logging.file");
            diff.requires_restart = true;
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    bridge_config config_;
    std::filesystem::path config_path_;
    mutable std::shared_mutex config_mutex_;

    // Timestamps
    std::chrono::system_clock::time_point last_load_time_;
    std::filesystem::file_time_type last_file_mtime_;

    // Callbacks
    std::vector<std::pair<size_t, reload_callback>> callbacks_;
    std::vector<std::pair<size_t, reload_callback_with_diff>> callbacks_with_diff_;
    std::mutex callbacks_mutex_;
    size_t next_callback_handle_ = 1;

    // Signal handling
    bool signal_handler_enabled_ = false;

    // File watching
    std::atomic<bool> file_watcher_running_{false};
    std::thread file_watcher_thread_;
    std::mutex file_watcher_mutex_;
    std::condition_variable file_watcher_cv_;

    // Statistics
    mutable std::mutex stats_mutex_;
    statistics stats_;
};

// =============================================================================
// Config Manager Public Interface
// =============================================================================

config_manager::config_manager(const bridge_config& initial_config,
                               const std::filesystem::path& config_path)
    : pimpl_(std::make_unique<impl>(initial_config, config_path)) {}

config_manager::config_manager(const std::filesystem::path& config_path)
    : pimpl_(std::make_unique<impl>(config_path)) {}

config_manager::~config_manager() = default;

config_manager::config_manager(config_manager&&) noexcept = default;
config_manager& config_manager::operator=(config_manager&&) noexcept = default;

const bridge_config& config_manager::get() const { return pimpl_->get(); }

bridge_config config_manager::get_copy() const { return pimpl_->get_copy(); }

const std::filesystem::path& config_manager::config_path() const noexcept {
    return pimpl_->config_path();
}

reload_result config_manager::reload() { return pimpl_->reload(); }

reload_result config_manager::reload_from(const std::filesystem::path& path) {
    return pimpl_->reload_from(path);
}

reload_result config_manager::apply(const bridge_config& new_config) {
    return pimpl_->apply(new_config);
}

bool config_manager::has_file_changed() const {
    return pimpl_->has_file_changed();
}

config_diff config_manager::compare(const bridge_config& old_config,
                                    const bridge_config& new_config) {
    return impl::compare(old_config, new_config);
}

bool config_manager::is_reloadable(std::string_view field_path) {
    return impl::is_reloadable(field_path);
}

size_t config_manager::on_reload(reload_callback callback) {
    return pimpl_->on_reload(std::move(callback));
}

size_t config_manager::on_reload(reload_callback_with_diff callback) {
    return pimpl_->on_reload(std::move(callback));
}

bool config_manager::remove_callback(size_t handle) {
    return pimpl_->remove_callback(handle);
}

void config_manager::clear_callbacks() { pimpl_->clear_callbacks(); }

bool config_manager::enable_signal_handler() {
    return pimpl_->enable_signal_handler();
}

void config_manager::disable_signal_handler() {
    pimpl_->disable_signal_handler();
}

bool config_manager::is_signal_handler_enabled() const noexcept {
    return pimpl_->is_signal_handler_enabled();
}

bool config_manager::process_pending_signal() {
    return pimpl_->process_pending_signal();
}

bool config_manager::enable_file_watcher(std::chrono::seconds check_interval) {
    return pimpl_->enable_file_watcher(check_interval);
}

void config_manager::disable_file_watcher() { pimpl_->disable_file_watcher(); }

bool config_manager::is_file_watcher_enabled() const noexcept {
    return pimpl_->is_file_watcher_enabled();
}

config_manager::statistics config_manager::get_statistics() const {
    return pimpl_->get_statistics();
}

}  // namespace pacs::bridge::config
