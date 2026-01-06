/**
 * @file subscription_manager.cpp
 * @brief FHIR Subscription manager implementation
 *
 * Implements the Subscription manager and handler for FHIR R4.
 *
 * @see include/pacs/bridge/fhir/subscription_manager.h
 * @see https://github.com/kcenon/pacs_bridge/issues/36
 */

#include "pacs/bridge/fhir/subscription_manager.h"

#include "pacs/bridge/integration/network_adapter.h"

#include <atomic>
#include <condition_variable>
#include <future>
#include <iomanip>
#include <mutex>
#include <queue>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace pacs::bridge::fhir {

// =============================================================================
// IExecutor Job Implementations (when available)
// =============================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD

/**
 * @brief Job implementation for subscription delivery execution
 *
 * Wraps a single delivery iteration for execution via IExecutor.
 */
class subscription_delivery_job : public kcenon::common::interfaces::IJob {
public:
    explicit subscription_delivery_job(std::function<void()> work_func)
        : work_func_(std::move(work_func)) {}

    kcenon::common::VoidResult execute() override {
        if (work_func_) {
            work_func_();
        }
        return {};
    }

    std::string get_name() const override { return "subscription_delivery"; }

private:
    std::function<void()> work_func_;
};

/**
 * @brief Job implementation for subscription retry execution
 *
 * Wraps periodic retry operations for execution via IExecutor.
 */
class subscription_retry_job : public kcenon::common::interfaces::IJob {
public:
    explicit subscription_retry_job(std::function<void()> retry_func)
        : retry_func_(std::move(retry_func)) {}

    kcenon::common::VoidResult execute() override {
        if (retry_func_) {
            retry_func_();
        }
        return {};
    }

    std::string get_name() const override { return "subscription_retry"; }

private:
    std::function<void()> retry_func_;
};

#endif  // PACS_BRIDGE_STANDALONE_BUILD

// =============================================================================
// Utility Functions
// =============================================================================

namespace {

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << dis(gen) << "-";
    ss << std::setw(4) << (dis(gen) & 0xFFFF) << "-";
    ss << std::setw(4) << ((dis(gen) & 0x0FFF) | 0x4000) << "-";
    ss << std::setw(4) << ((dis(gen) & 0x3FFF) | 0x8000) << "-";
    ss << std::setw(8) << dis(gen) << std::setw(4) << (dis(gen) & 0xFFFF);
    return ss.str();
}

std::string json_escape(std::string_view input) {
    std::string result;
    result.reserve(input.size() + 10);

    for (char c : input) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x",
                             static_cast<unsigned int>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

}  // namespace

// =============================================================================
// In-Memory Subscription Storage Implementation
// =============================================================================

class in_memory_subscription_storage::impl {
public:
    mutable std::shared_mutex mutex;
    std::unordered_map<std::string, std::string> subscriptions;  // id -> JSON
};

in_memory_subscription_storage::in_memory_subscription_storage()
    : pimpl_(std::make_unique<impl>()) {}

in_memory_subscription_storage::~in_memory_subscription_storage() = default;

bool in_memory_subscription_storage::store(
    const std::string& id,
    const subscription_resource& subscription) {
    std::unique_lock lock(pimpl_->mutex);
    pimpl_->subscriptions[id] = subscription.to_json();
    return true;
}

std::unique_ptr<subscription_resource> in_memory_subscription_storage::get(
    const std::string& id) const {
    std::shared_lock lock(pimpl_->mutex);
    auto it = pimpl_->subscriptions.find(id);
    if (it == pimpl_->subscriptions.end()) {
        return nullptr;
    }
    return subscription_resource::from_json(it->second);
}

bool in_memory_subscription_storage::update(
    const std::string& id,
    const subscription_resource& subscription) {
    std::unique_lock lock(pimpl_->mutex);
    auto it = pimpl_->subscriptions.find(id);
    if (it == pimpl_->subscriptions.end()) {
        return false;
    }
    it->second = subscription.to_json();
    return true;
}

