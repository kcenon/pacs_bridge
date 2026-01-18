/**
 * @file network_adapter.cpp
 * @brief Implementation of network adapter for pacs_bridge
 *
 * Bridges network_adapter interface with network_system's messaging_client.
 * Provides two implementations:
 *   - messaging_client_adapter: wraps messaging_client for full ecosystem integration
 *   - simple_network_adapter: standalone fallback using ASIO directly
 *
 * @see include/pacs/bridge/integration/network_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/270
 */

#include "pacs/bridge/integration/network_adapter.h"

#include <kcenon/network/core/messaging_client.h>
#include <kcenon/network/core/secure_messaging_client.h>

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>

namespace pacs::bridge::integration {

// =============================================================================
// messaging_client_adapter - Wraps network_system messaging_client
// =============================================================================

/**
 * @class messaging_client_adapter
 * @brief Network adapter that wraps network_system's messaging_client
 *
 * Provides full integration with the kcenon ecosystem networking infrastructure.
 * Supports both plain TCP and TLS connections.
 */
class messaging_client_adapter : public network_adapter {
public:
    messaging_client_adapter()
        : client_(std::make_shared<kcenon::network::core::messaging_client>(
              "pacs_bridge_network")) {
        setup_callbacks();
    }

    ~messaging_client_adapter() override {
        disconnect();
    }

    [[nodiscard]] bool connect(const connection_config& config) override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (connected_.load(std::memory_order_acquire)) {
            last_error_ = "Already connected";
            return false;
        }

        if (config.host.empty()) {
            last_error_ = "Invalid configuration: empty host";
            return false;
        }

        config_ = config;

        // Use promise/future for synchronous connect
        std::promise<bool> connect_promise;
        auto connect_future = connect_promise.get_future();
        bool promise_set = false;

        client_->set_connected_callback([this, &connect_promise, &promise_set]() {
            connected_.store(true, std::memory_order_release);
            if (!promise_set) {
                promise_set = true;
                connect_promise.set_value(true);
            }
        });

        client_->set_error_callback([this, &connect_promise, &promise_set](std::error_code ec) {
            last_error_ = ec.message();
            if (!promise_set) {
                promise_set = true;
                connect_promise.set_value(false);
            }
        });

        auto result = client_->start_client(config.host, config.port);
        if (!result.is_ok()) {
            last_error_ = result.error().message;
            return false;
        }

        // Wait for connection with timeout
        auto status = connect_future.wait_for(config.connect_timeout);
        if (status == std::future_status::timeout) {
            client_->stop_client();
            last_error_ = "Connection timeout";
            return false;
        }

        return connect_future.get();
    }

    void disconnect() override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_.load(std::memory_order_acquire)) {
            return;
        }

        client_->stop_client();
        connected_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool is_connected() const noexcept override {
        return connected_.load(std::memory_order_acquire);
    }

    [[nodiscard]] int64_t send(const std::vector<uint8_t>& data) override {
        if (!is_connected()) {
            last_error_ = "Not connected";
            return -1;
        }

        // Copy data for async send
        std::vector<uint8_t> data_copy = data;
        auto result = client_->send_packet(std::move(data_copy));

        if (!result.is_ok()) {
            last_error_ = result.error().message;
            return -1;
        }

        return static_cast<int64_t>(data.size());
    }

    [[nodiscard]] std::vector<uint8_t> receive(size_t max_size) override {
        if (!is_connected()) {
            last_error_ = "Not connected";
            return {};
        }

        std::unique_lock<std::mutex> lock(receive_mutex_);

        // Wait for data with timeout
        auto deadline = std::chrono::steady_clock::now() + config_.read_timeout;
        while (receive_queue_.empty()) {
            if (receive_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                return {};  // Timeout, return empty
            }
            if (!is_connected()) {
                return {};
            }
        }

        auto data = std::move(receive_queue_.front());
        receive_queue_.pop();

        // Truncate if necessary
        if (data.size() > max_size) {
            data.resize(max_size);
        }

        return data;
    }

    [[nodiscard]] std::string last_error() const override {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return last_error_;
    }

private:
    void setup_callbacks() {
        client_->set_receive_callback([this](const std::vector<uint8_t>& data) {
            std::lock_guard<std::mutex> lock(receive_mutex_);
            receive_queue_.push(data);
            receive_cv_.notify_one();
        });

        client_->set_disconnected_callback([this]() {
            connected_.store(false, std::memory_order_release);
            receive_cv_.notify_all();
        });

        client_->set_error_callback([this](std::error_code ec) {
            std::lock_guard<std::mutex> lock(error_mutex_);
            last_error_ = ec.message();
        });
    }

    std::shared_ptr<kcenon::network::core::messaging_client> client_;
    connection_config config_;
    std::atomic<bool> connected_{false};

    mutable std::mutex mutex_;
    mutable std::mutex error_mutex_;
    mutable std::string last_error_;

    std::mutex receive_mutex_;
    std::condition_variable receive_cv_;
    std::queue<std::vector<uint8_t>> receive_queue_;
};

