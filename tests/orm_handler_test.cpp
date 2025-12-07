/**
 * @file orm_handler_test.cpp
 * @brief Comprehensive unit tests for ORM message handler
 *
 * Tests for ORM^O01 message handling including order creation, modification,
 * cancellation, and status changes. Target coverage: >= 85%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/15
 */

#include "pacs/bridge/protocol/hl7/orm_handler.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <cassert>
#include <iostream>
#include <string>

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
// Sample ORM Messages
// =============================================================================

// ORM^O01 New Order message
const std::string SAMPLE_ORM_NW =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG001|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||123 MAIN ST^^SPRINGFIELD^IL^62701||555-123-4567\r"
    "PV1|1|I|WARD^101^A^HOSPITAL||||SMITH^ROBERT^MD\r"
    "ORC|NW|ORD001^HIS|ACC001^PACS||SC|||^^^20240115120000^^R||20240115110000|JONES^MARY^RN||||RADIOLOGY\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT||20240115110000|20240115120000||||||||SMITH^ROBERT^MD||||||20240115110000|||1^ROUTINE^HL70078|||||||CR\r"
    "ZDS|1.2.840.10008.5.1.4.12345^UID\r";

// ORM^O01 Change Order message
const std::string SAMPLE_ORM_XO =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115113000||ORM^O01|MSG002|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||456 OAK AVE^^SPRINGFIELD^IL^62702||555-987-6543\r"
    "PV1|1|I|WARD^102^B^HOSPITAL||||JONES^SARAH^MD\r"
    "ORC|XO|ORD001^HIS|ACC001^PACS||SC|||^^^20240115140000^^R||20240115113000|JONES^MARY^RN||||RADIOLOGY\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY 2VIEW^CPT||20240115113000|20240115140000||||||||JONES^SARAH^MD||||||20240115113000|||2^STAT^HL70078|||||||CR\r";

// ORM^O01 Cancel Order message
const std::string SAMPLE_ORM_CA =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115120000||ORM^O01|MSG003|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "ORC|CA|ORD001^HIS|ACC001^PACS||CA|||^^^20240115120000^^R||20240115120000|JONES^MARY^RN||||RADIOLOGY\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT\r";

// ORM^O01 Status Change message
const std::string SAMPLE_ORM_SC =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115130000||ORM^O01|MSG004|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "ORC|SC|ORD001^HIS|ACC001^PACS||IP|||^^^20240115130000^^R||20240115130000|JONES^MARY^RN||||RADIOLOGY\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT\r";

// ORM^O01 Discontinue Order message
const std::string SAMPLE_ORM_DC =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115140000||ORM^O01|MSG005|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "ORC|DC|ORD001^HIS|ACC001^PACS||DC|||^^^20240115140000^^R||20240115140000|JONES^MARY^RN||||RADIOLOGY\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT\r";

// =============================================================================
// Mock MWL Client
// =============================================================================

/**
 * @brief Mock MWL client for testing without pacs_system connection
 */
class mock_mwl_client : public pacs_adapter::mwl_client {
public:
    mock_mwl_client()
        : pacs_adapter::mwl_client(pacs_adapter::mwl_client_config{}) {}

    // Store entries for testing
    std::unordered_map<std::string, mapping::mwl_item> entries_;

    // Track operations
    size_t add_count_ = 0;
    size_t update_count_ = 0;
    size_t cancel_count_ = 0;

    // Override operations for testing
    bool mock_exists(std::string_view accession) const {
        return entries_.find(std::string(accession)) != entries_.end();
    }

    void mock_add(const mapping::mwl_item& item) {
        entries_[item.imaging_service_request.accession_number] = item;
        add_count_++;
    }

    void mock_update(std::string_view accession, const mapping::mwl_item& item) {
        entries_[std::string(accession)] = item;
        update_count_++;
    }

    void mock_cancel(std::string_view accession) {
        entries_.erase(std::string(accession));
        cancel_count_++;
    }

    mapping::mwl_item mock_get(std::string_view accession) const {
        auto it = entries_.find(std::string(accession));
        if (it != entries_.end()) {
            return it->second;
        }
        return {};
    }
};

