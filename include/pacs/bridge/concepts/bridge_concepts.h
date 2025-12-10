#ifndef PACS_BRIDGE_CONCEPTS_BRIDGE_CONCEPTS_H
#define PACS_BRIDGE_CONCEPTS_BRIDGE_CONCEPTS_H

/**
 * @file bridge_concepts.h
 * @brief C++20 Concepts for PACS Bridge
 *
 * Provides type constraints for template parameters used throughout
 * the PACS Bridge codebase. Leverages concepts from common_system
 * and adds bridge-specific concepts.
 *
 * Key concepts:
 *   - Queueable: Types suitable for lock-free queue storage
 *   - Poolable: Types suitable for object pool management
 *   - MessageHandler: Callable types for message processing
 *   - ConfigReloadHandler: Callable types for config reload notifications
 *
 * Benefits:
 *   - Clear compile-time error messages
 *   - Self-documenting type requirements
 *   - Better IDE support for auto-completion
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/70
 * @see https://github.com/kcenon/common_system/issues/192
 */

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>

namespace pacs::bridge::concepts {

// =============================================================================
// Queue and Container Concepts
// =============================================================================

/**
 * @concept Queueable
 * @brief A type suitable for lock-free queue storage
 *
 * Queueable types must be move-constructible for efficient transfer
 * through the queue without copying.
 *
 * Example:
 * @code
 * template<Queueable T>
 * class lockfree_queue { ... };
 * @endcode
 */
template <typename T>
concept Queueable = std::move_constructible<T>;

/**
 * @concept Poolable
 * @brief A type suitable for object pool management
 *
 * Poolable types must be default-constructible for pre-allocation
 * and destructible for cleanup when the pool is destroyed.
 *
 * Example:
 * @code
 * template<Poolable T>
 * class object_pool { ... };
 * @endcode
 */
template <typename T>
concept Poolable = std::default_initializable<T> && std::destructible<T>;

/**
 * @concept Resettable
 * @brief A type that can be reset to initial state
 *
 * Used for objects that need to be reused from pools.
 * Types must provide a reset() method.
 *
 * Example:
 * @code
 * template<Poolable T> requires Resettable<T>
 * void return_to_pool(T& obj) {
 *     obj.reset();
 *     pool.release(obj);
 * }
 * @endcode
 */
template <typename T>
concept Resettable = requires(T t) {
    { t.reset() } -> std::same_as<void>;
};

// =============================================================================
// Callback and Handler Concepts
// =============================================================================

/**
 * @concept MessageHandler
 * @brief A callable that handles messages of type M
 *
 * Message handlers receive messages by const reference and
 * can optionally return a response.
 *
 * Example:
 * @code
 * template<typename M, MessageHandler<M> H>
 * void register_handler(H&& handler) {
 *     handlers_.push_back(std::forward<H>(handler));
 * }
 * @endcode
 */
template <typename H, typename M>
concept MessageHandler = std::invocable<H, const M&>;

/**
 * @concept VoidMessageHandler
 * @brief A callable that handles messages and returns void
 *
 * Example:
 * @code
 * template<typename M, VoidMessageHandler<M> H>
 * void on_message(H&& handler) { ... }
 * @endcode
 */
template <typename H, typename M>
concept VoidMessageHandler =
    MessageHandler<H, M> && std::is_void_v<std::invoke_result_t<H, const M&>>;

/**
 * @concept ConfigCallback
 * @brief A callable for configuration reload notifications
 *
 * Config callbacks receive the new configuration by const reference.
 *
 * Example:
 * @code
 * template<ConfigCallback<bridge_config> C>
 * void on_reload(C&& callback) {
 *     callbacks_.push_back(std::forward<C>(callback));
 * }
 * @endcode
 */
template <typename C, typename Config>
concept ConfigCallback =
    std::invocable<C, const Config&> &&
    std::is_void_v<std::invoke_result_t<C, const Config&>>;

/**
 * @concept EventCallback
 * @brief A callable for event notifications
 *
 * Event callbacks receive event data and return void.
 *
 * Example:
 * @code
 * template<EventCallback<patient_event> H>
 * void on_patient_created(H&& handler) { ... }
 * @endcode
 */
template <typename H, typename E>
concept EventCallback =
    std::invocable<H, const E&> &&
    std::is_void_v<std::invoke_result_t<H, const E&>>;

/**
 * @concept ProgressCallback
 * @brief A callable for progress reporting
 *
 * Progress callbacks receive progress information.
 */
template <typename P, typename ProgressInfo>
concept ProgressCallback =
    std::invocable<P, const ProgressInfo&> &&
    std::is_void_v<std::invoke_result_t<P, const ProgressInfo&>>;

// =============================================================================
// Factory Concepts
// =============================================================================

/**
 * @concept ObjectFactory
 * @brief A callable that creates objects of type T
 *
 * Factories return unique_ptr to newly created objects.
 *
 * Example:
 * @code
 * template<Poolable T, ObjectFactory<T> F>
 * object_pool(F&& factory) { ... }
 * @endcode
 */
template <typename F, typename T>
concept ObjectFactory =
    std::invocable<F> &&
    std::convertible_to<std::invoke_result_t<F>, std::unique_ptr<T>>;

/**
 * @concept ObjectResetter
 * @brief A callable that resets objects before returning to pool
 *
 * Resetters receive a reference to the object to reset.
 */
template <typename R, typename T>
concept ObjectResetter =
    std::invocable<R, T&> && std::is_void_v<std::invoke_result_t<R, T&>>;

// =============================================================================
// Validation Concepts
// =============================================================================

/**
 * @concept Validatable
 * @brief A type that can validate its own state
 *
 * Types must provide a validate() method.
 *
 * Example:
 * @code
 * template<Validatable C>
 * bool is_valid(const C& config) {
 *     auto result = config.validate();
 *     return result.is_ok();
 * }
 * @endcode
 */
template <typename T>
concept Validatable = requires(const T t) {
    { t.validate() };
};

/**
 * @concept Serializable
 * @brief A type that can be serialized to/from bytes
 *
 * Types must provide serialize() and deserialize() methods.
 */
template <typename T>
concept Serializable = requires(T t, const std::vector<uint8_t>& bytes) {
    { t.serialize() } -> std::convertible_to<std::vector<uint8_t>>;
    { T::deserialize(bytes) };
};

// =============================================================================
// HL7/Healthcare-Specific Concepts
// =============================================================================

/**
 * @concept HL7Parseable
 * @brief A type that can parse HL7 message data
 *
 * Types must provide a parse() method accepting string_view.
 */
template <typename T>
concept HL7Parseable = requires(T t, std::string_view data) {
    { t.parse(data) };
};

/**
 * @concept HL7Buildable
 * @brief A type that can be built into an HL7 message
 *
 * Types must provide a build() method returning string.
 */
template <typename T>
concept HL7Buildable = requires(const T t) {
    { t.build() } -> std::convertible_to<std::string>;
};

}  // namespace pacs::bridge::concepts

#endif  // PACS_BRIDGE_CONCEPTS_BRIDGE_CONCEPTS_H
