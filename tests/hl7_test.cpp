/**
 * @file hl7_test.cpp
 * @brief Comprehensive unit tests for HL7 v2.x message handling module
 *
 * Tests for HL7 message parsing, building, types, and utilities.
 * Target coverage: >= 85%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/8
 * @see https://github.com/kcenon/pacs_bridge/issues/21
 */

#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
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

// Sample HL7 messages for testing
const std::string SAMPLE_ADT_A01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4|||AL|NE\r"
    "EVN|A01|20240115103000|||OPERATOR^JOHN\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||123 MAIN ST^^SPRINGFIELD^IL^62701||555-123-4567\r"
    "PV1|1|I|WARD^101^A^HOSPITAL||||SMITH^ROBERT^MD\r";

const std::string SAMPLE_ORM_O01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG003|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "PV1|1|I|WARD^101^A\r"
    "ORC|NW|ORD001^HIS|ACC001^PACS||SC\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT\r";

// =============================================================================
// HL7 Types Tests
// =============================================================================

bool test_encoding_characters_default() {
    hl7_encoding_characters enc;

    TEST_ASSERT(enc.field_separator == '|', "Default field separator should be |");
    TEST_ASSERT(enc.component_separator == '^', "Default component separator should be ^");
    TEST_ASSERT(enc.repetition_separator == '~', "Default repetition separator should be ~");
    TEST_ASSERT(enc.escape_character == '\\', "Default escape character should be \\");
    TEST_ASSERT(enc.subcomponent_separator == '&', "Default subcomponent separator should be &");

    return true;
}

bool test_encoding_characters_to_msh2() {
    hl7_encoding_characters enc;
    TEST_ASSERT(enc.to_msh2() == "^~\\&", "Encoding characters to_msh2 should be ^~\\&");

    enc.component_separator = '#';
    TEST_ASSERT(enc.to_msh2() == "#~\\&", "Modified encoding characters should reflect change");

    return true;
}

bool test_encoding_characters_from_msh2() {
    auto enc = hl7_encoding_characters::from_msh2("^~\\&");
    TEST_ASSERT(enc.component_separator == '^', "Component separator should be ^");
    TEST_ASSERT(enc.repetition_separator == '~', "Repetition separator should be ~");
    TEST_ASSERT(enc.escape_character == '\\', "Escape character should be \\");
    TEST_ASSERT(enc.subcomponent_separator == '&', "Subcomponent separator should be &");

    // Custom encoding
    auto enc2 = hl7_encoding_characters::from_msh2("#*!@");
    TEST_ASSERT(enc2.component_separator == '#', "Custom component separator");
    TEST_ASSERT(enc2.repetition_separator == '*', "Custom repetition separator");

    return true;
}

bool test_encoding_is_default() {
    hl7_encoding_characters enc;
    TEST_ASSERT(enc.is_default(), "Default encoding should return true for is_default()");

    enc.component_separator = '#';
    TEST_ASSERT(!enc.is_default(), "Modified encoding should return false for is_default()");

    return true;
}

bool test_hl7_error_codes() {
    // Verify error code values
    TEST_ASSERT(to_error_code(hl7_error::empty_message) == -950, "empty_message should be -950");
    TEST_ASSERT(to_error_code(hl7_error::missing_msh) == -951, "missing_msh should be -951");
    TEST_ASSERT(to_error_code(hl7_error::invalid_msh) == -952, "invalid_msh should be -952");
    TEST_ASSERT(to_error_code(hl7_error::invalid_segment) == -953, "invalid_segment should be -953");
    TEST_ASSERT(to_error_code(hl7_error::parse_error) == -966, "parse_error should be -966");

    // Verify error messages
    TEST_ASSERT(std::strcmp(to_string(hl7_error::invalid_segment), "Invalid segment structure") == 0,
                "Error message should match");
    TEST_ASSERT(std::strcmp(to_string(hl7_error::missing_msh), "Missing required MSH segment") == 0,
                "Error message should match");

    return true;
}

