/**
 * @file bridge_server.cpp
 * @brief Implementation of the main PACS Bridge orchestration server
 *
 * @see include/pacs/bridge/bridge_server.h
 * @see https://github.com/kcenon/pacs_bridge/issues/175
 */

#include "pacs/bridge/bridge_server.h"

#include "pacs/bridge/config/config_loader.h"
#include "pacs/bridge/monitoring/health_checker.h"
#include "pacs/bridge/pacs_adapter/mpps_handler.h"
#include "pacs/bridge/router/reliable_outbound_sender.h"
#include "pacs/bridge/workflow/mpps_hl7_workflow.h"

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <mutex>

namespace pacs::bridge {

// =============================================================================
// Global Signal Handler State
// =============================================================================

namespace {

std::atomic<bool> g_shutdown_requested{false};
std::condition_variable g_shutdown_cv;
std::mutex g_shutdown_mutex;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_shutdown_requested.store(true, std::memory_order_release);
        g_shutdown_cv.notify_all();
    }
}

void install_signal_handlers() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

}  // namespace

// =============================================================================
// Implementation Class
// =============================================================================

class bridge_server::impl {
public:
    explicit impl(const config::bridge_config& config) : config_(config) {
        validate_config();
    }

    explicit impl(const std::filesystem::path& config_path) {
        auto load_result = config::config_loader::load(config_path);
        if (!load_result) {
            throw std::runtime_error("Failed to load configuration: " +
                                     std::string(to_string(load_result.error())));
        }
        config_ = std::move(*load_result);
        config_path_ = config_path;
        validate_config();
    }

    ~impl() {
        if (running_.load(std::memory_order_acquire)) {
            stop(std::chrono::seconds{10});
        }
    }

    std::expected<void, bridge_server_error> start() {
        if (running_.load(std::memory_order_acquire)) {
            return std::unexpected(bridge_server_error::already_running);
        }

        // Initialize components in dependency order
        if (auto result = init_health_checker(); !result) {
            return result;
        }

        if (auto result = init_outbound_sender(); !result) {
            cleanup();
            return result;
        }

        if (auto result = init_workflow(); !result) {
            cleanup();
            return result;
        }

        if (auto result = init_mpps_handler(); !result) {
            cleanup();
            return result;
        }

        // Start components
        if (auto result = start_components(); !result) {
            cleanup();
            return result;
        }

        running_.store(true, std::memory_order_release);
        start_time_ = std::chrono::steady_clock::now();

        install_signal_handlers();

        return {};
    }

    void stop(std::chrono::seconds timeout) {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }

        running_.store(false, std::memory_order_release);

        // Stop in reverse dependency order
        stop_mpps_handler();
        stop_workflow();
        stop_outbound_sender(timeout);

