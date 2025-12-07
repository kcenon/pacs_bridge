/**
 * @file router_test.cpp
 * @brief Comprehensive unit tests for HL7 message routing module
 *
 * Tests for message pattern matching, route configuration, handler chains,
 * and routing statistics. Target coverage: >= 80%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/21
 */

#include "pacs/bridge/router/message_router.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace pacs::bridge::router::test {

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

#define RUN_TEST(test_func)                                                    \
    do {                                                                       \
        std::cout << "Running " << #test_func << "..." << std::endl;           \
        if (test_func()) {                                                     \
            std::cout << "  PASSED" << std::endl;                              \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "  FAILED" << std::endl;                              \
            failed++;                                                          \
        }                                                                      \
    } while (0)

// Sample messages for testing
const std::string SAMPLE_ADT_A01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
    "PID|1||12345|||DOE^JOHN||19800515|M\r";

const std::string SAMPLE_ADT_A08 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115143000||ADT^A08|MSG002|P|2.4\r"
    "PID|1||12345|||DOE^JOHN||19800515|M\r";

const std::string SAMPLE_ORM_O01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG003|P|2.4\r"
    "PID|1||12345|||DOE^JOHN||19800515|M\r"
    "ORC|NW|ORD001||ACC001||SC\r";

const std::string SAMPLE_ORU_R01 =
    "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115150000||ORU^R01|MSG004|P|2.4\r"
    "PID|1||12345|||DOE^JOHN||19800515|M\r"
    "OBX|1|TX|RESULT||Impression: Normal||||||F\r";

// Helper to parse messages
hl7::hl7_message parse_message(const std::string& msg) {
    hl7::hl7_parser parser;
    auto result = parser.parse(msg);
    return std::move(*result);
}

// =============================================================================
// Router Error Tests
// =============================================================================

bool test_router_error_codes() {
    TEST_ASSERT(to_error_code(router_error::no_matching_route) == -930,
                "no_matching_route should be -930");
    TEST_ASSERT(to_error_code(router_error::timeout) == -938,
                "timeout should be -938");

    TEST_ASSERT(std::string(to_string(router_error::handler_not_found)) ==
                    "Handler not found",
                "Error message should match");
    TEST_ASSERT(std::string(to_string(router_error::route_exists)) ==
                    "Route already exists",
                "Error message should match");

    return true;
}

// =============================================================================
// Message Pattern Tests
// =============================================================================

bool test_pattern_any() {
    auto pattern = message_pattern::any();

    TEST_ASSERT(pattern.message_type.empty(), "Any pattern should have empty type");
    TEST_ASSERT(pattern.trigger_event.empty(), "Any pattern should have empty trigger");
    TEST_ASSERT(pattern.sending_application.empty(), "Any pattern should have empty sender");

    return true;
}

bool test_pattern_for_type() {
    auto pattern = message_pattern::for_type("ADT");

    TEST_ASSERT(pattern.message_type == "ADT", "Pattern type should be ADT");
    TEST_ASSERT(pattern.trigger_event.empty(), "Trigger should be empty");

    return true;
}

bool test_pattern_for_type_trigger() {
    auto pattern = message_pattern::for_type_trigger("ADT", "A01");

    TEST_ASSERT(pattern.message_type == "ADT", "Pattern type should be ADT");
    TEST_ASSERT(pattern.trigger_event == "A01", "Trigger should be A01");

    return true;
}

// =============================================================================
// Handler Result Tests
// =============================================================================

bool test_handler_result_ok() {
    auto result = handler_result::ok();
    TEST_ASSERT(result.success, "ok() should be successful");
    TEST_ASSERT(result.continue_chain, "ok() should continue chain by default");
    TEST_ASSERT(!result.response.has_value(), "ok() should have no response");

    auto result_stop = handler_result::ok(false);
    TEST_ASSERT(result_stop.success, "ok(false) should be successful");
    TEST_ASSERT(!result_stop.continue_chain, "ok(false) should not continue chain");

    return true;
}

bool test_handler_result_ok_with_response() {
    auto msg = parse_message(SAMPLE_ADT_A01);
    auto result = handler_result::ok_with_response(std::move(msg));

    TEST_ASSERT(result.success, "Should be successful");
    TEST_ASSERT(!result.continue_chain, "Should stop chain");
    TEST_ASSERT(result.response.has_value(), "Should have response");

    return true;
}

