/**
 * @file health_server.cpp
 * @brief HTTP server implementation for health check and metrics endpoints
 *
 * Provides a lightweight HTTP server exposing health check and Prometheus metrics
 * endpoints. Uses BSD sockets for cross-platform TCP networking.
 *
 * Endpoints:
 *   GET /health/live  - Liveness check (K8s livenessProbe)
 *   GET /health/ready - Readiness check (K8s readinessProbe)
 *   GET /health/deep  - Deep health check with component details
 *   GET /metrics      - Prometheus metrics endpoint
 *
 * @see include/pacs/bridge/monitoring/health_server.h
 * @see https://github.com/kcenon/pacs_bridge/issues/88
 */

#include "pacs/bridge/monitoring/health_server.h"

#include "pacs/bridge/monitoring/bridge_metrics.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

// Platform-specific socket headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
#define CLOSE_SOCKET closesocket
// ssize_t is POSIX-specific, define for Windows
using ssize_t = std::ptrdiff_t;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t INVALID_SOCKET_VALUE = -1;
#define CLOSE_SOCKET ::close
#endif

namespace pacs::bridge::monitoring {

// =============================================================================
// IExecutor Job Implementations (when available)
// =============================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD

/**
 * @brief Job implementation for health server accept loop execution
 *
 * Wraps a single accept iteration for execution via IExecutor.
 */
class health_accept_job : public kcenon::common::interfaces::IJob {
public:
    explicit health_accept_job(std::function<void()> work_func)
        : work_func_(std::move(work_func)) {}

    kcenon::common::VoidResult execute() override {
        if (work_func_) {
            work_func_();
        }
        return std::monostate{};
    }

    std::string get_name() const override { return "health_accept"; }

private:
    std::function<void()> work_func_;
};

/**
 * @brief Job implementation for health server connection handler execution
 *
 * Wraps connection handling for execution via IExecutor.
 */
class health_handler_job : public kcenon::common::interfaces::IJob {
public:
    explicit health_handler_job(std::function<void()> handler_func)
        : handler_func_(std::move(handler_func)) {}

    kcenon::common::VoidResult execute() override {
        if (handler_func_) {
            handler_func_();
        }
        return std::monostate{};
    }

    std::string get_name() const override { return "health_handler"; }

private:
    std::function<void()> handler_func_;
};

#endif  // PACS_BRIDGE_STANDALONE_BUILD

// ═══════════════════════════════════════════════════════════════════════════
// HTTP Utilities
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/**
 * @brief Parse HTTP request line to extract method and path
 *
 * @param request Raw HTTP request data
 * @return Pair of (method, path), empty strings if parse fails
 */
std::pair<std::string, std::string> parse_http_request(std::string_view request) {
    // Find the first line
    auto line_end = request.find("\r\n");
    if (line_end == std::string_view::npos) {
        line_end = request.find("\n");
    }
    if (line_end == std::string_view::npos) {
        return {"", ""};
    }

    std::string_view first_line = request.substr(0, line_end);

    // Parse: METHOD PATH HTTP/x.x
    auto first_space = first_line.find(' ');
    if (first_space == std::string_view::npos) {
        return {"", ""};
    }

    std::string method(first_line.substr(0, first_space));

    auto path_start = first_space + 1;
    auto second_space = first_line.find(' ', path_start);
    if (second_space == std::string_view::npos) {
        return {"", ""};
    }

    std::string path(first_line.substr(path_start, second_space - path_start));

    // Remove query string if present
    auto query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        path = path.substr(0, query_pos);
    }

    return {method, path};
}

/**
 * @brief Format HTTP response for sending
 *
 * @param response HTTP response structure
 * @return Formatted HTTP response string
 */
