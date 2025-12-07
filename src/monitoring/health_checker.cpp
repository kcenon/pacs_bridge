/**
 * @file health_checker.cpp
 * @brief Health checker implementation for PACS Bridge
 *
 * @see include/pacs/bridge/monitoring/health_checker.h
 */

#include "pacs/bridge/monitoring/health_checker.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <shared_mutex>
#include <sstream>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace pacs::bridge::monitoring {

// ═══════════════════════════════════════════════════════════════════════════
// Lambda-based Component Check
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/**
 * @brief Simple component check wrapper for lambda functions
 */
class lambda_component_check : public component_check {
public:
    lambda_component_check(
        std::string component_name,
        std::function<component_health(std::chrono::milliseconds)> check_fn,
        bool critical)
        : name_(std::move(component_name)),
          check_fn_(std::move(check_fn)),
          critical_(critical) {}

    [[nodiscard]] std::string name() const override { return name_; }

    [[nodiscard]] component_health check(
        std::chrono::milliseconds timeout) override {
        return check_fn_(timeout);
    }

    [[nodiscard]] bool is_critical() const noexcept override {
        return critical_;
    }

private:
    std::string name_;
    std::function<component_health(std::chrono::milliseconds)> check_fn_;
    bool critical_;
};

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Built-in Component Checks Implementation
// ═══════════════════════════════════════════════════════════════════════════

// MLLP Server Check
mllp_server_check::mllp_server_check(status_provider is_running,
                                     stats_provider get_stats)
    : is_running_(std::move(is_running)), get_stats_(std::move(get_stats)) {}

component_health mllp_server_check::check(std::chrono::milliseconds /*timeout*/) {
    component_health health;
    health.name = name();

    auto start = std::chrono::steady_clock::now();

    if (!is_running_) {
        health.status = health_status::unhealthy;
        health.details = "Status provider not configured";
        return health;
    }

    bool running = is_running_();
    auto end = std::chrono::steady_clock::now();
    health.response_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();

    if (running) {
        health.status = health_status::healthy;

        if (get_stats_) {
            auto [active, total, errors] = get_stats_();
            health.metrics["active_connections"] = std::to_string(active);
            health.metrics["total_connections"] = std::to_string(total);
            health.metrics["errors"] = std::to_string(errors);
        }
    } else {
        health.status = health_status::unhealthy;
        health.details = "MLLP server is not running";
    }

    return health;
}

// PACS Connection Check
pacs_connection_check::pacs_connection_check(echo_provider echo_fn)
    : echo_fn_(std::move(echo_fn)) {}

component_health pacs_connection_check::check(
    std::chrono::milliseconds timeout) {
    component_health health;
    health.name = name();

    if (!echo_fn_) {
        health.status = health_status::unhealthy;
        health.details = "Echo provider not configured";
        return health;
    }

    auto start = std::chrono::steady_clock::now();
    bool success = echo_fn_(timeout);
    auto end = std::chrono::steady_clock::now();

    health.response_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();

    if (success) {
        health.status = health_status::healthy;
    } else {
        health.status = health_status::unhealthy;
        health.details = "DICOM C-ECHO failed";
    }

    return health;
}

// Queue Health Check
queue_health_check::queue_health_check(metrics_provider get_metrics,
                                       const health_thresholds& thresholds)
    : get_metrics_(std::move(get_metrics)), thresholds_(thresholds) {}

component_health queue_health_check::check(std::chrono::milliseconds /*timeout*/) {
    component_health health;
    health.name = name();

    if (!get_metrics_) {
        health.status = health_status::unhealthy;
        health.details = "Metrics provider not configured";
        return health;
    }

    auto start = std::chrono::steady_clock::now();
    auto metrics = get_metrics_();
    auto end = std::chrono::steady_clock::now();

    health.response_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();

    health.metrics["pending_messages"] = std::to_string(metrics.pending_messages);
    health.metrics["dead_letters"] = std::to_string(metrics.dead_letters);

    if (!metrics.database_connected) {
        health.status = health_status::unhealthy;
        health.details = "Queue database not connected";
        return health;
    }

    if (metrics.dead_letters > thresholds_.queue_dead_letters) {
        health.status = health_status::degraded;
        health.details = "Dead letter count exceeds threshold";
        return health;
    }

    if (metrics.pending_messages > thresholds_.queue_depth) {
        health.status = health_status::degraded;
        health.details = "Queue depth exceeds threshold";
        return health;
    }

    health.status = health_status::healthy;
    return health;
}

// FHIR Server Check
fhir_server_check::fhir_server_check(status_provider is_running,
                                     stats_provider get_stats)
    : is_running_(std::move(is_running)), get_stats_(std::move(get_stats)) {}

