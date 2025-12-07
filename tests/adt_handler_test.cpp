/**
 * @file adt_handler_test.cpp
 * @brief Unit tests for ADT (Admission, Discharge, Transfer) message handler
 *
 * Tests for ADT message handling, patient cache integration, and ACK generation.
 * Target coverage: >= 85%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/14
 */

#include "pacs/bridge/cache/patient_cache.h"
#include "pacs/bridge/protocol/hl7/adt_handler.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::hl7::test {

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

// =============================================================================
// Sample ADT Messages
// =============================================================================

const std::string SAMPLE_ADT_A01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4|||AL|NE\r"
    "EVN|A01|20240115103000|||OPERATOR^JOHN\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||123 MAIN ST^^SPRINGFIELD^IL^62701||555-123-4567\r"
    "PV1|1|I|WARD^101^A^HOSPITAL||||SMITH^ROBERT^MD\r";

const std::string SAMPLE_ADT_A04 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A04|MSG002|P|2.4|||AL|NE\r"
    "EVN|A04|20240115103000|||OPERATOR^JANE\r"
    "PID|1||54321^^^CLINIC^MR||SMITH^JANE^ANN||19900320|F|||456 OAK AVE^^CHICAGO^IL^60601||555-987-6543\r"
    "PV1|1|O|CLINIC^201^B^HOSPITAL||||JONES^MARY^MD\r";

const std::string SAMPLE_ADT_A08 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ADT^A08|MSG003|P|2.4|||AL|NE\r"
    "EVN|A08|20240115110000|||OPERATOR^JOHN\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||789 NEW ST^^SPRINGFIELD^IL^62702||555-111-2222\r"
    "PV1|1|I|WARD^102^B^HOSPITAL||||SMITH^ROBERT^MD\r";

const std::string SAMPLE_ADT_A40 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115120000||ADT^A40|MSG004|P|2.4|||AL|NE\r"
    "EVN|A40|20240115120000|||OPERATOR^ADMIN\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||123 MAIN ST^^SPRINGFIELD^IL^62701||555-123-4567\r"
    "MRG|99999^^^HOSPITAL^MR\r"
    "PV1|1|I|WARD^101^A^HOSPITAL||||SMITH^ROBERT^MD\r";

const std::string SAMPLE_ORM_O01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG005|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "ORC|NW|ORD001^HIS|ACC001^PACS||SC\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT\r";

// =============================================================================
// ADT Error Code Tests
// =============================================================================

bool test_adt_error_codes() {
    // Verify error code values
    TEST_ASSERT(to_error_code(adt_error::not_adt_message) == -850,
                "not_adt_message should be -850");
    TEST_ASSERT(to_error_code(adt_error::unsupported_trigger_event) == -851,
                "unsupported_trigger_event should be -851");
    TEST_ASSERT(to_error_code(adt_error::missing_patient_id) == -852,
                "missing_patient_id should be -852");
    TEST_ASSERT(to_error_code(adt_error::patient_not_found) == -853,
                "patient_not_found should be -853");
    TEST_ASSERT(to_error_code(adt_error::merge_failed) == -854,
                "merge_failed should be -854");
    TEST_ASSERT(to_error_code(adt_error::processing_failed) == -859,
                "processing_failed should be -859");

    // Verify error messages
    TEST_ASSERT(std::strcmp(to_string(adt_error::not_adt_message),
                            "Message is not an ADT message") == 0,
                "Error message should match");
    TEST_ASSERT(std::strcmp(to_string(adt_error::missing_patient_id),
                            "Patient ID not found in message") == 0,
                "Error message should match");

    return true;
}

