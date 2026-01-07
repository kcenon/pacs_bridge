/**
 * @file outbound_router_test.cpp
 * @brief Unit tests for outbound message router
 *
 * Tests for destination selection, priority-based routing,
 * failover logic, and health checking.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/28
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/router/outbound_router.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"

#include "utils/test_helpers.h"

#include <chrono>
#include <thread>

namespace pacs::bridge::router {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;
using namespace pacs::bridge::hl7;

// =============================================================================
// Destination Configuration Tests
// =============================================================================

class OutboundDestinationTest : public pacs_bridge_test {};

TEST_F(OutboundDestinationTest, DefaultValues) {
    outbound_destination dest;

    EXPECT_TRUE(dest.name.empty());
    EXPECT_TRUE(dest.host.empty());
    EXPECT_EQ(dest.port, mllp::MLLP_DEFAULT_PORT);
    EXPECT_TRUE(dest.message_types.empty());
    EXPECT_EQ(dest.priority, 100);
    EXPECT_TRUE(dest.enabled);
    EXPECT_EQ(dest.retry_count, 3u);
}

TEST_F(OutboundDestinationTest, ValidationEmpty) {
    outbound_destination dest;
    EXPECT_FALSE(dest.is_valid());

    dest.name = "TEST";
    EXPECT_FALSE(dest.is_valid());  // Still missing host

    dest.host = "localhost";
    EXPECT_TRUE(dest.is_valid());
}

TEST_F(OutboundDestinationTest, ValidationInvalidPort) {
    outbound_destination dest;
    dest.name = "TEST";
    dest.host = "localhost";
    dest.port = 0;

    EXPECT_FALSE(dest.is_valid());
}

TEST_F(OutboundDestinationTest, ToClientConfig) {
    outbound_destination dest;
    dest.name = "RIS";
    dest.host = "ris.hospital.local";
    dest.port = 2576;
    dest.connect_timeout = std::chrono::milliseconds{3000};
    dest.io_timeout = std::chrono::milliseconds{15000};
    dest.retry_count = 5;

    auto config = dest.to_client_config();

    EXPECT_EQ(config.host, "ris.hospital.local");
    EXPECT_EQ(config.port, 2576);
    EXPECT_EQ(config.connect_timeout, std::chrono::milliseconds{3000});
    EXPECT_EQ(config.io_timeout, std::chrono::milliseconds{15000});
    EXPECT_EQ(config.retry_count, 5u);
}

// =============================================================================
// Destination Builder Tests
// =============================================================================

class DestinationBuilderTest : public pacs_bridge_test {};

TEST_F(DestinationBuilderTest, BasicBuild) {
    auto dest = destination_builder::create("RIS_PRIMARY")
                    .host("ris.hospital.local")
                    .port(2576)
                    .build();

    EXPECT_EQ(dest.name, "RIS_PRIMARY");
    EXPECT_EQ(dest.host, "ris.hospital.local");
    EXPECT_EQ(dest.port, 2576);
    EXPECT_TRUE(dest.is_valid());
}

TEST_F(DestinationBuilderTest, FullConfiguration) {
    auto dest = destination_builder::create("RIS")
                    .host("ris.hospital.local")
                    .port(2576)
                    .message_types({"ORM^O01", "ORU^R01"})
                    .priority(1)
                    .enabled(true)
                    .connect_timeout(std::chrono::milliseconds{5000})
                    .io_timeout(std::chrono::milliseconds{30000})
                    .retry(3, std::chrono::milliseconds{1000})
                    .health_check_interval(std::chrono::seconds{60})
                    .description("Primary RIS endpoint")
                    .build();

    EXPECT_EQ(dest.name, "RIS");
    EXPECT_EQ(dest.host, "ris.hospital.local");
    EXPECT_EQ(dest.port, 2576);
    EXPECT_EQ(dest.message_types.size(), 2u);
    EXPECT_EQ(dest.priority, 1);
    EXPECT_TRUE(dest.enabled);
    EXPECT_EQ(dest.connect_timeout, std::chrono::milliseconds{5000});
    EXPECT_EQ(dest.retry_count, 3u);
    EXPECT_EQ(dest.health_check_interval, std::chrono::seconds{60});
    EXPECT_EQ(dest.description, "Primary RIS endpoint");
}

TEST_F(DestinationBuilderTest, SingleMessageType) {
    auto dest = destination_builder::create("REPORTS")
                    .host("reports.hospital.local")
                    .port(2577)
                    .message_type("ORU^R01")
                    .message_type("MDM^T02")
                    .build();

    EXPECT_EQ(dest.message_types.size(), 2u);
    EXPECT_THAT(dest.message_types, Contains("ORU^R01"));
    EXPECT_THAT(dest.message_types, Contains("MDM^T02"));
}

// =============================================================================
// Outbound Router Tests
// =============================================================================

class OutboundRouterTest : public pacs_bridge_test {};

TEST_F(OutboundRouterTest, DefaultConstruction) {
    outbound_router router;

    EXPECT_FALSE(router.is_running());
    EXPECT_TRUE(router.destinations().empty());
}

TEST_F(OutboundRouterTest, ConfiguredConstruction) {
    outbound_router_config config;

    outbound_destination dest1;
    dest1.name = "RIS_PRIMARY";
    dest1.host = "ris1.local";
    dest1.port = 2576;
    dest1.message_types = {"ORM^O01"};
    dest1.priority = 1;
    config.destinations.push_back(dest1);

    outbound_destination dest2;
    dest2.name = "RIS_BACKUP";
    dest2.host = "ris2.local";
    dest2.port = 2576;
    dest2.message_types = {"ORM^O01"};
    dest2.priority = 2;
    config.destinations.push_back(dest2);

    outbound_router router(config);

    auto dests = router.destinations();
    EXPECT_EQ(dests.size(), 2u);
}

TEST_F(OutboundRouterTest, StartStopLifecycle) {
    outbound_router_config config;
    config.enable_health_check = false;  // Disable for unit test

    outbound_router router(config);

    EXPECT_FALSE(router.is_running());

    auto start_result = router.start();
    EXPECT_TRUE(start_result.has_value());
    EXPECT_TRUE(router.is_running());

    // Starting again should fail
    auto second_start = router.start();
    EXPECT_FALSE(second_start.has_value());
    EXPECT_EQ(second_start.error(), outbound_error::already_running);

    router.stop();
    EXPECT_FALSE(router.is_running());
}

TEST_F(OutboundRouterTest, RouteBeforeStart) {
    outbound_router router;

    auto msg_result = hl7_builder::create()
                          .sending_app("HIS")
                          .receiving_app("PACS")
                          .message_type("ORM", "O01")
                          .control_id("MSG001")
                          .build();

    ASSERT_TRUE(msg_result.is_ok());

    auto route_result = router.route(msg_result.value());
    EXPECT_FALSE(route_result.has_value());
    EXPECT_EQ(route_result.error(), outbound_error::not_running);
}

TEST_F(OutboundRouterTest, GetDestinationsForMessageType) {
    outbound_router_config config;
    config.enable_health_check = false;

    outbound_destination orm_dest;
    orm_dest.name = "ORM_HANDLER";
    orm_dest.host = "localhost";
    orm_dest.port = 2576;
    orm_dest.message_types = {"ORM^O01"};
    orm_dest.priority = 1;
    config.destinations.push_back(orm_dest);

    outbound_destination oru_dest;
    oru_dest.name = "ORU_HANDLER";
    oru_dest.host = "localhost";
    oru_dest.port = 2577;
    oru_dest.message_types = {"ORU^R01"};
    oru_dest.priority = 1;
    config.destinations.push_back(oru_dest);

    outbound_destination all_dest;
    all_dest.name = "CATCH_ALL";
    all_dest.host = "localhost";
    all_dest.port = 2578;
    all_dest.message_types = {"*"};
    all_dest.priority = 100;
    config.destinations.push_back(all_dest);

    outbound_router router(config);

    // ORM^O01 should match ORM_HANDLER and CATCH_ALL
    auto orm_dests = router.get_destinations("ORM^O01");
    EXPECT_GE(orm_dests.size(), 1u);
    EXPECT_THAT(orm_dests, Contains("ORM_HANDLER"));

    // ORU^R01 should match ORU_HANDLER and CATCH_ALL
    auto oru_dests = router.get_destinations("ORU^R01");
    EXPECT_GE(oru_dests.size(), 1u);
    EXPECT_THAT(oru_dests, Contains("ORU_HANDLER"));

    // Unknown type should match CATCH_ALL only
    auto unknown_dests = router.get_destinations("ZZZ^Z01");
    EXPECT_GE(unknown_dests.size(), 1u);
    EXPECT_THAT(unknown_dests, Contains("CATCH_ALL"));
}

TEST_F(OutboundRouterTest, PriorityOrdering) {
    outbound_router_config config;
    config.enable_health_check = false;

    // Add destinations in non-priority order
    outbound_destination low_priority;
    low_priority.name = "LOW_PRIORITY";
    low_priority.host = "localhost";
    low_priority.port = 2576;
    low_priority.message_types = {"ORM^O01"};
    low_priority.priority = 100;
    config.destinations.push_back(low_priority);

    outbound_destination high_priority;
    high_priority.name = "HIGH_PRIORITY";
    high_priority.host = "localhost";
    high_priority.port = 2577;
    high_priority.message_types = {"ORM^O01"};
    high_priority.priority = 1;
    config.destinations.push_back(high_priority);

    outbound_destination medium_priority;
    medium_priority.name = "MEDIUM_PRIORITY";
    medium_priority.host = "localhost";
    medium_priority.port = 2578;
    medium_priority.message_types = {"ORM^O01"};
    medium_priority.priority = 50;
    config.destinations.push_back(medium_priority);

    outbound_router router(config);

    auto dests = router.get_destinations("ORM^O01");
    ASSERT_EQ(dests.size(), 3u);

    // Should be sorted by priority (lower = higher priority)
    EXPECT_EQ(dests[0], "HIGH_PRIORITY");
    EXPECT_EQ(dests[1], "MEDIUM_PRIORITY");
    EXPECT_EQ(dests[2], "LOW_PRIORITY");
}

TEST_F(OutboundRouterTest, DestinationManagement) {
    outbound_router_config config;
    config.enable_health_check = false;

    outbound_router router(config);

    EXPECT_TRUE(router.destinations().empty());

    // Add destination
    outbound_destination dest;
    dest.name = "NEW_DEST";
    dest.host = "localhost";
    dest.port = 2576;

    auto add_result = router.add_destination(dest);
    EXPECT_TRUE(add_result.has_value());
    EXPECT_EQ(router.destinations().size(), 1u);

    // Get destination
    const auto* retrieved = router.get_destination("NEW_DEST");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "NEW_DEST");
    EXPECT_EQ(retrieved->host, "localhost");

    // Get non-existent destination
    EXPECT_EQ(router.get_destination("UNKNOWN"), nullptr);

    // Enable/disable
    EXPECT_TRUE(router.set_destination_enabled("NEW_DEST", false));
    retrieved = router.get_destination("NEW_DEST");
    EXPECT_FALSE(retrieved->enabled);

    EXPECT_FALSE(router.set_destination_enabled("UNKNOWN", true));

    // Remove destination
    EXPECT_TRUE(router.remove_destination("NEW_DEST"));
    EXPECT_TRUE(router.destinations().empty());
    EXPECT_FALSE(router.remove_destination("NEW_DEST"));  // Already removed
}

TEST_F(OutboundRouterTest, AddInvalidDestination) {
    outbound_router router;

    outbound_destination invalid;
    // Missing name and host

    auto result = router.add_destination(invalid);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), outbound_error::invalid_configuration);
}

TEST_F(OutboundRouterTest, HealthStatusInitialization) {
    outbound_router_config config;
    config.enable_health_check = false;

    outbound_destination dest;
    dest.name = "TEST";
    dest.host = "localhost";
    dest.port = 2576;
    config.destinations.push_back(dest);

    outbound_router router(config);

    // Initial health should be unknown
    EXPECT_EQ(router.get_destination_health("TEST"), destination_health::unknown);
    EXPECT_EQ(router.get_destination_health("UNKNOWN"), destination_health::unknown);

    auto all_health = router.get_all_health();
    EXPECT_EQ(all_health.size(), 1u);
    EXPECT_EQ(all_health["TEST"], destination_health::unknown);
}

TEST_F(OutboundRouterTest, Statistics) {
    outbound_router_config config;
    config.enable_health_check = false;

    outbound_router router(config);

    auto stats = router.get_statistics();
    EXPECT_EQ(stats.total_messages, 0u);
    EXPECT_EQ(stats.successful_deliveries, 0u);
    EXPECT_EQ(stats.failed_deliveries, 0u);
    EXPECT_EQ(stats.failover_events, 0u);

    router.reset_statistics();
    stats = router.get_statistics();
    EXPECT_EQ(stats.total_messages, 0u);
}

TEST_F(OutboundRouterTest, HealthCallback) {
    outbound_router_config config;
    config.enable_health_check = false;

    outbound_destination dest;
    dest.name = "TEST";
    dest.host = "localhost";
    dest.port = 2576;
    config.destinations.push_back(dest);

    outbound_router router(config);

    bool callback_invoked = false;
    std::string callback_dest_name;
    destination_health callback_old_health;
    destination_health callback_new_health;

    router.set_health_callback(
        [&](std::string_view name, destination_health old_h, destination_health new_h) {
            callback_invoked = true;
            callback_dest_name = std::string(name);
            callback_old_health = old_h;
            callback_new_health = new_h;
        });

    // Clear callback
    router.clear_health_callback();

    // Callback should not be invoked after clear
    callback_invoked = false;
    // (Would need to trigger health change to test this properly)
}

// =============================================================================
// Delivery Result Tests
// =============================================================================

class DeliveryResultTest : public pacs_bridge_test {};

TEST_F(DeliveryResultTest, OkResult) {
    auto result = delivery_result::ok("RIS_PRIMARY", std::chrono::milliseconds{50});

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.destination_name, "RIS_PRIMARY");
    EXPECT_EQ(result.round_trip_time, std::chrono::milliseconds{50});
    EXPECT_TRUE(result.error_message.empty());
}

TEST_F(DeliveryResultTest, ErrorResult) {
    auto result = delivery_result::error("Connection refused");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.destination_name.empty());
    EXPECT_EQ(result.error_message, "Connection refused");
}

// =============================================================================
// Error Code Tests
// =============================================================================

class OutboundErrorTest : public pacs_bridge_test {};

TEST_F(OutboundErrorTest, ErrorCodes) {
    EXPECT_EQ(to_error_code(outbound_error::no_destination), -920);
    EXPECT_EQ(to_error_code(outbound_error::all_destinations_failed), -921);
    EXPECT_EQ(to_error_code(outbound_error::destination_not_found), -922);
    EXPECT_EQ(to_error_code(outbound_error::delivery_failed), -923);
    EXPECT_EQ(to_error_code(outbound_error::timeout), -929);
}

TEST_F(OutboundErrorTest, ErrorStrings) {
    EXPECT_STREQ(to_string(outbound_error::no_destination),
                 "No destination configured for message type");
    EXPECT_STREQ(to_string(outbound_error::all_destinations_failed),
                 "All destinations are unavailable");
    EXPECT_STREQ(to_string(outbound_error::not_running), "Router is not running");
}

// =============================================================================
// Health Status Tests
// =============================================================================

class HealthStatusTest : public pacs_bridge_test {};

TEST_F(HealthStatusTest, HealthStrings) {
    EXPECT_STREQ(to_string(destination_health::unknown), "unknown");
    EXPECT_STREQ(to_string(destination_health::healthy), "healthy");
    EXPECT_STREQ(to_string(destination_health::degraded), "degraded");
    EXPECT_STREQ(to_string(destination_health::unavailable), "unavailable");
}

// =============================================================================
// Partial Message Type Matching Tests
// =============================================================================

class MessageTypeMatchingTest : public pacs_bridge_test {};

TEST_F(MessageTypeMatchingTest, ExactMatch) {
    outbound_router_config config;
    config.enable_health_check = false;

    outbound_destination dest;
    dest.name = "EXACT";
    dest.host = "localhost";
    dest.port = 2576;
    dest.message_types = {"ORM^O01"};
    config.destinations.push_back(dest);

    outbound_router router(config);

    auto dests = router.get_destinations("ORM^O01");
    EXPECT_EQ(dests.size(), 1u);
    EXPECT_EQ(dests[0], "EXACT");
}

TEST_F(MessageTypeMatchingTest, PrefixMatch) {
    outbound_router_config config;
    config.enable_health_check = false;

    outbound_destination dest;
    dest.name = "ORM_ALL";
    dest.host = "localhost";
    dest.port = 2576;
    dest.message_types = {"ORM"};  // Should match ORM^O01, ORM^O02, etc.
    config.destinations.push_back(dest);

    outbound_router router(config);

    EXPECT_EQ(router.get_destinations("ORM^O01").size(), 1u);
    EXPECT_EQ(router.get_destinations("ORM^O02").size(), 1u);
    EXPECT_EQ(router.get_destinations("ORU^R01").size(), 0u);
}

TEST_F(MessageTypeMatchingTest, WildcardMatch) {
    outbound_router_config config;
    config.enable_health_check = false;

    outbound_destination dest;
    dest.name = "ALL";
    dest.host = "localhost";
    dest.port = 2576;
    dest.message_types = {"*"};
    config.destinations.push_back(dest);

    outbound_router router(config);

    EXPECT_EQ(router.get_destinations("ORM^O01").size(), 1u);
    EXPECT_EQ(router.get_destinations("ORU^R01").size(), 1u);
    EXPECT_EQ(router.get_destinations("ADT^A01").size(), 1u);
    EXPECT_EQ(router.get_destinations("ANYTHING").size(), 1u);
}

TEST_F(MessageTypeMatchingTest, EmptyMessageTypesMatchAll) {
    outbound_router_config config;
    config.enable_health_check = false;

    outbound_destination dest;
    dest.name = "DEFAULT";
    dest.host = "localhost";
    dest.port = 2576;
    // No message_types specified - should match all
    config.destinations.push_back(dest);

    outbound_router router(config);

    EXPECT_EQ(router.get_destinations("ORM^O01").size(), 1u);
    EXPECT_EQ(router.get_destinations("ORU^R01").size(), 1u);
}

}  // namespace
}  // namespace pacs::bridge::router
