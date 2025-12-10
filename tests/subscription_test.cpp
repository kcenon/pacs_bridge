/**
 * @file subscription_test.cpp
 * @brief Unit tests for FHIR Subscription resource and manager
 *
 * Tests cover:
 * - Subscription resource creation and serialization
 * - Subscription status and channel type parsing
 * - Criteria parsing and matching
 * - Subscription storage (in-memory)
 * - Subscription manager CRUD operations
 * - Subscription handler integration
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/36
 */

#include "pacs/bridge/fhir/subscription_resource.h"
#include "pacs/bridge/fhir/subscription_manager.h"
#include "pacs/bridge/fhir/imaging_study_resource.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace pacs::bridge::fhir::test {

// =============================================================================
// Test Utilities
// =============================================================================

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_TEST(test_fn)                                                      \
    do {                                                                       \
        std::cout << "Running " << #test_fn << "... ";                         \
        if (test_fn()) {                                                       \
            std::cout << "PASSED" << std::endl;                                \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "FAILED" << std::endl;                                \
            failed++;                                                          \
        }                                                                      \
    } while (0)

// =============================================================================
// Subscription Status Tests
// =============================================================================

bool test_subscription_status_to_string() {
    TEST_ASSERT(to_string(subscription_status::requested) == "requested",
                "requested status string");
    TEST_ASSERT(to_string(subscription_status::active) == "active",
                "active status string");
    TEST_ASSERT(to_string(subscription_status::error) == "error",
                "error status string");
    TEST_ASSERT(to_string(subscription_status::off) == "off",
                "off status string");
    return true;
}

bool test_subscription_status_parsing() {
    auto requested = parse_subscription_status("requested");
    TEST_ASSERT(requested.has_value() &&
                *requested == subscription_status::requested,
                "parse requested");

    auto active = parse_subscription_status("ACTIVE");
    TEST_ASSERT(active.has_value() && *active == subscription_status::active,
                "parse ACTIVE (uppercase)");

    auto off = parse_subscription_status("off");
    TEST_ASSERT(off.has_value() && *off == subscription_status::off,
                "parse off");

    auto invalid = parse_subscription_status("invalid");
    TEST_ASSERT(!invalid.has_value(), "invalid status returns nullopt");

    return true;
}

// =============================================================================
// Channel Type Tests
// =============================================================================

bool test_channel_type_to_string() {
    TEST_ASSERT(to_string(subscription_channel_type::rest_hook) == "rest-hook",
                "rest-hook channel string");
    TEST_ASSERT(to_string(subscription_channel_type::websocket) == "websocket",
                "websocket channel string");
    TEST_ASSERT(to_string(subscription_channel_type::email) == "email",
                "email channel string");
    TEST_ASSERT(to_string(subscription_channel_type::message) == "message",
                "message channel string");
    return true;
}

bool test_channel_type_parsing() {
    auto rest_hook = parse_channel_type("rest-hook");
    TEST_ASSERT(rest_hook.has_value() &&
                *rest_hook == subscription_channel_type::rest_hook,
                "parse rest-hook");

    auto websocket = parse_channel_type("WEBSOCKET");
    TEST_ASSERT(websocket.has_value() &&
                *websocket == subscription_channel_type::websocket,
                "parse WEBSOCKET (uppercase)");

    auto invalid = parse_channel_type("invalid");
    TEST_ASSERT(!invalid.has_value(), "invalid channel type returns nullopt");

    return true;
}

// =============================================================================
// Subscription Resource Tests
// =============================================================================

bool test_subscription_resource_creation() {
    subscription_resource sub;
    sub.set_id("sub-123");
    sub.set_status(subscription_status::active);
    sub.set_criteria("ImagingStudy?status=available");
    sub.set_reason("Monitor for completed studies");

    subscription_channel channel;
    channel.type = subscription_channel_type::rest_hook;
    channel.endpoint = "https://emr.hospital.local/fhir-notify";
    channel.payload = "application/fhir+json";
    channel.header.push_back("Authorization: Bearer token123");
    sub.set_channel(channel);

    TEST_ASSERT(sub.id() == "sub-123", "subscription ID");
    TEST_ASSERT(sub.status() == subscription_status::active, "subscription status");
    TEST_ASSERT(sub.criteria() == "ImagingStudy?status=available",
                "subscription criteria");
    TEST_ASSERT(sub.reason().has_value() &&
                *sub.reason() == "Monitor for completed studies",
                "subscription reason");
    TEST_ASSERT(sub.channel().type == subscription_channel_type::rest_hook,
                "channel type");
    TEST_ASSERT(sub.channel().endpoint == "https://emr.hospital.local/fhir-notify",
                "channel endpoint");
    TEST_ASSERT(sub.channel().payload.has_value() &&
                *sub.channel().payload == "application/fhir+json",
                "channel payload");
    TEST_ASSERT(sub.channel().header.size() == 1, "channel header count");

    return true;
}