bool in_memory_subscription_storage::remove(const std::string& id) {
    std::unique_lock lock(pimpl_->mutex);
    return pimpl_->subscriptions.erase(id) > 0;
}

std::vector<std::unique_ptr<subscription_resource>>
in_memory_subscription_storage::get_active() const {
    std::vector<std::unique_ptr<subscription_resource>> result;
    std::shared_lock lock(pimpl_->mutex);

    for (const auto& [id, json] : pimpl_->subscriptions) {
        auto sub = subscription_resource::from_json(json);
        if (sub && sub->status() == subscription_status::active) {
            result.push_back(std::move(sub));
        }
    }

    return result;
}

std::vector<std::unique_ptr<subscription_resource>>
in_memory_subscription_storage::get_by_resource_type(
    const std::string& resource_type) const {
    std::vector<std::unique_ptr<subscription_resource>> result;
    std::shared_lock lock(pimpl_->mutex);

    for (const auto& [id, json] : pimpl_->subscriptions) {
        auto sub = subscription_resource::from_json(json);
        if (sub && sub->status() == subscription_status::active) {
            auto criteria = parse_subscription_criteria(sub->criteria());
            if (criteria && criteria->resource_type == resource_type) {
                result.push_back(std::move(sub));
            }
        }
    }

    return result;
}

std::vector<std::string> in_memory_subscription_storage::keys() const {
    std::vector<std::string> result;
    std::shared_lock lock(pimpl_->mutex);
    result.reserve(pimpl_->subscriptions.size());

    for (const auto& [id, json] : pimpl_->subscriptions) {
        result.push_back(id);
    }

    return result;
}

void in_memory_subscription_storage::clear() {
    std::unique_lock lock(pimpl_->mutex);
    pimpl_->subscriptions.clear();
}

// =============================================================================
// HTTP Client Implementation
// =============================================================================

/**
 * @brief Stub HTTP client that always returns success
 *
 * This is a placeholder implementation. For production use, inject
 * a real HTTP client implementation via the subscription_manager
 * constructor that takes an http_client parameter.
 *
 * To implement a real HTTP client, you can:
 * 1. Use libcurl
 * 2. Use Boost.Beast
 * 3. Implement using raw sockets with the network_adapter when available
 */
class stub_http_client : public http_client {
public:
    response post(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers,
        std::chrono::milliseconds timeout) override {
        (void)url;
        (void)body;
        (void)headers;
        (void)timeout;

        response resp;
        // Return success for now - actual delivery requires
        // a real HTTP client implementation
        resp.status_code = 200;
        resp.body = "{}";
        return resp;
    }
};

std::unique_ptr<http_client> create_http_client() {
    return std::make_unique<stub_http_client>();
}

// =============================================================================
// Subscription Manager Implementation
// =============================================================================

struct pending_notification {
    std::string subscription_id;
    std::string resource_json;
    std::string resource_type;
    std::string resource_id;
    std::chrono::system_clock::time_point created_at;
    uint32_t retry_count = 0;
    std::chrono::system_clock::time_point next_retry_at;
};

class subscription_manager::impl {
public:
    impl(std::shared_ptr<subscription_storage> storage,
         std::unique_ptr<http_client> client,
         const delivery_config& cfg)
        : storage_(std::move(storage)),
          client_(std::move(client)),
          config_(cfg),
          running_(false) {}

    ~impl() {
        stop(false);
    }

    bool start() {
        if (running_.exchange(true)) {
            return false;  // Already running
        }

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        if (config_.executor) {
            schedule_delivery_job();
            schedule_retry_job();
        } else {
            delivery_thread_ = std::thread([this] { delivery_loop(); });
            retry_thread_ = std::thread([this] { retry_loop(); });
        }
#else
        delivery_thread_ = std::thread([this] { delivery_loop(); });
        retry_thread_ = std::thread([this] { retry_loop(); });
#endif

        return true;
    }