std::string format_http_response(const http_response& response) {
    std::ostringstream ss;

    // Status line
    ss << "HTTP/1.1 " << response.status_code << " ";
    switch (response.status_code) {
        case 200:
            ss << "OK";
            break;
        case 404:
            ss << "Not Found";
            break;
        case 500:
            ss << "Internal Server Error";
            break;
        case 503:
            ss << "Service Unavailable";
            break;
        default:
            ss << "Unknown";
            break;
    }
    ss << "\r\n";

    // Headers
    ss << "Content-Type: " << response.content_type << "\r\n";
    ss << "Content-Length: " << response.body.size() << "\r\n";
    ss << "Connection: close\r\n";

    // CORS headers for development
    ss << "Access-Control-Allow-Origin: *\r\n";
    ss << "Access-Control-Allow-Methods: GET, OPTIONS\r\n";

    // Empty line between headers and body
    ss << "\r\n";

    // Body
    ss << response.body;

    return ss.str();
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Health Server Implementation
// ═══════════════════════════════════════════════════════════════════════════

class health_server::impl {
public:
    impl(health_checker& checker, const config& cfg)
        : checker_(checker), config_(cfg) {}

    ~impl() { stop(true); }

    // Non-copyable
    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    void set_metrics_provider(metrics_provider provider) {
        std::lock_guard lock(metrics_mutex_);
        metrics_provider_ = std::move(provider);
    }

    // =========================================================================
    // Server Lifecycle
    // =========================================================================

    bool start() {
        std::lock_guard lock(state_mutex_);

        if (running_) {
            return false;  // Already running
        }

        // Initialize platform-specific networking
        if (!initialize_networking()) {
            return false;
        }

        // Create and bind server socket
        if (!create_server_socket()) {
            return false;
        }

        // Auto-configure metrics provider if not set
        configure_default_metrics_provider();

        // Start accept thread
        running_ = true;
        stop_requested_ = false;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        if (config_.executor) {
            schedule_accept_job();
        } else {
            accept_thread_ = std::thread([this] { accept_loop(); });
        }
#else
        accept_thread_ = std::thread([this] { accept_loop(); });
#endif

        return true;
    }

    void stop(bool wait_for_connections) {
        {
            std::lock_guard lock(state_mutex_);
            if (!running_) {
                return;
            }
            stop_requested_ = true;
        }

        // Close server socket to unblock accept
        if (server_socket_ != INVALID_SOCKET_VALUE) {
            CLOSE_SOCKET(server_socket_);
            server_socket_ = INVALID_SOCKET_VALUE;
        }

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        // Wait for executor-based jobs to complete
        if (config_.executor) {
            if (accept_future_.valid()) {
                accept_future_.wait_for(std::chrono::seconds{5});
            }
            {
                std::lock_guard lock(handler_futures_mutex_);
                for (auto& future : handler_futures_) {
                    if (future.valid()) {
                        future.wait_for(std::chrono::seconds{5});
                    }
                }
                handler_futures_.clear();
            }
        }
#endif

        // Wait for accept thread
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        // Wait for active connections if requested
        if (wait_for_connections) {
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(config_.connection_timeout_seconds);
            while (std::chrono::steady_clock::now() < deadline) {
                std::lock_guard lock(stats_mutex_);
                if (stats_.active_connections == 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        // Wait for handler threads
        {
            std::lock_guard lock(threads_mutex_);
            for (auto& t : handler_threads_) {
                if (t.joinable()) {
                    t.join();
                }
            }
            handler_threads_.clear();
        }

        {
            std::lock_guard lock(state_mutex_);
            running_ = false;
        }
    }

    [[nodiscard]] bool is_running() const noexcept {
        std::lock_guard lock(state_mutex_);
        return running_;
    }

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

    [[nodiscard]] std::string metrics_url() const {
        std::ostringstream url;
        url << "http://" << config_.bind_address << ":" << config_.port
            << config_.metrics_path;
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
        } else if (config_.enable_metrics_endpoint &&
                   (path == config_.metrics_path ||
                    normalized_path == config_.metrics_path)) {
            response = handle_metrics();
            std::lock_guard lock(stats_mutex_);
            stats_.metrics_requests++;
        } else {
            response = http_response::not_found();
            std::lock_guard lock(stats_mutex_);
            stats_.errors++;
        }

        return response;
    }

private:
    // =========================================================================
    // Platform Initialization
    // =========================================================================

    bool initialize_networking() {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            return false;
        }
#endif
        return true;
    }

    bool create_server_socket() {
        server_socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_socket_ == INVALID_SOCKET_VALUE) {
            return false;
        }

        // Enable address reuse
        int opt = 1;
#ifdef _WIN32
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        // Bind to address
        struct sockaddr_in server_addr {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(config_.port);

        if (config_.bind_address.empty() || config_.bind_address == "0.0.0.0") {
            server_addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_pton(AF_INET, config_.bind_address.c_str(),
                          &server_addr.sin_addr) != 1) {
                CLOSE_SOCKET(server_socket_);
                server_socket_ = INVALID_SOCKET_VALUE;
                return false;
            }
        }

        if (::bind(server_socket_,
                   reinterpret_cast<struct sockaddr*>(&server_addr),
                   sizeof(server_addr)) < 0) {
            CLOSE_SOCKET(server_socket_);
            server_socket_ = INVALID_SOCKET_VALUE;
            return false;
        }

        // Start listening
        if (::listen(server_socket_,
                     static_cast<int>(config_.max_connections)) < 0) {
            CLOSE_SOCKET(server_socket_);
            server_socket_ = INVALID_SOCKET_VALUE;
            return false;
        }

        return true;
    }

    void configure_default_metrics_provider() {
        std::lock_guard lock(metrics_mutex_);
        if (!metrics_provider_) {
            // Auto-configure with bridge_metrics_collector
            metrics_provider_ = []() {
                return bridge_metrics_collector::instance().get_prometheus_metrics();
            };
        }
    }

    // =========================================================================
    // Accept Loop
    // =========================================================================

    void accept_loop() {
        while (!stop_requested_) {
            // Check max connections
            {
                std::lock_guard lock(stats_mutex_);
                if (stats_.active_connections >= config_.max_connections) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
            }

            // Accept with timeout using poll
#ifndef _WIN32
            struct pollfd pfd {};
            pfd.fd = server_socket_;
            pfd.events = POLLIN;
            int poll_result = poll(&pfd, 1, 1000);  // 1 second timeout

            if (poll_result <= 0 || stop_requested_) {
                continue;
            }
#endif

            struct sockaddr_in client_addr {};
            socklen_t client_len = sizeof(client_addr);

            socket_t client_socket = ::accept(
                server_socket_,
                reinterpret_cast<struct sockaddr*>(&client_addr),
                &client_len);

            if (client_socket == INVALID_SOCKET_VALUE) {
                if (stop_requested_) {
                    break;
                }
                continue;
            }

            // Update active connections
            {
                std::lock_guard lock(stats_mutex_);
                stats_.active_connections++;
            }

            // Handle connection in separate thread
            {
                std::lock_guard lock(threads_mutex_);
                handler_threads_.emplace_back([this, client_socket] {
                    handle_connection(client_socket);
                });
            }
        }
    }

    void handle_connection(socket_t client_socket) {
        // Read HTTP request
        std::vector<char> buffer(4096);
        ssize_t bytes_read = ::recv(client_socket, buffer.data(),
                                    static_cast<int>(buffer.size() - 1), 0);

        http_response response;

        if (bytes_read > 0) {
            buffer[static_cast<size_t>(bytes_read)] = '\0';
            std::string_view request(buffer.data(),
                                     static_cast<size_t>(bytes_read));

            // Parse request
            auto [method, path] = parse_http_request(request);

            if (method == "GET") {
                response = handle_request(path);
            } else if (method == "OPTIONS") {
                // Handle CORS preflight
                response = http_response{200, "text/plain", ""};
            } else {
                response = http_response{405, "application/json",
                                         R"({"error": "Method not allowed"})"};
            }
        } else {
            response = http_response::internal_error("Failed to read request");
        }

        // Send response
        std::string response_str = format_http_response(response);
        ::send(client_socket, response_str.data(),
               static_cast<int>(response_str.size()), 0);

        // Close connection
        CLOSE_SOCKET(client_socket);

        // Update active connections
        {
            std::lock_guard lock(stats_mutex_);
            stats_.active_connections--;
        }
    }

    // =========================================================================
    // Request Handlers
    // =========================================================================

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

    [[nodiscard]] http_response handle_metrics() const {
        std::lock_guard lock(metrics_mutex_);
        if (!metrics_provider_) {
            return http_response{200, "text/plain; version=0.0.4; charset=utf-8",
                                 "# No metrics provider configured\n"};
        }

        try {
            std::string metrics = metrics_provider_();
            return http_response{200, "text/plain; version=0.0.4; charset=utf-8",
                                 std::move(metrics)};
        } catch (const std::exception& e) {
            return http_response::internal_error(e.what());
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    health_checker& checker_;
    config config_;
    socket_t server_socket_ = INVALID_SOCKET_VALUE;

    // State management
    mutable std::mutex state_mutex_;
    bool running_ = false;
    std::atomic<bool> stop_requested_{false};

    // Threads
    std::thread accept_thread_;
    std::mutex threads_mutex_;
    std::vector<std::thread> handler_threads_;

    // Statistics
    mutable std::mutex stats_mutex_;
    mutable statistics stats_;

    // Metrics
    mutable std::mutex metrics_mutex_;
    metrics_provider metrics_provider_;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    // Futures for tracking executor-based jobs
    std::future<void> accept_future_;
    std::vector<std::future<void>> handler_futures_;
    std::mutex handler_futures_mutex_;

    /**
     * @brief Schedule accept job using IExecutor
     *
     * Accepts connections and reschedules itself for continuous operation.
     */
    void schedule_accept_job() {
        if (stop_requested_.load() || !config_.executor) {
            return;
        }

        auto job = std::make_unique<health_accept_job>([this]() {
            if (stop_requested_.load()) {
                return;
            }

            // Check max connections
            {
                std::lock_guard lock(stats_mutex_);
                if (stats_.active_connections >= config_.max_connections) {
                    // Reschedule with small delay
                    schedule_accept_job();
                    return;
                }
            }

            // Accept with poll timeout
#ifndef _WIN32
            struct pollfd pfd {};
            pfd.fd = server_socket_;
            pfd.events = POLLIN;
            int poll_result = poll(&pfd, 1, 100);  // 100ms timeout

            if (poll_result <= 0 || stop_requested_.load()) {
                schedule_accept_job();
                return;
            }
#endif

            struct sockaddr_in client_addr {};
            socklen_t client_len = sizeof(client_addr);

            socket_t client_socket = ::accept(
                server_socket_,
                reinterpret_cast<struct sockaddr*>(&client_addr),
                &client_len);

            if (client_socket == INVALID_SOCKET_VALUE) {
                if (!stop_requested_.load()) {
                    schedule_accept_job();
                }
                return;
            }

            // Update active connections
            {
                std::lock_guard lock(stats_mutex_);
                stats_.active_connections++;
            }

            // Handle connection via executor
            schedule_handler_job(client_socket);

            // Reschedule accept
            schedule_accept_job();
        });

        auto result = config_.executor->execute(std::move(job));
        if (result.is_ok()) {
            accept_future_ = std::move(result.value());
        }
    }

    /**
     * @brief Schedule handler job using IExecutor
     *
     * Handles a single client connection.
     */
    void schedule_handler_job(socket_t client_socket) {
        if (!config_.executor) {
            return;
        }

        auto job = std::make_unique<health_handler_job>([this, client_socket]() {
            handle_connection(client_socket);
        });

        auto result = config_.executor->execute(std::move(job));
        if (result.is_ok()) {
            std::lock_guard lock(handler_futures_mutex_);
            handler_futures_.push_back(std::move(result.value()));
        }
    }
#endif
};

// ═══════════════════════════════════════════════════════════════════════════
// Health Server Public Interface
// ═══════════════════════════════════════════════════════════════════════════

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

std::string health_server::metrics_url() const { return pimpl_->metrics_url(); }

void health_server::set_metrics_provider(metrics_provider provider) {
    pimpl_->set_metrics_provider(std::move(provider));
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