bool test_handler_result_error() {
    auto result = handler_result::error("Test error message");

    TEST_ASSERT(!result.success, "error() should not be successful");
    TEST_ASSERT(!result.continue_chain, "error() should stop chain");
    TEST_ASSERT(result.error_message == "Test error message", "Error message should match");

    return true;
}

bool test_handler_result_stop() {
    auto result = handler_result::stop();

    TEST_ASSERT(result.success, "stop() should be successful");
    TEST_ASSERT(!result.continue_chain, "stop() should not continue chain");

    return true;
}

// =============================================================================
// Route Tests
// =============================================================================

bool test_route_basic() {
    route r;
    r.id = "test_route";
    r.name = "Test Route";
    r.pattern = message_pattern::for_type("ADT");
    r.priority = 10;
    r.enabled = true;
    r.terminal = false;

    TEST_ASSERT(r.id == "test_route", "Route ID should match");
    TEST_ASSERT(r.priority == 10, "Priority should match");
    TEST_ASSERT(r.enabled, "Route should be enabled");
    TEST_ASSERT(!r.terminal, "Route should not be terminal");

    return true;
}

bool test_route_matches_type() {
    route r;
    r.enabled = true;
    r.pattern = message_pattern::for_type("ADT");

    auto adt_msg = parse_message(SAMPLE_ADT_A01);
    auto orm_msg = parse_message(SAMPLE_ORM_O01);

    TEST_ASSERT(r.matches(adt_msg), "Should match ADT message");
    TEST_ASSERT(!r.matches(orm_msg), "Should not match ORM message");

    return true;
}

bool test_route_matches_type_trigger() {
    route r;
    r.enabled = true;
    r.pattern = message_pattern::for_type_trigger("ADT", "A01");

    auto a01_msg = parse_message(SAMPLE_ADT_A01);
    auto a08_msg = parse_message(SAMPLE_ADT_A08);

    TEST_ASSERT(r.matches(a01_msg), "Should match ADT A01");
    TEST_ASSERT(!r.matches(a08_msg), "Should not match ADT A08");

    return true;
}

bool test_route_matches_wildcard() {
    route r;
    r.enabled = true;
    r.pattern.message_type = "ADT";
    r.pattern.trigger_event = "A*";  // Wildcard

    auto a01_msg = parse_message(SAMPLE_ADT_A01);
    auto a08_msg = parse_message(SAMPLE_ADT_A08);
    auto orm_msg = parse_message(SAMPLE_ORM_O01);

    TEST_ASSERT(r.matches(a01_msg), "Should match ADT A01 with wildcard");
    TEST_ASSERT(r.matches(a08_msg), "Should match ADT A08 with wildcard");
    TEST_ASSERT(!r.matches(orm_msg), "Should not match ORM");

    return true;
}

bool test_route_matches_sender() {
    route r;
    r.enabled = true;
    r.pattern.message_type = "ADT";
    r.pattern.sending_application = "HIS";
    r.pattern.sending_facility = "HOSPITAL";

    auto msg = parse_message(SAMPLE_ADT_A01);
    TEST_ASSERT(r.matches(msg), "Should match sender");

    r.pattern.sending_application = "OTHER";
    TEST_ASSERT(!r.matches(msg), "Should not match different sender");

    return true;
}

bool test_route_matches_receiver() {
    route r;
    r.enabled = true;
    r.pattern.message_type = "ADT";
    r.pattern.receiving_application = "PACS";
    r.pattern.receiving_facility = "RADIOLOGY";

    auto msg = parse_message(SAMPLE_ADT_A01);
    TEST_ASSERT(r.matches(msg), "Should match receiver");

    r.pattern.receiving_application = "OTHER";
    TEST_ASSERT(!r.matches(msg), "Should not match different receiver");

    return true;
}

bool test_route_disabled() {
    route r;
    r.enabled = false;  // Disabled
    r.pattern = message_pattern::for_type("ADT");

    auto msg = parse_message(SAMPLE_ADT_A01);
    TEST_ASSERT(!r.matches(msg), "Disabled route should not match");

    return true;
}