bool test_message_type_enum() {
    // to_string tests
    TEST_ASSERT(std::strcmp(to_string(message_type::ADT), "ADT") == 0, "ADT should convert to string");
    TEST_ASSERT(std::strcmp(to_string(message_type::ORM), "ORM") == 0, "ORM should convert to string");
    TEST_ASSERT(std::strcmp(to_string(message_type::ORU), "ORU") == 0, "ORU should convert to string");
    TEST_ASSERT(std::strcmp(to_string(message_type::ACK), "ACK") == 0, "ACK should convert to string");
    TEST_ASSERT(std::strcmp(to_string(message_type::UNKNOWN), "UNKNOWN") == 0, "UNKNOWN should convert to string");

    // parse_message_type tests
    TEST_ASSERT(parse_message_type("ADT") == message_type::ADT, "ADT string should parse");
    TEST_ASSERT(parse_message_type("ORM") == message_type::ORM, "ORM string should parse");
    TEST_ASSERT(parse_message_type("ORU") == message_type::ORU, "ORU string should parse");
    TEST_ASSERT(parse_message_type("ACK") == message_type::ACK, "ACK string should parse");
    TEST_ASSERT(parse_message_type("INVALID") == message_type::UNKNOWN, "Invalid should be UNKNOWN");
    TEST_ASSERT(parse_message_type("SIU") == message_type::SIU, "SIU string should parse");

    return true;
}

bool test_ack_code_enum() {
    // to_string tests
    TEST_ASSERT(std::strcmp(to_string(ack_code::AA), "AA") == 0, "AA should convert");
    TEST_ASSERT(std::strcmp(to_string(ack_code::AE), "AE") == 0, "AE should convert");
    TEST_ASSERT(std::strcmp(to_string(ack_code::AR), "AR") == 0, "AR should convert");
    TEST_ASSERT(std::strcmp(to_string(ack_code::CA), "CA") == 0, "CA should convert");
    TEST_ASSERT(std::strcmp(to_string(ack_code::CE), "CE") == 0, "CE should convert");
    TEST_ASSERT(std::strcmp(to_string(ack_code::CR), "CR") == 0, "CR should convert");

    // parse_ack_code tests
    TEST_ASSERT(parse_ack_code("AA") == ack_code::AA, "AA string should parse");
    TEST_ASSERT(parse_ack_code("AE") == ack_code::AE, "AE string should parse");
    TEST_ASSERT(parse_ack_code("AR") == ack_code::AR, "AR string should parse");
    TEST_ASSERT(parse_ack_code("CA") == ack_code::CA, "CA string should parse");
    TEST_ASSERT(parse_ack_code("INVALID") == ack_code::AA, "Invalid should default to AA");

    // is_ack_success tests
    TEST_ASSERT(is_ack_success(ack_code::AA), "AA should be success");
    TEST_ASSERT(is_ack_success(ack_code::CA), "CA should be success");
    TEST_ASSERT(!is_ack_success(ack_code::AE), "AE should not be success");
    TEST_ASSERT(!is_ack_success(ack_code::AR), "AR should not be success");

    return true;
}

bool test_hl7_timestamp() {
    // Parse full timestamp
    auto ts = hl7_timestamp::parse("20240115103045");
    TEST_ASSERT(ts.has_value(), "Timestamp should parse");
    TEST_ASSERT(ts->year == 2024, "Year should be 2024");
    TEST_ASSERT(ts->month == 1, "Month should be 1");
    TEST_ASSERT(ts->day == 15, "Day should be 15");
    TEST_ASSERT(ts->hour == 10, "Hour should be 10");
    TEST_ASSERT(ts->minute == 30, "Minute should be 30");
    TEST_ASSERT(ts->second == 45, "Second should be 45");

    // Format timestamp
    std::string formatted = ts->to_string();
    TEST_ASSERT(formatted.substr(0, 8) == "20240115", "Date portion should match");

    // Now timestamp
    auto now = hl7_timestamp::now();
    TEST_ASSERT(now.year >= 2024, "Current year should be >= 2024");

    // Date-only parsing
    auto date_only = hl7_timestamp::parse("20240515");
    TEST_ASSERT(date_only.has_value(), "Date-only should parse");
    TEST_ASSERT(date_only->year == 2024, "Date-only year should parse");
    TEST_ASSERT(date_only->month == 5, "Date-only month should parse");
    TEST_ASSERT(date_only->day == 15, "Date-only day should parse");

    // Invalid timestamp
    auto invalid = hl7_timestamp::parse("invalid");
    TEST_ASSERT(!invalid.has_value(), "Invalid timestamp should not parse");

    return true;
}

bool test_hl7_person_name() {
    hl7_person_name name;
    name.family_name = "DOE";
    name.given_name = "JOHN";
    name.middle_name = "WILLIAM";
    name.suffix = "JR";
    name.prefix = "DR";

    // Check empty
    TEST_ASSERT(!name.empty(), "Name should not be empty");

    // Display name
    std::string display = name.display_name();
    TEST_ASSERT(display.find("JOHN") != std::string::npos, "Display should contain given name");
    TEST_ASSERT(display.find("DOE") != std::string::npos, "Display should contain family name");

    // Formatted name
    std::string formatted = name.formatted_name();
    TEST_ASSERT(!formatted.empty(), "Formatted name should not be empty");

    // Empty name
    hl7_person_name empty_name;
    TEST_ASSERT(empty_name.empty(), "Empty name should be empty");

    return true;
}