bool test_subscription_resource_type() {
    subscription_resource sub;

    TEST_ASSERT(sub.type() == resource_type::subscription, "resource type enum");
    TEST_ASSERT(sub.type_name() == "Subscription", "resource type name");

    return true;
}

bool test_subscription_resource_validation() {
    subscription_resource valid_sub;
    valid_sub.set_criteria("ImagingStudy?status=available");
    subscription_channel channel;
    channel.endpoint = "https://example.com/notify";
    valid_sub.set_channel(channel);

    TEST_ASSERT(valid_sub.validate(), "valid subscription validates");

    subscription_resource invalid_sub;
    // Missing criteria and endpoint
    TEST_ASSERT(!invalid_sub.validate(), "invalid subscription fails validation");

    return true;
}

bool test_subscription_resource_json_serialization() {
    subscription_resource sub;
    sub.set_id("sub-456");
    sub.set_status(subscription_status::active);
    sub.set_criteria("DiagnosticReport?status=final");
    sub.set_reason("Report notifications");

    subscription_channel channel;
    channel.type = subscription_channel_type::rest_hook;
    channel.endpoint = "https://ris.hospital.local/notify";
    channel.payload = "application/fhir+json";
    sub.set_channel(channel);

    std::string json = sub.to_json();

    TEST_ASSERT(json.find("\"resourceType\": \"Subscription\"") != std::string::npos,
                "JSON contains resourceType");
    TEST_ASSERT(json.find("\"id\": \"sub-456\"") != std::string::npos,
                "JSON contains id");
    TEST_ASSERT(json.find("\"status\": \"active\"") != std::string::npos,
                "JSON contains status");
    TEST_ASSERT(json.find("\"criteria\": \"DiagnosticReport?status=final\"") != std::string::npos,
                "JSON contains criteria");
    TEST_ASSERT(json.find("\"channel\"") != std::string::npos,
                "JSON contains channel");
    TEST_ASSERT(json.find("\"type\": \"rest-hook\"") != std::string::npos,
                "JSON contains channel type");
    TEST_ASSERT(json.find("\"endpoint\"") != std::string::npos,
                "JSON contains endpoint");

    return true;
}

bool test_subscription_resource_json_parsing() {
    std::string json = R"({
        "resourceType": "Subscription",
        "id": "sub-789",
        "status": "active",
        "criteria": "ImagingStudy?status=available",
        "reason": "Study monitoring",
        "channel": {
            "type": "rest-hook",
            "endpoint": "https://notify.example.com",
            "payload": "application/fhir+json"
        }
    })";

    auto sub = subscription_resource::from_json(json);

    TEST_ASSERT(sub != nullptr, "parsing succeeds");
    TEST_ASSERT(sub->id() == "sub-789", "parsed ID");
    TEST_ASSERT(sub->status() == subscription_status::active, "parsed status");
    TEST_ASSERT(sub->criteria() == "ImagingStudy?status=available", "parsed criteria");
    TEST_ASSERT(sub->reason().has_value() && *sub->reason() == "Study monitoring",
                "parsed reason");
    TEST_ASSERT(sub->channel().type == subscription_channel_type::rest_hook,
                "parsed channel type");
    TEST_ASSERT(sub->channel().endpoint == "https://notify.example.com",
                "parsed endpoint");

    return true;
}

// =============================================================================
// Criteria Parsing Tests
// =============================================================================

bool test_criteria_parsing_simple() {
    auto criteria = parse_subscription_criteria("ImagingStudy");

    TEST_ASSERT(criteria.has_value(), "parsing simple criteria succeeds");
    TEST_ASSERT(criteria->resource_type == "ImagingStudy", "resource type extracted");
    TEST_ASSERT(criteria->params.empty(), "no parameters");

    return true;
}