        cleanup();
    }

    void wait_for_shutdown() {
        std::unique_lock<std::mutex> lock(g_shutdown_mutex);
        g_shutdown_cv.wait(lock, [this] {
            return g_shutdown_requested.load(std::memory_order_acquire) ||
                   !running_.load(std::memory_order_acquire);
        });
    }

    bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    std::expected<void, bridge_server_error>
    reload_config(const std::filesystem::path& config_path) {
        auto load_result = config::config_loader::load(config_path);
        if (!load_result) {
            return std::unexpected(bridge_server_error::config_load_failed);
        }

        // Apply hot-reloadable settings
        auto& new_config = *load_result;

        // Update routing rules in workflow
        if (workflow_) {
            workflow::mpps_hl7_workflow_config wf_config;
            for (const auto& rule : new_config.routing_rules) {
                workflow::destination_rule dest_rule;
                dest_rule.name = rule.name;
                dest_rule.pattern = rule.message_type_pattern;
                dest_rule.destination = rule.destination;
                dest_rule.priority = rule.priority;
                dest_rule.enabled = rule.enabled;
                wf_config.routing_rules.push_back(dest_rule);
            }
            workflow_->set_config(wf_config);
        }

        config_path_ = config_path;
        return {};
    }

    std::expected<void, bridge_server_error>
    add_destination(const config::outbound_destination& destination) {
        if (!outbound_sender_) {
            return std::unexpected(bridge_server_error::not_running);
        }

        router::outbound_destination router_dest;
        router_dest.name = destination.name;
        router_dest.host = destination.host;
        router_dest.port = destination.port;
        router_dest.enabled = destination.enabled;
        router_dest.priority = destination.priority;
        router_dest.message_types = destination.message_types;

        auto result = outbound_sender_->add_destination(router_dest);
        if (!result) {
            return std::unexpected(bridge_server_error::internal_error);
        }

        config_.hl7.outbound_destinations.push_back(destination);
        return {};
    }

    void remove_destination(std::string_view name) {
        if (outbound_sender_) {
            outbound_sender_->remove_destination(name);
        }

        auto& dests = config_.hl7.outbound_destinations;
        dests.erase(
            std::remove_if(dests.begin(), dests.end(),
                           [name](const auto& d) { return d.name == name; }),
            dests.end());
    }

    std::vector<std::string> destinations() const {
        std::vector<std::string> result;
        for (const auto& dest : config_.hl7.outbound_destinations) {
            result.push_back(dest.name);
        }
        return result;
    }

    bridge_statistics get_statistics() const {
        bridge_statistics stats;

        // Calculate uptime
        if (running_.load(std::memory_order_acquire)) {
            auto now = std::chrono::steady_clock::now();
            stats.uptime = std::chrono::duration_cast<std::chrono::seconds>(
                now - start_time_);
        }

        // Aggregate MPPS statistics
        if (mpps_handler_) {
            auto mpps_stats = mpps_handler_->get_statistics();
            stats.mpps_events_received =
                mpps_stats.n_create_count + mpps_stats.n_set_count;
            stats.mpps_in_progress_count = mpps_stats.in_progress_count;
            stats.mpps_completed_count = mpps_stats.completed_count;
            stats.mpps_discontinued_count = mpps_stats.discontinued_count;
        }

        // Aggregate workflow statistics
        if (workflow_) {
            auto wf_stats = workflow_->get_statistics();
            stats.workflow_executions = wf_stats.total_events;
            stats.workflow_successes = wf_stats.successful_events;
            stats.workflow_failures = wf_stats.failed_events;
        }

        // Aggregate queue statistics
        if (outbound_sender_) {
            auto sender_stats = outbound_sender_->get_statistics();
            stats.queue_depth = sender_stats.queue_depth;
            stats.queue_dead_letters = sender_stats.dlq_depth;
            stats.queue_total_enqueued = sender_stats.total_enqueued;
            stats.queue_total_delivered = sender_stats.total_delivered;
        }

        stats.last_activity = std::chrono::system_clock::now();
        return stats;
    }

    void reset_statistics() {
        if (mpps_handler_) {
            mpps_handler_->reset_statistics();
        }
        if (workflow_) {
            workflow_->reset_statistics();
        }
        if (outbound_sender_) {
            outbound_sender_->reset_statistics();
        }
    }

    bool is_healthy() const {
        return get_health_status().healthy;
    }

    bridge_health_status get_health_status() const {
        bridge_health_status status;
        status.healthy = true;

        // Check MPPS handler
        if (mpps_handler_) {
            status.mpps_handler_healthy =
                mpps_handler_->is_running() && mpps_handler_->is_connected();
            if (!status.mpps_handler_healthy) {
                status.healthy = false;
            }
        }

        // Check outbound sender
        if (outbound_sender_) {
            status.outbound_sender_healthy = outbound_sender_->is_running();
            if (!status.outbound_sender_healthy) {
                status.healthy = false;
            }

            // Check queue health
            auto stats = outbound_sender_->get_statistics();
            status.queue_healthy = stats.queue_stats.database_connected;
            if (!status.queue_healthy) {
                status.healthy = false;
            }
        }

        // Check workflow
        if (workflow_) {
            status.workflow_healthy = workflow_->is_running();
            if (!status.workflow_healthy) {
                status.healthy = false;
            }
        }

        // Build details string
        if (status.healthy) {
            status.details = "All components operational";
        } else {
            std::string details;
            if (!status.mpps_handler_healthy) {
                details += "MPPS handler unhealthy; ";
            }
            if (!status.outbound_sender_healthy) {
                details += "Outbound sender unhealthy; ";
            }
            if (!status.workflow_healthy) {
                details += "Workflow unhealthy; ";
            }
            if (!status.queue_healthy) {
                details += "Queue unhealthy; ";
            }
            status.details = details;
        }

        return status;
    }

    std::string_view name() const noexcept { return config_.name; }

    const config::bridge_config& config() const noexcept { return config_; }