bool test_adt_trigger_event_parsing() {
    TEST_ASSERT(parse_adt_trigger("A01") == adt_trigger_event::A01,
                "A01 should parse correctly");
    TEST_ASSERT(parse_adt_trigger("A04") == adt_trigger_event::A04,
                "A04 should parse correctly");
    TEST_ASSERT(parse_adt_trigger("A08") == adt_trigger_event::A08,
                "A08 should parse correctly");
    TEST_ASSERT(parse_adt_trigger("A40") == adt_trigger_event::A40,
                "A40 should parse correctly");
    TEST_ASSERT(parse_adt_trigger("A99") == adt_trigger_event::unknown,
                "Unknown trigger should be unknown");
    TEST_ASSERT(parse_adt_trigger("") == adt_trigger_event::unknown,
                "Empty trigger should be unknown");

    // Test to_string
    TEST_ASSERT(std::strcmp(to_string(adt_trigger_event::A01), "A01") == 0,
                "A01 to_string should work");
    TEST_ASSERT(std::strcmp(to_string(adt_trigger_event::A04), "A04") == 0,
                "A04 to_string should work");
    TEST_ASSERT(std::strcmp(to_string(adt_trigger_event::A08), "A08") == 0,
                "A08 to_string should work");
    TEST_ASSERT(std::strcmp(to_string(adt_trigger_event::A40), "A40") == 0,
                "A40 to_string should work");

    return true;
}

// =============================================================================
// ADT Handler Construction Tests
// =============================================================================

bool test_handler_construction_default() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    TEST_ASSERT(handler.cache() == cache, "Cache should be set");
    TEST_ASSERT(handler.config().allow_a01_update == true,
                "Default config should allow A01 update");
    TEST_ASSERT(handler.config().allow_a08_create == false,
                "Default config should not allow A08 create");

    auto triggers = handler.supported_triggers();
    TEST_ASSERT(triggers.size() == 4, "Should support 4 triggers");

    return true;
}

bool test_handler_construction_with_config() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler_config config;
    config.allow_a01_update = false;
    config.allow_a08_create = true;
    config.detailed_ack = false;

    adt_handler handler(cache, config);

    TEST_ASSERT(handler.config().allow_a01_update == false,
                "Config should be applied");
    TEST_ASSERT(handler.config().allow_a08_create == true,
                "Config should be applied");
    TEST_ASSERT(handler.config().detailed_ack == false,
                "Config should be applied");

    return true;
}

// =============================================================================
// Message Handling Tests
// =============================================================================

bool test_can_handle_adt_message() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    // ADT messages should be handleable
    auto adt_result = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(adt_result.has_value(), "ADT message should parse");
    TEST_ASSERT(handler.can_handle(*adt_result), "Should handle ADT messages");

    // ORM messages should not be handleable
    auto orm_result = hl7_message::parse(SAMPLE_ORM_O01);
    TEST_ASSERT(orm_result.has_value(), "ORM message should parse");
    TEST_ASSERT(!handler.can_handle(*orm_result),
                "Should not handle ORM messages");

    return true;
}

bool test_handle_a01_admit_new_patient() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(msg.has_value(), "Message should parse");

    auto result = handler.handle(*msg);
    TEST_ASSERT(result.has_value(), "Handle should succeed");
    TEST_ASSERT(result->success, "Result should be successful");
    TEST_ASSERT(result->trigger == adt_trigger_event::A01,
                "Trigger should be A01");
    TEST_ASSERT(result->patient_id == "12345", "Patient ID should be 12345");

    // Verify patient is in cache
    TEST_ASSERT(cache->contains("12345"), "Patient should be in cache");

    auto patient = cache->get("12345");
    TEST_ASSERT(patient.has_value(), "Patient should be retrievable");
    TEST_ASSERT(patient->patient_id == "12345", "Patient ID should match");

    // Verify statistics
    auto stats = handler.get_statistics();
    TEST_ASSERT(stats.total_processed == 1, "Should have processed 1 message");
    TEST_ASSERT(stats.a01_count == 1, "Should have 1 A01");
    TEST_ASSERT(stats.patients_created == 1, "Should have created 1 patient");

    return true;
}