bool test_criteria_parsing_with_params() {
    auto criteria = parse_subscription_criteria("ImagingStudy?status=available");

    TEST_ASSERT(criteria.has_value(), "parsing criteria with params succeeds");
    TEST_ASSERT(criteria->resource_type == "ImagingStudy", "resource type extracted");
    TEST_ASSERT(criteria->params.size() == 1, "one parameter");
    TEST_ASSERT(criteria->params.count("status") == 1, "status param exists");
    TEST_ASSERT(criteria->params.at("status") == "available", "status param value");

    return true;
}

bool test_criteria_parsing_multiple_params() {
    auto criteria = parse_subscription_criteria(
        "DiagnosticReport?status=final&patient=Patient/123");

    TEST_ASSERT(criteria.has_value(), "parsing multiple params succeeds");
    TEST_ASSERT(criteria->resource_type == "DiagnosticReport", "resource type");
    TEST_ASSERT(criteria->params.size() == 2, "two parameters");
    TEST_ASSERT(criteria->params.at("status") == "final", "status param");
    TEST_ASSERT(criteria->params.at("patient") == "Patient/123", "patient param");

    return true;
}

bool test_criteria_parsing_empty() {
    auto criteria = parse_subscription_criteria("");

    TEST_ASSERT(!criteria.has_value(), "empty criteria returns nullopt");

    return true;
}

// =============================================================================
// Criteria Matching Tests
// =============================================================================

bool test_criteria_matching_type_only() {
    imaging_study_resource study;
    study.set_id("study-123");
    study.set_status(imaging_study_status::available);

    auto criteria = parse_subscription_criteria("ImagingStudy");
    TEST_ASSERT(criteria.has_value(), "criteria parsed");

    TEST_ASSERT(matches_criteria(study, *criteria),
                "study matches type-only criteria");

    return true;
}

bool test_criteria_matching_with_status() {
    imaging_study_resource study;
    study.set_id("study-456");
    study.set_status(imaging_study_status::available);

    auto match_criteria = parse_subscription_criteria("ImagingStudy?status=available");
    TEST_ASSERT(match_criteria.has_value(), "match criteria parsed");

    TEST_ASSERT(matches_criteria(study, *match_criteria),
                "study matches status=available criteria");

    auto no_match_criteria = parse_subscription_criteria("ImagingStudy?status=cancelled");
    TEST_ASSERT(no_match_criteria.has_value(), "no-match criteria parsed");

    TEST_ASSERT(!matches_criteria(study, *no_match_criteria),
                "study does not match status=cancelled criteria");

    return true;
}

bool test_criteria_matching_type_mismatch() {
    imaging_study_resource study;
    study.set_id("study-789");

    auto criteria = parse_subscription_criteria("DiagnosticReport");
    TEST_ASSERT(criteria.has_value(), "criteria parsed");

    TEST_ASSERT(!matches_criteria(study, *criteria),
                "ImagingStudy does not match DiagnosticReport criteria");

    return true;
}

// =============================================================================
// Storage Tests
// =============================================================================

bool test_in_memory_storage_crud() {
    in_memory_subscription_storage storage;

    // Create subscription
    subscription_resource sub;
    sub.set_id("sub-storage-1");
    sub.set_status(subscription_status::active);
    sub.set_criteria("ImagingStudy?status=available");
    subscription_channel channel;
    channel.endpoint = "https://example.com/notify";
    sub.set_channel(channel);

    // Store
    TEST_ASSERT(storage.store("sub-storage-1", sub), "store succeeds");

    // Get
    auto retrieved = storage.get("sub-storage-1");
    TEST_ASSERT(retrieved != nullptr, "retrieval succeeds");
    TEST_ASSERT(retrieved->id() == "sub-storage-1", "retrieved ID matches");
    TEST_ASSERT(retrieved->status() == subscription_status::active,
                "retrieved status matches");

    // Update
    sub.set_status(subscription_status::off);
    TEST_ASSERT(storage.update("sub-storage-1", sub), "update succeeds");

    auto updated = storage.get("sub-storage-1");
    TEST_ASSERT(updated != nullptr, "updated retrieval succeeds");
    TEST_ASSERT(updated->status() == subscription_status::off,
                "updated status matches");

    // Remove
    TEST_ASSERT(storage.remove("sub-storage-1"), "remove succeeds");
    TEST_ASSERT(storage.get("sub-storage-1") == nullptr,
                "removed subscription not found");

    return true;
}