    void stop(bool wait_for_pending) {
        if (!running_.exchange(false)) {
            return;  // Not running
        }

        if (wait_for_pending) {
            // Wait for pending notifications
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait_for(lock, std::chrono::seconds(10), [this] {
                return pending_queue_.empty();
            });
        }

        queue_cv_.notify_all();
        retry_cv_.notify_all();

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        // Wait for executor-based jobs to complete
        if (config_.executor) {
            if (delivery_future_.valid()) {
                delivery_future_.wait_for(std::chrono::seconds{5});
            }
            if (retry_future_.valid()) {
                retry_future_.wait_for(std::chrono::seconds{5});
            }
        }
#endif

        if (delivery_thread_.joinable()) {
            delivery_thread_.join();
        }
        if (retry_thread_.joinable()) {
            retry_thread_.join();
        }
    }

    bool is_running() const noexcept {
        return running_.load();
    }

    resource_result<std::unique_ptr<subscription_resource>> create_subscription(
        const subscription_resource& subscription) {
        auto new_sub = std::make_unique<subscription_resource>();

        // Copy properties
        new_sub->set_status(subscription.status());
        new_sub->set_criteria(subscription.criteria());
        new_sub->set_channel(subscription.channel());

        if (subscription.reason().has_value()) {
            new_sub->set_reason(*subscription.reason());
        }
        if (subscription.end().has_value()) {
            new_sub->set_end(*subscription.end());
        }
        for (const auto& contact : subscription.contacts()) {
            new_sub->add_contact(contact);
        }

        // Generate ID
        std::string id = generate_uuid();
        new_sub->set_id(id);
        new_sub->set_version_id("1");

        // Validate
        if (!new_sub->validate()) {
            return operation_outcome::validation_error(
                "Invalid subscription resource: missing required fields");
        }

        // Store
        if (!storage_->store(id, *new_sub)) {
            return operation_outcome::internal_error(
                "Failed to store subscription");
        }

        // Update stats
        if (new_sub->status() == subscription_status::active) {
            ++stats_.active_subscriptions;
        }

        return new_sub;
    }

    resource_result<std::unique_ptr<subscription_resource>> get_subscription(
        const std::string& id) {
        auto sub = storage_->get(id);
        if (!sub) {
            return operation_outcome::not_found("Subscription", id);
        }
        return sub;
    }

    resource_result<std::unique_ptr<subscription_resource>> update_subscription(
        const std::string& id,
        const subscription_resource& subscription) {
        auto existing = storage_->get(id);
        if (!existing) {
            return operation_outcome::not_found("Subscription", id);
        }

        auto updated = std::make_unique<subscription_resource>();

        // Copy properties
        updated->set_id(id);
        updated->set_status(subscription.status());
        updated->set_criteria(subscription.criteria());
        updated->set_channel(subscription.channel());

        if (subscription.reason().has_value()) {
            updated->set_reason(*subscription.reason());
        }
        if (subscription.end().has_value()) {
            updated->set_end(*subscription.end());
        }
        if (subscription.error().has_value()) {
            updated->set_error(*subscription.error());
        }
        for (const auto& contact : subscription.contacts()) {
            updated->add_contact(contact);
        }

        // Update version
        std::string version_id = existing->version_id();
        if (version_id.empty()) {
            version_id = "1";
        }
        int version = std::stoi(version_id) + 1;
        updated->set_version_id(std::to_string(version));

        // Validate
        if (!updated->validate()) {
            return operation_outcome::validation_error(
                "Invalid subscription resource: missing required fields");
        }

        // Update stats
        if (existing->status() == subscription_status::active &&
            updated->status() != subscription_status::active) {
            --stats_.active_subscriptions;
        } else if (existing->status() != subscription_status::active &&
                   updated->status() == subscription_status::active) {
            ++stats_.active_subscriptions;
        }

        // Store
        if (!storage_->update(id, *updated)) {
            return operation_outcome::internal_error(
                "Failed to update subscription");
        }

        return updated;
    }