// =============================================================================
// Order Control Tests
// =============================================================================

bool test_order_control_parsing() {
    TEST_ASSERT(parse_order_control("NW") == order_control::new_order,
                "NW should parse to new_order");
    TEST_ASSERT(parse_order_control("XO") == order_control::change_order,
                "XO should parse to change_order");
    TEST_ASSERT(parse_order_control("CA") == order_control::cancel_order,
                "CA should parse to cancel_order");
    TEST_ASSERT(parse_order_control("DC") == order_control::discontinue_order,
                "DC should parse to discontinue_order");
    TEST_ASSERT(parse_order_control("SC") == order_control::status_change,
                "SC should parse to status_change");
    TEST_ASSERT(parse_order_control("XX") == order_control::unknown,
                "XX should parse to unknown");
    return true;
}

bool test_order_control_to_string() {
    TEST_ASSERT(std::string(to_string(order_control::new_order)) == "NW",
                "new_order should convert to NW");
    TEST_ASSERT(std::string(to_string(order_control::change_order)) == "XO",
                "change_order should convert to XO");
    TEST_ASSERT(std::string(to_string(order_control::cancel_order)) == "CA",
                "cancel_order should convert to CA");
    TEST_ASSERT(std::string(to_string(order_control::discontinue_order)) == "DC",
                "discontinue_order should convert to DC");
    TEST_ASSERT(std::string(to_string(order_control::status_change)) == "SC",
                "status_change should convert to SC");
    return true;
}

// =============================================================================
// Order Status Tests
// =============================================================================

bool test_order_status_parsing() {
    TEST_ASSERT(parse_order_status("SC") == order_status::scheduled,
                "SC should parse to scheduled");
    TEST_ASSERT(parse_order_status("IP") == order_status::in_progress,
                "IP should parse to in_progress");
    TEST_ASSERT(parse_order_status("CM") == order_status::completed,
                "CM should parse to completed");
    TEST_ASSERT(parse_order_status("CA") == order_status::cancelled,
                "CA should parse to cancelled");
    TEST_ASSERT(parse_order_status("DC") == order_status::discontinued,
                "DC should parse to discontinued");
    TEST_ASSERT(parse_order_status("HD") == order_status::hold,
                "HD should parse to hold");
    TEST_ASSERT(parse_order_status("XX") == order_status::unknown,
                "XX should parse to unknown");
    return true;
}

bool test_order_status_to_mwl_status() {
    TEST_ASSERT(std::string(to_mwl_status(order_status::scheduled)) == "SCHEDULED",
                "scheduled should convert to SCHEDULED");
    TEST_ASSERT(std::string(to_mwl_status(order_status::in_progress)) == "STARTED",
                "in_progress should convert to STARTED");
    TEST_ASSERT(std::string(to_mwl_status(order_status::completed)) == "COMPLETED",
                "completed should convert to COMPLETED");
    TEST_ASSERT(
        std::string(to_mwl_status(order_status::cancelled)) == "DISCONTINUED",
        "cancelled should convert to DISCONTINUED");
    TEST_ASSERT(
        std::string(to_mwl_status(order_status::discontinued)) == "DISCONTINUED",
        "discontinued should convert to DISCONTINUED");
    return true;
}

// =============================================================================
// Error Code Tests
// =============================================================================

bool test_orm_error_codes() {
    TEST_ASSERT(to_error_code(orm_error::not_orm_message) == -860,
                "not_orm_message should be -860");
    TEST_ASSERT(to_error_code(orm_error::unsupported_order_control) == -861,
                "unsupported_order_control should be -861");
    TEST_ASSERT(to_error_code(orm_error::missing_required_field) == -862,
                "missing_required_field should be -862");
    TEST_ASSERT(to_error_code(orm_error::order_not_found) == -863,
                "order_not_found should be -863");
    TEST_ASSERT(to_error_code(orm_error::processing_failed) == -869,
                "processing_failed should be -869");
    return true;
}

bool test_orm_error_to_string() {
    TEST_ASSERT(
        std::string(to_string(orm_error::not_orm_message)) ==
            "Message is not an ORM message",
        "not_orm_message description should match");
    TEST_ASSERT(
        std::string(to_string(orm_error::order_not_found)) ==
            "Order not found for update/cancel operation",
        "order_not_found description should match");
    return true;
}