component_health fhir_server_check::check(std::chrono::milliseconds /*timeout*/) {
    component_health health;
    health.name = name();

    auto start = std::chrono::steady_clock::now();

    if (!is_running_) {
        // FHIR server might be disabled
        health.status = health_status::healthy;
        health.details = "FHIR server not enabled";
        return health;
    }

    bool running = is_running_();
    auto end = std::chrono::steady_clock::now();
    health.response_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();

    if (running) {
        health.status = health_status::healthy;

        if (get_stats_) {
            auto [active, total] = get_stats_();
            health.metrics["active_requests"] = std::to_string(active);
            health.metrics["total_requests"] = std::to_string(total);
        }
    } else {
        health.status = health_status::unhealthy;
        health.details = "FHIR server is not running";
    }

    return health;
}

// Memory Health Check
memory_health_check::memory_health_check(const health_thresholds& thresholds)
    : thresholds_(thresholds) {}

component_health memory_health_check::check(std::chrono::milliseconds /*timeout*/) {
    component_health health;
    health.name = name();

    auto start = std::chrono::steady_clock::now();
    size_t memory_bytes = get_process_memory();
    auto end = std::chrono::steady_clock::now();

    health.response_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();

    size_t memory_mb = memory_bytes / (1024 * 1024);
    health.metrics["usage_mb"] = std::to_string(memory_mb);
    health.metrics["threshold_mb"] = std::to_string(thresholds_.memory_mb);

    if (memory_mb > thresholds_.memory_mb) {
        health.status = health_status::degraded;
        health.details = "Memory usage exceeds threshold";
    } else if (memory_mb > thresholds_.memory_mb * 0.8) {
        // Warning at 80% of threshold
        health.status = health_status::healthy;
        health.details = "Memory usage approaching threshold";
    } else {
        health.status = health_status::healthy;
    }

    return health;
}

size_t memory_health_check::get_process_memory() {
#if defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info,
                  &count) == KERN_SUCCESS) {
        return info.resident_size;
    }
    return 0;
#elif defined(__linux__)
    // Read from /proc/self/statm
    FILE* file = fopen("/proc/self/statm", "r");
    if (file) {
        unsigned long size, resident;
        if (fscanf(file, "%lu %lu", &size, &resident) == 2) {
            fclose(file);
            long page_size = sysconf(_SC_PAGESIZE);
            return resident * static_cast<size_t>(page_size);
        }
        fclose(file);
    }
    return 0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
#else
    return 0;
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Health Checker Implementation
// ═══════════════════════════════════════════════════════════════════════════

class health_checker::impl {
public:
    explicit impl(const health_config& config) : config_(config) {}

    void register_check(std::unique_ptr<component_check> check) {
        std::unique_lock lock(mutex_);
        checks_.push_back(std::move(check));
    }

    void register_check(
        std::string name,
        std::function<component_health(std::chrono::milliseconds)> check_fn,
        bool critical) {
        auto check = std::make_unique<lambda_component_check>(
            std::move(name), std::move(check_fn), critical);
        register_check(std::move(check));
    }

    bool unregister_check(std::string_view name) {
        std::unique_lock lock(mutex_);
        auto it = std::remove_if(checks_.begin(), checks_.end(),
                                  [&name](const auto& check) {
                                      return check->name() == name;
                                  });
        if (it != checks_.end()) {
            checks_.erase(it, checks_.end());
            return true;
        }
        return false;
    }