    resource_result<std::monostate> delete_subscription(const std::string& id) {
        auto existing = storage_->get(id);
        if (!existing) {
            return operation_outcome::not_found("Subscription", id);
        }

        if (existing->status() == subscription_status::active) {
            --stats_.active_subscriptions;
        }

        if (!storage_->remove(id)) {
            return operation_outcome::internal_error(
                "Failed to delete subscription");
        }

        return std::monostate{};
    }

    std::vector<std::unique_ptr<subscription_resource>> list_subscriptions() const {
        std::vector<std::unique_ptr<subscription_resource>> result;
        auto keys = storage_->keys();

        for (const auto& key : keys) {
            auto sub = storage_->get(key);
            if (sub) {
                result.push_back(std::move(sub));
            }
        }

        return result;
    }

    void notify(const fhir_resource& resource) {
        if (!config_.enabled || !running_.load()) {
            return;
        }

        // Get matching subscriptions
        auto subscriptions = storage_->get_by_resource_type(resource.type_name());

        for (auto& sub : subscriptions) {
            auto criteria = parse_subscription_criteria(sub->criteria());
            if (!criteria || !matches_criteria(resource, *criteria)) {
                continue;
            }

            // Create pending notification
            pending_notification notification;
            notification.subscription_id = sub->id();
            notification.resource_json = resource.to_json();
            notification.resource_type = resource.type_name();
            notification.resource_id = resource.id();
            notification.created_at = std::chrono::system_clock::now();
            notification.retry_count = 0;
            notification.next_retry_at = notification.created_at;

            // Enqueue
            {
                std::lock_guard lock(queue_mutex_);
                pending_queue_.push(std::move(notification));
                ++stats_.pending_notifications;
            }
            queue_cv_.notify_one();
        }
    }

    void set_event_callback(subscription_event_callback callback) {
        std::lock_guard lock(callback_mutex_);
        callback_ = std::move(callback);
    }

    void clear_event_callback() {
        std::lock_guard lock(callback_mutex_);
        callback_ = nullptr;
    }

    subscription_manager_stats get_statistics() const {
        return stats_;
    }

    const delivery_config& config() const noexcept {
        return config_;
    }

private:
    void delivery_loop() {
        while (running_.load()) {
            pending_notification notification;

            {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait(lock, [this] {
                    return !pending_queue_.empty() || !running_.load();
                });

                if (!running_.load()) {
                    break;
                }

                if (pending_queue_.empty()) {
                    continue;
                }

                notification = std::move(pending_queue_.front());
                pending_queue_.pop();
                --stats_.pending_notifications;
            }

            deliver_notification(notification);
        }
    }

    void retry_loop() {
        while (running_.load()) {
            std::unique_lock lock(retry_mutex_);
            retry_cv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return !running_.load();
            });

            if (!running_.load()) {
                break;
            }

            // Process retry queue
            auto now = std::chrono::system_clock::now();
            std::vector<pending_notification> to_retry;

