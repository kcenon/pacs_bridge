#ifndef PACS_BRIDGE_ROUTER_MESSAGE_ROUTER_H
#define PACS_BRIDGE_ROUTER_MESSAGE_ROUTER_H

/**
 * @file message_router.h
 * @brief HL7 message routing engine
 *
 * Provides a flexible message routing system for directing HL7 messages
 * to appropriate handlers based on configurable rules. Supports:
 *   - Pattern-based matching (message type, trigger event, sender)
 *   - Priority-based routing
 *   - Content-based routing
 *   - Handler chains for message processing
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/19
 * @see docs/reference_materials/07_routing_rules.md
 */

#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace pacs::bridge::router {

// =============================================================================
// Routing Error Codes (-930 to -939)
// =============================================================================

/**
 * @brief Router specific error codes
 *
 * Allocated range: -930 to -939
 */
enum class router_error : int {
    /** No matching route found */
    no_matching_route = -930,

    /** Handler returned error */
    handler_error = -931,

    /** Invalid route configuration */
    invalid_route = -932,

    /** Route pattern is invalid */
    invalid_pattern = -933,

    /** Handler not found */
    handler_not_found = -934,

    /** Route already exists */
    route_exists = -935,

    /** Maximum handlers exceeded */
    max_handlers_exceeded = -936,

    /** Message rejected by filter */
    message_rejected = -937,

    /** Routing timeout */
    timeout = -938
};

/**
 * @brief Convert router_error to error code
 */