// =============================================================================
// Order Info Extraction Tests
// =============================================================================

bool test_extract_order_info_nw() {
    auto parse_result = hl7_parser::parse(SAMPLE_ORM_NW);
    TEST_ASSERT(parse_result.has_value(), "Should parse NW message");

    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    auto info_result = handler.extract_order_info(*parse_result);
    TEST_ASSERT(info_result.has_value(), "Should extract order info");

    const auto& info = *info_result;
    TEST_ASSERT(info.control == order_control::new_order,
                "Control should be new_order");
    TEST_ASSERT(info.status == order_status::scheduled,
                "Status should be scheduled");
    TEST_ASSERT(info.placer_order_number == "ORD001",
                "Placer order number should be ORD001");
    TEST_ASSERT(info.filler_order_number == "ACC001",
                "Filler order number should be ACC001");
    TEST_ASSERT(info.patient_id == "12345", "Patient ID should be 12345");
    TEST_ASSERT(info.patient_name == "DOE^JOHN",
                "Patient name should be DOE^JOHN");
    TEST_ASSERT(info.modality == "CR", "Modality should be CR");
    TEST_ASSERT(info.procedure_code == "71020",
                "Procedure code should be 71020");
    TEST_ASSERT(!info.study_instance_uid.empty(),
                "Study Instance UID should be extracted from ZDS");

    return true;
}

bool test_extract_order_info_xo() {
    auto parse_result = hl7_parser::parse(SAMPLE_ORM_XO);
    TEST_ASSERT(parse_result.has_value(), "Should parse XO message");

    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    auto info_result = handler.extract_order_info(*parse_result);
    TEST_ASSERT(info_result.has_value(), "Should extract order info");

    const auto& info = *info_result;
    TEST_ASSERT(info.control == order_control::change_order,
                "Control should be change_order");

    return true;
}

bool test_extract_order_info_ca() {
    auto parse_result = hl7_parser::parse(SAMPLE_ORM_CA);
    TEST_ASSERT(parse_result.has_value(), "Should parse CA message");

    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    auto info_result = handler.extract_order_info(*parse_result);
    TEST_ASSERT(info_result.has_value(), "Should extract order info");

    const auto& info = *info_result;
    TEST_ASSERT(info.control == order_control::cancel_order,
                "Control should be cancel_order");
    TEST_ASSERT(info.status == order_status::cancelled,
                "Status should be cancelled");

    return true;
}

// =============================================================================
// Handler Configuration Tests
// =============================================================================

bool test_handler_config_defaults() {
    orm_handler_config config;

    TEST_ASSERT(config.allow_nw_update == false,
                "allow_nw_update default should be false");
    TEST_ASSERT(config.allow_xo_create == false,
                "allow_xo_create default should be false");
    TEST_ASSERT(config.auto_generate_study_uid == true,
                "auto_generate_study_uid default should be true");
    TEST_ASSERT(config.validate_order_data == true,
                "validate_order_data default should be true");
    TEST_ASSERT(config.detailed_ack == true,
                "detailed_ack default should be true");
    TEST_ASSERT(config.ack_sending_application == "PACS_BRIDGE",
                "ack_sending_application should be PACS_BRIDGE");

    return true;
}

bool test_handler_with_custom_config() {
    orm_handler_config config;
    config.allow_nw_update = true;
    config.allow_xo_create = true;
    config.default_modality = "CT";

    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl),
                        config);

    TEST_ASSERT(handler.config().allow_nw_update == true,
                "Config allow_nw_update should be true");
    TEST_ASSERT(handler.config().allow_xo_create == true,
                "Config allow_xo_create should be true");

    return true;
}

// =============================================================================
// can_handle Tests
// =============================================================================

bool test_can_handle_orm_message() {
    auto parse_result = hl7_parser::parse(SAMPLE_ORM_NW);
    TEST_ASSERT(parse_result.has_value(), "Should parse ORM message");

    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    TEST_ASSERT(handler.can_handle(*parse_result) == true,
                "Should be able to handle ORM message");

    return true;
}

