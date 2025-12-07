/**
 * @file audit_logger.cpp
 * @brief Implementation of healthcare-specific audit logging
 *
 * @see include/pacs/bridge/security/audit_logger.h
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include "pacs/bridge/security/audit_logger.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace pacs::bridge::security {

// =============================================================================
// String Conversion Functions
// =============================================================================

const char* to_string(healthcare_audit_category category) noexcept {
    switch (category) {
        case healthcare_audit_category::system:
            return "system";
        case healthcare_audit_category::security:
            return "security";
        case healthcare_audit_category::hl7_transaction:
            return "hl7_transaction";
        case healthcare_audit_category::fhir_transaction:
            return "fhir_transaction";
        case healthcare_audit_category::dicom_transaction:
            return "dicom_transaction";
        case healthcare_audit_category::phi_access:
            return "phi_access";
        case healthcare_audit_category::error:
            return "error";
        case healthcare_audit_category::network:
            return "network";
        case healthcare_audit_category::configuration:
            return "configuration";
        default:
            return "unknown";
    }
}

const char* to_string(healthcare_audit_event event) noexcept {
    switch (event) {
        case healthcare_audit_event::system_start: return "system_start";
        case healthcare_audit_event::system_stop: return "system_stop";
        case healthcare_audit_event::config_load: return "config_load";
        case healthcare_audit_event::config_change: return "config_change";
        case healthcare_audit_event::auth_attempt: return "auth_attempt";
        case healthcare_audit_event::auth_success: return "auth_success";
        case healthcare_audit_event::auth_failure: return "auth_failure";
        case healthcare_audit_event::access_denied: return "access_denied";
        case healthcare_audit_event::certificate_validation: return "certificate_validation";
        case healthcare_audit_event::tls_handshake: return "tls_handshake";
        case healthcare_audit_event::rate_limit_exceeded: return "rate_limit_exceeded";
        case healthcare_audit_event::ip_blocked: return "ip_blocked";
        case healthcare_audit_event::ip_whitelisted: return "ip_whitelisted";
        case healthcare_audit_event::hl7_message_received: return "hl7_message_received";
        case healthcare_audit_event::hl7_message_sent: return "hl7_message_sent";
        case healthcare_audit_event::hl7_message_processed: return "hl7_message_processed";
        case healthcare_audit_event::hl7_message_rejected: return "hl7_message_rejected";
        case healthcare_audit_event::hl7_ack_sent: return "hl7_ack_sent";
        case healthcare_audit_event::hl7_nak_sent: return "hl7_nak_sent";
        case healthcare_audit_event::hl7_validation_failed: return "hl7_validation_failed";
        case healthcare_audit_event::fhir_request_received: return "fhir_request_received";
        case healthcare_audit_event::fhir_response_sent: return "fhir_response_sent";
        case healthcare_audit_event::fhir_resource_created: return "fhir_resource_created";
        case healthcare_audit_event::fhir_resource_updated: return "fhir_resource_updated";
        case healthcare_audit_event::fhir_resource_deleted: return "fhir_resource_deleted";
        case healthcare_audit_event::fhir_search_executed: return "fhir_search_executed";
        case healthcare_audit_event::phi_accessed: return "phi_accessed";
        case healthcare_audit_event::phi_created: return "phi_created";
        case healthcare_audit_event::phi_modified: return "phi_modified";
        case healthcare_audit_event::phi_deleted: return "phi_deleted";
        case healthcare_audit_event::phi_exported: return "phi_exported";
        case healthcare_audit_event::phi_query: return "phi_query";
        case healthcare_audit_event::connection_opened: return "connection_opened";
        case healthcare_audit_event::connection_closed: return "connection_closed";
        case healthcare_audit_event::connection_rejected: return "connection_rejected";
        case healthcare_audit_event::validation_error: return "validation_error";
        case healthcare_audit_event::processing_error: return "processing_error";
        case healthcare_audit_event::connection_error: return "connection_error";
        case healthcare_audit_event::timeout_error: return "timeout_error";
        default: return "unknown";
    }
}

const char* to_string(audit_severity severity) noexcept {
    switch (severity) {
        case audit_severity::info: return "info";
        case audit_severity::warning: return "warning";
        case audit_severity::error: return "error";
        case audit_severity::critical: return "critical";
        case audit_severity::emergency: return "emergency";
        default: return "unknown";
    }
}

// =============================================================================
// Healthcare Audit Event Record
// =============================================================================

namespace {

std::string escape_json(const std::string& str) {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b";  break;
            case '\f': oss << "\\f";  break;
            case '\n': oss << "\\n";  break;
            case '\r': oss << "\\r";  break;
            case '\t': oss << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 32) {
                    oss << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<int>(c);
                } else {
                    oss << c;
                }
                break;
        }
    }
    return oss.str();
}

std::string format_timestamp(const std::chrono::system_clock::time_point& tp) {
    auto time_t_value = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_value{};
#ifdef _WIN32
    gmtime_s(&tm_value, &time_t_value);
#else
    gmtime_r(&time_t_value, &tm_value);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_value, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string generate_event_id() {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream oss;
    oss << std::hex << ms << "-" << std::setw(8) << std::setfill('0')
        << (counter.fetch_add(1) & 0xFFFFFFFF);
    return oss.str();
}

}  // namespace

std::string healthcare_audit_event_record::to_json() const {
    std::ostringstream oss;

    oss << "{";
    oss << "\"timestamp\":\"" << format_timestamp(timestamp) << "\"";
    oss << ",\"event_id\":\"" << escape_json(event_id) << "\"";
    oss << ",\"category\":\"" << to_string(category) << "\"";
    oss << ",\"event\":\"" << to_string(type) << "\"";
    oss << ",\"severity\":\"" << to_string(severity) << "\"";
    oss << ",\"description\":\"" << escape_json(description) << "\"";

    if (!source_component.empty()) {
        oss << ",\"source\":\"" << escape_json(source_component) << "\"";
    }

    if (session_id) {
        oss << ",\"session_id\":" << *session_id;
    }
    if (remote_address) {
        oss << ",\"remote_address\":\"" << escape_json(*remote_address) << "\"";
    }
    if (remote_port) {
        oss << ",\"remote_port\":" << *remote_port;
    }
    if (tls_enabled) {
        oss << ",\"tls_enabled\":" << (*tls_enabled ? "true" : "false");
    }
    if (client_cert_subject) {
        oss << ",\"client_cert\":\"" << escape_json(*client_cert_subject) << "\"";
    }

    if (message_control_id) {
        oss << ",\"message_control_id\":\"" << escape_json(*message_control_id) << "\"";
    }
    if (message_type) {
        oss << ",\"message_type\":\"" << escape_json(*message_type) << "\"";
    }
    if (sending_application) {
        oss << ",\"sending_app\":\"" << escape_json(*sending_application) << "\"";
    }
    if (sending_facility) {
        oss << ",\"sending_facility\":\"" << escape_json(*sending_facility) << "\"";
    }
    if (message_size) {
        oss << ",\"message_size\":" << *message_size;
    }

    oss << ",\"outcome\":\"" << escape_json(outcome) << "\"";

    if (error_code) {
        oss << ",\"error_code\":" << *error_code;
    }
    if (error_message) {
        oss << ",\"error_message\":\"" << escape_json(*error_message) << "\"";
    }
    if (processing_time_ms) {
        oss << ",\"processing_time_ms\":" << std::fixed << std::setprecision(3)
            << *processing_time_ms;
    }

    if (!properties.empty()) {
        oss << ",\"properties\":{";
        bool first = true;
        for (const auto& [key, value] : properties) {
            if (!first) oss << ",";
            oss << "\"" << escape_json(key) << "\":\"" << escape_json(value) << "\"";
            first = false;
        }
        oss << "}";
    }

    oss << "}";
    return oss.str();
}

// =============================================================================
// Implementation Class
// =============================================================================

class healthcare_audit_logger::impl {
public:
    explicit impl(const healthcare_audit_config& config)
        : config_(config) {}

    bool start() {
        if (running_) return true;

        // Create directory if needed
        auto parent = config_.log_path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }

        // Open log file
        log_file_.open(config_.log_path, std::ios::app);
        if (!log_file_) {
            return false;
        }

        running_ = true;
        stats_.started_at = std::chrono::system_clock::now();
        return true;
    }

    void stop() {
        if (!running_) return;
        flush();
        log_file_.close();
        running_ = false;
    }

    void log(const healthcare_audit_event_record& event) {
        if (!running_ || !config_.enabled) return;

        // Check severity filter
        if (static_cast<int>(event.severity) < static_cast<int>(config_.min_severity)) {
            return;
        }

        // Check category filter
        if (!config_.categories.empty() &&
            config_.categories.find(event.category) == config_.categories.end()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Write JSON line
        log_file_ << event.to_json() << "\n";
        log_file_.flush();

        // Update statistics
        ++stats_.events_logged;
        stats_.last_event_at = std::chrono::system_clock::now();

        if (event.category == healthcare_audit_category::hl7_transaction) {
            ++stats_.hl7_transactions;
        }
        if (event.category == healthcare_audit_category::security) {
            ++stats_.security_events;
        }
        if (event.category == healthcare_audit_category::error) {
            ++stats_.error_events;
        }
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (log_file_.is_open()) {
            log_file_.flush();
        }
    }

    healthcare_audit_config config_;
    std::ofstream log_file_;
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    statistics stats_{};
};

// =============================================================================
// Event Builder
// =============================================================================

healthcare_audit_logger::event_builder::event_builder(
    healthcare_audit_logger& logger,
    healthcare_audit_category category,
    healthcare_audit_event type)
    : logger_(logger) {
    event_.timestamp = std::chrono::system_clock::now();
    event_.event_id = generate_event_id();
    event_.category = category;
    event_.type = type;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::severity(audit_severity sev) {
    event_.severity = sev;
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::description(std::string_view desc) {
    event_.description = std::string(desc);
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::source(std::string_view component) {
    event_.source_component = std::string(component);
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::session(uint64_t id) {
    event_.session_id = id;
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::remote_address(std::string_view addr, uint16_t port) {
    event_.remote_address = std::string(addr);
    if (port > 0) {
        event_.remote_port = port;
    }
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::tls(bool enabled, std::string_view cert_subject) {
    event_.tls_enabled = enabled;
    if (!cert_subject.empty()) {
        event_.client_cert_subject = std::string(cert_subject);
    }
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::message(std::string_view control_id,
                                                 std::string_view msg_type,
                                                 std::string_view sending_app,
                                                 std::string_view sending_facility) {
    if (!control_id.empty()) event_.message_control_id = std::string(control_id);
    if (!msg_type.empty()) event_.message_type = std::string(msg_type);
    if (!sending_app.empty()) event_.sending_application = std::string(sending_app);
    if (!sending_facility.empty()) event_.sending_facility = std::string(sending_facility);
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::message_size(size_t bytes) {
    event_.message_size = bytes;
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::outcome(std::string_view result) {
    event_.outcome = std::string(result);
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::error(int code, std::string_view message) {
    event_.error_code = code;
    event_.error_message = std::string(message);
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::processing_time(double ms) {
    event_.processing_time_ms = ms;
    return *this;
}

healthcare_audit_logger::event_builder&
healthcare_audit_logger::event_builder::property(std::string_view key, std::string_view value) {
    event_.properties[std::string(key)] = std::string(value);
    return *this;
}

void healthcare_audit_logger::event_builder::commit() {
    logger_.log(event_);
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

healthcare_audit_logger::healthcare_audit_logger(const healthcare_audit_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

healthcare_audit_logger::~healthcare_audit_logger() {
    stop();
}

healthcare_audit_logger::healthcare_audit_logger(healthcare_audit_logger&&) noexcept = default;
healthcare_audit_logger& healthcare_audit_logger::operator=(healthcare_audit_logger&&) noexcept = default;

// =============================================================================
// Lifecycle Methods
// =============================================================================

bool healthcare_audit_logger::start() {
    return pimpl_->start();
}

void healthcare_audit_logger::stop() {
    pimpl_->stop();
}

bool healthcare_audit_logger::is_running() const noexcept {
    return pimpl_->running_;
}

void healthcare_audit_logger::flush() {
    pimpl_->flush();
}

// =============================================================================
// Logging Methods
// =============================================================================

void healthcare_audit_logger::log(const healthcare_audit_event_record& event) {
    pimpl_->log(event);
}

healthcare_audit_logger::event_builder
healthcare_audit_logger::log_event(healthcare_audit_category category,
                                   healthcare_audit_event type) {
    return event_builder(*this, category, type);
}

// =============================================================================
// HL7 Transaction Logging
// =============================================================================

void healthcare_audit_logger::log_hl7_received(std::string_view message_type,
                                                std::string_view control_id,
                                                std::string_view sending_app,
                                                size_t message_size,
                                                uint64_t session_id) {
    log_event(healthcare_audit_category::hl7_transaction,
              healthcare_audit_event::hl7_message_received)
        .description("HL7 message received")
        .message(control_id, message_type, sending_app, "")
        .message_size(message_size)
        .session(session_id)
        .outcome("received")
        .commit();
}

void healthcare_audit_logger::log_hl7_processed(std::string_view control_id,
                                                 bool success,
                                                 double processing_time_ms,
                                                 std::optional<int> error_code) {
    auto builder = log_event(healthcare_audit_category::hl7_transaction,
                             healthcare_audit_event::hl7_message_processed)
        .description(success ? "HL7 message processed successfully" : "HL7 message processing failed")
        .message(control_id, "", "", "")
        .processing_time(processing_time_ms)
        .outcome(success ? "success" : "failure");

    if (error_code) {
        builder.error(*error_code, "Processing failed");
    }

    builder.commit();
}

void healthcare_audit_logger::log_hl7_response(std::string_view control_id,
                                                bool ack,
                                                std::string_view ack_code) {
    log_event(healthcare_audit_category::hl7_transaction,
              ack ? healthcare_audit_event::hl7_ack_sent : healthcare_audit_event::hl7_nak_sent)
        .description(ack ? "HL7 ACK sent" : "HL7 NAK sent")
        .message(control_id, "", "", "")
        .property("ack_code", ack_code)
        .outcome("sent")
        .commit();
}

void healthcare_audit_logger::log_hl7_validation_failed(std::string_view control_id,
                                                         std::string_view reason,
                                                         std::string_view field) {
    auto builder = log_event(healthcare_audit_category::hl7_transaction,
                             healthcare_audit_event::hl7_validation_failed)
        .severity(audit_severity::warning)
        .description("HL7 message validation failed")
        .message(control_id, "", "", "")
        .property("reason", reason)
        .outcome("rejected");

    if (!field.empty()) {
        builder.property("field", field);
    }

    builder.commit();
}

// =============================================================================
// Security Event Logging
// =============================================================================

void healthcare_audit_logger::log_auth_attempt(std::string_view remote_address,
                                                bool success,
                                                std::string_view method,
                                                std::string_view details) {
    log_event(healthcare_audit_category::security,
              success ? healthcare_audit_event::auth_success : healthcare_audit_event::auth_failure)
        .severity(success ? audit_severity::info : audit_severity::warning)
        .description(success ? "Authentication successful" : "Authentication failed")
        .remote_address(remote_address)
        .property("method", method)
        .property("details", details)
        .outcome(success ? "success" : "failure")
        .commit();
}

void healthcare_audit_logger::log_access_denied(std::string_view remote_address,
                                                 std::string_view reason,
                                                 uint64_t session_id) {
    auto builder = log_event(healthcare_audit_category::security,
                             healthcare_audit_event::access_denied)
        .severity(audit_severity::warning)
        .description("Access denied")
        .remote_address(remote_address)
        .property("reason", reason)
        .outcome("denied");

    if (session_id > 0) {
        builder.session(session_id);
    }

    builder.commit();
}

void healthcare_audit_logger::log_rate_limited(std::string_view remote_address,
                                                std::string_view limit_type,
                                                uint64_t session_id) {
    auto builder = log_event(healthcare_audit_category::security,
                             healthcare_audit_event::rate_limit_exceeded)
        .severity(audit_severity::warning)
        .description("Rate limit exceeded")
        .remote_address(remote_address)
        .property("limit_type", limit_type)
        .outcome("rate_limited");

    if (session_id > 0) {
        builder.session(session_id);
    }

    builder.commit();
}

void healthcare_audit_logger::log_security_violation(audit_severity severity,
                                                      std::string_view description,
                                                      std::string_view remote_address,
                                                      uint64_t session_id) {
    auto builder = log_event(healthcare_audit_category::security,
                             healthcare_audit_event::access_denied)
        .severity(severity)
        .description(description)
        .outcome("violation");

    if (!remote_address.empty()) {
        builder.remote_address(remote_address);
    }
    if (session_id > 0) {
        builder.session(session_id);
    }

    builder.commit();
}

// =============================================================================
// System Event Logging
// =============================================================================

void healthcare_audit_logger::log_system_start(std::string_view version,
                                                std::string_view config_path) {
    auto builder = log_event(healthcare_audit_category::system,
                             healthcare_audit_event::system_start)
        .description("System started")
        .outcome("started");

    if (!version.empty()) {
        builder.property("version", version);
    }
    if (!config_path.empty()) {
        builder.property("config_path", config_path);
    }

    builder.commit();
}

void healthcare_audit_logger::log_system_stop(std::string_view reason) {
    log_event(healthcare_audit_category::system,
              healthcare_audit_event::system_stop)
        .description("System stopped")
        .property("reason", reason)
        .outcome("stopped")
        .commit();
}

void healthcare_audit_logger::log_config_change(std::string_view component,
                                                 std::string_view setting,
                                                 std::string_view old_value,
                                                 std::string_view new_value) {
    log_event(healthcare_audit_category::configuration,
              healthcare_audit_event::config_change)
        .description("Configuration changed")
        .property("component", component)
        .property("setting", setting)
        .property("old_value", old_value)
        .property("new_value", new_value)
        .outcome("changed")
        .commit();
}

// =============================================================================
// Network Event Logging
// =============================================================================

void healthcare_audit_logger::log_connection_opened(std::string_view remote_address,
                                                     uint16_t remote_port,
                                                     uint64_t session_id,
                                                     bool tls_enabled) {
    log_event(healthcare_audit_category::network,
              healthcare_audit_event::connection_opened)
        .description("Connection opened")
        .remote_address(remote_address, remote_port)
        .session(session_id)
        .tls(tls_enabled)
        .outcome("connected")
        .commit();
}

void healthcare_audit_logger::log_connection_closed(uint64_t session_id,
                                                     std::string_view reason) {
    log_event(healthcare_audit_category::network,
              healthcare_audit_event::connection_closed)
        .description("Connection closed")
        .session(session_id)
        .property("reason", reason)
        .outcome("disconnected")
        .commit();
}

void healthcare_audit_logger::log_connection_rejected(std::string_view remote_address,
                                                       std::string_view reason) {
    log_event(healthcare_audit_category::network,
              healthcare_audit_event::connection_rejected)
        .severity(audit_severity::warning)
        .description("Connection rejected")
        .remote_address(remote_address)
        .property("reason", reason)
        .outcome("rejected")
        .commit();
}

// =============================================================================
// Integrity Verification
// =============================================================================

bool healthcare_audit_logger::verify_integrity(
    const std::filesystem::path& log_file) const {
    // Basic implementation - full HMAC verification would require
    // integration with the base audit_logger from logger_system
    std::filesystem::path path = log_file.empty() ? pimpl_->config_.log_path : log_file;

    if (!std::filesystem::exists(path)) {
        return false;
    }

    // Verify file is readable and not empty
    std::ifstream file(path);
    return file.good() && file.peek() != std::ifstream::traits_type::eof();
}

// =============================================================================
// Statistics
// =============================================================================

healthcare_audit_logger::statistics healthcare_audit_logger::get_statistics() const {
    return pimpl_->stats_;
}

const healthcare_audit_config& healthcare_audit_logger::config() const noexcept {
    return pimpl_->config_;
}

// =============================================================================
// Global Instance
// =============================================================================

namespace {

std::unique_ptr<healthcare_audit_logger> g_audit_logger;
std::once_flag g_audit_logger_init_flag;

}  // namespace

healthcare_audit_logger& global_healthcare_audit_logger() {
    std::call_once(g_audit_logger_init_flag, []() {
        g_audit_logger = std::make_unique<healthcare_audit_logger>();
    });
    return *g_audit_logger;
}

void init_global_healthcare_audit_logger(const healthcare_audit_config& config) {
    g_audit_logger = std::make_unique<healthcare_audit_logger>(config);
    g_audit_logger->start();
}

void shutdown_global_healthcare_audit_logger() {
    if (g_audit_logger) {
        g_audit_logger->stop();
        g_audit_logger.reset();
    }
}

}  // namespace pacs::bridge::security