bool test_handle_a04_register_outpatient() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    auto msg = hl7_message::parse(SAMPLE_ADT_A04);
    TEST_ASSERT(msg.has_value(), "Message should parse");

    auto result = handler.handle(*msg);
    TEST_ASSERT(result.has_value(), "Handle should succeed");
    TEST_ASSERT(result->success, "Result should be successful");
    TEST_ASSERT(result->trigger == adt_trigger_event::A04,
                "Trigger should be A04");
    TEST_ASSERT(result->patient_id == "54321", "Patient ID should be 54321");

    // Verify patient is in cache
    TEST_ASSERT(cache->contains("54321"), "Patient should be in cache");

    auto stats = handler.get_statistics();
    TEST_ASSERT(stats.a04_count == 1, "Should have 1 A04");
    TEST_ASSERT(stats.patients_created == 1, "Should have created 1 patient");

    return true;
}

bool test_handle_a08_update_patient() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    // First, create patient with A01
    auto a01_msg = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(a01_msg.has_value(), "A01 message should parse");
    auto a01_result = handler.handle(*a01_msg);
    TEST_ASSERT(a01_result.has_value(), "A01 should succeed");

    // Then update with A08
    auto a08_msg = hl7_message::parse(SAMPLE_ADT_A08);
    TEST_ASSERT(a08_msg.has_value(), "A08 message should parse");

    auto result = handler.handle(*a08_msg);
    TEST_ASSERT(result.has_value(), "Handle should succeed");
    TEST_ASSERT(result->success, "Result should be successful");
    TEST_ASSERT(result->trigger == adt_trigger_event::A08,
                "Trigger should be A08");

    // Verify statistics
    auto stats = handler.get_statistics();
    TEST_ASSERT(stats.a08_count == 1, "Should have 1 A08");
    TEST_ASSERT(stats.patients_updated == 1, "Should have updated 1 patient");

    return true;
}

bool test_handle_a08_patient_not_found() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    // Try to update without creating first
    auto msg = hl7_message::parse(SAMPLE_ADT_A08);
    TEST_ASSERT(msg.has_value(), "Message should parse");

    auto result = handler.handle(*msg);
    TEST_ASSERT(!result.has_value(), "Handle should fail");
    TEST_ASSERT(result.error() == adt_error::patient_not_found,
                "Error should be patient_not_found");

    return true;
}

bool test_handle_a08_create_if_configured() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler_config config;
    config.allow_a08_create = true;
    adt_handler handler(cache, config);

    // Try to update without creating first - should create
    auto msg = hl7_message::parse(SAMPLE_ADT_A08);
    TEST_ASSERT(msg.has_value(), "Message should parse");

    auto result = handler.handle(*msg);
    TEST_ASSERT(result.has_value(), "Handle should succeed with config");
    TEST_ASSERT(result->success, "Result should be successful");

    // Patient should be created
    TEST_ASSERT(cache->contains("12345"), "Patient should be in cache");

    return true;
}

bool test_handle_a40_merge_patients() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    // First create the secondary patient
    mapping::dicom_patient secondary;
    secondary.patient_id = "99999";
    secondary.patient_name = "OLD^PATIENT";
    cache->put("99999", secondary);

    // Now handle merge
    auto msg = hl7_message::parse(SAMPLE_ADT_A40);
    TEST_ASSERT(msg.has_value(), "Message should parse");

    auto result = handler.handle(*msg);
    TEST_ASSERT(result.has_value(), "Handle should succeed");
    TEST_ASSERT(result->success, "Result should be successful");
    TEST_ASSERT(result->trigger == adt_trigger_event::A40,
                "Trigger should be A40");
    TEST_ASSERT(result->patient_id == "12345", "Primary ID should be 12345");
    TEST_ASSERT(result->merged_patient_id == "99999",
                "Merged ID should be 99999");

    // Primary patient should exist
    TEST_ASSERT(cache->contains("12345"), "Primary patient should be in cache");

    // Secondary ID should still resolve (via alias)
    TEST_ASSERT(cache->contains("99999"), "Alias should exist for secondary ID");

    auto stats = handler.get_statistics();
    TEST_ASSERT(stats.a40_count == 1, "Should have 1 A40");
    TEST_ASSERT(stats.patients_merged == 1, "Should have merged 1 patient");

    return true;
}

