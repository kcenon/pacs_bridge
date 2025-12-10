/**
 * @file admin_server.cpp
 * @brief Administrative HTTP server implementation
 *
 * Provides HTTP endpoints for configuration hot-reload and runtime
 * management operations.
 *
 * @see include/pacs/bridge/config/admin_server.h
 * @see https://github.com/kcenon/pacs_bridge/issues/39
 */

#include "pacs/bridge/config/admin_server.h"

#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>

namespace pacs::bridge::config {

// =============================================================================
// Admin Server Implementation
// =============================================================================

class admin_server::impl {
public:
    impl(config_manager& manager, const config& cfg)
        : manager_(manager), config_(cfg) {}

    ~impl() { stop(true); }

    bool start() {
        if (running_.exchange(true)) {
            return false;  // Already running
        }

        // In production, this would start an actual HTTP server
        // using network_system. For now, the server is ready to
        // handle requests via handle_request().
        return true;
    }

    void stop(bool wait_for_connections) {
        if (!running_.exchange(false)) {
            return;  // Not running
        }

        if (wait_for_connections) {
            // Wait for active connections to complete
            while (stats_.active_connections > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    [[nodiscard]] bool is_running() const noexcept { return running_; }

    [[nodiscard]] uint16_t port() const noexcept { return config_.port; }

    [[nodiscard]] std::string base_path() const { return config_.base_path; }

    [[nodiscard]] std::string reload_url() const {
        std::ostringstream url;
        url << "http://" << config_.bind_address << ":" << config_.port
            << config_.base_path << "/reload";
        return url.str();
    }

    [[nodiscard]] statistics get_statistics() const {
        std::lock_guard lock(stats_mutex_);
        return stats_;
    }

    [[nodiscard]] admin_response handle_request(std::string_view method,
                                                 std::string_view path) const {
        // Update statistics
        {
            std::lock_guard lock(stats_mutex_);
            stats_.total_requests++;
            stats_.active_connections++;
        }

        admin_response response;

        // Normalize path - remove base_path prefix if present
        std::string normalized_path(path);
        if (normalized_path.find(config_.base_path) == 0) {
            normalized_path = normalized_path.substr(config_.base_path.size());
        }

        // Route to appropriate handler
        if (normalized_path == "/reload") {
            if (method == "POST") {
                response = handle_reload();
            } else {
                response = admin_response::method_not_allowed();
            }
        } else if (normalized_path == "/config") {
            if (method == "GET") {
                response = handle_config();
            } else {
                response = admin_response::method_not_allowed();
            }
        } else if (normalized_path == "/status") {
            if (method == "GET") {
                response = handle_status();
            } else {
                response = admin_response::method_not_allowed();
            }
        } else {
            response = admin_response::not_found();
        }

        // Update active connections
        {
            std::lock_guard lock(stats_mutex_);
            stats_.active_connections--;
        }

        return response;
    }

private:
    [[nodiscard]] admin_response handle_reload() const {
        {
            std::lock_guard lock(stats_mutex_);
            stats_.reload_requests++;
        }

        auto result = manager_.reload();

        std::ostringstream json;
        json << R"({"success": )" << (result.success ? "true" : "false");
        json << R"(, "components_notified": )" << result.components_notified;
        json << R"(, "duration_ms": )" << result.duration.count();

        if (!result.success) {
            json << R"(, "error": ")" << escape_json(result.error_message.value_or("Unknown error")) << R"(")";

            if (!result.validation_errors.empty()) {
                json << R"(, "validation_errors": [)";
                for (size_t i = 0; i < result.validation_errors.size(); ++i) {
                    if (i > 0) json << ", ";
                    const auto& err = result.validation_errors[i];
                    json << R"({"field": ")" << escape_json(err.field_path)
                         << R"(", "message": ")" << escape_json(err.message)
                         << R"("})";
                }
                json << "]";
            }

            std::lock_guard lock(stats_mutex_);
            stats_.failed_reloads++;
        } else {
            std::lock_guard lock(stats_mutex_);
            stats_.successful_reloads++;
        }

        json << "}";

        if (result.success) {
            return admin_response::ok(json.str());
        } else {
            return admin_response::bad_request(json.str());
        }
    }

    [[nodiscard]] admin_response handle_config() const {
        if (!config_.enable_config_view) {
            return admin_response{403, "application/json",
                R"({"success": false, "error": "Configuration view is disabled"})"};
        }

        // Return sanitized configuration (no sensitive data)
        const auto& cfg = manager_.get();

        std::ostringstream json;
        json << R"({"success": true, "config": {)";
        json << R"("name": ")" << escape_json(cfg.name) << R"(",)";
        json << R"("hl7": {"listener": {"port": )" << cfg.hl7.listener.port << "}},";
        json << R"("fhir": {"enabled": )" << (cfg.fhir.enabled ? "true" : "false") << "},";
        json << R"("logging": {"level": ")" << to_string(cfg.logging.level) << R"("})";
        json << "}}";

        return admin_response::ok(json.str());
    }

    [[nodiscard]] admin_response handle_status() const {
        auto stats = manager_.get_statistics();

        std::ostringstream json;
        json << R"({"success": true, "statistics": {)";
        json << R"("reload_attempts": )" << stats.reload_attempts << ",";
        json << R"("reload_successes": )" << stats.reload_successes << ",";
        json << R"("reload_failures": )" << stats.reload_failures << ",";
        json << R"("callback_count": )" << stats.callback_count;

        if (stats.last_reload_time) {
            auto time_t = std::chrono::system_clock::to_time_t(*stats.last_reload_time);
            json << R"(, "last_reload_time": ")" << std::ctime(&time_t) << R"(")";
        }

        if (stats.last_error) {
            json << R"(, "last_error": ")" << escape_json(*stats.last_error) << R"(")";
        }

        json << "}}";

        return admin_response::ok(json.str());
    }

    [[nodiscard]] static std::string escape_json(const std::string& str) {
        std::string result;
        result.reserve(str.size());

        for (char c : str) {
            switch (c) {
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
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
                    result += c;
            }
        }

        return result;
    }

    config_manager& manager_;
    config config_;
    std::atomic<bool> running_{false};

    mutable std::mutex stats_mutex_;
    mutable statistics stats_;
};

// =============================================================================
// Admin Server Public Interface
// =============================================================================

admin_server::admin_server(config_manager& manager)
    : pimpl_(std::make_unique<impl>(manager, config{})) {}

admin_server::admin_server(config_manager& manager, const config& cfg)
    : pimpl_(std::make_unique<impl>(manager, cfg)) {}

admin_server::~admin_server() = default;

admin_server::admin_server(admin_server&&) noexcept = default;
admin_server& admin_server::operator=(admin_server&&) noexcept = default;

bool admin_server::start() { return pimpl_->start(); }

void admin_server::stop(bool wait_for_connections) {
    pimpl_->stop(wait_for_connections);
}

bool admin_server::is_running() const noexcept { return pimpl_->is_running(); }

uint16_t admin_server::port() const noexcept { return pimpl_->port(); }

std::string admin_server::base_path() const { return pimpl_->base_path(); }

std::string admin_server::reload_url() const { return pimpl_->reload_url(); }

admin_server::statistics admin_server::get_statistics() const {
    return pimpl_->get_statistics();
}

admin_response admin_server::handle_request(std::string_view method,
                                             std::string_view path) const {
    return pimpl_->handle_request(method, path);
}

}  // namespace pacs::bridge::config
