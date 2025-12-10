#ifndef PACS_BRIDGE_FHIR_SUBSCRIPTION_RESOURCE_H
#define PACS_BRIDGE_FHIR_SUBSCRIPTION_RESOURCE_H

/**
 * @file subscription_resource.h
 * @brief FHIR Subscription resource implementation
 *
 * Implements the FHIR R4 Subscription resource for event-based notifications
 * when studies become available or reports are completed.
 *
 * @see https://hl7.org/fhir/R4/subscription.html
 * @see https://github.com/kcenon/pacs_bridge/issues/36
 */

#include "fhir_resource.h"
#include "fhir_types.h"
#include "resource_handler.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::fhir {

// =============================================================================
// FHIR Subscription Status Codes
// =============================================================================

/**
 * @brief FHIR Subscription status codes
 *
 * @see https://hl7.org/fhir/R4/valueset-subscription-status.html
 */
enum class subscription_status {
    requested,
    active,
    error,
    off
};

/**
 * @brief Convert subscription_status to FHIR code string
 */
[[nodiscard]] constexpr std::string_view to_string(
    subscription_status status) noexcept {
    switch (status) {
        case subscription_status::requested: return "requested";
        case subscription_status::active: return "active";
        case subscription_status::error: return "error";
        case subscription_status::off: return "off";
    }
    return "off";
}

/**
 * @brief Parse subscription_status from string
 */
[[nodiscard]] std::optional<subscription_status> parse_subscription_status(
    std::string_view status_str) noexcept;

// =============================================================================
// FHIR Subscription Channel Types
// =============================================================================

/**
 * @brief FHIR Subscription channel types
 *
 * @see https://hl7.org/fhir/R4/valueset-subscription-channel-type.html
 */
enum class subscription_channel_type {
    rest_hook,
    websocket,
    email,
    message
};

/**
 * @brief Convert subscription_channel_type to FHIR code string
 */
[[nodiscard]] constexpr std::string_view to_string(
    subscription_channel_type type) noexcept {
    switch (type) {
        case subscription_channel_type::rest_hook: return "rest-hook";
        case subscription_channel_type::websocket: return "websocket";
        case subscription_channel_type::email: return "email";
        case subscription_channel_type::message: return "message";
    }
    return "rest-hook";
}

/**
 * @brief Parse subscription_channel_type from string
 */
[[nodiscard]] std::optional<subscription_channel_type> parse_channel_type(
    std::string_view type_str) noexcept;

// =============================================================================
// FHIR Subscription Data Types
// =============================================================================

/**
 * @brief FHIR Subscription.channel element
 *
 * Details where notifications should be sent.
 * @see https://hl7.org/fhir/R4/subscription-definitions.html#Subscription.channel
 */
struct subscription_channel {
    /** The type of channel to send notifications on */
    subscription_channel_type type = subscription_channel_type::rest_hook;

    /** The url that describes the actual end-point to send notifications */
    std::string endpoint;

    /** MIME type to send (e.g., "application/fhir+json") */
    std::optional<std::string> payload;

    /** Additional headers for rest-hook channel */
    std::vector<std::string> header;
};

/**
 * @brief Subscription delivery status for tracking
 */
enum class delivery_status {
    pending,
    in_progress,
    completed,
    failed,
    abandoned
};

/**
 * @brief Convert delivery_status to string
 */
[[nodiscard]] constexpr std::string_view to_string(
    delivery_status status) noexcept {
    switch (status) {
        case delivery_status::pending: return "pending";
        case delivery_status::in_progress: return "in-progress";
        case delivery_status::completed: return "completed";
        case delivery_status::failed: return "failed";
        case delivery_status::abandoned: return "abandoned";
    }
    return "pending";
}

/**
 * @brief Parse delivery_status from string
 */
[[nodiscard]] std::optional<delivery_status> parse_delivery_status(
    std::string_view status_str) noexcept;

/**
 * @brief Delivery attempt record
 */
struct delivery_attempt {
    std::chrono::system_clock::time_point timestamp;
    delivery_status status = delivery_status::pending;
    std::optional<int> http_status_code;
    std::optional<std::string> error_message;
    uint32_t attempt_number = 0;
};

/**
 * @brief Notification delivery record
 */
struct notification_record {
    std::string id;
    std::string subscription_id;
    std::string resource_type;
    std::string resource_id;
    std::chrono::system_clock::time_point created_at;
    delivery_status status = delivery_status::pending;
    std::vector<delivery_attempt> attempts;
    uint32_t retry_count = 0;
    std::optional<std::chrono::system_clock::time_point> next_retry_at;
};

// =============================================================================
// FHIR Subscription Resource
// =============================================================================