bool test_route_with_filter() {
    route r;
    r.enabled = true;
    r.pattern = message_pattern::for_type("ADT");
    r.filter = [](const hl7::hl7_message& msg) {
        // Only match if patient ID contains "123"
        return msg.get_value("PID.3").find("123") != std::string::npos;
    };

    auto msg = parse_message(SAMPLE_ADT_A01);
    TEST_ASSERT(r.matches(msg), "Should match with filter");

    // Message without matching patient ID would fail filter
    route r2;
    r2.enabled = true;
    r2.pattern = message_pattern::for_type("ADT");
    r2.filter = [](const hl7::hl7_message& msg) {
        return msg.get_value("PID.3").find("99999") != std::string::npos;
    };

    TEST_ASSERT(!r2.matches(msg), "Should not match with failing filter");

    return true;
}

bool test_route_regex_matching() {
    route r;
    r.enabled = true;
    r.pattern.message_type = "ADT";
    r.pattern.trigger_event = "A0[1-3]";  // Regex pattern
    r.pattern.use_regex = true;

    auto a01_msg = parse_message(SAMPLE_ADT_A01);
    auto a08_msg = parse_message(SAMPLE_ADT_A08);

    TEST_ASSERT(r.matches(a01_msg), "Should match A01 with regex");
    TEST_ASSERT(!r.matches(a08_msg), "Should not match A08 with regex");

    return true;
}

// =============================================================================
// Message Router Tests
// =============================================================================

bool test_router_handler_registration() {
    message_router router;

    bool registered = router.register_handler("handler1", [](const hl7::hl7_message&) {
        return handler_result::ok();
    });
    TEST_ASSERT(registered, "Should register handler successfully");
    TEST_ASSERT(router.has_handler("handler1"), "Should have handler1");

    // Duplicate registration
    bool duplicate = router.register_handler("handler1", [](const hl7::hl7_message&) {
        return handler_result::ok();
    });
    TEST_ASSERT(!duplicate, "Should not allow duplicate handler ID");

    // Handler list
    auto ids = router.handler_ids();
    TEST_ASSERT(ids.size() == 1, "Should have 1 handler");
    TEST_ASSERT(ids[0] == "handler1", "Handler ID should match");

    return true;
}

bool test_router_handler_unregister() {
    message_router router;

    router.register_handler("handler1", [](const hl7::hl7_message&) {
        return handler_result::ok();
    });

    TEST_ASSERT(router.has_handler("handler1"), "Should have handler");

    bool removed = router.unregister_handler("handler1");
    TEST_ASSERT(removed, "Should remove handler");
    TEST_ASSERT(!router.has_handler("handler1"), "Handler should be gone");

    // Remove non-existent
    bool not_found = router.unregister_handler("nonexistent");
    TEST_ASSERT(!not_found, "Should not find non-existent handler");

    return true;
}

bool test_router_add_route() {
    message_router router;

    // Register handler first
    router.register_handler("adt_handler", [](const hl7::hl7_message&) {
        return handler_result::ok();
    });

    route r;
    r.id = "adt_route";
    r.pattern = message_pattern::for_type("ADT");
    r.handler_ids = {"adt_handler"};

    auto result = router.add_route(r);
    TEST_ASSERT(result.has_value(), "Should add route successfully");

    auto routes = router.routes();
    TEST_ASSERT(routes.size() == 1, "Should have 1 route");
    TEST_ASSERT(routes[0].id == "adt_route", "Route ID should match");

    return true;
}

bool test_router_add_route_validation() {
    message_router router;

    // Empty ID
    route empty_id;
    empty_id.pattern = message_pattern::any();
    auto result1 = router.add_route(empty_id);
    TEST_ASSERT(!result1.has_value(), "Should reject empty ID");
    TEST_ASSERT(result1.error() == router_error::invalid_route, "Error should be invalid_route");

    // Handler not found
    route missing_handler;
    missing_handler.id = "test";
    missing_handler.handler_ids = {"nonexistent"};
    auto result2 = router.add_route(missing_handler);
    TEST_ASSERT(!result2.has_value(), "Should reject missing handler");
    TEST_ASSERT(result2.error() == router_error::handler_not_found, "Error should be handler_not_found");

    // Duplicate route
    router.register_handler("h1", [](const hl7::hl7_message&) { return handler_result::ok(); });
    route r1;
    r1.id = "route1";
    r1.handler_ids = {"h1"};
    router.add_route(r1);

    route r2;
    r2.id = "route1";  // Duplicate
    r2.handler_ids = {"h1"};
    auto result3 = router.add_route(r2);
    TEST_ASSERT(!result3.has_value(), "Should reject duplicate route");
    TEST_ASSERT(result3.error() == router_error::route_exists, "Error should be route_exists");

    return true;
}