bool test_in_memory_storage_get_active() {
    in_memory_subscription_storage storage;

    // Create multiple subscriptions
    subscription_resource sub1;
    sub1.set_id("sub-active-1");
    sub1.set_status(subscription_status::active);
    sub1.set_criteria("ImagingStudy");
    subscription_channel channel1;
    channel1.endpoint = "https://example.com/1";
    sub1.set_channel(channel1);
    storage.store("sub-active-1", sub1);

    subscription_resource sub2;
    sub2.set_id("sub-off-1");
    sub2.set_status(subscription_status::off);
    sub2.set_criteria("ImagingStudy");
    subscription_channel channel2;
    channel2.endpoint = "https://example.com/2";
    sub2.set_channel(channel2);
    storage.store("sub-off-1", sub2);

    subscription_resource sub3;
    sub3.set_id("sub-active-2");
    sub3.set_status(subscription_status::active);
    sub3.set_criteria("DiagnosticReport");
    subscription_channel channel3;
    channel3.endpoint = "https://example.com/3";
    sub3.set_channel(channel3);
    storage.store("sub-active-2", sub3);

    // Get active subscriptions
    auto active = storage.get_active();
    TEST_ASSERT(active.size() == 2, "two active subscriptions");

    return true;
}

bool test_in_memory_storage_get_by_resource_type() {
    in_memory_subscription_storage storage;

    subscription_resource sub1;
    sub1.set_id("sub-imaging-1");
    sub1.set_status(subscription_status::active);
    sub1.set_criteria("ImagingStudy?status=available");
    subscription_channel channel1;
    channel1.endpoint = "https://example.com/1";
    sub1.set_channel(channel1);
    storage.store("sub-imaging-1", sub1);

    subscription_resource sub2;
    sub2.set_id("sub-report-1");
    sub2.set_status(subscription_status::active);
    sub2.set_criteria("DiagnosticReport?status=final");
    subscription_channel channel2;
    channel2.endpoint = "https://example.com/2";
    sub2.set_channel(channel2);
    storage.store("sub-report-1", sub2);

    // Get by resource type
    auto imaging_subs = storage.get_by_resource_type("ImagingStudy");
    TEST_ASSERT(imaging_subs.size() == 1, "one ImagingStudy subscription");
    TEST_ASSERT(imaging_subs[0]->id() == "sub-imaging-1", "correct subscription");

    auto report_subs = storage.get_by_resource_type("DiagnosticReport");
    TEST_ASSERT(report_subs.size() == 1, "one DiagnosticReport subscription");

    auto patient_subs = storage.get_by_resource_type("Patient");
    TEST_ASSERT(patient_subs.empty(), "no Patient subscriptions");

    return true;
}

// =============================================================================
// Manager Tests
// =============================================================================

bool test_manager_create_subscription() {
    auto storage = std::make_shared<in_memory_subscription_storage>();
    delivery_config config;
    config.enabled = false;  // Disable delivery for testing
    subscription_manager manager(storage, config);

    subscription_resource sub;
    sub.set_status(subscription_status::active);
    sub.set_criteria("ImagingStudy?status=available");
    subscription_channel channel;
    channel.endpoint = "https://example.com/notify";
    sub.set_channel(channel);

    auto result = manager.create_subscription(sub);

    TEST_ASSERT(is_success(result), "create succeeds");

    const auto& created = get_resource(result);
    TEST_ASSERT(!created->id().empty(), "ID assigned");
    TEST_ASSERT(created->status() == subscription_status::active, "status preserved");
    TEST_ASSERT(created->criteria() == "ImagingStudy?status=available",
                "criteria preserved");

    return true;
}

