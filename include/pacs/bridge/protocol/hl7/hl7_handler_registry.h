#ifndef PACS_BRIDGE_PROTOCOL_HL7_HL7_HANDLER_REGISTRY_H
#define PACS_BRIDGE_PROTOCOL_HL7_HL7_HANDLER_REGISTRY_H

/**
 * @file hl7_handler_registry.h
 * @brief Registry for HL7 message handlers with type erasure
 *
 * Provides a centralized registry for HL7 message handlers that uses
 * type erasure wrappers to enable runtime handler lookup while maintaining
 * CRTP-based static dispatch within individual handlers.
 *
 * Design:
 *   - Handlers registered via type-erased IHL7Handler interface
 *   - Runtime message routing to appropriate handler
 *   - Virtual calls only at registry boundary
 *   - Zero-overhead dispatch within handlers via CRTP
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/202
 * @see https://github.com/kcenon/pacs_bridge/issues/261
 */

#include "pacs/bridge/protocol/hl7/hl7_handler_base.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::hl7 {

// =============================================================================
// Registry Error Codes (-890 to -899)
// =============================================================================

/**
 * @brief Registry error codes
 *
 * Allocated range: -890 to -899
 */
enum class registry_error : int {
    /** Handler already registered */
    handler_exists = -890,

    /** No handler found for message */
    no_handler = -891,

    /** Handler registration failed */
    registration_failed = -892,

    /** Multiple handlers can process message */
    ambiguous_handler = -893,

    /** Registry is empty */
    empty_registry = -894
};

/**
 * @brief Convert registry_error to error code
 */
[[nodiscard]] constexpr int to_error_code(registry_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(registry_error error) noexcept {
    switch (error) {
        case registry_error::handler_exists:
            return "Handler already registered for this type";
        case registry_error::no_handler:
            return "No handler found for message type";
        case registry_error::registration_failed:
            return "Handler registration failed";
        case registry_error::ambiguous_handler:
            return "Multiple handlers can process this message";
        case registry_error::empty_registry:
            return "No handlers registered";
        default:
            return "Unknown registry error";
    }
}

/**
 * @brief Convert registry_error to error_info
 */
[[nodiscard]] inline error_info to_error_info(
    registry_error error,
    const std::string& details = "") {
    return error_info{
        static_cast<int>(error),
        to_string(error),
        "hl7::registry",
        details
    };
}

// =============================================================================
// Handler Registry
// =============================================================================

/**
 * @brief Registry for HL7 message handlers
 *
 * Provides centralized registration and lookup of HL7 message handlers.
 * Uses type erasure to store handlers of different types while enabling
 * runtime message routing.
 *
 * Thread-safe for concurrent read access and handler registration.
 *
 * @example Basic Usage
 * ```cpp
 * hl7_handler_registry registry;
 *
 * // Register handlers
 * registry.register_handler<adt_handler>(patient_cache);
 * registry.register_handler<orm_handler>(mwl_client);
 * registry.register_handler<siu_handler>(mwl_client);
 *
 * // Process message
 * auto result = registry.process(message);
 * if (result) {
 *     send_ack(result->ack_message);
 * }
 * ```
 *
 * @example With Pre-built Handlers
 * ```cpp
 * auto adt = make_handler_wrapper<adt_handler>(cache);
 * registry.register_handler(std::move(adt));
 * ```
 */
class hl7_handler_registry {
public:
    /**
     * @brief Default constructor
     */
    hl7_handler_registry() = default;

    /**
     * @brief Destructor
     */
    ~hl7_handler_registry() = default;

    // Non-copyable, non-movable (contains mutex)
    hl7_handler_registry(const hl7_handler_registry&) = delete;
    hl7_handler_registry& operator=(const hl7_handler_registry&) = delete;
    hl7_handler_registry(hl7_handler_registry&&) = delete;
    hl7_handler_registry& operator=(hl7_handler_registry&&) = delete;

    // =========================================================================
    // Handler Registration
    // =========================================================================

    /**
     * @brief Register a handler using CRTP wrapper
     *
     * Creates a type-erased wrapper around the handler and registers it.
     *
     * @tparam Handler CRTP handler type
     * @tparam Args Constructor argument types
     * @param args Handler constructor arguments
     * @return Success or error
     *
     * @example
     * ```cpp
     * registry.register_handler<adt_handler>(patient_cache);
     * ```
     */
    template<typename Handler, typename... Args>
        requires HL7HandlerConcept<Handler>
    [[nodiscard]] VoidResult register_handler(Args&&... args) {
        auto wrapper = make_handler_wrapper<Handler>(std::forward<Args>(args)...);
        return register_handler(std::move(wrapper));
    }

    /**
     * @brief Register a pre-built handler wrapper
     *
     * @param handler Handler wrapper to register
     * @return Success or error
     */
    [[nodiscard]] VoidResult register_handler(
        std::unique_ptr<IHL7Handler> handler);

    /**
     * @brief Unregister a handler by type name
     *
     * @param type_name Handler type name (e.g., "ADT", "ORM")
     * @return true if handler was removed
     */
    bool unregister_handler(std::string_view type_name);

    /**
     * @brief Check if a handler is registered
     *
     * @param type_name Handler type name
     * @return true if handler is registered
     */
    [[nodiscard]] bool has_handler(std::string_view type_name) const;

    /**
     * @brief Get list of registered handler types
     *
     * @return Vector of handler type names
     */
    [[nodiscard]] std::vector<std::string> registered_types() const;

    /**
     * @brief Get handler count
     */
    [[nodiscard]] size_t handler_count() const noexcept;

    /**
     * @brief Clear all handlers
     */
    void clear();

    // =========================================================================
    // Message Processing
    // =========================================================================

    /**
     * @brief Find handler for a message
     *
     * Searches registered handlers for one that can process the message.
     *
     * @param message HL7 message
     * @return Pointer to handler or nullptr if none found
     */
    [[nodiscard]] IHL7Handler* find_handler(const hl7_message& message) const;

    /**
     * @brief Process message with appropriate handler
     *
     * Finds a handler that can process the message and delegates to it.
     *
     * @param message HL7 message to process
     * @return Handler result or error
     */
    [[nodiscard]] Result<handler_result> process(const hl7_message& message);

    /**
     * @brief Check if any handler can process the message
     *
     * @param message HL7 message
     * @return true if a handler exists for this message
     */
    [[nodiscard]] bool can_process(const hl7_message& message) const;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Registry statistics
     */
    struct statistics {
        /** Total messages processed */
        size_t total_processed = 0;

        /** Successfully processed */
        size_t success_count = 0;

        /** Failed processing */
        size_t failure_count = 0;

        /** Messages with no handler */
        size_t no_handler_count = 0;

        /** Per-handler statistics */
        std::unordered_map<std::string, size_t> handler_counts;
    };

    /**
     * @brief Get registry statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

private:
    // Handler storage by type name
    std::unordered_map<std::string, std::unique_ptr<IHL7Handler>> handlers_;

    // Statistics
    mutable std::mutex stats_mutex_;
    mutable statistics stats_;

    // Handler lookup mutex
    mutable std::mutex handlers_mutex_;
};

// =============================================================================
// Global Registry (Optional)
// =============================================================================

/**
 * @brief Get the default global registry
 *
 * Provides a singleton-like global registry for convenience.
 * For production use, prefer explicit registry instances.
 *
 * @return Reference to global registry
 */
[[nodiscard]] hl7_handler_registry& default_registry();

}  // namespace pacs::bridge::hl7

#endif  // PACS_BRIDGE_PROTOCOL_HL7_HL7_HANDLER_REGISTRY_H