bool test_router_remove_route() {
    message_router router;
    router.register_handler("h1", [](const hl7::hl7_message&) { return handler_result::ok(); });

    route r;
    r.id = "test_route";
    r.handler_ids = {"h1"};
    router.add_route(r);

    bool removed = router.remove_route("test_route");
    TEST_ASSERT(removed, "Should remove route");
    TEST_ASSERT(router.routes().empty(), "Routes should be empty");

    bool not_found = router.remove_route("nonexistent");
    TEST_ASSERT(!not_found, "Should not find non-existent route");

    return true;
}

bool test_router_route_enabled() {
    message_router router;
    router.register_handler("h1", [](const hl7::hl7_message&) { return handler_result::ok(); });

    route r;
    r.id = "test_route";
    r.handler_ids = {"h1"};
    r.enabled = true;
    router.add_route(r);

    router.set_route_enabled("test_route", false);

    auto* route_ptr = router.get_route("test_route");
    TEST_ASSERT(route_ptr != nullptr, "Should find route");
    TEST_ASSERT(!route_ptr->enabled, "Route should be disabled");

    return true;
}

bool test_router_clear_routes() {
    message_router router;
    router.register_handler("h1", [](const hl7::hl7_message&) { return handler_result::ok(); });

    for (int i = 0; i < 5; i++) {
        route r;
        r.id = "route" + std::to_string(i);
        r.handler_ids = {"h1"};
        router.add_route(r);
    }

    TEST_ASSERT(router.routes().size() == 5, "Should have 5 routes");

    router.clear_routes();
    TEST_ASSERT(router.routes().empty(), "Routes should be empty");

    return true;
}

// =============================================================================
// Routing Tests
// =============================================================================

bool test_routing_basic() {
    message_router router;

    int handler_called = 0;
    router.register_handler("adt_handler", [&handler_called](const hl7::hl7_message&) {
        handler_called++;
        return handler_result::ok();
    });

    route r;
    r.id = "adt_route";
    r.pattern = message_pattern::for_type("ADT");
    r.handler_ids = {"adt_handler"};
    router.add_route(r);

    auto msg = parse_message(SAMPLE_ADT_A01);
    auto result = router.route(msg);

    TEST_ASSERT(result.has_value(), "Routing should succeed");
    TEST_ASSERT(result->success, "Result should be successful");
    TEST_ASSERT(handler_called == 1, "Handler should be called once");

    return true;
}

bool test_routing_no_match() {
    message_router router;
    router.register_handler("orm_handler", [](const hl7::hl7_message&) {
        return handler_result::ok();
    });

    route r;
    r.id = "orm_route";
    r.pattern = message_pattern::for_type("ORM");
    r.handler_ids = {"orm_handler"};
    router.add_route(r);

    auto adt_msg = parse_message(SAMPLE_ADT_A01);
    auto result = router.route(adt_msg);

    TEST_ASSERT(!result.has_value(), "Should not match");
    TEST_ASSERT(result.error() == router_error::no_matching_route, "Error should be no_matching_route");

    return true;
}

bool test_routing_with_default_handler() {
    message_router router;

    int default_called = 0;
    router.set_default_handler([&default_called](const hl7::hl7_message&) {
        default_called++;
        return handler_result::ok();
    });

    auto msg = parse_message(SAMPLE_ADT_A01);
    auto result = router.route(msg);

    TEST_ASSERT(result.has_value(), "Should succeed with default handler");
    TEST_ASSERT(default_called == 1, "Default handler should be called");

    router.clear_default_handler();
    auto result2 = router.route(msg);
    TEST_ASSERT(!result2.has_value(), "Should fail without routes or default");

    return true;
}