bool test_cannot_handle_adt_message() {
    // ADT message
    const std::string adt_msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r";

    auto parse_result = hl7_parser::parse(adt_msg);
    TEST_ASSERT(parse_result.has_value(), "Should parse ADT message");

    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    TEST_ASSERT(handler.can_handle(*parse_result) == false,
                "Should not be able to handle ADT message");

    return true;
}

// =============================================================================
// Supported Controls Tests
// =============================================================================

bool test_supported_controls() {
    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    auto controls = handler.supported_controls();
    TEST_ASSERT(controls.size() == 5, "Should support 5 order controls");

    auto has_nw = std::find(controls.begin(), controls.end(), "NW") != controls.end();
    auto has_xo = std::find(controls.begin(), controls.end(), "XO") != controls.end();
    auto has_ca = std::find(controls.begin(), controls.end(), "CA") != controls.end();
    auto has_dc = std::find(controls.begin(), controls.end(), "DC") != controls.end();
    auto has_sc = std::find(controls.begin(), controls.end(), "SC") != controls.end();

    TEST_ASSERT(has_nw, "Should support NW");
    TEST_ASSERT(has_xo, "Should support XO");
    TEST_ASSERT(has_ca, "Should support CA");
    TEST_ASSERT(has_dc, "Should support DC");
    TEST_ASSERT(has_sc, "Should support SC");

    return true;
}

// =============================================================================
// Statistics Tests
// =============================================================================

bool test_initial_statistics() {
    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    auto stats = handler.get_statistics();
    TEST_ASSERT(stats.total_processed == 0, "Initial total should be 0");
    TEST_ASSERT(stats.success_count == 0, "Initial success count should be 0");
    TEST_ASSERT(stats.failure_count == 0, "Initial failure count should be 0");
    TEST_ASSERT(stats.nw_count == 0, "Initial NW count should be 0");
    TEST_ASSERT(stats.entries_created == 0, "Initial entries created should be 0");

    return true;
}

bool test_reset_statistics() {
    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    // Manually set some values (would normally be set by processing)
    handler.reset_statistics();

    auto stats = handler.get_statistics();
    TEST_ASSERT(stats.total_processed == 0, "After reset, total should be 0");

    return true;
}

// =============================================================================
// ACK Generation Tests
// =============================================================================

bool test_generate_ack_success() {
    auto parse_result = hl7_parser::parse(SAMPLE_ORM_NW);
    TEST_ASSERT(parse_result.has_value(), "Should parse ORM message");

    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    auto ack = handler.generate_ack(*parse_result, true);

    auto header = ack.header();
    TEST_ASSERT(header.type == message_type::ACK, "ACK type should be ACK");
    TEST_ASSERT(header.trigger_event == "O01", "Trigger event should be O01");

    const auto* msa = ack.segment("MSA");
    TEST_ASSERT(msa != nullptr, "ACK should have MSA segment");
    TEST_ASSERT(msa->field_value(1) == "AA", "MSA-1 should be AA for success");
    TEST_ASSERT(msa->field_value(2) == "MSG001",
                "MSA-2 should contain original message control ID");

    return true;
}

bool test_generate_ack_error() {
    auto parse_result = hl7_parser::parse(SAMPLE_ORM_NW);
    TEST_ASSERT(parse_result.has_value(), "Should parse ORM message");

    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    auto ack = handler.generate_ack(*parse_result, false, "AE", "Order not found");

    const auto* msa = ack.segment("MSA");
    TEST_ASSERT(msa != nullptr, "ACK should have MSA segment");
    TEST_ASSERT(msa->field_value(1) == "AE", "MSA-1 should be AE for error");

    return true;
}

// =============================================================================
// Order Result Structure Tests
// =============================================================================

bool test_orm_result_defaults() {
    orm_result result;

    TEST_ASSERT(result.success == false, "Default success should be false");
    TEST_ASSERT(result.control == order_control::unknown,
                "Default control should be unknown");
    TEST_ASSERT(result.status == order_status::unknown,
                "Default status should be unknown");
    TEST_ASSERT(result.accession_number.empty(),
                "Default accession number should be empty");
    TEST_ASSERT(result.warnings.empty(), "Default warnings should be empty");

    return true;
}