[[nodiscard]] constexpr int to_error_code(router_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(router_error error) noexcept {
    switch (error) {
        case router_error::no_matching_route:
            return "No matching route found for message";
        case router_error::handler_error:
            return "Message handler returned error";
        case router_error::invalid_route:
            return "Invalid route configuration";
        case router_error::invalid_pattern:
            return "Route pattern is invalid";
        case router_error::handler_not_found:
            return "Handler not found";
        case router_error::route_exists:
            return "Route already exists";
        case router_error::max_handlers_exceeded:
            return "Maximum number of handlers exceeded";
        case router_error::message_rejected:
            return "Message was rejected by filter";
        case router_error::timeout:
            return "Routing operation timed out";
        default:
            return "Unknown router error";
    }
}

// =============================================================================
// Route Matching Criteria
// =============================================================================

/**
 * @brief Message pattern for route matching
 *
 * All fields are optional; empty fields match any value.
 * Supports wildcards (*) and regular expressions.
 */
struct message_pattern {
    /** Message type (ADT, ORM, etc.) - supports wildcards */
    std::string message_type;

    /** Trigger event (A01, O01, etc.) - supports wildcards */
    std::string trigger_event;

    /** Sending application pattern */
    std::string sending_application;

    /** Sending facility pattern */
    std::string sending_facility;

    /** Receiving application pattern */
    std::string receiving_application;

    /** Receiving facility pattern */
    std::string receiving_facility;

    /** Processing ID (P, D, T) */
    std::string processing_id;

    /** HL7 version pattern */
    std::string version;

    /** Use regex matching (default: wildcard) */
    bool use_regex = false;

    /**
     * @brief Create a pattern matching any message
     */
    [[nodiscard]] static message_pattern any();

    /**
     * @brief Create a pattern for specific message type
     */
    [[nodiscard]] static message_pattern for_type(std::string_view type);

    /**
     * @brief Create a pattern for message type and trigger
     */
    [[nodiscard]] static message_pattern for_type_trigger(std::string_view type,
                                                           std::string_view trigger);
};

// =============================================================================
// Route Handler
// =============================================================================

/**
 * @brief Result returned by message handlers
 */
struct handler_result {
    /** Processing was successful */
    bool success = true;

    /** Continue to next handler in chain */
    bool continue_chain = true;

    /** Optional response message (for ACK, etc.) */
    std::optional<hl7::hl7_message> response;

    /** Error message if not successful */
    std::string error_message;

    /**
     * @brief Create success result
     */
    [[nodiscard]] static handler_result ok(bool continue_chain = true);

    /**
     * @brief Create success result with response
     */
    [[nodiscard]] static handler_result ok_with_response(hl7::hl7_message response);

    /**
     * @brief Create error result
     */
    [[nodiscard]] static handler_result error(std::string_view message);

    /**
     * @brief Create stop result (success but stop chain)
     */
    [[nodiscard]] static handler_result stop();
};

/**
 * @brief Message handler function type
 */
using message_handler = std::function<handler_result(const hl7::hl7_message& message)>;

/**
 * @brief Filter function type - returns true to accept message
 */
using message_filter = std::function<bool(const hl7::hl7_message& message)>;

// =============================================================================
// Route Definition
// =============================================================================

/**
 * @brief Single routing rule
 */
struct route {
    /** Route identifier */
    std::string id;

    /** Route name for logging */
    std::string name;

    /** Pattern to match */
    message_pattern pattern;

    /** Priority (lower = higher priority) */
    int priority = 100;

    /** Is route enabled */
    bool enabled = true;

    /** Stop processing after this route matches */
    bool terminal = false;

    /** Handler chain */
    std::vector<std::string> handler_ids;

    /** Optional filter function */
    message_filter filter;

    /** Description */
    std::string description;

    /**
     * @brief Check if route matches a message
     */
    [[nodiscard]] bool matches(const hl7::hl7_message& message) const;
};

// =============================================================================
// Message Router
// =============================================================================

/**
 * @brief HL7 message routing engine
 *
 * Routes incoming HL7 messages to appropriate handlers based on
 * configurable matching rules. Supports priority-based routing,
 * handler chains, and content-based filtering.
 *
 * @example Basic Usage
 * ```cpp
 * message_router router;
 *
 * // Register handlers
 * router.register_handler("adt_handler", [](const hl7_message& msg) {
 *     // Process ADT message
 *     return handler_result::ok();
 * });
 *
 * // Add route
 * route r;
 * r.id = "adt_route";
 * r.pattern = message_pattern::for_type("ADT");
 * r.handler_ids = {"adt_handler"};
 * router.add_route(r);
 *
 * // Route message
 * auto result = router.route(message);
 * ```
 *
 * @example Priority Routing
 * ```cpp
 * // Higher priority route (lower number)
 * route emergency;
 * emergency.id = "emergency";
 * emergency.pattern = message_pattern::for_type_trigger("ADT", "A01");
 * emergency.priority = 1;  // High priority
 * emergency.terminal = true;  // Stop after this
 * router.add_route(emergency);
 *
 * // Default route
 * route default_route;
 * default_route.id = "default";
 * default_route.pattern = message_pattern::any();
 * default_route.priority = 1000;  // Low priority
 * router.add_route(default_route);
 * ```
 *
 * @example Content-Based Filtering
 * ```cpp
 * route vip_route;
 * vip_route.id = "vip";
 * vip_route.pattern = message_pattern::for_type("ADT");
 * vip_route.filter = [](const hl7_message& msg) {
 *     return msg.get_value("PV1.18") == "VIP";  // VIP patient
 * };
 * router.add_route(vip_route);
 * ```
 */
class message_router {
public:
    /**
     * @brief Default constructor
     */
    message_router();

    /**
     * @brief Destructor
     */
    ~message_router();

    // Non-copyable, movable
    message_router(const message_router&) = delete;
    message_router& operator=(const message_router&) = delete;
    message_router(message_router&&) noexcept;
    message_router& operator=(message_router&&) noexcept;

    // =========================================================================
    // Handler Registration
    // =========================================================================

    /**
     * @brief Register a message handler
     *
     * @param id Unique handler identifier
     * @param handler Handler function
     * @return true if registered, false if ID already exists
     */
    bool register_handler(std::string_view id, message_handler handler);

    /**
     * @brief Unregister a handler
     *
     * @param id Handler identifier
     * @return true if removed, false if not found
     */
    bool unregister_handler(std::string_view id);

    /**
     * @brief Check if handler exists
     */
    [[nodiscard]] bool has_handler(std::string_view id) const noexcept;

    /**
     * @brief Get list of registered handler IDs
     */
    [[nodiscard]] std::vector<std::string> handler_ids() const;

    // =========================================================================
    // Route Management
    // =========================================================================

    /**
     * @brief Add a routing rule
     *
     * @param r Route to add
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, router_error> add_route(const route& r);

    /**
     * @brief Remove a route
     *
     * @param route_id Route identifier
     * @return true if removed
     */
    bool remove_route(std::string_view route_id);

    /**
     * @brief Enable or disable a route
     *
     * @param route_id Route identifier
     * @param enabled Enable state
     */
    void set_route_enabled(std::string_view route_id, bool enabled);

    /**
     * @brief Get route by ID
     */
    [[nodiscard]] const route* get_route(std::string_view route_id) const;

    /**
     * @brief Get all routes
     */
    [[nodiscard]] std::vector<route> routes() const;

    /**
     * @brief Clear all routes
     */
    void clear_routes();

    // =========================================================================
    // Message Routing
    // =========================================================================

    /**
     * @brief Route a message to matching handlers
     *
     * @param message HL7 message to route
     * @return Handler result or error
     */
    [[nodiscard]] std::expected<handler_result, router_error> route(
        const hl7::hl7_message& message) const;

    /**
     * @brief Find matching routes for a message (without executing)
     *
     * @param message HL7 message
     * @return List of matching route IDs in priority order
     */
    [[nodiscard]] std::vector<std::string> find_matching_routes(
        const hl7::hl7_message& message) const;

    /**
     * @brief Check if any route matches a message
     */
    [[nodiscard]] bool has_matching_route(
        const hl7::hl7_message& message) const noexcept;

    // =========================================================================
    // Default Handler
    // =========================================================================

    /**
     * @brief Set default handler for unmatched messages
     *
     * Called when no routes match a message.
     */
    void set_default_handler(message_handler handler);

    /**
     * @brief Clear default handler
     */
    void clear_default_handler();

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Routing statistics
     */
    struct statistics {
        /** Total messages routed */
        size_t total_messages = 0;

        /** Messages matched by routes */
        size_t matched_messages = 0;

        /** Messages handled by default handler */
        size_t default_handled = 0;

        /** Messages with no handler */
        size_t unhandled_messages = 0;

        /** Handler errors */
        size_t handler_errors = 0;

        /** Per-route match counts */
        std::unordered_map<std::string, size_t> route_matches;
    };

    /**
     * @brief Get routing statistics
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
// Route Builder (Fluent API)
// =============================================================================

/**
 * @brief Fluent builder for route configuration
 *
 * @example
 * ```cpp
 * auto route = route_builder::create("adt_a01")
 *     .name("ADT A01 Handler")
 *     .match_type("ADT")
 *     .match_trigger("A01")
 *     .handler("adt_processor")
 *     .handler("audit_logger")
 *     .priority(10)
 *     .build();
 * ```
 */
class route_builder {
public:
    /**
     * @brief Create a new route builder
     */
    [[nodiscard]] static route_builder create(std::string_view id);

    /** Set route name */
    route_builder& name(std::string_view n);

    /** Set route description */
    route_builder& description(std::string_view desc);

    /** Match message type pattern */
    route_builder& match_type(std::string_view type);

    /** Match trigger event pattern */
    route_builder& match_trigger(std::string_view trigger);

    /** Match sending application */
    route_builder& match_sender(std::string_view app, std::string_view facility = "");

    /** Match receiving application */
    route_builder& match_receiver(std::string_view app, std::string_view facility = "");

    /** Match any message */
    route_builder& match_any();

    /** Use regex patterns */
    route_builder& use_regex(bool enable = true);

    /** Add handler to chain */
    route_builder& handler(std::string_view handler_id);

    /** Set priority */
    route_builder& priority(int p);

    /** Set as terminal route */
    route_builder& terminal(bool t = true);

    /** Add content filter */
    route_builder& filter(message_filter f);

    /** Build the route */
    [[nodiscard]] route build() const;

private:
    explicit route_builder(std::string_view id);
    route route_;
};

}  // namespace pacs::bridge::router

#endif  // PACS_BRIDGE_ROUTER_MESSAGE_ROUTER_H
