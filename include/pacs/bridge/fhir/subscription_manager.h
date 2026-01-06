#ifndef PACS_BRIDGE_FHIR_SUBSCRIPTION_MANAGER_H
#define PACS_BRIDGE_FHIR_SUBSCRIPTION_MANAGER_H

/**
 * @file subscription_manager.h
 * @brief FHIR Subscription manager implementation
 *
 * Manages FHIR Subscription resources and handles event-based notifications
 * when studies become available or reports are completed.
 *
 * @see https://hl7.org/fhir/R4/subscription.html
 * @see https://github.com/kcenon/pacs_bridge/issues/36
 */

#include "fhir_resource.h"
#include "fhir_types.h"
#include "resource_handler.h"
#include "subscription_resource.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// IExecutor interface for task execution (when available)
#ifndef PACS_BRIDGE_STANDALONE_BUILD
#include <kcenon/common/interfaces/executor_interface.h>
#endif

namespace pacs::bridge::fhir {

// =============================================================================
// Subscription Storage Interface
// =============================================================================

/**
 * @brief Subscription storage interface
 *
 * Abstracts the subscription storage to allow different backend implementations
 * (in-memory cache, database, etc.).
 */
class subscription_storage {
public:
    virtual ~subscription_storage() = default;

    /**
     * @brief Store a subscription
     * @param id Subscription ID
     * @param subscription Subscription data
     * @return true on success
     */
    [[nodiscard]] virtual bool store(
        const std::string& id,
        const subscription_resource& subscription) = 0;

    /**
     * @brief Get subscription by ID
     * @param id Subscription ID
     * @return Subscription or nullptr if not found
     */
    [[nodiscard]] virtual std::unique_ptr<subscription_resource> get(
        const std::string& id) const = 0;

    /**
     * @brief Update a subscription
     * @param id Subscription ID
     * @param subscription Updated subscription data
     * @return true on success
     */
    [[nodiscard]] virtual bool update(
        const std::string& id,
        const subscription_resource& subscription) = 0;

    /**
     * @brief Remove a subscription
     * @param id Subscription ID
     * @return true if removed
     */
    [[nodiscard]] virtual bool remove(const std::string& id) = 0;

    /**
     * @brief Get all active subscriptions
     * @return List of active subscriptions
     */
    [[nodiscard]] virtual std::vector<std::unique_ptr<subscription_resource>>
    get_active() const = 0;

    /**
     * @brief Get subscriptions by criteria resource type
     * @param resource_type Resource type to filter by
     * @return List of matching subscriptions
     */
    [[nodiscard]] virtual std::vector<std::unique_ptr<subscription_resource>>
    get_by_resource_type(const std::string& resource_type) const = 0;

    /**
     * @brief Get all subscription IDs
     * @return List of subscription IDs
     */
    [[nodiscard]] virtual std::vector<std::string> keys() const = 0;

    /**
     * @brief Clear all subscriptions
     */
    virtual void clear() = 0;
};

/**
 * @brief In-memory subscription storage implementation
 */
class in_memory_subscription_storage : public subscription_storage {
public:
    in_memory_subscription_storage();
    ~in_memory_subscription_storage() override;

    // Non-copyable, non-movable (contains mutex)
    in_memory_subscription_storage(const in_memory_subscription_storage&) = delete;
    in_memory_subscription_storage& operator=(const in_memory_subscription_storage&) = delete;
    in_memory_subscription_storage(in_memory_subscription_storage&&) = delete;
    in_memory_subscription_storage& operator=(in_memory_subscription_storage&&) = delete;

    [[nodiscard]] bool store(
        const std::string& id,
        const subscription_resource& subscription) override;
    [[nodiscard]] std::unique_ptr<subscription_resource> get(
        const std::string& id) const override;
    [[nodiscard]] bool update(
        const std::string& id,
        const subscription_resource& subscription) override;
    [[nodiscard]] bool remove(const std::string& id) override;
    [[nodiscard]] std::vector<std::unique_ptr<subscription_resource>>
    get_active() const override;
    [[nodiscard]] std::vector<std::unique_ptr<subscription_resource>>
    get_by_resource_type(const std::string& resource_type) const override;
    [[nodiscard]] std::vector<std::string> keys() const override;
    void clear() override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Notification Delivery Interface
// =============================================================================

/**
 * @brief HTTP client interface for REST-hook delivery
 */
class http_client {
public:
    virtual ~http_client() = default;

    /**
     * @brief HTTP response from delivery attempt
     */
    struct response {
        int status_code = 0;
        std::string body;
        std::map<std::string, std::string> headers;
        std::optional<std::string> error;
    };