bool test_hl7_address() {
    hl7_address addr;
    addr.street1 = "123 MAIN ST";
    addr.city = "SPRINGFIELD";
    addr.state = "IL";
    addr.postal_code = "62701";
    addr.country = "USA";

    // Check empty
    TEST_ASSERT(!addr.empty(), "Address should not be empty");

    // Formatted address
    std::string formatted = addr.formatted();
    TEST_ASSERT(formatted.find("123 MAIN ST") != std::string::npos, "Street should be in output");
    TEST_ASSERT(formatted.find("SPRINGFIELD") != std::string::npos, "City should be in output");

    // Empty address
    hl7_address empty_addr;
    TEST_ASSERT(empty_addr.empty(), "Empty address should be empty");

    return true;
}

bool test_hl7_patient_id() {
    hl7_patient_id pid;
    pid.id = "12345";
    pid.assigning_authority = "HOSPITAL";
    pid.id_type = "MR";

    // Check empty
    TEST_ASSERT(!pid.empty(), "Patient ID should not be empty");

    // Equality
    hl7_patient_id pid2;
    pid2.id = "12345";
    pid2.assigning_authority = "HOSPITAL";
    pid2.id_type = "MRN";
    TEST_ASSERT(pid == pid2, "Patient IDs with same id and authority should be equal");

    // Empty patient id
    hl7_patient_id empty_pid;
    TEST_ASSERT(empty_pid.empty(), "Empty patient ID should be empty");

    return true;
}

bool test_hl7_message_header() {
    hl7_message_header header;
    header.sending_application = "HIS";
    header.sending_facility = "HOSPITAL";
    header.receiving_application = "PACS";
    header.receiving_facility = "RADIOLOGY";
    header.type = message_type::ADT;
    header.type_string = "ADT";
    header.trigger_event = "A01";
    header.message_control_id = "MSG001";
    header.processing_id = "P";
    header.version_id = "2.4";

    // Verify structure
    TEST_ASSERT(header.sending_application == "HIS", "Sending app should match");
    TEST_ASSERT(header.type == message_type::ADT, "Message type should be ADT");
    TEST_ASSERT(header.trigger_event == "A01", "Trigger event should be A01");
    TEST_ASSERT(!header.is_ack(), "Should not be ACK");
    TEST_ASSERT(header.full_message_type() == "ADT^A01", "Full message type should be ADT^A01");

    return true;
}

bool test_validation_result() {
    validation_result result;
    TEST_ASSERT(result.valid, "New validation result should be valid");
    TEST_ASSERT(!result.has_errors(), "New result should have no errors");
    TEST_ASSERT(result.error_count() == 0, "Error count should be 0");

    // Add error
    result.add_error(hl7_error::missing_required_field, "MSH.9", "Message type is required");
    TEST_ASSERT(!result.valid, "Result should be invalid after error");
    TEST_ASSERT(result.has_errors(), "Result should have errors");
    TEST_ASSERT(result.error_count() == 1, "Error count should be 1");

    // Add warning
    result.add_warning(hl7_error::validation_failed, "PID.5", "Patient name is empty");
    TEST_ASSERT(result.warning_count() == 1, "Warning count should be 1");
    TEST_ASSERT(result.error_count() == 1, "Error count should still be 1");

    return true;
}

// =============================================================================
// HL7 Message Tests
// =============================================================================

bool test_hl7_subcomponent() {
    hl7_subcomponent sub("test value");
    TEST_ASSERT(sub.value() == "test value", "Subcomponent value should match");
    TEST_ASSERT(!sub.empty(), "Subcomponent should not be empty");

    hl7_subcomponent empty_sub;
    TEST_ASSERT(empty_sub.empty(), "Empty subcomponent should be empty");

    sub.set_value("new value");
    TEST_ASSERT(sub.value() == "new value", "Updated value should match");

    // Comparison
    hl7_subcomponent sub2("new value");
    TEST_ASSERT(sub == sub2, "Equal subcomponents should be equal");
    TEST_ASSERT(sub == "new value", "Subcomponent should equal string_view");

    return true;
}

