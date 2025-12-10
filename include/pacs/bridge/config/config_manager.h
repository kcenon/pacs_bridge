#ifndef PACS_BRIDGE_CONFIG_CONFIG_MANAGER_H
#define PACS_BRIDGE_CONFIG_CONFIG_MANAGER_H

/**
 * @file config_manager.h
 * @brief Configuration manager with hot-reload support
 *
 * Provides runtime configuration management with support for:
 *   - Configuration hot-reload without restart
 *   - SIGHUP signal handling for reload trigger
 *   - Component reload callbacks
 *   - Validation before applying changes
 *   - File change watching (optional)
 *
 * Reloadable configuration items:
 *   - Routing rules
 *   - Outbound destinations
 *   - Mapping configurations
 *   - Log levels
 *
 * Non-reloadable configuration items (requires restart):
 *   - Listener ports
 *   - TLS certificates
 *   - Database paths
 *
 * @example Basic Usage
 * ```cpp
 * auto result = config_loader::load("/etc/pacs/config.yaml");
 * if (!result) return 1;
 *
 * config_manager manager(result.value(), "/etc/pacs/config.yaml");
 *
 * // Register component callbacks
 * manager.on_reload([](const bridge_config& config) {
 *     // Apply new routing rules
 * });
 *
 * // Enable SIGHUP handling
 * manager.enable_signal_handler();
 *
 * // Or trigger reload programmatically
 * auto reload_result = manager.reload();
 * ```
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/39
 * @see docs/PRD.md - FR-5.1.4, NFR-5.4
 */

#include "bridge_config.h"
#include "config_loader.h"

#include <atomic>
#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace pacs::bridge::config {

// =============================================================================
// Reload Result Types
// =============================================================================

/**
 * @brief Result of a configuration reload operation
 */
struct reload_result {
    /** Whether the reload was successful */
    bool success = false;

    /** Number of components notified */
    size_t components_notified = 0;

    /** Error message if reload failed */
    std::optional<std::string> error_message;

    /** Validation errors if validation failed */
    std::vector<validation_error_info> validation_errors;

    /** Time taken to reload */
    std::chrono::milliseconds duration{0};
};

/**
 * @brief Configuration change detection result
 */
struct config_diff {
    /** Fields that changed between old and new configuration */
    std::vector<std::string> changed_fields;

    /** Whether any non-reloadable fields changed */
    bool requires_restart = false;

    /** List of non-reloadable fields that changed */
    std::vector<std::string> non_reloadable_changes;
};

// =============================================================================
// Configuration Manager
// =============================================================================

/**
 * @brief Configuration manager with hot-reload support
 *
 * Manages the bridge configuration and provides hot-reload capabilities
 * for runtime configuration changes without requiring a restart.
 *
 * Thread-safe: All operations are protected by appropriate locks.
 */
class config_manager {
public:
    /**
     * @brief Callback type for configuration reload notifications
     *
     * @param config The new configuration after reload
     */
    using reload_callback = std::function<void(const bridge_config&)>;

    /**
     * @brief Callback type with change details
     *
     * @param config The new configuration
     * @param diff The changes between old and new configuration
     */
    using reload_callback_with_diff =
        std::function<void(const bridge_config&, const config_diff&)>;

    // =========================================================================
    // Construction
    // =========================================================================

    /**
     * @brief Constructor with initial configuration
     *
     * @param initial_config The initial configuration to manage
     * @param config_path Path to configuration file for reloads
     */
    config_manager(const bridge_config& initial_config,
                   const std::filesystem::path& config_path);

    /**
     * @brief Constructor with configuration file path
     *
     * Loads the configuration from the specified file.
     *
     * @param config_path Path to configuration file
     * @throws std::runtime_error if configuration loading fails
     */
    explicit config_manager(const std::filesystem::path& config_path);

    /**
     * @brief Destructor
     *
     * Stops file watcher and signal handler if running.
     */
    ~config_manager();

    // Non-copyable
    config_manager(const config_manager&) = delete;
    config_manager& operator=(const config_manager&) = delete;

    // Movable
    config_manager(config_manager&&) noexcept;
    config_manager& operator=(config_manager&&) noexcept;

    // =========================================================================
    // Configuration Access
    // =========================================================================

    /**
     * @brief Get the current configuration
     *
     * Thread-safe: uses shared lock for read access.
     *
     * @return Const reference to current configuration
     */
    [[nodiscard]] const bridge_config& get() const;

    /**
     * @brief Get a copy of the current configuration
     *
     * Thread-safe: creates a copy under shared lock.
     *
     * @return Copy of current configuration
     */
    [[nodiscard]] bridge_config get_copy() const;

    /**
     * @brief Get the configuration file path
     */
    [[nodiscard]] const std::filesystem::path& config_path() const noexcept;