    [[nodiscard]] std::vector<std::string> registered_components() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> names;
        names.reserve(checks_.size());
        for (const auto& check : checks_) {
            names.push_back(check->name());
        }
        return names;
    }

    [[nodiscard]] liveness_result check_liveness() const {
        // Simple liveness - just verify the process is responsive
        return liveness_result::ok();
    }

    [[nodiscard]] readiness_result check_readiness() const {
        readiness_result result;
        result.timestamp = std::chrono::system_clock::now();

        std::shared_lock lock(mutex_);
        bool all_critical_healthy = true;

        auto timeout = std::chrono::milliseconds(
            config_.thresholds.component_timeout_ms);

        for (const auto& check : checks_) {
            auto health = check->check(timeout);
            result.components[check->name()] = health.status;

            if (check->is_critical() &&
                health.status == health_status::unhealthy) {
                all_critical_healthy = false;
            }
        }

        result.status = all_critical_healthy ? health_status::healthy
                                              : health_status::unhealthy;
        return result;
    }

    [[nodiscard]] deep_health_result check_deep() const {
        deep_health_result result;
        result.timestamp = std::chrono::system_clock::now();

        std::shared_lock lock(mutex_);
        auto timeout = std::chrono::milliseconds(
            config_.thresholds.component_timeout_ms);

        for (const auto& check : checks_) {
            result.components.push_back(check->check(timeout));
        }

        result.calculate_overall_status();
        return result;
    }

    [[nodiscard]] std::optional<component_health> check_component(
        std::string_view name) const {
        std::shared_lock lock(mutex_);
        auto timeout = std::chrono::milliseconds(
            config_.thresholds.component_timeout_ms);

        for (const auto& check : checks_) {
            if (check->name() == name) {
                return check->check(timeout);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] const health_config& config() const noexcept {
        return config_;
    }

    void update_thresholds(const health_thresholds& thresholds) {
        std::unique_lock lock(mutex_);
        config_.thresholds = thresholds;
    }

private:
    health_config config_;
    std::vector<std::unique_ptr<component_check>> checks_;
    mutable std::shared_mutex mutex_;
};

// Health Checker public methods
health_checker::health_checker(const health_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

health_checker::~health_checker() = default;

health_checker::health_checker(health_checker&&) noexcept = default;
health_checker& health_checker::operator=(health_checker&&) noexcept = default;

void health_checker::register_check(std::unique_ptr<component_check> check) {
    pimpl_->register_check(std::move(check));
}

void health_checker::register_check(
    std::string name,
    std::function<component_health(std::chrono::milliseconds)> check_fn,
    bool critical) {
    pimpl_->register_check(std::move(name), std::move(check_fn), critical);
}

bool health_checker::unregister_check(std::string_view name) {
    return pimpl_->unregister_check(name);
}

std::vector<std::string> health_checker::registered_components() const {
    return pimpl_->registered_components();
}

liveness_result health_checker::check_liveness() const {
    return pimpl_->check_liveness();
}

readiness_result health_checker::check_readiness() const {
    return pimpl_->check_readiness();
}

deep_health_result health_checker::check_deep() const {
    return pimpl_->check_deep();
}

std::optional<component_health> health_checker::check_component(
    std::string_view name) const {
    return pimpl_->check_component(name);
}

const health_config& health_checker::config() const noexcept {
    return pimpl_->config();
}

void health_checker::update_thresholds(const health_thresholds& thresholds) {
    pimpl_->update_thresholds(thresholds);
}

// ═══════════════════════════════════════════════════════════════════════════
// JSON Serialization
// ═══════════════════════════════════════════════════════════════════════════

std::string format_timestamp(std::chrono::system_clock::time_point tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  tp.time_since_epoch()) %
              1000;

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

namespace {

std::string escape_json_string(std::string_view str) {
    std::string result;
    result.reserve(str.size() + 2);
    for (char c : str) {
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
                result += c;
        }
    }
    return result;
}

}  // namespace

std::string to_json(const liveness_result& result) {
    std::ostringstream json;
    json << R"({"status": ")" << to_string(result.status) << R"(",)";
    json << R"("timestamp": ")" << format_timestamp(result.timestamp) << R"("})";
    return json.str();
}

std::string to_json(const readiness_result& result) {
    std::ostringstream json;
    json << R"({"status": ")" << to_string(result.status) << R"(",)";
    json << R"("timestamp": ")" << format_timestamp(result.timestamp) << R"(",)";
    json << R"("checks": {)";

    bool first = true;
    for (const auto& [name, status] : result.components) {
        if (!first) json << ",";
        first = false;
        json << R"(")" << escape_json_string(name) << R"(": ")";
        json << to_string(status) << R"(")";
    }

    json << "}}";
    return json.str();
}

std::string to_json(const deep_health_result& result) {
    std::ostringstream json;
    json << R"({"status": ")" << to_string(result.status) << R"(",)";
    json << R"("timestamp": ")" << format_timestamp(result.timestamp) << R"(",)";

    if (result.message) {
        json << R"("message": ")" << escape_json_string(*result.message)
             << R"(",)";
    }

    json << R"("components": {)";

    bool first_comp = true;
    for (const auto& comp : result.components) {
        if (!first_comp) json << ",";
        first_comp = false;

        json << R"(")" << escape_json_string(comp.name) << R"(": {)";
        json << R"("status": ")" << to_string(comp.status) << R"(")";

        if (comp.response_time_ms) {
            json << R"(,"response_time_ms": )" << *comp.response_time_ms;
        }

        if (comp.details) {
            json << R"(,"details": ")" << escape_json_string(*comp.details)
                 << R"(")";
        }

        if (!comp.metrics.empty()) {
            json << R"(,"metrics": {)";
            bool first_metric = true;
            for (const auto& [key, value] : comp.metrics) {
                if (!first_metric) json << ",";
                first_metric = false;
                json << R"(")" << escape_json_string(key) << R"(": )";
                // Try to output as number if possible
                bool is_number = !value.empty() &&
                    std::all_of(value.begin(), value.end(), [](char c) {
                        return std::isdigit(c) || c == '.' || c == '-';
                    });
                if (is_number) {
                    json << value;
                } else {
                    json << R"(")" << escape_json_string(value) << R"(")";
                }
            }
            json << "}";
        }

        json << "}";
    }

    json << "}}";
    return json.str();
}

}  // namespace pacs::bridge::monitoring