bool test_hl7_component() {
    hl7_component comp("component value");
    TEST_ASSERT(comp.value() == "component value", "Component value should match");
    TEST_ASSERT(comp.subcomponent_count() >= 1, "Should have at least one subcomponent");
    TEST_ASSERT(!comp.empty(), "Component should not be empty");

    // Access subcomponent (1-based indexing)
    const auto& sub = comp.subcomponent(1);
    TEST_ASSERT(sub.value() == "component value", "First subcomponent should have value");

    // Mutable subcomponent access
    comp.subcomponent(2).set_value("sub2");
    TEST_ASSERT(comp.subcomponent_count() >= 2, "Should have at least two subcomponents");

    // Empty component
    hl7_component empty_comp;
    TEST_ASSERT(empty_comp.empty(), "Empty component should be empty");

    return true;
}

bool test_hl7_field() {
    hl7_field field("field value");
    TEST_ASSERT(field.value() == "field value", "Field value should match");
    TEST_ASSERT(!field.empty(), "Field should not be empty");

    // Access component (1-based indexing)
    const auto& comp = field.component(1);
    TEST_ASSERT(comp.value() == "field value", "First component should have value");

    // Mutable component access
    field.component(2).set_value("comp2");
    TEST_ASSERT(field.component_count() >= 2, "Should have at least two components");

    // Repetitions
    TEST_ASSERT(field.repetition_count() >= 1, "Should have at least one repetition");
    field.add_repetition();
    TEST_ASSERT(field.repetition_count() >= 2, "Should have at least two repetitions");

    // Empty field
    hl7_field empty_field;
    TEST_ASSERT(empty_field.empty(), "Empty field should be empty");

    return true;
}

bool test_hl7_segment() {
    hl7_segment seg("PID");
    TEST_ASSERT(seg.segment_id() == "PID", "Segment ID should be PID");
    TEST_ASSERT(seg.field_count() == 0, "New segment should have no fields");
    TEST_ASSERT(!seg.is_msh(), "PID segment should not be MSH");

    // Add fields (1-based indexing)
    seg.set_field(1, "1");
    seg.set_field(3, "12345");
    seg.set_field(5, "DOE^JOHN");

    TEST_ASSERT(seg.field_count() >= 5, "Should have at least 5 fields");

    // Get field
    const auto& f3 = seg.field(3);
    TEST_ASSERT(f3.value() == "12345", "Field 3 value should match");

    // Get field value helper
    TEST_ASSERT(seg.field_value(3) == "12345", "field_value should return field value");
    TEST_ASSERT(seg.field_value(5) == "DOE^JOHN", "field_value should return field 5 value");

    // Path-based access
    TEST_ASSERT(seg.get_value("3") == "12345", "get_value should return field value");

    // MSH segment
    hl7_segment msh("MSH");
    TEST_ASSERT(msh.is_msh(), "MSH segment should be MSH");

    return true;
}

bool test_hl7_message_creation() {
    hl7_message msg;
    TEST_ASSERT(msg.empty(), "New message should be empty");
    TEST_ASSERT(msg.segment_count() == 0, "New message should have no segments");

    // Add MSH segment
    auto& msh = msg.add_segment("MSH");
    msh.set_field(1, "|");
    msh.set_field(2, "^~\\&");
    msh.set_field(3, "HIS");
    msh.set_field(4, "HOSPITAL");
    msh.set_field(9, "ADT^A01");
    msh.set_field(10, "MSG001");
    msh.set_field(11, "P");
    msh.set_field(12, "2.4");

    TEST_ASSERT(msg.segment_count() == 1, "Message should have 1 segment");
    TEST_ASSERT(msg.has_segment("MSH"), "Message should have MSH");
    TEST_ASSERT(!msg.empty(), "Message should not be empty");

    // Get segment
    auto* msh_ptr = msg.segment("MSH");
    TEST_ASSERT(msh_ptr != nullptr, "MSH segment should exist");

    return true;
}

bool test_hl7_message_parsing() {
    // Parse using hl7_message::parse directly
    auto result = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse successfully");

    auto& msg = *result;

    // Verify segment count
    TEST_ASSERT(msg.segment_count() == 4, "Should have 4 segments");
    TEST_ASSERT(msg.has_segment("MSH"), "Should have MSH");
    TEST_ASSERT(msg.has_segment("EVN"), "Should have EVN");
    TEST_ASSERT(msg.has_segment("PID"), "Should have PID");
    TEST_ASSERT(msg.has_segment("PV1"), "Should have PV1");

    // Path-based access
    TEST_ASSERT(msg.get_value("MSH.3") == "HIS", "Sending app should be HIS");
    TEST_ASSERT(msg.get_value("MSH.4") == "HOSPITAL", "Sending facility should be HOSPITAL");
    TEST_ASSERT(msg.get_value("PID.5.1") == "DOE", "Family name should be DOE");
    TEST_ASSERT(msg.get_value("PID.5.2") == "JOHN", "Given name should be JOHN");
    TEST_ASSERT(msg.get_value("PID.8") == "M", "Gender should be M");

    // Non-existent path
    TEST_ASSERT(msg.get_value("ZZZ.1").empty(), "Non-existent segment should return empty");
    TEST_ASSERT(msg.get_value("PID.999").empty(), "Non-existent field should return empty");

    return true;
}