// =============================================================================
// secure_messaging_client_adapter - TLS-enabled adapter
// =============================================================================

/**
 * @class secure_messaging_client_adapter
 * @brief Network adapter with TLS support
 *
 * Wraps network_system's secure_messaging_client for encrypted connections.
 */
class secure_messaging_client_adapter : public network_adapter {
public:
    explicit secure_messaging_client_adapter(bool verify_cert = true)
        : client_(std::make_shared<kcenon::network::core::secure_messaging_client>(
              "pacs_bridge_secure_network", verify_cert)) {
        setup_callbacks();
    }

    ~secure_messaging_client_adapter() override {
        disconnect();
    }

    [[nodiscard]] bool connect(const connection_config& config) override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (connected_.load(std::memory_order_acquire)) {
            last_error_ = "Already connected";
            return false;
        }

        if (config.host.empty()) {
            last_error_ = "Invalid configuration: empty host";
            return false;
        }

        config_ = config;

        // Use promise/future for synchronous connect
        std::promise<bool> connect_promise;
        auto connect_future = connect_promise.get_future();
        bool promise_set = false;

        client_->set_connected_callback([this, &connect_promise, &promise_set]() {
            connected_.store(true, std::memory_order_release);
            if (!promise_set) {
                promise_set = true;
                connect_promise.set_value(true);
            }
        });

        client_->set_error_callback([this, &connect_promise, &promise_set](std::error_code ec) {
            last_error_ = ec.message();
            if (!promise_set) {
                promise_set = true;
                connect_promise.set_value(false);
            }
        });

        auto result = client_->start_client(config.host, config.port);
        if (!result.is_ok()) {
            last_error_ = result.error().message;
            return false;
        }

        // Wait for connection with timeout
        auto status = connect_future.wait_for(config.connect_timeout);
        if (status == std::future_status::timeout) {
            client_->stop_client();
            last_error_ = "TLS connection timeout";
            return false;
        }

        return connect_future.get();
    }

    void disconnect() override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_.load(std::memory_order_acquire)) {
            return;
        }

        client_->stop_client();
        connected_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool is_connected() const noexcept override {
        return connected_.load(std::memory_order_acquire);
    }

    [[nodiscard]] int64_t send(const std::vector<uint8_t>& data) override {
        if (!is_connected()) {
            last_error_ = "Not connected";
            return -1;
        }

        std::vector<uint8_t> data_copy = data;
        auto result = client_->send_packet(std::move(data_copy));

        if (!result.is_ok()) {
            last_error_ = result.error().message;
            return -1;
        }

        return static_cast<int64_t>(data.size());
    }

    [[nodiscard]] std::vector<uint8_t> receive(size_t max_size) override {
        if (!is_connected()) {
            last_error_ = "Not connected";
            return {};
        }

        std::unique_lock<std::mutex> lock(receive_mutex_);

        auto deadline = std::chrono::steady_clock::now() + config_.read_timeout;
        while (receive_queue_.empty()) {
            if (receive_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                return {};
            }
            if (!is_connected()) {
                return {};
            }
        }

        auto data = std::move(receive_queue_.front());
        receive_queue_.pop();

        if (data.size() > max_size) {
            data.resize(max_size);
        }

        return data;
    }

    [[nodiscard]] std::string last_error() const override {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return last_error_;
    }

private:
    void setup_callbacks() {
        client_->set_receive_callback([this](const std::vector<uint8_t>& data) {
            std::lock_guard<std::mutex> lock(receive_mutex_);
            receive_queue_.push(data);
            receive_cv_.notify_one();
        });

        client_->set_disconnected_callback([this]() {
            connected_.store(false, std::memory_order_release);
            receive_cv_.notify_all();
        });

        client_->set_error_callback([this](std::error_code ec) {
            std::lock_guard<std::mutex> lock(error_mutex_);
            last_error_ = ec.message();
        });
    }

    std::shared_ptr<kcenon::network::core::secure_messaging_client> client_;
    connection_config config_;
    std::atomic<bool> connected_{false};

    mutable std::mutex mutex_;
    mutable std::mutex error_mutex_;
    mutable std::string last_error_;

    std::mutex receive_mutex_;
    std::condition_variable receive_cv_;
    std::queue<std::vector<uint8_t>> receive_queue_;
};

// =============================================================================
// Factory Function
// =============================================================================

std::unique_ptr<network_adapter> create_network_adapter() {
    return std::make_unique<messaging_client_adapter>();
}

std::unique_ptr<network_adapter> create_network_adapter(bool use_tls, bool verify_cert) {
    if (use_tls) {
        return std::make_unique<secure_messaging_client_adapter>(verify_cert);
    }
    return std::make_unique<messaging_client_adapter>();
}

}  // namespace pacs::bridge::integration