/**
 * @brief FHIR R4 Subscription resource
 *
 * Represents subscription information per FHIR R4 specification.
 * Used for event-based notifications when studies become available
 * or reports are completed.
 *
 * @example Creating a Subscription Resource
 * ```cpp
 * subscription_resource subscription;
 * subscription.set_id("subscription-123");
 * subscription.set_status(subscription_status::active);
 *
 * // Set criteria for ImagingStudy availability notifications
 * subscription.set_criteria("ImagingStudy?status=available");
 *
 * // Set reason
 * subscription.set_reason("Monitor for completed studies");
 *
 * // Configure channel
 * subscription_channel channel;
 * channel.type = subscription_channel_type::rest_hook;
 * channel.endpoint = "https://emr.hospital.local/fhir-notify";
 * channel.payload = "application/fhir+json";
 * channel.header.push_back("Authorization: Bearer xxx");
 * subscription.set_channel(channel);
 *
 * // Serialize to JSON
 * std::string json = subscription.to_json();
 * ```
 *
 * @see https://hl7.org/fhir/R4/subscription.html
 */
class subscription_resource : public fhir_resource {
public:
    /**
     * @brief Default constructor
     */
    subscription_resource();

    /**
     * @brief Destructor
     */
    ~subscription_resource() override;

    // Non-copyable, movable
    subscription_resource(const subscription_resource&) = delete;
    subscription_resource& operator=(const subscription_resource&) = delete;
    subscription_resource(subscription_resource&&) noexcept;
    subscription_resource& operator=(subscription_resource&&) noexcept;

    // =========================================================================
    // fhir_resource Interface
    // =========================================================================

    /**
     * @brief Get resource type
     */
    [[nodiscard]] resource_type type() const noexcept override;

    /**
     * @brief Get resource type name
     */
    [[nodiscard]] std::string type_name() const override;

    /**
     * @brief Serialize to JSON
     */
    [[nodiscard]] std::string to_json() const override;

    /**
     * @brief Validate the resource
     */
    [[nodiscard]] bool validate() const override;

    // =========================================================================
    // Status
    // =========================================================================

    /**
     * @brief Set status (required)
     */
    void set_status(subscription_status status);

    /**
     * @brief Get status
     */
    [[nodiscard]] subscription_status status() const noexcept;

    // =========================================================================
    // Contact Information
    // =========================================================================

    /**
     * @brief Add a contact point
     */
    void add_contact(const std::string& contact);

    /**
     * @brief Get all contact points
     */
    [[nodiscard]] const std::vector<std::string>& contacts() const noexcept;

    /**
     * @brief Clear all contact points
     */
    void clear_contacts();

    // =========================================================================
    // Subscription Details
    // =========================================================================

    /**
     * @brief Set end time (when subscription should expire)
     */
    void set_end(std::string datetime);

    /**
     * @brief Get end time
     */
    [[nodiscard]] const std::optional<std::string>& end() const noexcept;

    /**
     * @brief Set reason for the subscription
     */
    void set_reason(std::string reason);

    /**
     * @brief Get reason
     */
    [[nodiscard]] const std::optional<std::string>& reason() const noexcept;

    /**
     * @brief Set criteria (search URL for triggering events)
     */
    void set_criteria(std::string criteria);

    /**
     * @brief Get criteria
     */
    [[nodiscard]] const std::string& criteria() const noexcept;

    /**
     * @brief Set error message (populated when status is "error")
     */
    void set_error(std::string error);

    /**
     * @brief Get error message
     */
    [[nodiscard]] const std::optional<std::string>& error() const noexcept;

    // =========================================================================
    // Channel
    // =========================================================================

    /**
     * @brief Set channel (required)
     */
    void set_channel(const subscription_channel& channel);

    /**
     * @brief Get channel
     */
    [[nodiscard]] const subscription_channel& channel() const noexcept;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Create Subscription resource from JSON
     * @param json JSON string
     * @return Subscription resource or nullptr on parse error
     */
    [[nodiscard]] static std::unique_ptr<subscription_resource> from_json(
        const std::string& json);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Criteria Matching
// =============================================================================

/**
 * @brief Parsed criteria components
 */
struct parsed_criteria {
    std::string resource_type;
    std::map<std::string, std::string> params;
};

/**
 * @brief Parse subscription criteria string
 *
 * Parses criteria like "ImagingStudy?status=available" into
 * resource type and search parameters.
 *
 * @param criteria Criteria string
 * @return Parsed criteria or nullopt if invalid
 */
[[nodiscard]] std::optional<parsed_criteria> parse_subscription_criteria(
    std::string_view criteria) noexcept;

/**
 * @brief Check if a resource matches subscription criteria
 *
 * @param resource Resource to check
 * @param criteria Parsed criteria
 * @return true if resource matches criteria
 */
[[nodiscard]] bool matches_criteria(
    const fhir_resource& resource,
    const parsed_criteria& criteria);

}  // namespace pacs::bridge::fhir

#endif  // PACS_BRIDGE_FHIR_SUBSCRIPTION_RESOURCE_H