bool test_hl7_message_parsed_header() {
    auto result = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse successfully");

    auto header = result->header();
    TEST_ASSERT(header.sending_application == "HIS", "Sending app should be HIS");
    TEST_ASSERT(header.sending_facility == "HOSPITAL", "Sending facility should be HOSPITAL");
    TEST_ASSERT(header.receiving_application == "PACS", "Receiving app should be PACS");
    TEST_ASSERT(header.receiving_facility == "RADIOLOGY", "Receiving facility should be RADIOLOGY");
    TEST_ASSERT(header.type_string == "ADT", "Message type should be ADT");
    TEST_ASSERT(header.trigger_event == "A01", "Trigger event should be A01");
    TEST_ASSERT(header.message_control_id == "MSG001", "Control ID should be MSG001");
    TEST_ASSERT(header.processing_id == "P", "Processing ID should be P");
    TEST_ASSERT(header.version_id == "2.4", "Version should be 2.4");

    return true;
}

bool test_hl7_message_serialization() {
    auto result = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse successfully");

    // Serialize back
    std::string serialized = result->serialize();
    TEST_ASSERT(!serialized.empty(), "Serialized message should not be empty");
    TEST_ASSERT(serialized.find("MSH|") == 0, "Should start with MSH|");
    TEST_ASSERT(serialized.find("HIS") != std::string::npos, "Should contain HIS");
    TEST_ASSERT(serialized.find("DOE^JOHN") != std::string::npos, "Should contain patient name");

    // Re-parse the serialized message
    auto reparsed = hl7_message::parse(serialized);
    TEST_ASSERT(reparsed.has_value(), "Should re-parse successfully");
    TEST_ASSERT(reparsed->get_value("PID.5.1") == "DOE", "Re-parsed name should match");

    return true;
}

bool test_hl7_message_modification() {
    auto result = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse successfully");

    auto& msg = *result;

    // Modify via set_value
    msg.set_value("PID.5.1", "SMITH");
    TEST_ASSERT(msg.get_value("PID.5.1") == "SMITH", "Modified name should match");

    // Add new segment
    auto& obx = msg.add_segment("OBX");
    obx.set_field(1, "1");
    obx.set_field(2, "TX");
    TEST_ASSERT(msg.has_segment("OBX"), "Should have OBX segment");
    TEST_ASSERT(msg.segment_count() == 5, "Should now have 5 segments");

    return true;
}

bool test_hl7_message_validation() {
    auto result = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse successfully");

    auto validation = result->validate();
    TEST_ASSERT(validation.valid, "Valid message should pass validation");
    TEST_ASSERT(!validation.has_errors(), "Valid message should have no errors");

    // Test is_valid helper
    TEST_ASSERT(result->is_valid(), "is_valid should return true for valid message");

    return true;
}

bool test_hl7_message_ack_creation() {
    auto result = hl7_message::parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse successfully");

    // Create ACK
    auto ack = result->create_ack(ack_code::AA, "Message accepted");
    TEST_ASSERT(ack.has_segment("MSH"), "ACK should have MSH");
    TEST_ASSERT(ack.has_segment("MSA"), "ACK should have MSA");

    auto header = ack.header();
    TEST_ASSERT(header.type_string == "ACK", "Type should be ACK");

    // Sender/receiver should be swapped
    TEST_ASSERT(header.sending_application == "PACS", "Sender should be original receiver");
    TEST_ASSERT(header.receiving_application == "HIS", "Receiver should be original sender");

    // Check MSA segment
    TEST_ASSERT(ack.get_value("MSA.1") == "AA", "Ack code should be AA");
    TEST_ASSERT(ack.get_value("MSA.2") == "MSG001", "Control ID should match original");

    return true;
}

// =============================================================================
// HL7 Parser Tests
// =============================================================================

bool test_parser_basic() {
    hl7_parser parser;

    auto result = parser.parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse ADT A01 successfully");
    TEST_ASSERT(result->segment_count() == 4, "Should have 4 segments");

    auto result2 = parser.parse(SAMPLE_ORM_O01);
    TEST_ASSERT(result2.has_value(), "Should parse ORM O01 successfully");
    TEST_ASSERT(result2->segment_count() == 5, "Should have 5 segments");

    return true;
}