bool test_routing_handler_chain() {
    message_router router;

    std::vector<int> call_order;

    router.register_handler("handler1", [&call_order](const hl7::hl7_message&) {
        call_order.push_back(1);
        return handler_result::ok(true);  // Continue
    });

    router.register_handler("handler2", [&call_order](const hl7::hl7_message&) {
        call_order.push_back(2);
        return handler_result::ok(true);  // Continue
    });

    router.register_handler("handler3", [&call_order](const hl7::hl7_message&) {
        call_order.push_back(3);
        return handler_result::ok();  // Stop
    });

    route r;
    r.id = "chain_route";
    r.pattern = message_pattern::any();
    r.handler_ids = {"handler1", "handler2", "handler3"};
    router.add_route(r);

    auto msg = parse_message(SAMPLE_ADT_A01);
    router.route(msg);

    TEST_ASSERT(call_order.size() == 3, "All handlers should be called");
    TEST_ASSERT(call_order[0] == 1, "Handler 1 first");
    TEST_ASSERT(call_order[1] == 2, "Handler 2 second");
    TEST_ASSERT(call_order[2] == 3, "Handler 3 third");

    return true;
}

bool test_routing_handler_stops_chain() {
    message_router router;

    std::vector<int> call_order;

    router.register_handler("handler1", [&call_order](const hl7::hl7_message&) {
        call_order.push_back(1);
        return handler_result::ok(true);
    });

    router.register_handler("handler2", [&call_order](const hl7::hl7_message&) {
        call_order.push_back(2);
        return handler_result::stop();  // Stop chain
    });

    router.register_handler("handler3", [&call_order](const hl7::hl7_message&) {
        call_order.push_back(3);
        return handler_result::ok();
    });

    route r;
    r.id = "stop_route";
    r.pattern = message_pattern::any();
    r.handler_ids = {"handler1", "handler2", "handler3"};
    router.add_route(r);

    auto msg = parse_message(SAMPLE_ADT_A01);
    router.route(msg);

    TEST_ASSERT(call_order.size() == 2, "Only 2 handlers should be called");
    TEST_ASSERT(call_order[0] == 1, "Handler 1 called");
    TEST_ASSERT(call_order[1] == 2, "Handler 2 called");

    return true;
}

bool test_routing_priority() {
    message_router router;

    std::vector<std::string> call_order;

    router.register_handler("low", [&call_order](const hl7::hl7_message&) {
        call_order.push_back("low");
        return handler_result::ok(true);
    });

    router.register_handler("high", [&call_order](const hl7::hl7_message&) {
        call_order.push_back("high");
        return handler_result::ok(true);
    });

    // Add low priority first
    route r_low;
    r_low.id = "low_route";
    r_low.priority = 100;
    r_low.pattern = message_pattern::any();
    r_low.handler_ids = {"low"};
    router.add_route(r_low);

    // Add high priority second
    route r_high;
    r_high.id = "high_route";
    r_high.priority = 10;  // Lower number = higher priority
    r_high.pattern = message_pattern::any();
    r_high.handler_ids = {"high"};
    router.add_route(r_high);

    auto msg = parse_message(SAMPLE_ADT_A01);
    router.route(msg);

    TEST_ASSERT(call_order.size() == 2, "Both handlers should be called");
    TEST_ASSERT(call_order[0] == "high", "High priority should be first");
    TEST_ASSERT(call_order[1] == "low", "Low priority should be second");

    return true;
}

bool test_routing_terminal_route() {
    message_router router;

    std::vector<std::string> call_order;

    router.register_handler("first", [&call_order](const hl7::hl7_message&) {
        call_order.push_back("first");
        return handler_result::ok(true);
    });

    router.register_handler("second", [&call_order](const hl7::hl7_message&) {
        call_order.push_back("second");
        return handler_result::ok();
    });

    route r1;
    r1.id = "terminal_route";
    r1.priority = 10;
    r1.pattern = message_pattern::any();
    r1.handler_ids = {"first"};
    r1.terminal = true;  // Terminal route
    router.add_route(r1);

    route r2;
    r2.id = "second_route";
    r2.priority = 100;
    r2.pattern = message_pattern::any();
    r2.handler_ids = {"second"};
    router.add_route(r2);

    auto msg = parse_message(SAMPLE_ADT_A01);
    router.route(msg);

    TEST_ASSERT(call_order.size() == 1, "Only terminal route should be called");
    TEST_ASSERT(call_order[0] == "first", "First handler should be called");

    return true;
}