// =============================================================================
// Order Info Structure Tests
// =============================================================================

bool test_order_info_defaults() {
    order_info info;

    TEST_ASSERT(info.control == order_control::unknown,
                "Default control should be unknown");
    TEST_ASSERT(info.status == order_status::unknown,
                "Default status should be unknown");
    TEST_ASSERT(info.patient_id.empty(), "Default patient ID should be empty");
    TEST_ASSERT(info.modality.empty(), "Default modality should be empty");

    return true;
}

// =============================================================================
// Callback Tests
// =============================================================================

bool test_callback_registration() {
    auto mwl = std::make_shared<mock_mwl_client>();
    orm_handler handler(std::static_pointer_cast<pacs_adapter::mwl_client>(mwl));

    bool created_called = false;
    bool updated_called = false;
    bool cancelled_called = false;
    bool status_changed_called = false;

    handler.on_order_created(
        [&created_called](const order_info&, const mapping::mwl_item&) {
            created_called = true;
        });

    handler.on_order_updated([&updated_called](const order_info&,
                                                const mapping::mwl_item&,
                                                const mapping::mwl_item&) {
        updated_called = true;
    });

    handler.on_order_cancelled(
        [&cancelled_called](const std::string&, const std::string&) {
            cancelled_called = true;
        });

    handler.on_status_changed([&status_changed_called](const std::string&,
                                                        order_status,
                                                        order_status) {
        status_changed_called = true;
    });

    // Callbacks are registered but not called without processing
    TEST_ASSERT(!created_called, "created callback should not be called yet");
    TEST_ASSERT(!updated_called, "updated callback should not be called yet");
    TEST_ASSERT(!cancelled_called, "cancelled callback should not be called yet");
    TEST_ASSERT(!status_changed_called,
                "status_changed callback should not be called yet");

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "\n=== ORM Handler Unit Tests ===\n" << std::endl;

    // Order Control Tests
    std::cout << "\n--- Order Control Tests ---" << std::endl;
    RUN_TEST(test_order_control_parsing);
    RUN_TEST(test_order_control_to_string);

    // Order Status Tests
    std::cout << "\n--- Order Status Tests ---" << std::endl;
    RUN_TEST(test_order_status_parsing);
    RUN_TEST(test_order_status_to_mwl_status);

    // Error Code Tests
    std::cout << "\n--- Error Code Tests ---" << std::endl;
    RUN_TEST(test_orm_error_codes);
    RUN_TEST(test_orm_error_to_string);

    // Order Info Extraction Tests
    std::cout << "\n--- Order Info Extraction Tests ---" << std::endl;
    RUN_TEST(test_extract_order_info_nw);
    RUN_TEST(test_extract_order_info_xo);
    RUN_TEST(test_extract_order_info_ca);

    // Configuration Tests
    std::cout << "\n--- Configuration Tests ---" << std::endl;
    RUN_TEST(test_handler_config_defaults);
    RUN_TEST(test_handler_with_custom_config);

    // Handler Tests
    std::cout << "\n--- Handler Tests ---" << std::endl;
    RUN_TEST(test_can_handle_orm_message);
    RUN_TEST(test_cannot_handle_adt_message);
    RUN_TEST(test_supported_controls);

    // Statistics Tests
    std::cout << "\n--- Statistics Tests ---" << std::endl;
    RUN_TEST(test_initial_statistics);
    RUN_TEST(test_reset_statistics);

    // ACK Generation Tests
    std::cout << "\n--- ACK Generation Tests ---" << std::endl;
    RUN_TEST(test_generate_ack_success);
    RUN_TEST(test_generate_ack_error);

    // Structure Tests
    std::cout << "\n--- Structure Tests ---" << std::endl;
    RUN_TEST(test_orm_result_defaults);
    RUN_TEST(test_order_info_defaults);

    // Callback Tests
    std::cout << "\n--- Callback Tests ---" << std::endl;
    RUN_TEST(test_callback_registration);

    // Summary
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    return failed;
}

}  // namespace pacs::bridge::hl7::test

int main() {
    return pacs::bridge::hl7::test::run_all_tests();
}
