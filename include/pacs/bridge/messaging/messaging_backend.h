#ifndef PACS_BRIDGE_MESSAGING_MESSAGING_BACKEND_H
#define PACS_BRIDGE_MESSAGING_MESSAGING_BACKEND_H

/**
 * @file messaging_backend.h
 * @brief Messaging backend selection and factory
 *
 * Provides backend selection for messaging system integration:
 *   - Standalone: Self-contained thread pool
 *   - Integration: External executor integration
 *   - Auto-detection based on configuration
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/146
 * @see https://github.com/kcenon/pacs_bridge/issues/156
 */

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace pacs::bridge::messaging {

// Forward declarations
class hl7_message_bus;

// =============================================================================
// Backend Error Codes (-830 to -839)
// =============================================================================

/**
 * @brief Backend specific error codes
 *
 * Allocated range: -830 to -839
 */
enum class backend_error : int {
    /** Backend not initialized */
    not_initialized = -830,

    /** Backend already initialized */
    already_initialized = -831,

    /** Invalid backend type */
    invalid_type = -832,

    /** Backend creation failed */
    creation_failed = -833,

    /** External executor not available */
    executor_unavailable = -834,

    /** Configuration error */
    config_error = -835
};

/**
 * @brief Convert backend_error to error code
 */
[[nodiscard]] constexpr int to_error_code(backend_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(backend_error error) noexcept {
    switch (error) {
        case backend_error::not_initialized:
            return "Backend not initialized";
        case backend_error::already_initialized:
            return "Backend already initialized";
        case backend_error::invalid_type:
            return "Invalid backend type";
        case backend_error::creation_failed:
            return "Backend creation failed";
        case backend_error::executor_unavailable:
            return "External executor not available";
        case backend_error::config_error:
            return "Backend configuration error";
        default:
            return "Unknown backend error";
    }
}

// =============================================================================
// Backend Types
// =============================================================================

/**
 * @brief Available backend types
 */
enum class backend_type {
    /** Standalone backend with internal thread pool */
    standalone,

    /** Integration backend using external executor */
    integration,

    /** Auto-detect best available backend */
    automatic
};

/**
 * @brief Convert backend_type to string
 */
[[nodiscard]] constexpr const char* to_string(backend_type type) noexcept {
    switch (type) {
        case backend_type::standalone: return "standalone";
        case backend_type::integration: return "integration";
        case backend_type::automatic: return "automatic";
        default: return "unknown";
    }
}

// =============================================================================
// Backend Configuration
// =============================================================================

/**
 * @brief Backend configuration options
 */
struct backend_config {
    /** Backend type to use */
    backend_type type = backend_type::automatic;

    /** Number of worker threads (standalone mode) */
    size_t worker_threads = 0;  // 0 = auto-detect

    /** Queue capacity */
    size_t queue_capacity = 10000;

    /** Enable work stealing (standalone mode) */
    bool enable_work_stealing = true;

    /** Shutdown timeout */
    std::chrono::milliseconds shutdown_timeout{5000};

    /**
     * @brief Create default configuration
     */
    [[nodiscard]] static backend_config defaults() {
        return {};
    }

    /**
     * @brief Create standalone configuration
     */
    [[nodiscard]] static backend_config standalone(size_t threads = 0) {
        backend_config config;
        config.type = backend_type::standalone;
        config.worker_threads = threads;
        return config;
    }

    /**
     * @brief Create integration configuration
     */
    [[nodiscard]] static backend_config integration() {
        backend_config config;
        config.type = backend_type::integration;
        return config;
    }
};

// =============================================================================
// Backend Factory
// =============================================================================

/**
 * @brief Factory for creating messaging backends
 *
 * Provides centralized backend creation with automatic type detection
 * and configuration management.
 *
 * @example Usage
 * ```cpp
 * // Automatic backend selection
 * auto bus = messaging_backend_factory::create_message_bus();
 *
 * // Explicit standalone backend
 * auto config = backend_config::standalone(4);  // 4 threads
 * auto bus = messaging_backend_factory::create_message_bus(config);
 *
 * // Integration with external executor
 * messaging_backend_factory::set_external_executor(my_executor);
 * auto bus = messaging_backend_factory::create_message_bus(
 *     backend_config::integration());
 * ```
 */
class messaging_backend_factory {
public:
    /**
     * @brief Create a message bus with default configuration
     *
     * @return Message bus or error
     */
    [[nodiscard]] static std::expected<std::shared_ptr<hl7_message_bus>, backend_error>
    create_message_bus();

    /**
     * @brief Create a message bus with specific configuration
     *
     * @param config Backend configuration
     * @return Message bus or error
     */
    [[nodiscard]] static std::expected<std::shared_ptr<hl7_message_bus>, backend_error>
    create_message_bus(const backend_config& config);

    /**
     * @brief Set external executor for integration mode
     *
     * @param executor Executor function
     */
    static void set_external_executor(
        std::function<void(std::function<void()>)> executor);

    /**
     * @brief Clear external executor
     */
    static void clear_external_executor();

    /**
     * @brief Check if external executor is available
     */
    [[nodiscard]] static bool has_external_executor() noexcept;

    /**
     * @brief Get recommended backend type
     *
     * Based on available resources and configuration.
     *
     * @return Recommended backend type
     */
    [[nodiscard]] static backend_type recommended_backend() noexcept;

    /**
     * @brief Get default worker thread count
     *
     * @return Recommended number of worker threads
     */
    [[nodiscard]] static size_t default_worker_threads() noexcept;

private:
    messaging_backend_factory() = delete;
};

// =============================================================================
// Backend Status
// =============================================================================

/**
 * @brief Backend runtime status information
 */
struct backend_status {
    /** Current backend type */
    backend_type type = backend_type::standalone;

    /** Number of active workers */
    size_t active_workers = 0;

    /** Number of queued tasks */
    size_t queued_tasks = 0;

    /** Number of completed tasks */
    uint64_t completed_tasks = 0;

    /** Backend is healthy */
    bool healthy = false;

    /** Error message if not healthy */
    std::string error_message;
};

/**
 * @brief Get backend status from message bus
 *
 * @param bus Message bus to query
 * @return Backend status
 */
[[nodiscard]] backend_status get_backend_status(
    const hl7_message_bus& bus);

}  // namespace pacs::bridge::messaging

#endif  // PACS_BRIDGE_MESSAGING_MESSAGING_BACKEND_H