bool test_parser_error_handling() {
    hl7_parser parser;

    // Empty message
    auto result = parser.parse("");
    TEST_ASSERT(!result.has_value(), "Empty message should fail");
    TEST_ASSERT(result.error() == hl7_error::empty_message, "Error should be empty_message");

    // Missing MSH
    auto result2 = parser.parse("PID|1||12345\r");
    TEST_ASSERT(!result2.has_value(), "Message without MSH should fail");
    TEST_ASSERT(result2.error() == hl7_error::missing_msh, "Error should be missing_msh");

    // Invalid MSH
    auto result3 = parser.parse("MSH\r");
    TEST_ASSERT(!result3.has_value(), "Invalid MSH should fail");

    return true;
}

bool test_parser_encoding_detection() {
    hl7_parser parser;

    auto result = parser.parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse successfully");

    auto enc = result->encoding();
    TEST_ASSERT(enc.field_separator == '|', "Field separator should be |");
    TEST_ASSERT(enc.component_separator == '^', "Component separator should be ^");
    TEST_ASSERT(enc.repetition_separator == '~', "Repetition separator should be ~");
    TEST_ASSERT(enc.escape_character == '\\', "Escape character should be \\");
    TEST_ASSERT(enc.subcomponent_separator == '&', "Subcomponent separator should be &");

    return true;
}

bool test_parser_with_options() {
    parser_options opts;
    opts.lenient_mode = false;
    opts.validate_structure = true;

    hl7_parser parser(opts);

    auto result = parser.parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse with strict options");

    return true;
}

bool test_parser_with_details() {
    hl7_parser parser;
    parse_details details;

    auto result = parser.parse(SAMPLE_ADT_A01, &details);
    TEST_ASSERT(result.has_value(), "Should parse successfully");
    TEST_ASSERT(details.segment_count == 4, "Details should show 4 segments");
    TEST_ASSERT(details.original_size > 0, "Original size should be recorded");

    return true;
}

bool test_parser_extract_encoding() {
    auto enc_result = hl7_parser::extract_encoding(SAMPLE_ADT_A01);
    TEST_ASSERT(enc_result.has_value(), "Should extract encoding");
    TEST_ASSERT(enc_result->field_separator == '|', "Field separator should be |");
    TEST_ASSERT(enc_result->component_separator == '^', "Component separator should be ^");

    return true;
}

bool test_parser_extract_header() {
    auto header_result = hl7_parser::extract_header(SAMPLE_ADT_A01);
    TEST_ASSERT(header_result.has_value(), "Should extract header");
    TEST_ASSERT(header_result->sending_application == "HIS", "Sending app should be HIS");
    TEST_ASSERT(header_result->type_string == "ADT", "Type should be ADT");

    return true;
}

bool test_parser_looks_like_hl7() {
    TEST_ASSERT(hl7_parser::looks_like_hl7(SAMPLE_ADT_A01), "ADT should look like HL7");
    TEST_ASSERT(hl7_parser::looks_like_hl7(SAMPLE_ORM_O01), "ORM should look like HL7");
    TEST_ASSERT(!hl7_parser::looks_like_hl7("Hello World"), "Random text should not look like HL7");
    TEST_ASSERT(!hl7_parser::looks_like_hl7(""), "Empty string should not look like HL7");

    return true;
}

bool test_parser_segment_iteration() {
    hl7_parser parser;
    auto result = parser.parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse successfully");

    // Get all segments of a type
    auto pid_segments = result->segments("PID");
    TEST_ASSERT(pid_segments.size() == 1, "Should have 1 PID segment");

    auto pv1_segments = result->segments("PV1");
    TEST_ASSERT(pv1_segments.size() == 1, "Should have 1 PV1 segment");

    auto zxx_segments = result->segments("ZXX");
    TEST_ASSERT(zxx_segments.empty(), "Should have no ZXX segments");

    return true;
}

// =============================================================================
// HL7 Builder Tests
// =============================================================================

bool test_builder_basic() {
    auto result = hl7_builder::create()
                      .sending_app("TEST_APP")
                      .sending_facility("TEST_FAC")
                      .receiving_app("DEST_APP")
                      .receiving_facility("DEST_FAC")
                      .message_type("ADT", "A01")
                      .control_id("MSG12345")
                      .processing_id("P")
                      .version("2.4")
                      .build();

    TEST_ASSERT(result.has_value(), "Build should succeed");
    TEST_ASSERT(result->has_segment("MSH"), "Built message should have MSH");

    auto header = result->header();
    TEST_ASSERT(header.sending_application == "TEST_APP", "Sending app should match");
    TEST_ASSERT(header.receiving_application == "DEST_APP", "Receiving app should match");
    TEST_ASSERT(header.type_string == "ADT", "Type should be ADT");
    TEST_ASSERT(header.trigger_event == "A01", "Trigger should be A01");
    TEST_ASSERT(header.message_control_id == "MSG12345", "Control ID should match");

    return true;
}