private:
    void validate_config() {
        if (!config_.is_valid()) {
            throw std::invalid_argument("Invalid bridge configuration");
        }
    }

    std::expected<void, bridge_server_error> init_health_checker() {
        monitoring::health_config health_config;
        health_checker_ =
            std::make_unique<monitoring::health_checker>(health_config);
        return {};
    }

    std::expected<void, bridge_server_error> init_outbound_sender() {
        router::reliable_sender_config sender_config;

        // Configure queue
        sender_config.queue.database_path = config_.queue.database_path.string();
        sender_config.queue.max_queue_size = config_.queue.max_queue_size;
        sender_config.queue.max_retry_count = config_.queue.max_retry_count;
        sender_config.queue.initial_retry_delay = config_.queue.initial_retry_delay;
        sender_config.queue.retry_backoff_multiplier =
            config_.queue.retry_backoff_multiplier;
        sender_config.queue.message_ttl = config_.queue.message_ttl;
        sender_config.queue.worker_count = config_.queue.worker_count;

        // Configure destinations
        for (const auto& dest : config_.hl7.outbound_destinations) {
            router::outbound_destination router_dest;
            router_dest.name = dest.name;
            router_dest.host = dest.host;
            router_dest.port = dest.port;
            router_dest.enabled = dest.enabled;
            router_dest.priority = dest.priority;
            router_dest.message_types = dest.message_types;
            sender_config.router.destinations.push_back(router_dest);
        }

        outbound_sender_ =
            std::make_unique<router::reliable_outbound_sender>(sender_config);

        return {};
    }

    std::expected<void, bridge_server_error> init_workflow() {
        workflow::mpps_hl7_workflow_config wf_config;
        wf_config.enable_queue_fallback = true;
        wf_config.enable_tracing = true;
        wf_config.enable_metrics = true;

        // Configure routing rules
        for (const auto& rule : config_.routing_rules) {
            workflow::destination_rule dest_rule;
            dest_rule.name = rule.name;
            dest_rule.pattern = rule.message_type_pattern;
            dest_rule.destination = rule.destination;
            dest_rule.priority = rule.priority;
            dest_rule.enabled = rule.enabled;
            wf_config.routing_rules.push_back(dest_rule);
        }

        // Set default destination if configured
        if (!config_.hl7.outbound_destinations.empty()) {
            wf_config.default_destination =
                config_.hl7.outbound_destinations.front().name;
        }

        workflow_ = std::make_unique<workflow::mpps_hl7_workflow>(wf_config);

        // Wire workflow to outbound sender's queue
        workflow_->set_queue_manager(
            std::shared_ptr<router::queue_manager>(
                &outbound_sender_->get_queue_manager(),
                [](router::queue_manager*) {}  // No-op deleter
            ));

        workflow_->set_outbound_router(
            std::shared_ptr<router::outbound_router>(
                &outbound_sender_->get_outbound_router(),
                [](router::outbound_router*) {}  // No-op deleter
            ));

        return {};
    }

    std::expected<void, bridge_server_error> init_mpps_handler() {
        pacs_adapter::mpps_handler_config mpps_config;
        mpps_config.pacs_host = config_.pacs.host;
        mpps_config.pacs_port = config_.pacs.port;
        mpps_config.our_ae_title = config_.pacs.ae_title;
        mpps_config.pacs_ae_title = config_.pacs.called_ae;
        mpps_config.auto_reconnect = true;

        mpps_handler_ = pacs_adapter::mpps_handler::create(mpps_config);

        // Wire MPPS handler to workflow
        workflow_->wire_to_handler(*mpps_handler_);

        return {};
    }

    std::expected<void, bridge_server_error> start_components() {
        // Start outbound sender first (queue needs to be ready)
        if (auto result = outbound_sender_->start(); !result) {
            return std::unexpected(bridge_server_error::outbound_init_failed);
        }

        // Start workflow
        if (auto result = workflow_->start(); !result) {
            return std::unexpected(bridge_server_error::workflow_init_failed);
        }

        // Start MPPS handler (begins receiving events)
        if (auto result = mpps_handler_->start(); !result) {
            return std::unexpected(bridge_server_error::mpps_init_failed);
        }

        return {};
    }

    void stop_mpps_handler() {
        if (mpps_handler_) {
            mpps_handler_->stop(true);  // Graceful stop
        }
    }

    void stop_workflow() {
        if (workflow_) {
            workflow_->stop();
        }
    }

    void stop_outbound_sender(std::chrono::seconds timeout) {
        if (outbound_sender_) {
            outbound_sender_->stop();
        }
    }

    void cleanup() {
        mpps_handler_.reset();
        workflow_.reset();
        outbound_sender_.reset();
        health_checker_.reset();
    }

    config::bridge_config config_;
    std::filesystem::path config_path_;

    std::unique_ptr<monitoring::health_checker> health_checker_;
    std::unique_ptr<router::reliable_outbound_sender> outbound_sender_;
    std::unique_ptr<workflow::mpps_hl7_workflow> workflow_;
    std::unique_ptr<pacs_adapter::mpps_handler> mpps_handler_;

    std::atomic<bool> running_{false};
    std::chrono::steady_clock::time_point start_time_;
};