    // =========================================================================
    // Reload Operations
    // =========================================================================

    /**
     * @brief Reload configuration from file
     *
     * Reloads the configuration from the configured file path.
     * Validates the new configuration before applying.
     * Notifies all registered callbacks on success.
     *
     * @return Reload result with success status and details
     */
    [[nodiscard]] reload_result reload();

    /**
     * @brief Reload configuration from a specific file
     *
     * @param path Path to configuration file to load
     * @return Reload result with success status and details
     */
    [[nodiscard]] reload_result reload_from(const std::filesystem::path& path);

    /**
     * @brief Apply a new configuration directly
     *
     * Validates and applies the given configuration.
     * Does not read from file.
     *
     * @param new_config Configuration to apply
     * @return Reload result with success status and details
     */
    [[nodiscard]] reload_result apply(const bridge_config& new_config);

    /**
     * @brief Check if configuration file has changed
     *
     * Compares the file's modification time with the last reload time.
     *
     * @return true if the file has been modified since last reload
     */
    [[nodiscard]] bool has_file_changed() const;

    // =========================================================================
    // Change Detection
    // =========================================================================

    /**
     * @brief Compare two configurations and detect changes
     *
     * @param old_config Previous configuration
     * @param new_config New configuration
     * @return Detailed change information
     */
    [[nodiscard]] static config_diff compare(const bridge_config& old_config,
                                             const bridge_config& new_config);

    /**
     * @brief Check if a specific field is reloadable
     *
     * Reloadable fields can be changed at runtime without restart.
     *
     * @param field_path Path to the field (e.g., "logging.level")
     * @return true if the field can be hot-reloaded
     */
    [[nodiscard]] static bool is_reloadable(std::string_view field_path);

    // =========================================================================
    // Callback Registration
    // =========================================================================

    /**
     * @brief Register a callback for configuration reload
     *
     * The callback will be invoked after a successful reload.
     *
     * @param callback Function to call on reload
     * @return Handle for unregistering the callback
     */
    size_t on_reload(reload_callback callback);

    /**
     * @brief Register a callback with change details
     *
     * @param callback Function to call on reload with diff
     * @return Handle for unregistering the callback
     */
    size_t on_reload(reload_callback_with_diff callback);

    /**
     * @brief Unregister a reload callback
     *
     * @param handle Handle returned from on_reload()
     * @return true if callback was found and removed
     */
    bool remove_callback(size_t handle);

    /**
     * @brief Remove all registered callbacks
     */
    void clear_callbacks();

    // =========================================================================
    // Signal Handling
    // =========================================================================

    /**
     * @brief Enable SIGHUP signal handler for reload
     *
     * After calling this, sending SIGHUP to the process will
     * trigger a configuration reload.
     *
     * @return true if signal handler was installed successfully
     */
    bool enable_signal_handler();

    /**
     * @brief Disable SIGHUP signal handler
     */
    void disable_signal_handler();

    /**
     * @brief Check if signal handler is enabled
     */
    [[nodiscard]] bool is_signal_handler_enabled() const noexcept;

    /**
     * @brief Process pending signal (call from main loop if needed)
     *
     * If a SIGHUP was received, this will trigger a reload.
     *
     * @return true if a reload was triggered
     */
    bool process_pending_signal();

    // =========================================================================
    // File Watching (Optional)
    // =========================================================================

    /**
     * @brief Enable file change watching
     *
     * Periodically checks if the configuration file has changed
     * and automatically triggers a reload.
     *
     * @param check_interval How often to check for changes
     * @return true if file watcher was started successfully
     */
    bool enable_file_watcher(
        std::chrono::seconds check_interval = std::chrono::seconds{5});

    /**
     * @brief Disable file change watching
     */
    void disable_file_watcher();

    /**
     * @brief Check if file watcher is enabled
     */
    [[nodiscard]] bool is_file_watcher_enabled() const noexcept;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Reload statistics
     */
    struct statistics {
        /** Total reload attempts */
        size_t reload_attempts = 0;

        /** Successful reloads */
        size_t reload_successes = 0;

        /** Failed reloads */
        size_t reload_failures = 0;

        /** Last reload time */
        std::optional<std::chrono::system_clock::time_point> last_reload_time;

        /** Last successful reload time */
        std::optional<std::chrono::system_clock::time_point>
            last_successful_reload_time;

        /** Last error message */
        std::optional<std::string> last_error;

        /** Number of registered callbacks */
        size_t callback_count = 0;
    };

    /**
     * @brief Get reload statistics
     */
    [[nodiscard]] statistics get_statistics() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::config

#endif  // PACS_BRIDGE_CONFIG_CONFIG_MANAGER_H