bool test_handle_non_adt_message() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    auto msg = hl7_message::parse(SAMPLE_ORM_O01);
    TEST_ASSERT(msg.has_value(), "ORM message should parse");

    auto result = handler.handle(*msg);
    TEST_ASSERT(!result.has_value(), "Handle should fail for non-ADT");
    TEST_ASSERT(result.error() == adt_error::not_adt_message,
                "Error should be not_adt_message");

    return true;
}

// =============================================================================
// Callback Tests
// =============================================================================

bool test_patient_created_callback() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    std::string created_patient_id;
    handler.on_patient_created([&](const mapping::dicom_patient& patient) {
        created_patient_id = patient.patient_id;
    });

    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(msg.has_value(), "Message should parse");

    auto result = handler.handle(*msg);
    TEST_ASSERT(result.has_value(), "Handle should succeed");
    TEST_ASSERT(created_patient_id == "12345",
                "Callback should receive patient ID");

    return true;
}

bool test_patient_updated_callback() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    std::string old_id, new_id;
    handler.on_patient_updated([&](const mapping::dicom_patient& old_patient,
                                   const mapping::dicom_patient& new_patient) {
        old_id = old_patient.patient_id;
        new_id = new_patient.patient_id;
    });

    // Create patient first
    auto a01_msg = hl7_message::parse(SAMPLE_ADT_A01);
    handler.handle(*a01_msg);

    // Update patient
    auto a08_msg = hl7_message::parse(SAMPLE_ADT_A08);
    auto result = handler.handle(*a08_msg);
    TEST_ASSERT(result.has_value(), "Handle should succeed");
    TEST_ASSERT(old_id == "12345" && new_id == "12345",
                "Callback should receive both patients");

    return true;
}

bool test_patient_merged_callback() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    std::string primary_id, secondary_id;
    handler.on_patient_merged([&](const merge_info& info) {
        primary_id = info.primary_patient_id;
        secondary_id = info.secondary_patient_id;
    });

    // Create secondary patient
    mapping::dicom_patient secondary;
    secondary.patient_id = "99999";
    cache->put("99999", secondary);

    // Handle merge
    auto msg = hl7_message::parse(SAMPLE_ADT_A40);
    auto result = handler.handle(*msg);
    TEST_ASSERT(result.has_value(), "Handle should succeed");
    TEST_ASSERT(primary_id == "12345", "Primary ID should be correct");
    TEST_ASSERT(secondary_id == "99999", "Secondary ID should be correct");

    return true;
}

// =============================================================================
// ACK Generation Tests
// =============================================================================

bool test_ack_generation() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(msg.has_value(), "Message should parse");

    auto result = handler.handle(*msg);
    TEST_ASSERT(result.has_value(), "Handle should succeed");

    // Check ACK message
    auto ack_header = result->ack_message.header();
    TEST_ASSERT(ack_header.type == message_type::ACK, "ACK type should be ACK");

    return true;
}

// =============================================================================
// Statistics Tests
// =============================================================================

bool test_statistics_tracking() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    // Initial stats should be zero
    auto initial_stats = handler.get_statistics();
    TEST_ASSERT(initial_stats.total_processed == 0,
                "Initial total should be 0");

    // Create a message for non-existent patient update
    const std::string a08_nonexistent =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ADT^A08|MSG006|P|2.4|||AL|NE\r"
        "EVN|A08|20240115110000|||OPERATOR^JOHN\r"
        "PID|1||99999^^^HOSPITAL^MR||NOBODY^JOHN||19800515|M\r"
        "PV1|1|I|WARD^102^B^HOSPITAL\r";

    // Process some messages
    auto a01 = hl7_message::parse(SAMPLE_ADT_A01);
    auto a04 = hl7_message::parse(SAMPLE_ADT_A04);
    auto a08 = hl7_message::parse(a08_nonexistent);

    (void)handler.handle(*a01);
    (void)handler.handle(*a04);
    (void)handler.handle(*a08);  // Will fail - no patient 99999 to update

    auto stats = handler.get_statistics();
    TEST_ASSERT(stats.total_processed == 3, "Should have processed 3 messages");
    TEST_ASSERT(stats.success_count == 2, "Should have 2 successes");
    TEST_ASSERT(stats.failure_count == 1, "Should have 1 failure");
    TEST_ASSERT(stats.a01_count == 1, "Should have 1 A01");
    TEST_ASSERT(stats.a04_count == 1, "Should have 1 A04");
    TEST_ASSERT(stats.a08_count == 1, "Should have 1 A08");

    // Reset statistics
    handler.reset_statistics();
    auto reset_stats = handler.get_statistics();
    TEST_ASSERT(reset_stats.total_processed == 0,
                "Reset should clear total");

    return true;
}