            {
                std::lock_guard retry_lock(retry_queue_mutex_);
                auto it = retry_queue_.begin();
                while (it != retry_queue_.end()) {
                    if (it->next_retry_at <= now) {
                        to_retry.push_back(std::move(*it));
                        it = retry_queue_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            // Re-queue for delivery
            for (auto& notification : to_retry) {
                {
                    std::lock_guard lock(queue_mutex_);
                    pending_queue_.push(std::move(notification));
                    ++stats_.pending_notifications;
                }
                queue_cv_.notify_one();
            }
        }
    }

    void deliver_notification(pending_notification& notification) {
        auto sub = storage_->get(notification.subscription_id);
        if (!sub || sub->status() != subscription_status::active) {
            return;
        }

        const auto& channel = sub->channel();
        if (channel.type != subscription_channel_type::rest_hook) {
            return;
        }

        // Build headers
        std::map<std::string, std::string> headers;
        headers["Content-Type"] = channel.payload.value_or("application/fhir+json");

        for (const auto& header : channel.header) {
            auto colon_pos = header.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = header.substr(0, colon_pos);
                std::string value = header.substr(colon_pos + 1);
                // Trim leading whitespace from value
                while (!value.empty() && value[0] == ' ') {
                    value = value.substr(1);
                }
                headers[key] = value;
            }
        }

        // Deliver
        ++stats_.total_notifications_sent;

        auto response = client_->post(
            channel.endpoint,
            notification.resource_json,
            headers,
            config_.request_timeout);

        bool success = (response.status_code >= 200 && response.status_code < 300);

        if (success) {
            ++stats_.successful_deliveries;

            // Invoke callback
            std::lock_guard lock(callback_mutex_);
            if (callback_) {
                callback_(
                    notification.subscription_id,
                    *subscription_resource::from_json(notification.resource_json),
                    delivery_status::completed,
                    std::nullopt);
            }
        } else {
            // Handle failure
            ++notification.retry_count;

            std::string error_msg = response.error.value_or(
                "HTTP " + std::to_string(response.status_code));

            if (notification.retry_count < config_.max_retries) {
                // Schedule retry
                auto delay = std::min(
                    config_.initial_retry_delay * (1 << notification.retry_count),
                    config_.max_retry_delay);
                notification.next_retry_at =
                    std::chrono::system_clock::now() + delay;

                {
                    std::lock_guard lock(retry_queue_mutex_);
                    retry_queue_.push_back(std::move(notification));
                }
            } else {
                // Max retries reached
                ++stats_.failed_deliveries;

                // Invoke callback
                std::lock_guard lock(callback_mutex_);
                if (callback_) {
                    callback_(
                        notification.subscription_id,
                        *subscription_resource::from_json(notification.resource_json),
                        delivery_status::abandoned,
                        error_msg);
                }

                // Optionally set subscription to error state
                auto existing = storage_->get(notification.subscription_id);
                if (existing) {
                    existing->set_status(subscription_status::error);
                    existing->set_error(error_msg);
                    storage_->update(notification.subscription_id, *existing);
                    --stats_.active_subscriptions;
                }
            }
        }
    }

    std::shared_ptr<subscription_storage> storage_;
    std::unique_ptr<http_client> client_;
    delivery_config config_;

    std::atomic<bool> running_;
    std::thread delivery_thread_;
    std::thread retry_thread_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<pending_notification> pending_queue_;

    std::mutex retry_mutex_;
    std::condition_variable retry_cv_;
    std::mutex retry_queue_mutex_;
    std::vector<pending_notification> retry_queue_;

    std::mutex callback_mutex_;
    subscription_event_callback callback_;

    mutable subscription_manager_stats stats_;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    // Futures for tracking executor-based jobs
    std::future<void> delivery_future_;
    std::future<void> retry_future_;

    /**
     * @brief Schedule delivery job using IExecutor
     *
     * Processes notifications and reschedules itself for continuous operation.
     */
    void schedule_delivery_job() {
        if (!running_.load() || !config_.executor) {
            return;
        }

        auto job = std::make_unique<subscription_delivery_job>([this]() {
            if (!running_.load()) {
                return;
            }

            pending_notification notification;
            bool has_notification = false;

            {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait_for(lock, std::chrono::milliseconds{100}, [this] {
                    return !pending_queue_.empty() || !running_.load();
                });

                if (!running_.load()) {
                    return;
                }

                if (!pending_queue_.empty()) {
                    notification = std::move(pending_queue_.front());
                    pending_queue_.pop();
                    --stats_.pending_notifications;
                    has_notification = true;
                }
            }

            if (has_notification) {
                deliver_notification(notification);
            }

            // Reschedule for next iteration
            schedule_delivery_job();
        });

        auto result = config_.executor->execute(std::move(job));
        if (result.is_ok()) {
            delivery_future_ = std::move(result.value());
        }
    }