bool test_manager_get_subscription() {
    auto storage = std::make_shared<in_memory_subscription_storage>();
    delivery_config config;
    config.enabled = false;
    subscription_manager manager(storage, config);

    // Create subscription
    subscription_resource sub;
    sub.set_status(subscription_status::active);
    sub.set_criteria("ImagingStudy");
    subscription_channel channel;
    channel.endpoint = "https://example.com";
    sub.set_channel(channel);

    auto create_result = manager.create_subscription(sub);
    TEST_ASSERT(is_success(create_result), "create succeeds");

    std::string id = get_resource(create_result)->id();

    // Get subscription
    auto get_result = manager.get_subscription(id);
    TEST_ASSERT(is_success(get_result), "get succeeds");
    TEST_ASSERT(get_resource(get_result)->id() == id, "ID matches");

    // Get non-existent
    auto not_found = manager.get_subscription("non-existent");
    TEST_ASSERT(!is_success(not_found), "get non-existent fails");

    return true;
}

bool test_manager_update_subscription() {
    auto storage = std::make_shared<in_memory_subscription_storage>();
    delivery_config config;
    config.enabled = false;
    subscription_manager manager(storage, config);

    // Create subscription
    subscription_resource sub;
    sub.set_status(subscription_status::active);
    sub.set_criteria("ImagingStudy");
    subscription_channel channel;
    channel.endpoint = "https://example.com";
    sub.set_channel(channel);

    auto create_result = manager.create_subscription(sub);
    TEST_ASSERT(is_success(create_result), "create succeeds");

    std::string id = get_resource(create_result)->id();

    // Update subscription
    subscription_resource updated_sub;
    updated_sub.set_status(subscription_status::off);
    updated_sub.set_criteria("ImagingStudy");
    updated_sub.set_channel(channel);

    auto update_result = manager.update_subscription(id, updated_sub);
    TEST_ASSERT(is_success(update_result), "update succeeds");
    TEST_ASSERT(get_resource(update_result)->status() == subscription_status::off,
                "status updated");
    TEST_ASSERT(get_resource(update_result)->version_id() == "2",
                "version incremented");

    return true;
}

bool test_manager_delete_subscription() {
    auto storage = std::make_shared<in_memory_subscription_storage>();
    delivery_config config;
    config.enabled = false;
    subscription_manager manager(storage, config);

    // Create subscription
    subscription_resource sub;
    sub.set_status(subscription_status::active);
    sub.set_criteria("ImagingStudy");
    subscription_channel channel;
    channel.endpoint = "https://example.com";
    sub.set_channel(channel);

    auto create_result = manager.create_subscription(sub);
    TEST_ASSERT(is_success(create_result), "create succeeds");

    std::string id = get_resource(create_result)->id();

    // Delete subscription
    auto delete_result = manager.delete_subscription(id);
    TEST_ASSERT(is_success(delete_result), "delete succeeds");

    // Verify deleted
    auto get_result = manager.get_subscription(id);
    TEST_ASSERT(!is_success(get_result), "get after delete fails");

    return true;
}

bool test_manager_statistics() {
    auto storage = std::make_shared<in_memory_subscription_storage>();
    delivery_config config;
    config.enabled = false;
    subscription_manager manager(storage, config);

    // Create active subscription
    subscription_resource sub;
    sub.set_status(subscription_status::active);
    sub.set_criteria("ImagingStudy");
    subscription_channel channel;
    channel.endpoint = "https://example.com";
    sub.set_channel(channel);

    auto result = manager.create_subscription(sub);
    TEST_ASSERT(is_success(result), "create succeeds");

    auto stats = manager.get_statistics();
    TEST_ASSERT(stats.active_subscriptions == 1, "one active subscription");

    return true;
}

// =============================================================================
// Handler Tests
// =============================================================================

bool test_handler_type_info() {
    auto storage = std::make_shared<in_memory_subscription_storage>();
    auto manager = std::make_shared<subscription_manager>(storage);
    subscription_handler handler(manager);

    TEST_ASSERT(handler.handled_type() == resource_type::subscription,
                "handled type is subscription");
    TEST_ASSERT(handler.type_name() == "Subscription", "type name is Subscription");

    return true;
}

bool test_handler_supported_interactions() {
    auto storage = std::make_shared<in_memory_subscription_storage>();
    auto manager = std::make_shared<subscription_manager>(storage);
    subscription_handler handler(manager);

    TEST_ASSERT(handler.supports_interaction(interaction_type::read),
                "supports read");
    TEST_ASSERT(handler.supports_interaction(interaction_type::create),
                "supports create");
    TEST_ASSERT(handler.supports_interaction(interaction_type::update),
                "supports update");
    TEST_ASSERT(handler.supports_interaction(interaction_type::delete_resource),
                "supports delete");
    TEST_ASSERT(handler.supports_interaction(interaction_type::search),
                "supports search");
    TEST_ASSERT(!handler.supports_interaction(interaction_type::vread),
                "does not support vread");

    return true;
}