// =============================================================================
// Concurrent Processing Tests
// =============================================================================

bool test_concurrent_processing() {
    auto cache = std::make_shared<cache::patient_cache>();
    adt_handler handler(cache);

    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    auto process_messages = [&](int thread_id) {
        for (int i = 0; i < 10; i++) {
            // Create unique patient ID for each iteration
            std::string patient_id = std::to_string(thread_id * 1000 + i);
            std::string msg_str =
                "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG" +
                patient_id + "|P|2.4|||AL|NE\r"
                "EVN|A01|20240115103000|||OPERATOR^JOHN\r"
                "PID|1||" + patient_id + "^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
                "PV1|1|I|WARD^101^A^HOSPITAL||||SMITH^ROBERT^MD\r";

            auto msg = hl7_message::parse(msg_str);
            if (msg) {
                auto result = handler.handle(*msg);
                if (result && result->success) {
                    success_count++;
                } else {
                    failure_count++;
                }
            }
        }
    };

    // Run multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back(process_messages, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    // All messages should succeed
    TEST_ASSERT(success_count == 40, "All 40 messages should succeed");
    TEST_ASSERT(failure_count == 0, "No messages should fail");

    // Verify statistics
    auto stats = handler.get_statistics();
    TEST_ASSERT(stats.total_processed == 40, "Should have processed 40 messages");
    TEST_ASSERT(stats.patients_created == 40, "Should have created 40 patients");

    return true;
}

}  // namespace pacs::bridge::hl7::test

// =============================================================================
// Main
// =============================================================================

int main() {
    using namespace pacs::bridge::hl7::test;

    int passed = 0;
    int failed = 0;

    std::cout << "========================================" << std::endl;
    std::cout << "ADT Handler Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    // Error code tests
    std::cout << "\n--- Error Code Tests ---" << std::endl;
    RUN_TEST(test_adt_error_codes);
    RUN_TEST(test_adt_trigger_event_parsing);

    // Construction tests
    std::cout << "\n--- Construction Tests ---" << std::endl;
    RUN_TEST(test_handler_construction_default);
    RUN_TEST(test_handler_construction_with_config);

    // Message handling tests
    std::cout << "\n--- Message Handling Tests ---" << std::endl;
    RUN_TEST(test_can_handle_adt_message);
    RUN_TEST(test_handle_a01_admit_new_patient);
    RUN_TEST(test_handle_a04_register_outpatient);
    RUN_TEST(test_handle_a08_update_patient);
    RUN_TEST(test_handle_a08_patient_not_found);
    RUN_TEST(test_handle_a08_create_if_configured);
    RUN_TEST(test_handle_a40_merge_patients);
    RUN_TEST(test_handle_non_adt_message);

    // Callback tests
    std::cout << "\n--- Callback Tests ---" << std::endl;
    RUN_TEST(test_patient_created_callback);
    RUN_TEST(test_patient_updated_callback);
    RUN_TEST(test_patient_merged_callback);

    // ACK generation tests
    std::cout << "\n--- ACK Generation Tests ---" << std::endl;
    RUN_TEST(test_ack_generation);

    // Statistics tests
    std::cout << "\n--- Statistics Tests ---" << std::endl;
    RUN_TEST(test_statistics_tracking);

    // Concurrent tests
    std::cout << "\n--- Concurrent Processing Tests ---" << std::endl;
    RUN_TEST(test_concurrent_processing);

    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << std::endl;
    std::cout << "========================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