    /**
     * @brief Send HTTP POST request
     * @param url Target URL
     * @param body Request body
     * @param headers Request headers
     * @param timeout Request timeout
     * @return Response or error
     */
    [[nodiscard]] virtual response post(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers,
        std::chrono::milliseconds timeout) = 0;
};

/**
 * @brief Create default HTTP client implementation
 */
[[nodiscard]] std::unique_ptr<http_client> create_http_client();

/**
 * @brief Notification delivery configuration
 */
struct delivery_config {
    /** Maximum number of retry attempts */
    uint32_t max_retries = 3;

    /** Initial retry delay (doubles with each attempt) */
    std::chrono::seconds initial_retry_delay{5};

    /** Maximum retry delay */
    std::chrono::seconds max_retry_delay{300};

    /** Request timeout */
    std::chrono::milliseconds request_timeout{30000};

    /** Enable delivery (can be disabled for testing) */
    bool enabled = true;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    /** Optional executor for delivery and retry task execution (nullptr = use internal std::thread) */
    std::shared_ptr<kcenon::common::interfaces::IExecutor> executor;
#endif
};

// =============================================================================
// Subscription Manager
// =============================================================================

/**
 * @brief Event callback for subscription notifications
 */
using subscription_event_callback = std::function<void(
    const std::string& subscription_id,
    const fhir_resource& resource,
    delivery_status status,
    const std::optional<std::string>& error)>;

/**
 * @brief Subscription manager statistics
 */
struct subscription_manager_stats {
    size_t active_subscriptions = 0;
    size_t total_notifications_sent = 0;
    size_t successful_deliveries = 0;
    size_t failed_deliveries = 0;
    size_t pending_notifications = 0;
};

/**
 * @brief FHIR Subscription Manager
 *
 * Manages FHIR Subscription resources and handles event-based notifications.
 * Supports REST-hook channel type for delivering notifications.
 *
 * @example Basic Usage
 * ```cpp
 * auto storage = std::make_shared<in_memory_subscription_storage>();
 * subscription_manager manager(storage);
 *
 * // Start the manager
 * manager.start();
 *
 * // Create a subscription
 * subscription_resource sub;
 * sub.set_status(subscription_status::active);
 * sub.set_criteria("ImagingStudy?status=available");
 * subscription_channel channel;
 * channel.type = subscription_channel_type::rest_hook;
 * channel.endpoint = "https://emr.hospital.local/fhir-notify";
 * channel.payload = "application/fhir+json";
 * sub.set_channel(channel);
 *
 * auto result = manager.create_subscription(sub);
 * if (is_success(result)) {
 *     auto& created = get_resource(result);
 *     std::cout << "Created: " << created->id() << std::endl;
 * }
 *
 * // Notify subscribers when a study becomes available
 * imaging_study_resource study;
 * // ... populate study ...
 * manager.notify(study);
 *
 * // Stop the manager
 * manager.stop();
 * ```
 *
 * Thread-safety: All operations are thread-safe.
 */
class subscription_manager {
public:
    /**
     * @brief Constructor
     * @param storage Subscription storage backend
     * @param delivery_cfg Delivery configuration
     */
    explicit subscription_manager(
        std::shared_ptr<subscription_storage> storage,
        const delivery_config& delivery_cfg = delivery_config{});

    /**
     * @brief Constructor with custom HTTP client
     * @param storage Subscription storage backend
     * @param client HTTP client for REST-hook delivery
     * @param delivery_cfg Delivery configuration
     */
    subscription_manager(
        std::shared_ptr<subscription_storage> storage,
        std::unique_ptr<http_client> client,
        const delivery_config& delivery_cfg = delivery_config{});

    /**
     * @brief Destructor
     */
    ~subscription_manager();

    // Non-copyable, non-movable
    subscription_manager(const subscription_manager&) = delete;
    subscription_manager& operator=(const subscription_manager&) = delete;
    subscription_manager(subscription_manager&&) = delete;
    subscription_manager& operator=(subscription_manager&&) = delete;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Start the subscription manager
     *
     * Starts background threads for notification delivery and retry processing.
     *
     * @return true if started successfully
     */
    [[nodiscard]] bool start();

    /**
     * @brief Stop the subscription manager
     *
     * Stops background threads and waits for pending deliveries to complete.
     *
     * @param wait_for_pending Wait for pending notifications to be delivered
     */
    void stop(bool wait_for_pending = true);

    /**
     * @brief Check if manager is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // CRUD Operations
    // =========================================================================

    /**
     * @brief Create a new subscription
     *
     * @param subscription Subscription to create
     * @return Created subscription with assigned ID, or OperationOutcome
     */
    [[nodiscard]] resource_result<std::unique_ptr<subscription_resource>>
    create_subscription(const subscription_resource& subscription);

    /**
     * @brief Get a subscription by ID
     *
     * @param id Subscription ID
     * @return Subscription or OperationOutcome
     */
    [[nodiscard]] resource_result<std::unique_ptr<subscription_resource>>
    get_subscription(const std::string& id);