bool test_builder_with_patient() {
    auto result = hl7_builder::create()
                      .sending_app("APP")
                      .sending_facility("FAC")
                      .receiving_app("DEST")
                      .receiving_facility("DFAC")
                      .message_type("ADT", "A01")
                      .control_id("MSG1")
                      .processing_id("P")
                      .version("2.4")
                      .patient_id("12345", "HOSPITAL", "MR")
                      .patient_name("DOE", "JOHN", "M")
                      .patient_sex("M")
                      .build();

    TEST_ASSERT(result.has_value(), "Build should succeed");
    TEST_ASSERT(result->has_segment("MSH"), "Should have MSH");
    TEST_ASSERT(result->has_segment("PID"), "Should have PID segment");

    return true;
}

bool test_builder_create_ack() {
    // First create a message to acknowledge
    auto original_result = hl7_builder::create()
                               .sending_app("HIS")
                               .sending_facility("HOSPITAL")
                               .receiving_app("PACS")
                               .receiving_facility("RADIOLOGY")
                               .message_type("ORM", "O01")
                               .control_id("ORM001")
                               .processing_id("P")
                               .version("2.4")
                               .build();

    TEST_ASSERT(original_result.has_value(), "Original should build");

    // Build ACK
    auto ack = hl7_builder::create_ack(*original_result, ack_code::AA, "Message accepted");

    TEST_ASSERT(ack.has_segment("MSH"), "ACK should have MSH");
    TEST_ASSERT(ack.has_segment("MSA"), "ACK should have MSA");

    auto header = ack.header();
    TEST_ASSERT(header.type_string == "ACK", "Type should be ACK");
    TEST_ASSERT(header.sending_application == "PACS", "Sender/receiver should swap");
    TEST_ASSERT(header.receiving_application == "HIS", "Sender/receiver should swap");

    TEST_ASSERT(ack.get_value("MSA.1") == "AA", "Ack code should be AA");
    TEST_ASSERT(ack.get_value("MSA.2") == "ORM001", "Control ID should match original");

    return true;
}

bool test_builder_set_field() {
    auto result = hl7_builder::create()
                      .sending_app("APP")
                      .sending_facility("FAC")
                      .receiving_app("DEST")
                      .receiving_facility("DFAC")
                      .message_type("ADT", "A01")
                      .control_id("MSG1")
                      .processing_id("P")
                      .version("2.4")
                      .set_field("PID.3", "67890")
                      .set_field("PID.5.1", "SMITH")
                      .build();

    TEST_ASSERT(result.has_value(), "Build should succeed");
    TEST_ASSERT(result->get_value("PID.3") == "67890", "Patient ID should be set");
    TEST_ASSERT(result->get_value("PID.5.1") == "SMITH", "Family name should be set");

    return true;
}

bool test_adt_builder() {
    auto builder = adt_builder::admit();
    auto result = builder
                      .sending_app("HIS")
                      .sending_facility("HOSPITAL")
                      .receiving_app("PACS")
                      .receiving_facility("RADIOLOGY")
                      .control_id("ADT001")
                      .patient_id("67890", "HOSPITAL", "MR")
                      .patient_name("SMITH", "JANE")
                      .patient_sex("F")
                      .build();

    TEST_ASSERT(result.has_value(), "Build should succeed");
    TEST_ASSERT(result->has_segment("MSH"), "Should have MSH");

    auto header = result->header();
    TEST_ASSERT(header.type_string == "ADT", "Type should be ADT");
    TEST_ASSERT(header.trigger_event == "A01", "Trigger should be A01");

    return true;
}

bool test_orm_builder() {
    auto builder = orm_builder::new_order();
    auto result = builder
                      .sending_app("HIS")
                      .sending_facility("HOSPITAL")
                      .receiving_app("PACS")
                      .receiving_facility("RADIOLOGY")
                      .control_id("ORM001")
                      .patient_id("12345", "HOSPITAL", "MR")
                      .patient_name("DOE", "JOHN")
                      .order_control("NW")
                      .placer_order_number("ORD001")
                      .procedure_code("71020", "CHEST XRAY", "CPT")
                      .build();

    TEST_ASSERT(result.has_value(), "Build should succeed");
    TEST_ASSERT(result->has_segment("MSH"), "Should have MSH");

    auto header = result->header();
    TEST_ASSERT(header.type_string == "ORM", "Type should be ORM");
    TEST_ASSERT(header.trigger_event == "O01", "Trigger should be O01");

    return true;
}