bool test_routing_find_matching_routes() {
    message_router router;
    router.register_handler("h1", [](const hl7::hl7_message&) { return handler_result::ok(); });

    route r1;
    r1.id = "adt_route";
    r1.pattern = message_pattern::for_type("ADT");
    r1.handler_ids = {"h1"};
    router.add_route(r1);

    route r2;
    r2.id = "all_route";
    r2.pattern = message_pattern::any();
    r2.handler_ids = {"h1"};
    router.add_route(r2);

    route r3;
    r3.id = "orm_route";
    r3.pattern = message_pattern::for_type("ORM");
    r3.handler_ids = {"h1"};
    router.add_route(r3);

    auto adt_msg = parse_message(SAMPLE_ADT_A01);
    auto matching = router.find_matching_routes(adt_msg);

    TEST_ASSERT(matching.size() == 2, "Should match 2 routes");
    // adt_route and all_route should match

    TEST_ASSERT(router.has_matching_route(adt_msg), "Should have matching route");

    auto orm_msg = parse_message(SAMPLE_ORM_O01);
    auto orm_matching = router.find_matching_routes(orm_msg);
    TEST_ASSERT(orm_matching.size() == 2, "Should match 2 routes for ORM");

    return true;
}

// =============================================================================
// Statistics Tests
// =============================================================================

bool test_router_statistics() {
    message_router router;
    router.register_handler("h1", [](const hl7::hl7_message&) { return handler_result::ok(); });

    route r;
    r.id = "stats_route";
    r.pattern = message_pattern::for_type("ADT");
    r.handler_ids = {"h1"};
    router.add_route(r);

    // Route some messages
    auto adt_msg = parse_message(SAMPLE_ADT_A01);
    router.route(adt_msg);
    router.route(adt_msg);

    auto orm_msg = parse_message(SAMPLE_ORM_O01);
    router.route(orm_msg);  // Will not match

    auto stats = router.get_statistics();
    TEST_ASSERT(stats.total_messages == 3, "Should have 3 total messages");
    TEST_ASSERT(stats.matched_messages == 2, "Should have 2 matched messages");
    TEST_ASSERT(stats.unhandled_messages == 1, "Should have 1 unhandled message");
    TEST_ASSERT(stats.route_matches["stats_route"] == 2, "Route should have 2 matches");

    return true;
}

bool test_router_statistics_reset() {
    message_router router;
    router.register_handler("h1", [](const hl7::hl7_message&) { return handler_result::ok(); });

    route r;
    r.id = "test_route";
    r.pattern = message_pattern::any();
    r.handler_ids = {"h1"};
    router.add_route(r);

    auto msg = parse_message(SAMPLE_ADT_A01);
    router.route(msg);

    auto stats = router.get_statistics();
    TEST_ASSERT(stats.total_messages == 1, "Should have 1 message");

    router.reset_statistics();

    auto reset_stats = router.get_statistics();
    TEST_ASSERT(reset_stats.total_messages == 0, "Should have 0 after reset");
    TEST_ASSERT(reset_stats.matched_messages == 0, "Should have 0 matches after reset");

    return true;
}

// =============================================================================
// Route Builder Tests
// =============================================================================

bool test_route_builder_basic() {
    auto r = route_builder::create("test_route")
                 .name("Test Route")
                 .description("A test route")
                 .match_type("ADT")
                 .match_trigger("A01")
                 .handler("handler1")
                 .priority(50)
                 .build();

    TEST_ASSERT(r.id == "test_route", "Route ID should match");
    TEST_ASSERT(r.name == "Test Route", "Name should match");
    TEST_ASSERT(r.description == "A test route", "Description should match");
    TEST_ASSERT(r.pattern.message_type == "ADT", "Type should be ADT");
    TEST_ASSERT(r.pattern.trigger_event == "A01", "Trigger should be A01");
    TEST_ASSERT(r.handler_ids.size() == 1, "Should have 1 handler");
    TEST_ASSERT(r.handler_ids[0] == "handler1", "Handler ID should match");
    TEST_ASSERT(r.priority == 50, "Priority should be 50");

    return true;
}

bool test_route_builder_sender_receiver() {
    auto r = route_builder::create("sender_route")
                 .match_sender("HIS", "HOSPITAL")
                 .match_receiver("PACS", "RADIOLOGY")
                 .handler("h1")
                 .build();

    TEST_ASSERT(r.pattern.sending_application == "HIS", "Sending app should match");
    TEST_ASSERT(r.pattern.sending_facility == "HOSPITAL", "Sending facility should match");
    TEST_ASSERT(r.pattern.receiving_application == "PACS", "Receiving app should match");
    TEST_ASSERT(r.pattern.receiving_facility == "RADIOLOGY", "Receiving facility should match");

    return true;
}