bool test_handler_search() {
    auto storage = std::make_shared<in_memory_subscription_storage>();
    auto manager = std::make_shared<subscription_manager>(storage);
    subscription_handler handler(manager);

    // Create subscriptions
    subscription_resource sub1;
    sub1.set_status(subscription_status::active);
    sub1.set_criteria("ImagingStudy");
    subscription_channel channel;
    channel.endpoint = "https://example.com";
    sub1.set_channel(channel);
    manager->create_subscription(sub1);

    subscription_resource sub2;
    sub2.set_status(subscription_status::off);
    sub2.set_criteria("DiagnosticReport");
    sub2.set_channel(channel);
    manager->create_subscription(sub2);

    // Search all
    std::map<std::string, std::string> empty_params;
    pagination_params pagination;
    auto all_result = handler.search(empty_params, pagination);

    TEST_ASSERT(is_success(all_result), "search all succeeds");
    TEST_ASSERT(get_resource(all_result).total == 2, "found 2 subscriptions");

    // Search by status
    std::map<std::string, std::string> status_params = {{"status", "active"}};
    auto active_result = handler.search(status_params, pagination);

    TEST_ASSERT(is_success(active_result), "search by status succeeds");
    TEST_ASSERT(get_resource(active_result).total == 1, "found 1 active subscription");

    return true;
}

}  // namespace pacs::bridge::fhir::test

// =============================================================================
// Main
// =============================================================================

int main() {
    using namespace pacs::bridge::fhir::test;

    int passed = 0;
    int failed = 0;

    std::cout << "=== FHIR Subscription Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "--- Status Tests ---" << std::endl;
    RUN_TEST(test_subscription_status_to_string);
    RUN_TEST(test_subscription_status_parsing);
    std::cout << std::endl;

    std::cout << "--- Channel Type Tests ---" << std::endl;
    RUN_TEST(test_channel_type_to_string);
    RUN_TEST(test_channel_type_parsing);
    std::cout << std::endl;

    std::cout << "--- Subscription Resource Tests ---" << std::endl;
    RUN_TEST(test_subscription_resource_creation);
    RUN_TEST(test_subscription_resource_type);
    RUN_TEST(test_subscription_resource_validation);
    RUN_TEST(test_subscription_resource_json_serialization);
    RUN_TEST(test_subscription_resource_json_parsing);
    std::cout << std::endl;

    std::cout << "--- Criteria Parsing Tests ---" << std::endl;
    RUN_TEST(test_criteria_parsing_simple);
    RUN_TEST(test_criteria_parsing_with_params);
    RUN_TEST(test_criteria_parsing_multiple_params);
    RUN_TEST(test_criteria_parsing_empty);
    std::cout << std::endl;

    std::cout << "--- Criteria Matching Tests ---" << std::endl;
    RUN_TEST(test_criteria_matching_type_only);
    RUN_TEST(test_criteria_matching_with_status);
    RUN_TEST(test_criteria_matching_type_mismatch);
    std::cout << std::endl;

    std::cout << "--- Storage Tests ---" << std::endl;
    RUN_TEST(test_in_memory_storage_crud);
    RUN_TEST(test_in_memory_storage_get_active);
    RUN_TEST(test_in_memory_storage_get_by_resource_type);
    std::cout << std::endl;

    std::cout << "--- Manager Tests ---" << std::endl;
    RUN_TEST(test_manager_create_subscription);
    RUN_TEST(test_manager_get_subscription);
    RUN_TEST(test_manager_update_subscription);
    RUN_TEST(test_manager_delete_subscription);
    RUN_TEST(test_manager_statistics);
    std::cout << std::endl;

    std::cout << "--- Handler Tests ---" << std::endl;
    RUN_TEST(test_handler_type_info);
    RUN_TEST(test_handler_supported_interactions);
    RUN_TEST(test_handler_search);
    std::cout << std::endl;

    std::cout << "================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << std::endl;

    return failed > 0 ? 1 : 0;
}