// =============================================================================
// Bridge Server Public Interface
// =============================================================================

bridge_server::bridge_server(const config::bridge_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

bridge_server::bridge_server(const std::filesystem::path& config_path)
    : pimpl_(std::make_unique<impl>(config_path)) {}

bridge_server::~bridge_server() = default;

bridge_server::bridge_server(bridge_server&&) noexcept = default;
bridge_server& bridge_server::operator=(bridge_server&&) noexcept = default;

std::expected<void, bridge_server_error> bridge_server::start() {
    return pimpl_->start();
}

void bridge_server::stop(std::chrono::seconds timeout) {
    pimpl_->stop(timeout);
}

void bridge_server::wait_for_shutdown() {
    pimpl_->wait_for_shutdown();
}

bool bridge_server::is_running() const noexcept {
    return pimpl_->is_running();
}

std::expected<void, bridge_server_error>
bridge_server::reload_config(const std::filesystem::path& config_path) {
    return pimpl_->reload_config(config_path);
}

std::expected<void, bridge_server_error>
bridge_server::add_destination(const config::outbound_destination& destination) {
    return pimpl_->add_destination(destination);
}

void bridge_server::remove_destination(std::string_view name) {
    pimpl_->remove_destination(name);
}

std::vector<std::string> bridge_server::destinations() const {
    return pimpl_->destinations();
}

bridge_statistics bridge_server::get_statistics() const {
    return pimpl_->get_statistics();
}

void bridge_server::reset_statistics() {
    pimpl_->reset_statistics();
}

bool bridge_server::is_healthy() const {
    return pimpl_->is_healthy();
}

bridge_health_status bridge_server::get_health_status() const {
    return pimpl_->get_health_status();
}

std::string_view bridge_server::name() const noexcept {
    return pimpl_->name();
}

const config::bridge_config& bridge_server::config() const noexcept {
    return pimpl_->config();
}

}  // namespace pacs::bridge