bool test_route_builder_options() {
    auto r = route_builder::create("options_route")
                 .match_any()
                 .use_regex(true)
                 .terminal(true)
                 .handler("h1")
                 .build();

    TEST_ASSERT(r.pattern.use_regex, "Should use regex");
    TEST_ASSERT(r.terminal, "Should be terminal");

    return true;
}

bool test_route_builder_filter() {
    auto r = route_builder::create("filter_route")
                 .match_type("ADT")
                 .filter([](const hl7::hl7_message& msg) {
                     // PID-9 is the sex field in standard HL7 v2.x
                     return msg.get_value("PID.9") == "M";
                 })
                 .handler("h1")
                 .build();

    TEST_ASSERT(r.filter != nullptr, "Filter should be set");

    auto msg = parse_message(SAMPLE_ADT_A01);
    TEST_ASSERT(r.filter(msg), "Filter should match male patient");

    return true;
}

bool test_route_builder_multiple_handlers() {
    auto r = route_builder::create("multi_handler")
                 .match_any()
                 .handler("handler1")
                 .handler("handler2")
                 .handler("handler3")
                 .build();

    TEST_ASSERT(r.handler_ids.size() == 3, "Should have 3 handlers");
    TEST_ASSERT(r.handler_ids[0] == "handler1", "First handler should match");
    TEST_ASSERT(r.handler_ids[1] == "handler2", "Second handler should match");
    TEST_ASSERT(r.handler_ids[2] == "handler3", "Third handler should match");

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== Router Error Tests ===" << std::endl;
    RUN_TEST(test_router_error_codes);

    std::cout << "\n=== Message Pattern Tests ===" << std::endl;
    RUN_TEST(test_pattern_any);
    RUN_TEST(test_pattern_for_type);
    RUN_TEST(test_pattern_for_type_trigger);

    std::cout << "\n=== Handler Result Tests ===" << std::endl;
    RUN_TEST(test_handler_result_ok);
    RUN_TEST(test_handler_result_ok_with_response);
    RUN_TEST(test_handler_result_error);
    RUN_TEST(test_handler_result_stop);

    std::cout << "\n=== Route Tests ===" << std::endl;
    RUN_TEST(test_route_basic);
    RUN_TEST(test_route_matches_type);
    RUN_TEST(test_route_matches_type_trigger);
    RUN_TEST(test_route_matches_wildcard);
    RUN_TEST(test_route_matches_sender);
    RUN_TEST(test_route_matches_receiver);
    RUN_TEST(test_route_disabled);
    RUN_TEST(test_route_with_filter);
    RUN_TEST(test_route_regex_matching);

    std::cout << "\n=== Message Router Tests ===" << std::endl;
    RUN_TEST(test_router_handler_registration);
    RUN_TEST(test_router_handler_unregister);
    RUN_TEST(test_router_add_route);
    RUN_TEST(test_router_add_route_validation);
    RUN_TEST(test_router_remove_route);
    RUN_TEST(test_router_route_enabled);
    RUN_TEST(test_router_clear_routes);

    std::cout << "\n=== Routing Tests ===" << std::endl;
    RUN_TEST(test_routing_basic);
    RUN_TEST(test_routing_no_match);
    RUN_TEST(test_routing_with_default_handler);
    RUN_TEST(test_routing_handler_chain);
    RUN_TEST(test_routing_handler_stops_chain);
    RUN_TEST(test_routing_priority);
    RUN_TEST(test_routing_terminal_route);
    RUN_TEST(test_routing_find_matching_routes);

    std::cout << "\n=== Statistics Tests ===" << std::endl;
    RUN_TEST(test_router_statistics);
    RUN_TEST(test_router_statistics_reset);

    std::cout << "\n=== Route Builder Tests ===" << std::endl;
    RUN_TEST(test_route_builder_basic);
    RUN_TEST(test_route_builder_sender_receiver);
    RUN_TEST(test_route_builder_options);
    RUN_TEST(test_route_builder_filter);
    RUN_TEST(test_route_builder_multiple_handlers);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    double coverage = (passed * 100.0) / (passed + failed);
    std::cout << "Pass Rate: " << coverage << "%" << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::router::test

int main() {
    return pacs::bridge::router::test::run_all_tests();
}