bool test_oru_builder() {
    auto builder = oru_builder::result();
    auto result = builder
                      .sending_app("PACS")
                      .sending_facility("RADIOLOGY")
                      .receiving_app("HIS")
                      .receiving_facility("HOSPITAL")
                      .control_id("ORU001")
                      .patient_id("12345", "HOSPITAL", "MR")
                      .patient_name("DOE", "JOHN")
                      .result_status("F")
                      .build();

    TEST_ASSERT(result.has_value(), "Build should succeed");
    TEST_ASSERT(result->has_segment("MSH"), "Should have MSH");

    auto header = result->header();
    TEST_ASSERT(header.type_string == "ORU", "Type should be ORU");
    TEST_ASSERT(header.trigger_event == "R01", "Trigger should be R01");

    return true;
}

bool test_message_id_generator() {
    auto id1 = message_id_generator::generate();
    TEST_ASSERT(!id1.empty(), "Generated ID should not be empty");

    auto id2 = message_id_generator::generate();
    // IDs should be unique (unless generated in same millisecond)
    // Just check that both are non-empty
    TEST_ASSERT(!id2.empty(), "Second ID should not be empty");

    auto uuid = message_id_generator::generate_uuid();
    TEST_ASSERT(!uuid.empty(), "UUID should not be empty");
    TEST_ASSERT(uuid.length() >= 32, "UUID should be at least 32 chars");

    auto prefixed = message_id_generator::generate_with_prefix("TEST");
    TEST_ASSERT(prefixed.find("TEST") == 0, "Prefixed ID should start with prefix");

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== HL7 Types Tests ===" << std::endl;
    RUN_TEST(test_encoding_characters_default);
    RUN_TEST(test_encoding_characters_to_msh2);
    RUN_TEST(test_encoding_characters_from_msh2);
    RUN_TEST(test_encoding_is_default);
    RUN_TEST(test_hl7_error_codes);
    RUN_TEST(test_message_type_enum);
    RUN_TEST(test_ack_code_enum);
    RUN_TEST(test_hl7_timestamp);
    RUN_TEST(test_hl7_person_name);
    RUN_TEST(test_hl7_address);
    RUN_TEST(test_hl7_patient_id);
    RUN_TEST(test_hl7_message_header);
    RUN_TEST(test_validation_result);

    std::cout << "\n=== HL7 Message Tests ===" << std::endl;
    RUN_TEST(test_hl7_subcomponent);
    RUN_TEST(test_hl7_component);
    RUN_TEST(test_hl7_field);
    RUN_TEST(test_hl7_segment);
    RUN_TEST(test_hl7_message_creation);
    RUN_TEST(test_hl7_message_parsing);
    RUN_TEST(test_hl7_message_parsed_header);
    RUN_TEST(test_hl7_message_serialization);
    RUN_TEST(test_hl7_message_modification);
    RUN_TEST(test_hl7_message_validation);
    RUN_TEST(test_hl7_message_ack_creation);

    std::cout << "\n=== HL7 Parser Tests ===" << std::endl;
    RUN_TEST(test_parser_basic);
    RUN_TEST(test_parser_error_handling);
    RUN_TEST(test_parser_encoding_detection);
    RUN_TEST(test_parser_with_options);
    RUN_TEST(test_parser_with_details);
    RUN_TEST(test_parser_extract_encoding);
    RUN_TEST(test_parser_extract_header);
    RUN_TEST(test_parser_looks_like_hl7);
    RUN_TEST(test_parser_segment_iteration);

    std::cout << "\n=== HL7 Builder Tests ===" << std::endl;
    RUN_TEST(test_builder_basic);
    RUN_TEST(test_builder_with_patient);
    RUN_TEST(test_builder_create_ack);
    RUN_TEST(test_builder_set_field);
    RUN_TEST(test_adt_builder);
    RUN_TEST(test_orm_builder);
    RUN_TEST(test_oru_builder);
    RUN_TEST(test_message_id_generator);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    if (passed + failed > 0) {
        double coverage = (passed * 100.0) / (passed + failed);
        std::cout << "Pass Rate: " << coverage << "%" << std::endl;
    }

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::hl7::test

int main() {
    return pacs::bridge::hl7::test::run_all_tests();
}
