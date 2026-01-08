#ifndef PACS_BRIDGE_PROTOCOL_HL7_HL7_HANDLER_BASE_H
#define PACS_BRIDGE_PROTOCOL_HL7_HL7_HANDLER_BASE_H

/**
 * @file hl7_handler_base.h
 * @brief CRTP base class and type erasure for HL7 message handlers
 *
 * Provides zero-overhead polymorphism for HL7 message handlers using
 * the Curiously Recurring Template Pattern (CRTP). This eliminates
 * virtual function overhead in the hot path while maintaining runtime
 * handler dispatch capability through type erasure wrappers.
 *
 * Design:
 *   - HL7HandlerBase<Derived>: CRTP base for static polymorphism
 *   - HL7HandlerConcept: Compile-time handler validation
 *   - IHL7Handler: Type erasure interface for runtime dispatch
 *   - HL7HandlerWrapper<T>: Bridges CRTP handlers to interface
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/202
 * @see https://github.com/kcenon/pacs_bridge/issues/259
 */

#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <concepts>
#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::hl7 {

// =============================================================================
// Handler Error Codes (-880 to -889)
// =============================================================================

/**
 * @brief Handler base error codes
 *
 * Allocated range: -880 to -889
 */
enum class handler_error : int {
    /** Handler cannot process this message type */
    unsupported_message_type = -880,

    /** Handler processing failed */
    processing_failed = -881,

    /** Handler not initialized */
    not_initialized = -882,

    /** Handler is busy */
    busy = -883,

    /** Invalid handler state */
    invalid_state = -884
};

/**
 * @brief Convert handler_error to error code
 */
