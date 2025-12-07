/**
 * @file health_server.cpp
 * @brief HTTP server implementation for health check endpoints
 *
 * This implementation provides a lightweight HTTP server for health checks.
 * In production, this would integrate with network_system for TCP operations.
 * Currently, it provides a complete interface that can be connected to
 * the actual network implementation.
 *
 * @see include/pacs/bridge/monitoring/health_server.h
 */

#include "pacs/bridge/monitoring/health_server.h"

#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>

namespace pacs::bridge::monitoring {

// ═══════════════════════════════════════════════════════════════════════════
// Health Server Implementation
// ═══════════════════════════════════════════════════════════════════════════

class health_server::impl {
public:
    impl(health_checker& checker, const config& cfg)
        : checker_(checker), config_(cfg) {}

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

    [[nodiscard]] std::string liveness_url() const {
        std::ostringstream url;
        url << "http://" << config_.bind_address << ":" << config_.port
            << config_.base_path << "/live";
        return url.str();
    }

    [[nodiscard]] std::string readiness_url() const {
        std::ostringstream url;
        url << "http://" << config_.bind_address << ":" << config_.port
            << config_.base_path << "/ready";
        return url.str();
    }

    [[nodiscard]] std::string deep_health_url() const {
        std::ostringstream url;
        url << "http://" << config_.bind_address << ":" << config_.port
            << config_.base_path << "/deep";
        return url.str();
    }

    [[nodiscard]] statistics get_statistics() const {
        std::lock_guard lock(stats_mutex_);
        return stats_;
    }

    [[nodiscard]] http_response handle_request(std::string_view path) const {
        // Update statistics
        {
            std::lock_guard lock(stats_mutex_);
            stats_.total_requests++;
            stats_.active_connections++;
        }

        http_response response;

        // Normalize path - remove base_path prefix if present
        std::string normalized_path(path);
        if (normalized_path.find(config_.base_path) == 0) {
            normalized_path = normalized_path.substr(config_.base_path.size());
        }

        // Route to appropriate handler
        if (normalized_path == "/live" || normalized_path == "/liveness") {
            response = handle_liveness();
            std::lock_guard lock(stats_mutex_);
            stats_.liveness_requests++;
        } else if (normalized_path == "/ready" ||
                   normalized_path == "/readiness") {
            response = handle_readiness();
            std::lock_guard lock(stats_mutex_);
            stats_.readiness_requests++;
        } else if (normalized_path == "/deep" || normalized_path == "/") {
            response = handle_deep();
            std::lock_guard lock(stats_mutex_);
            stats_.deep_health_requests++;
        } else {
            response = http_response::not_found();
            std::lock_guard lock(stats_mutex_);
            stats_.errors++;
        }

        // Add CORS headers if enabled
        if (config_.enable_cors && !response.body.empty()) {
            // CORS headers would be added to response headers here
        }

        // Update active connections
        {
            std::lock_guard lock(stats_mutex_);
            stats_.active_connections--;
        }

        return response;
    }

private:
    [[nodiscard]] http_response handle_liveness() const {
        auto result = checker_.check_liveness();
        auto json = to_json(result);

        if (result.status == health_status::healthy) {
            return http_response::ok(std::move(json));
        } else {
            return http_response::service_unavailable(std::move(json));
        }
    }

    [[nodiscard]] http_response handle_readiness() const {
        auto result = checker_.check_readiness();
        auto json = to_json(result);

        if (result.status == health_status::healthy) {
            return http_response::ok(std::move(json));
        } else {
            return http_response::service_unavailable(std::move(json));
        }
    }

    [[nodiscard]] http_response handle_deep() const {
        auto result = checker_.check_deep();
        auto json = to_json(result);

        if (result.status == health_status::healthy) {
            return http_response::ok(std::move(json));
        } else if (result.status == health_status::degraded) {
            // Return 200 for degraded (operational but not optimal)
            return http_response::ok(std::move(json));
        } else {
            return http_response::service_unavailable(std::move(json));
        }
    }

    health_checker& checker_;
    config config_;
    std::atomic<bool> running_{false};

    mutable std::mutex stats_mutex_;
    mutable statistics stats_;
};

// Health Server public methods
health_server::health_server(health_checker& checker)
    : pimpl_(std::make_unique<impl>(checker, config{})) {}

health_server::health_server(health_checker& checker, const config& cfg)
    : pimpl_(std::make_unique<impl>(checker, cfg)) {}

health_server::~health_server() = default;

health_server::health_server(health_server&&) noexcept = default;
health_server& health_server::operator=(health_server&&) noexcept = default;

bool health_server::start() { return pimpl_->start(); }

void health_server::stop(bool wait_for_connections) {
    pimpl_->stop(wait_for_connections);
}

bool health_server::is_running() const noexcept { return pimpl_->is_running(); }

uint16_t health_server::port() const noexcept { return pimpl_->port(); }

std::string health_server::base_path() const { return pimpl_->base_path(); }

std::string health_server::liveness_url() const {
    return pimpl_->liveness_url();
}

std::string health_server::readiness_url() const {
    return pimpl_->readiness_url();
}

std::string health_server::deep_health_url() const {
    return pimpl_->deep_health_url();
}

health_server::statistics health_server::get_statistics() const {
    return pimpl_->get_statistics();
}

http_response health_server::handle_request(std::string_view path) const {
    return pimpl_->handle_request(path);
}

// ═══════════════════════════════════════════════════════════════════════════
// Configuration Helpers
// ═══════════════════════════════════════════════════════════════════════════

std::string generate_k8s_probe_config(uint16_t port,
                                       std::string_view base_path) {
    std::ostringstream yaml;
    yaml << "livenessProbe:\n";
    yaml << "  httpGet:\n";
    yaml << "    path: " << base_path << "/live\n";
    yaml << "    port: " << port << "\n";
    yaml << "  initialDelaySeconds: 5\n";
    yaml << "  periodSeconds: 10\n";
    yaml << "  timeoutSeconds: 3\n";
    yaml << "  failureThreshold: 3\n";
    yaml << "\n";
    yaml << "readinessProbe:\n";
    yaml << "  httpGet:\n";
    yaml << "    path: " << base_path << "/ready\n";
    yaml << "    port: " << port << "\n";
    yaml << "  initialDelaySeconds: 5\n";
    yaml << "  periodSeconds: 5\n";
    yaml << "  timeoutSeconds: 3\n";
    yaml << "  failureThreshold: 3\n";

    return yaml.str();
}

std::string generate_docker_healthcheck(uint16_t port,
                                         std::string_view base_path) {
    std::ostringstream cmd;
    cmd << "HEALTHCHECK --interval=30s --timeout=3s --start-period=5s "
        << "--retries=3 CMD curl -f http://localhost:" << port << base_path
        << "/live || exit 1";
    return cmd.str();
}

}  // namespace pacs::bridge::monitoring