    /**
     * @brief Schedule retry job using IExecutor with delayed execution
     *
     * Processes retry queue and reschedules itself.
     */
    void schedule_retry_job() {
        if (!running_.load() || !config_.executor) {
            return;
        }

        auto job = std::make_unique<subscription_retry_job>([this]() {
            if (!running_.load()) {
                return;
            }

            // Process retry queue
            auto now = std::chrono::system_clock::now();
            std::vector<pending_notification> to_retry;

            {
                std::lock_guard retry_lock(retry_queue_mutex_);
                auto it = retry_queue_.begin();
                while (it != retry_queue_.end()) {
                    if (it->next_retry_at <= now) {
                        to_retry.push_back(std::move(*it));
                        it = retry_queue_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            // Re-queue for delivery
            for (auto& notification : to_retry) {
                {
                    std::lock_guard lock(queue_mutex_);
                    pending_queue_.push(std::move(notification));
                    ++stats_.pending_notifications;
                }
                queue_cv_.notify_one();
            }

            // Reschedule for next iteration
            schedule_retry_job();
        });

        auto result = config_.executor->execute_delayed(
            std::move(job), std::chrono::milliseconds{1000});
        if (result.is_ok()) {
            retry_future_ = std::move(result.value());
        }
    }
#endif
};

// =============================================================================
// Subscription Manager Public Interface
// =============================================================================

subscription_manager::subscription_manager(
    std::shared_ptr<subscription_storage> storage,
    const delivery_config& delivery_cfg)
    : impl_(std::make_unique<impl>(
          std::move(storage),
          create_http_client(),
          delivery_cfg)) {}

subscription_manager::subscription_manager(
    std::shared_ptr<subscription_storage> storage,
    std::unique_ptr<http_client> client,
    const delivery_config& delivery_cfg)
    : impl_(std::make_unique<impl>(
          std::move(storage),
          std::move(client),
          delivery_cfg)) {}

subscription_manager::~subscription_manager() = default;

bool subscription_manager::start() {
    return impl_->start();
}

void subscription_manager::stop(bool wait_for_pending) {
    impl_->stop(wait_for_pending);
}

bool subscription_manager::is_running() const noexcept {
    return impl_->is_running();
}

resource_result<std::unique_ptr<subscription_resource>>
subscription_manager::create_subscription(const subscription_resource& subscription) {
    return impl_->create_subscription(subscription);
}

resource_result<std::unique_ptr<subscription_resource>>
subscription_manager::get_subscription(const std::string& id) {
    return impl_->get_subscription(id);
}

resource_result<std::unique_ptr<subscription_resource>>
subscription_manager::update_subscription(
    const std::string& id,
    const subscription_resource& subscription) {
    return impl_->update_subscription(id, subscription);
}

resource_result<std::monostate>
subscription_manager::delete_subscription(const std::string& id) {
    return impl_->delete_subscription(id);
}

std::vector<std::unique_ptr<subscription_resource>>
subscription_manager::list_subscriptions() const {
    return impl_->list_subscriptions();
}

void subscription_manager::notify(const fhir_resource& resource) {
    impl_->notify(resource);
}

void subscription_manager::set_event_callback(subscription_event_callback callback) {
    impl_->set_event_callback(std::move(callback));
}

void subscription_manager::clear_event_callback() {
    impl_->clear_event_callback();
}

subscription_manager_stats subscription_manager::get_statistics() const {
    return impl_->get_statistics();
}

const delivery_config& subscription_manager::config() const noexcept {
    return impl_->config();
}

// =============================================================================
// Subscription Handler Implementation
// =============================================================================

class subscription_handler::impl {
public:
    explicit impl(std::shared_ptr<subscription_manager> manager)
        : manager_(std::move(manager)) {}

    std::shared_ptr<subscription_manager> manager_;
};

subscription_handler::subscription_handler(
    std::shared_ptr<subscription_manager> manager)
    : pimpl_(std::make_unique<impl>(std::move(manager))) {}

subscription_handler::~subscription_handler() = default;

subscription_handler::subscription_handler(
    subscription_handler&&) noexcept = default;

subscription_handler& subscription_handler::operator=(
    subscription_handler&&) noexcept = default;

resource_type subscription_handler::handled_type() const noexcept {
    return resource_type::subscription;
}

std::string_view subscription_handler::type_name() const noexcept {
    return "Subscription";
}

resource_result<std::unique_ptr<fhir_resource>> subscription_handler::read(
    const std::string& id) {
    auto result = pimpl_->manager_->get_subscription(id);
    if (is_success(result)) {
        auto& sub = std::get<std::unique_ptr<subscription_resource>>(result);
        return std::unique_ptr<fhir_resource>(sub.release());
    }
    return get_outcome(result);
}

resource_result<std::unique_ptr<fhir_resource>> subscription_handler::create(
    std::unique_ptr<fhir_resource> resource) {
    auto* subscription = dynamic_cast<subscription_resource*>(resource.get());
    if (!subscription) {
        return operation_outcome::validation_error(
            "Invalid resource type: expected Subscription");
    }

    auto result = pimpl_->manager_->create_subscription(*subscription);
    if (is_success(result)) {
        auto& sub = std::get<std::unique_ptr<subscription_resource>>(result);
        return std::unique_ptr<fhir_resource>(sub.release());
    }
    return get_outcome(result);
}

resource_result<std::unique_ptr<fhir_resource>> subscription_handler::update(
    const std::string& id,
    std::unique_ptr<fhir_resource> resource) {
    auto* subscription = dynamic_cast<subscription_resource*>(resource.get());
    if (!subscription) {
        return operation_outcome::validation_error(
            "Invalid resource type: expected Subscription");
    }

    auto result = pimpl_->manager_->update_subscription(id, *subscription);
    if (is_success(result)) {
        auto& sub = std::get<std::unique_ptr<subscription_resource>>(result);
        return std::unique_ptr<fhir_resource>(sub.release());
    }
    return get_outcome(result);
}

resource_result<std::monostate> subscription_handler::delete_resource(
    const std::string& id) {
    return pimpl_->manager_->delete_subscription(id);
}

resource_result<search_result> subscription_handler::search(
    const std::map<std::string, std::string>& params,
    const pagination_params& pagination) {
    search_result result;

    auto all_subs = pimpl_->manager_->list_subscriptions();

    // Filter based on search parameters
    std::vector<std::unique_ptr<subscription_resource>> matching;

    for (auto& sub : all_subs) {
        bool matches = true;

        for (const auto& [param_name, param_value] : params) {
            if (param_name == "_id") {
                if (sub->id() != param_value) {
                    matches = false;
                    break;
                }
            } else if (param_name == "status") {
                if (to_string(sub->status()) != param_value) {
                    matches = false;
                    break;
                }
            } else if (param_name == "criteria") {
                if (sub->criteria().find(param_value) == std::string::npos) {
                    matches = false;
                    break;
                }
            }
        }

        if (matches) {
            matching.push_back(std::move(sub));
        }
    }

    result.total = matching.size();

    // Apply pagination
    size_t start = pagination.offset;
    size_t count = pagination.count;

    if (start >= matching.size()) {
        return result;
    }

    size_t end = std::min(start + count, matching.size());

    for (size_t i = start; i < end; ++i) {
        result.entries.push_back(std::move(matching[i]));
        result.search_modes.push_back("match");
    }

    return result;
}

std::map<std::string, std::string>
subscription_handler::supported_search_params() const {
    return {{"_id", "Resource ID"},
            {"status", "Subscription status"},
            {"criteria", "Subscription criteria (partial match)"}};
}

bool subscription_handler::supports_interaction(
    interaction_type type) const noexcept {
    switch (type) {
        case interaction_type::read:
        case interaction_type::create:
        case interaction_type::update:
        case interaction_type::delete_resource:
        case interaction_type::search:
            return true;
        default:
            return false;
    }
}

std::vector<interaction_type>
subscription_handler::supported_interactions() const {
    return {interaction_type::read,
            interaction_type::create,
            interaction_type::update,
            interaction_type::delete_resource,
            interaction_type::search};
}

}  // namespace pacs::bridge::fhir