    /**
     * @brief Update a subscription
     *
     * @param id Subscription ID
     * @param subscription Updated subscription
     * @return Updated subscription or OperationOutcome
     */
    [[nodiscard]] resource_result<std::unique_ptr<subscription_resource>>
    update_subscription(const std::string& id,
                       const subscription_resource& subscription);

    /**
     * @brief Delete a subscription
     *
     * @param id Subscription ID
     * @return Empty outcome on success, or error outcome
     */
    [[nodiscard]] resource_result<std::monostate>
    delete_subscription(const std::string& id);

    /**
     * @brief List all subscriptions
     *
     * @return List of all subscriptions
     */
    [[nodiscard]] std::vector<std::unique_ptr<subscription_resource>>
    list_subscriptions() const;

    // =========================================================================
    // Notification
    // =========================================================================

    /**
     * @brief Notify subscribers of a resource event
     *
     * Finds all active subscriptions whose criteria match the resource
     * and delivers notifications via REST-hook.
     *
     * @param resource Resource that triggered the event
     */
    void notify(const fhir_resource& resource);

    /**
     * @brief Set event callback
     *
     * @param callback Callback to invoke on notification events
     */
    void set_event_callback(subscription_event_callback callback);

    /**
     * @brief Clear event callback
     */
    void clear_event_callback();

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get manager statistics
     */
    [[nodiscard]] subscription_manager_stats get_statistics() const;

    /**
     * @brief Get delivery configuration
     */
    [[nodiscard]] const delivery_config& config() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// Subscription Resource Handler
// =============================================================================

/**
 * @brief Handler for FHIR Subscription resource operations
 *
 * Implements CRUD operations for Subscription resources.
 *
 * Supported operations:
 * - create: POST /Subscription
 * - read: GET /Subscription/{id}
 * - update: PUT /Subscription/{id}
 * - delete: DELETE /Subscription/{id}
 * - search: GET /Subscription?status=xxx
 *
 * @example Basic Usage
 * ```cpp
 * auto manager = std::make_shared<subscription_manager>(storage);
 * auto handler = std::make_shared<subscription_handler>(manager);
 *
 * // Register with FHIR server
 * server.register_handler(handler);
 * ```
 *
 * Thread-safety: All operations are thread-safe.
 */
class subscription_handler : public resource_handler {
public:
    /**
     * @brief Constructor
     * @param manager Subscription manager
     */
    explicit subscription_handler(
        std::shared_ptr<subscription_manager> manager);

    /**
     * @brief Destructor
     */
    ~subscription_handler() override;

    // Non-copyable, movable
    subscription_handler(const subscription_handler&) = delete;
    subscription_handler& operator=(const subscription_handler&) = delete;
    subscription_handler(subscription_handler&&) noexcept;
    subscription_handler& operator=(subscription_handler&&) noexcept;

    // =========================================================================
    // resource_handler Interface
    // =========================================================================

    /**
     * @brief Get handled resource type
     */
    [[nodiscard]] resource_type handled_type() const noexcept override;

    /**
     * @brief Get resource type name
     */
    [[nodiscard]] std::string_view type_name() const noexcept override;

    /**
     * @brief Read subscription by ID
     */
    [[nodiscard]] resource_result<std::unique_ptr<fhir_resource>> read(
        const std::string& id) override;

    /**
     * @brief Create a new subscription
     */
    [[nodiscard]] resource_result<std::unique_ptr<fhir_resource>> create(
        std::unique_ptr<fhir_resource> resource) override;

    /**
     * @brief Update a subscription
     */
    [[nodiscard]] resource_result<std::unique_ptr<fhir_resource>> update(
        const std::string& id,
        std::unique_ptr<fhir_resource> resource) override;

    /**
     * @brief Delete a subscription
     */
    [[nodiscard]] resource_result<std::monostate> delete_resource(
        const std::string& id) override;

    /**
     * @brief Search for subscriptions
     *
     * Supported search parameters:
     * - _id: Resource ID
     * - status: Subscription status
     * - criteria: Subscription criteria
     */
    [[nodiscard]] resource_result<search_result> search(
        const std::map<std::string, std::string>& params,
        const pagination_params& pagination) override;

    /**
     * @brief Get supported search parameters
     */
    [[nodiscard]] std::map<std::string, std::string>
    supported_search_params() const override;

    /**
     * @brief Check if interaction is supported
     */
    [[nodiscard]] bool supports_interaction(
        interaction_type type) const noexcept override;

    /**
     * @brief Get supported interactions
     */
    [[nodiscard]] std::vector<interaction_type>
    supported_interactions() const override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::fhir

#endif  // PACS_BRIDGE_FHIR_SUBSCRIPTION_MANAGER_H
