/**
 * @file messaging_backend.cpp
 * @brief Implementation of messaging backend factory
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/146
 * @see https://github.com/kcenon/pacs_bridge/issues/156
 */

#include "pacs/bridge/messaging/messaging_backend.h"
#include "pacs/bridge/messaging/hl7_message_bus.h"

#include <mutex>
#include <thread>

namespace pacs::bridge::messaging {

namespace {
    std::mutex g_executor_mutex;
    std::function<void(std::function<void()>)> g_external_executor;
}

// =============================================================================
// messaging_backend_factory Implementation
// =============================================================================

std::expected<std::shared_ptr<hl7_message_bus>, backend_error>
messaging_backend_factory::create_message_bus() {
    return create_message_bus(backend_config::defaults());
}

std::expected<std::shared_ptr<hl7_message_bus>, backend_error>
messaging_backend_factory::create_message_bus(const backend_config& config) {

    backend_type actual_type = config.type;

    // Auto-detect if automatic
    if (actual_type == backend_type::automatic) {
        actual_type = recommended_backend();
    }

    // Check if integration is requested but executor not available
    if (actual_type == backend_type::integration && !has_external_executor()) {
        // Fall back to standalone
        actual_type = backend_type::standalone;
    }

    try {
        hl7_message_bus_config bus_config;
        bus_config.worker_threads = config.worker_threads;
        bus_config.queue_capacity = config.queue_capacity;

        auto bus = std::make_shared<hl7_message_bus>(bus_config);
        return bus;
    } catch (const std::exception&) {
        return std::unexpected(backend_error::creation_failed);
    }
}

void messaging_backend_factory::set_external_executor(
    std::function<void(std::function<void()>)> executor) {
    std::lock_guard lock(g_executor_mutex);
    g_external_executor = std::move(executor);
}

void messaging_backend_factory::clear_external_executor() {
    std::lock_guard lock(g_executor_mutex);
    g_external_executor = nullptr;
}

bool messaging_backend_factory::has_external_executor() noexcept {
    std::lock_guard lock(g_executor_mutex);
    return g_external_executor != nullptr;
}

backend_type messaging_backend_factory::recommended_backend() noexcept {
    // Prefer integration if external executor is available
    if (has_external_executor()) {
        return backend_type::integration;
    }

    // Default to standalone
    return backend_type::standalone;
}

size_t messaging_backend_factory::default_worker_threads() noexcept {
    auto threads = std::thread::hardware_concurrency();
    return threads > 0 ? threads : 2;
}

// =============================================================================
// Backend Status
// =============================================================================

backend_status get_backend_status(const hl7_message_bus& bus) {
    backend_status status;

    status.type = backend_type::standalone;  // Currently only standalone
    status.healthy = bus.is_running();
    status.active_workers = messaging_backend_factory::default_worker_threads();

    auto stats = bus.get_statistics();
    status.completed_tasks = stats.messages_delivered;
    status.queued_tasks = stats.messages_published - stats.messages_delivered;

    if (!status.healthy) {
        status.error_message = "Message bus is not running";
    }

    return status;
}

}  // namespace pacs::bridge::messaging