[[nodiscard]] constexpr int to_error_code(handler_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(handler_error error) noexcept {
    switch (error) {
        case handler_error::unsupported_message_type:
            return "Handler cannot process this message type";
        case handler_error::processing_failed:
            return "Handler processing failed";
        case handler_error::not_initialized:
            return "Handler not initialized";
        case handler_error::busy:
            return "Handler is busy processing another message";
        case handler_error::invalid_state:
            return "Handler is in invalid state";
        default:
            return "Unknown handler error";
    }
}

/**
 * @brief Convert handler_error to error_info for Result<T>
 */
[[nodiscard]] inline error_info to_error_info(
    handler_error error,
    const std::string& details = "") {
    return error_info{
        static_cast<int>(error),
        to_string(error),
        "hl7::handler",
        details
    };
}

// =============================================================================
// Generic Handler Result
// =============================================================================

/**
 * @brief Generic result for handler processing
 *
 * Used by the type-erased IHL7Handler interface. Handlers with specific
 * result types should convert to this generic form.
 */
struct handler_result {
    /** Processing was successful */
    bool success = false;

    /** Message type that was processed */
    std::string message_type;

    /** Handler type name */
    std::string handler_type;

    /** Description of what was done */
    std::string description;

    /** ACK response message (if applicable) */
    hl7_message ack_message;

    /** Processing warnings (non-fatal issues) */
    std::vector<std::string> warnings;
};

// =============================================================================
// Handler Concept
// =============================================================================

/**
 * @brief Concept for HL7 message handlers
 *
 * Validates that a type provides the required handler interface:
 * - can_handle(const hl7_message&) -> bool
 * - handler_type() -> std::string_view
 * - static type_name member
 *
 * @tparam T Handler type to validate
 */
template<typename T>
concept HL7HandlerConcept = requires(T handler, const hl7_message& msg) {
    // Check for can_handle method
    { handler.can_handle(msg) } -> std::convertible_to<bool>;

    // Check for handler_type method
    { handler.handler_type() } -> std::convertible_to<std::string_view>;

    // Check for static type_name
    { T::type_name } -> std::convertible_to<std::string_view>;
};

// =============================================================================
// CRTP Base Class
// =============================================================================

/**
 * @brief CRTP base class for HL7 message handlers
 *
 * Provides zero-overhead polymorphism through static dispatch.
 * Derived classes must implement:
 *   - can_handle_impl(const hl7_message&) -> bool
 *   - static constexpr std::string_view type_name
 *
 * @tparam Derived The derived handler class (CRTP)
 *
 * @example
 * ```cpp
 * class MyHandler : public HL7HandlerBase<MyHandler> {
 *     friend class HL7HandlerBase<MyHandler>;
 *
 *     static constexpr std::string_view type_name = "MY";
 *
 * private:
 *     bool can_handle_impl(const hl7_message& msg) const noexcept {
 *         return msg.message_type() == "MY";
 *     }
 * };
 * ```
 */
template<typename Derived>
class HL7HandlerBase {
public:
    /**
     * @brief Check if handler can process the message
     *
     * Delegates to derived class implementation via CRTP.
     *
     * @param message HL7 message to check
     * @return true if handler can process this message
     */
    [[nodiscard]] bool can_handle(const hl7_message& message) const noexcept {
        return derived().can_handle_impl(message);
    }

    /**
     * @brief Get the handler type name
     *
     * Returns the static type_name from the derived class.
     *
     * @return Handler type identifier string
     */
    [[nodiscard]] static constexpr std::string_view handler_type() noexcept {
        return Derived::type_name;
    }

protected:
    // Protected constructor - only derived classes can construct
    HL7HandlerBase() = default;
    ~HL7HandlerBase() = default;

    // Non-copyable, movable
    HL7HandlerBase(const HL7HandlerBase&) = default;
    HL7HandlerBase& operator=(const HL7HandlerBase&) = default;
    HL7HandlerBase(HL7HandlerBase&&) = default;
    HL7HandlerBase& operator=(HL7HandlerBase&&) = default;

private:
    /**
     * @brief Get reference to derived class
     */
    [[nodiscard]] Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    /**
     * @brief Get const reference to derived class
     */
    [[nodiscard]] const Derived& derived() const noexcept {
        return static_cast<const Derived&>(*this);
    }
};

// =============================================================================
// Type Erasure Interface
// =============================================================================

/**
 * @brief Type-erased interface for HL7 handlers
 *
 * Provides runtime polymorphism at the registry boundary while
 * preserving CRTP static dispatch within handlers. Used by the
 * handler registry for runtime message routing.
 */
class IHL7Handler {
public:
    virtual ~IHL7Handler() = default;

    /**
     * @brief Check if handler can process the message
     */
    [[nodiscard]] virtual bool can_handle(
        const hl7_message& message) const noexcept = 0;

    /**
     * @brief Process HL7 message
     *
     * @param message HL7 message to process
     * @return Generic handler result or error
     */
    [[nodiscard]] virtual Result<handler_result> process(
        const hl7_message& message) = 0;

    /**
     * @brief Get handler type name
     */
    [[nodiscard]] virtual std::string_view handler_type() const noexcept = 0;

protected:
    IHL7Handler() = default;
    IHL7Handler(const IHL7Handler&) = default;
    IHL7Handler& operator=(const IHL7Handler&) = default;
    IHL7Handler(IHL7Handler&&) = default;
    IHL7Handler& operator=(IHL7Handler&&) = default;
};

// =============================================================================
// Type Erasure Wrapper
// =============================================================================

/**
 * @brief Wrapper that bridges CRTP handlers to IHL7Handler interface
 *
 * Wraps a CRTP-based handler to provide the IHL7Handler interface,
 * allowing registration in the handler registry while maintaining
 * zero-overhead dispatch within the handler itself.
 *
 * @tparam Handler CRTP handler type that satisfies HL7HandlerConcept
 *
 * @example
 * ```cpp
 * auto handler = std::make_unique<HL7HandlerWrapper<adt_handler>>(cache);
 * registry.register_handler(std::move(handler));
 * ```
 */
template<typename Handler>
    requires HL7HandlerConcept<Handler>
class HL7HandlerWrapper : public IHL7Handler {
public:
    /**
     * @brief Construct wrapper with handler arguments
     *
     * Perfect forwards arguments to the underlying handler constructor.
     *
     * @tparam Args Handler constructor argument types
     * @param args Handler constructor arguments
     */
    template<typename... Args>
    explicit HL7HandlerWrapper(Args&&... args)
        : handler_(std::forward<Args>(args)...) {}

    /**
     * @brief Check if handler can process the message
     */
    [[nodiscard]] bool can_handle(
        const hl7_message& message) const noexcept override {
        return handler_.can_handle(message);
    }

    /**
     * @brief Process HL7 message using the wrapped handler
     *
     * Delegates to the handler's handle() method and converts
     * the handler-specific result to the generic handler_result.
     */
    [[nodiscard]] Result<handler_result> process(
        const hl7_message& message) override {
        auto result = handler_.handle(message);
        if (!result) {
            return kcenon::common::make_error<handler_result>(
                to_error_info(handler_error::processing_failed,
                              result.error().message));
        }

        // Convert handler-specific result to generic result
        handler_result generic_result;
        generic_result.success = result->success;
        generic_result.handler_type = std::string(Handler::type_name);
        generic_result.description = result->description;
        generic_result.ack_message = result->ack_message;
        generic_result.warnings = result->warnings;

        return generic_result;
    }

    /**
     * @brief Get handler type name
     */
    [[nodiscard]] std::string_view handler_type() const noexcept override {
        return Handler::type_name;
    }

    /**
     * @brief Get reference to underlying handler
     */
    [[nodiscard]] Handler& handler() noexcept {
        return handler_;
    }

    /**
     * @brief Get const reference to underlying handler
     */
    [[nodiscard]] const Handler& handler() const noexcept {
        return handler_;
    }

private:
    Handler handler_;
};

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Create a type-erased handler wrapper
 *
 * Factory function to create HL7HandlerWrapper instances with
 * automatic type deduction.
 *
 * @tparam Handler Handler type
 * @tparam Args Constructor argument types
 * @param args Constructor arguments
 * @return Unique pointer to IHL7Handler
 *
 * @example
 * ```cpp
 * auto wrapper = make_handler_wrapper<adt_handler>(patient_cache);
 * registry.register_handler(std::move(wrapper));
 * ```
 */
template<typename Handler, typename... Args>
    requires HL7HandlerConcept<Handler>
[[nodiscard]] std::unique_ptr<IHL7Handler> make_handler_wrapper(Args&&... args) {
    return std::make_unique<HL7HandlerWrapper<Handler>>(
        std::forward<Args>(args)...);
}

}  // namespace pacs::bridge::hl7

#endif  // PACS_BRIDGE_PROTOCOL_HL7_HL7_HANDLER_BASE_H
