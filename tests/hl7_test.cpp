/**
 * @file hl7_test.cpp
 * @brief Comprehensive unit tests for HL7 v2.x message handling module
 *
 * Tests for HL7 message parsing, building, types, and utilities.
 * Target coverage: >= 85%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/21
 */

#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <cassert>
#include <chrono>
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
    encoding_characters enc;

    TEST_ASSERT(enc.field_separator == '|', "Default field separator should be |");
    TEST_ASSERT(enc.component_separator == '^', "Default component separator should be ^");
    TEST_ASSERT(enc.repetition_separator == '~', "Default repetition separator should be ~");
    TEST_ASSERT(enc.escape_character == '\\', "Default escape character should be \\");
    TEST_ASSERT(enc.subcomponent_separator == '&', "Default subcomponent separator should be &");

    return true;
}

bool test_encoding_characters_to_string() {
    encoding_characters enc;
    TEST_ASSERT(enc.to_string() == "^~\\&", "Encoding characters string should be ^~\\&");

    enc.component_separator = '#';
    TEST_ASSERT(enc.to_string() == "#~\\&", "Modified encoding characters should reflect change");

    return true;
}

bool test_hl7_error_codes() {
    TEST_ASSERT(to_error_code(hl7_error::parse_error) == -950, "parse_error should be -950");
    TEST_ASSERT(to_error_code(hl7_error::invalid_version) == -967, "invalid_version should be -967");

    TEST_ASSERT(std::string(to_string(hl7_error::invalid_segment)) ==
                    "Invalid segment format",
                "Error message should match");
    TEST_ASSERT(std::string(to_string(hl7_error::missing_msh)) ==
                    "Missing MSH segment",
                "Error message should match");

    return true;
}

bool test_message_type_enum() {
    TEST_ASSERT(to_string(message_type::ADT) == "ADT", "ADT should convert to string");
    TEST_ASSERT(to_string(message_type::ORM) == "ORM", "ORM should convert to string");
    TEST_ASSERT(to_string(message_type::ORU) == "ORU", "ORU should convert to string");
    TEST_ASSERT(to_string(message_type::ACK) == "ACK", "ACK should convert to string");
    TEST_ASSERT(to_string(message_type::UNKNOWN) == "UNKNOWN", "UNKNOWN should convert to string");

    TEST_ASSERT(message_type_from_string("ADT") == message_type::ADT, "ADT string should parse");
    TEST_ASSERT(message_type_from_string("ORM") == message_type::ORM, "ORM string should parse");
    TEST_ASSERT(message_type_from_string("INVALID") == message_type::UNKNOWN, "Invalid should be UNKNOWN");

    return true;
}

bool test_ack_code_enum() {
    TEST_ASSERT(to_string(ack_code::AA) == "AA", "AA should convert");
    TEST_ASSERT(to_string(ack_code::AE) == "AE", "AE should convert");
    TEST_ASSERT(to_string(ack_code::AR) == "AR", "AR should convert");
    TEST_ASSERT(to_string(ack_code::CA) == "CA", "CA should convert");

    TEST_ASSERT(ack_code_from_string("AA") == ack_code::AA, "AA string should parse");
    TEST_ASSERT(ack_code_from_string("INVALID") == ack_code::UNKNOWN, "Invalid should be UNKNOWN");

    return true;
}

bool test_hl7_timestamp() {
    // Parse timestamp
    auto ts = hl7_timestamp::parse("20240115103045.123");
    TEST_ASSERT(ts.year == 2024, "Year should be 2024");
    TEST_ASSERT(ts.month == 1, "Month should be 1");
    TEST_ASSERT(ts.day == 15, "Day should be 15");
    TEST_ASSERT(ts.hour == 10, "Hour should be 10");
    TEST_ASSERT(ts.minute == 30, "Minute should be 30");
    TEST_ASSERT(ts.second == 45, "Second should be 45");

    // Format timestamp
    std::string formatted = ts.to_string();
    TEST_ASSERT(formatted.substr(0, 8) == "20240115", "Date portion should match");

    // Now timestamp
    auto now = hl7_timestamp::now();
    TEST_ASSERT(now.year >= 2024, "Current year should be >= 2024");

    // Date-only parsing
    auto date_only = hl7_timestamp::parse("20240515");
    TEST_ASSERT(date_only.year == 2024, "Date-only year should parse");
    TEST_ASSERT(date_only.month == 5, "Date-only month should parse");
    TEST_ASSERT(date_only.day == 15, "Date-only day should parse");

    return true;
}

bool test_person_name() {
    person_name name;
    name.family_name = "DOE";
    name.given_name = "JOHN";
    name.middle_name = "WILLIAM";
    name.suffix = "JR";
    name.prefix = "DR";

    std::string formatted = name.to_hl7();
    TEST_ASSERT(formatted == "DOE^JOHN^WILLIAM^JR^DR", "HL7 format should match");

    // Parse from HL7
    auto parsed = person_name::from_hl7("SMITH^JANE^M");
    TEST_ASSERT(parsed.family_name == "SMITH", "Family name should parse");
    TEST_ASSERT(parsed.given_name == "JANE", "Given name should parse");
    TEST_ASSERT(parsed.middle_name == "M", "Middle name should parse");

    // Parse simple name
    auto simple = person_name::from_hl7("WILSON");
    TEST_ASSERT(simple.family_name == "WILSON", "Simple family name should parse");
    TEST_ASSERT(simple.given_name.empty(), "Given name should be empty");

    return true;
}

bool test_hl7_address() {
    hl7_address addr;
    addr.street = "123 MAIN ST";
    addr.city = "SPRINGFIELD";
    addr.state = "IL";
    addr.postal_code = "62701";
    addr.country = "USA";

    std::string formatted = addr.to_hl7();
    TEST_ASSERT(formatted.find("123 MAIN ST") != std::string::npos, "Street should be in output");
    TEST_ASSERT(formatted.find("SPRINGFIELD") != std::string::npos, "City should be in output");

    // Parse from HL7
    auto parsed = hl7_address::from_hl7("456 OAK AVE^^CHICAGO^IL^60601^USA");
    TEST_ASSERT(parsed.street == "456 OAK AVE", "Street should parse");
    TEST_ASSERT(parsed.city == "CHICAGO", "City should parse");
    TEST_ASSERT(parsed.state == "IL", "State should parse");
    TEST_ASSERT(parsed.postal_code == "60601", "Postal code should parse");

    return true;
}

bool test_patient_id() {
    patient_id pid;
    pid.id = "12345";
    pid.assigning_authority = "HOSPITAL";
    pid.identifier_type = "MR";

    std::string formatted = pid.to_hl7();
    TEST_ASSERT(formatted.find("12345") != std::string::npos, "ID should be in output");
    TEST_ASSERT(formatted.find("HOSPITAL") != std::string::npos, "Authority should be in output");
    TEST_ASSERT(formatted.find("MR") != std::string::npos, "Type should be in output");

    // Parse from HL7
    auto parsed = patient_id::from_hl7("67890^^^CLINIC^MRN");
    TEST_ASSERT(parsed.id == "67890", "ID should parse");
    TEST_ASSERT(parsed.assigning_authority == "CLINIC", "Authority should parse");
    TEST_ASSERT(parsed.identifier_type == "MRN", "Type should parse");

    return true;
}

bool test_message_header() {
    message_header header;
    header.sending_application = "HIS";
    header.sending_facility = "HOSPITAL";
    header.receiving_application = "PACS";
    header.receiving_facility = "RADIOLOGY";
    header.message_type = message_type::ADT;
    header.type_string = "ADT";
    header.trigger_event = "A01";
    header.message_control_id = "MSG001";
    header.processing_id = "P";
    header.version_id = "2.4";

    // Verify structure
    TEST_ASSERT(header.sending_application == "HIS", "Sending app should match");
    TEST_ASSERT(header.message_type == message_type::ADT, "Message type should be ADT");
    TEST_ASSERT(header.trigger_event == "A01", "Trigger event should be A01");

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

    return true;
}

bool test_hl7_component() {
    hl7_component comp("component value");
    TEST_ASSERT(comp.value() == "component value", "Component value should match");
    TEST_ASSERT(comp.subcomponent_count() >= 1, "Should have at least one subcomponent");

    // Add subcomponent
    comp.add_subcomponent("sub1");
    TEST_ASSERT(comp.subcomponent_count() >= 2, "Should have at least two subcomponents");

    // Access subcomponent
    auto* sub = comp.subcomponent(0);
    TEST_ASSERT(sub != nullptr, "First subcomponent should exist");

    return true;
}

bool test_hl7_field() {
    hl7_field field("field value");
    TEST_ASSERT(field.value() == "field value", "Field value should match");
    TEST_ASSERT(!field.empty(), "Field should not be empty");

    // Set component
    field.set_component(1, "comp1");
    field.set_component(2, "comp2");

    auto* comp = field.component(1);
    TEST_ASSERT(comp != nullptr, "Component 1 should exist");

    // Repetitions
    field.add_repetition("rep1");
    TEST_ASSERT(field.repetition_count() >= 1, "Should have repetitions");

    return true;
}

bool test_hl7_segment() {
    hl7_segment seg("PID");
    TEST_ASSERT(seg.id() == "PID", "Segment ID should be PID");
    TEST_ASSERT(seg.field_count() == 0, "New segment should have no fields");

    // Add fields
    seg.set_field(1, "1");
    seg.set_field(3, "12345");
    seg.set_field(5, "DOE^JOHN");

    TEST_ASSERT(seg.field_count() >= 5, "Should have at least 5 fields");

    auto* f3 = seg.field(3);
    TEST_ASSERT(f3 != nullptr, "Field 3 should exist");
    TEST_ASSERT(f3->value() == "12345", "Field 3 value should match");

    // Get value helper
    TEST_ASSERT(seg.get_value(5) == "DOE^JOHN", "get_value should return field value");

    return true;
}

bool test_hl7_message_creation() {
    hl7_message msg;
    TEST_ASSERT(msg.segment_count() == 0, "New message should have no segments");

    // Add MSH segment
    auto& msh = msg.add_segment("MSH");
    msh.set_field(0, "MSH");
    msh.set_field(1, "|");
    msh.set_field(2, "^~\\&");
    msh.set_field(3, "HIS");
    msh.set_field(4, "HOSPITAL");
    msh.set_field(9, "ADT^A01");

    TEST_ASSERT(msg.segment_count() == 1, "Message should have 1 segment");
    TEST_ASSERT(msg.has_segment("MSH"), "Message should have MSH");

    // Get segment
    auto* msh_ptr = msg.segment("MSH");
    TEST_ASSERT(msh_ptr != nullptr, "MSH segment should exist");

    return true;
}

bool test_hl7_message_path_access() {
    // Parse a sample message first
    hl7_parser parser;
    auto result = parser.parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse successfully");

    auto& msg = *result;

    // Path-based access
    TEST_ASSERT(msg.get_value("MSH.3") == "HIS", "Sending app should be HIS");
    TEST_ASSERT(msg.get_value("MSH.4") == "HOSPITAL", "Sending facility should be HOSPITAL");
    TEST_ASSERT(msg.get_value("PID.3") == "12345^^^HOSPITAL^MR", "Patient ID should match");
    TEST_ASSERT(msg.get_value("PID.5.1") == "DOE", "Family name should be DOE");
    TEST_ASSERT(msg.get_value("PID.5.2") == "JOHN", "Given name should be JOHN");
    TEST_ASSERT(msg.get_value("PID.8") == "M", "Gender should be M");

    // Non-existent path
    TEST_ASSERT(msg.get_value("ZZZ.1").empty(), "Non-existent segment should return empty");
    TEST_ASSERT(msg.get_value("PID.999").empty(), "Non-existent field should return empty");

    return true;
}

bool test_hl7_message_header() {
    hl7_parser parser;
    auto result = parser.parse(SAMPLE_ADT_A01);
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
    hl7_parser parser;
    auto result = parser.parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse successfully");

    // Serialize back
    std::string serialized = result->to_string();
    TEST_ASSERT(!serialized.empty(), "Serialized message should not be empty");
    TEST_ASSERT(serialized.find("MSH|") == 0, "Should start with MSH|");
    TEST_ASSERT(serialized.find("HIS") != std::string::npos, "Should contain HIS");
    TEST_ASSERT(serialized.find("DOE^JOHN") != std::string::npos, "Should contain patient name");

    // Re-parse the serialized message
    auto reparsed = parser.parse(serialized);
    TEST_ASSERT(reparsed.has_value(), "Should re-parse successfully");
    TEST_ASSERT(reparsed->get_value("PID.5.1") == "DOE", "Re-parsed name should match");

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

    // Standard encoding
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

bool test_parser_escape_sequences() {
    hl7_parser parser;

    // Message with escape sequences
    std::string msg_with_escapes =
        "MSH|^~\\&|TEST|FAC|||20240115120000||ADT^A01|MSG|P|2.4\r"
        "PID|1||123||DOE\\T\\SMITH^JOHN||19800101|M\r";

    auto result = parser.parse(msg_with_escapes);
    TEST_ASSERT(result.has_value(), "Should parse message with escapes");

    // The escaped & should be handled
    std::string name = result->get_value("PID.5.1");
    TEST_ASSERT(!name.empty(), "Name should not be empty");

    return true;
}

bool test_parser_options() {
    hl7_parser_options opts;
    opts.strict_mode = true;
    opts.preserve_empty_fields = true;

    hl7_parser parser(opts);

    auto result = parser.parse(SAMPLE_ADT_A01);
    TEST_ASSERT(result.has_value(), "Should parse in strict mode");

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
    auto msg = hl7_builder::create()
                   .msh()
                   .sending_application("TEST_APP")
                   .sending_facility("TEST_FAC")
                   .receiving_application("DEST_APP")
                   .receiving_facility("DEST_FAC")
                   .message_type("ADT", "A01")
                   .message_control_id("MSG12345")
                   .processing_id("P")
                   .version("2.4")
                   .end_msh()
                   .build();

    TEST_ASSERT(msg.has_segment("MSH"), "Built message should have MSH");

    auto header = msg.header();
    TEST_ASSERT(header.sending_application == "TEST_APP", "Sending app should match");
    TEST_ASSERT(header.receiving_application == "DEST_APP", "Receiving app should match");
    TEST_ASSERT(header.type_string == "ADT", "Type should be ADT");
    TEST_ASSERT(header.trigger_event == "A01", "Trigger should be A01");
    TEST_ASSERT(header.message_control_id == "MSG12345", "Control ID should match");

    return true;
}

bool test_builder_add_segment() {
    auto msg = hl7_builder::create()
                   .msh()
                   .sending_application("APP")
                   .sending_facility("FAC")
                   .receiving_application("DEST")
                   .receiving_facility("DFAC")
                   .message_type("ADT", "A01")
                   .message_control_id("MSG1")
                   .processing_id("P")
                   .version("2.4")
                   .end_msh()
                   .segment("PID")
                   .field(1, "1")
                   .field(3, "12345")
                   .field(5, "DOE^JOHN")
                   .field(8, "M")
                   .end_segment()
                   .build();

    TEST_ASSERT(msg.segment_count() == 2, "Should have 2 segments");
    TEST_ASSERT(msg.has_segment("PID"), "Should have PID segment");
    TEST_ASSERT(msg.get_value("PID.5.1") == "DOE", "Patient name should match");
    TEST_ASSERT(msg.get_value("PID.8") == "M", "Gender should match");

    return true;
}

bool test_adt_builder() {
    person_name name;
    name.family_name = "SMITH";
    name.given_name = "JANE";

    auto msg = adt_builder::create()
                   .msh()
                   .sending_application("HIS")
                   .sending_facility("HOSPITAL")
                   .receiving_application("PACS")
                   .receiving_facility("RADIOLOGY")
                   .message_control_id("ADT001")
                   .end_msh()
                   .admission("A01")
                   .pid()
                   .patient_id("67890", "HOSPITAL", "MR")
                   .patient_name(name)
                   .date_of_birth("19900101")
                   .gender("F")
                   .end_pid()
                   .pv1()
                   .patient_class("I")
                   .assigned_location("ICU^101^A")
                   .end_pv1()
                   .build();

    TEST_ASSERT(msg.has_segment("MSH"), "Should have MSH");
    TEST_ASSERT(msg.has_segment("EVN"), "Should have EVN");
    TEST_ASSERT(msg.has_segment("PID"), "Should have PID");
    TEST_ASSERT(msg.has_segment("PV1"), "Should have PV1");

    auto header = msg.header();
    TEST_ASSERT(header.type_string == "ADT", "Type should be ADT");
    TEST_ASSERT(header.trigger_event == "A01", "Trigger should be A01");

    return true;
}

bool test_orm_builder() {
    auto msg = orm_builder::create()
                   .msh()
                   .sending_application("HIS")
                   .sending_facility("HOSPITAL")
                   .receiving_application("PACS")
                   .receiving_facility("RADIOLOGY")
                   .message_control_id("ORM001")
                   .end_msh()
                   .pid()
                   .patient_id("12345", "HOSPITAL", "MR")
                   .patient_name("DOE", "JOHN")
                   .date_of_birth("19800515")
                   .gender("M")
                   .end_pid()
                   .pv1()
                   .patient_class("O")
                   .end_pv1()
                   .orc()
                   .order_control("NW")
                   .placer_order_number("ORD001")
                   .filler_order_number("ACC001")
                   .order_status("SC")
                   .end_orc()
                   .obr()
                   .set_id("1")
                   .placer_order_number("ORD001")
                   .filler_order_number("ACC001")
                   .universal_service_id("71020^CHEST XRAY^CPT")
                   .end_obr()
                   .build();

    TEST_ASSERT(msg.has_segment("MSH"), "Should have MSH");
    TEST_ASSERT(msg.has_segment("PID"), "Should have PID");
    TEST_ASSERT(msg.has_segment("ORC"), "Should have ORC");
    TEST_ASSERT(msg.has_segment("OBR"), "Should have OBR");

    auto header = msg.header();
    TEST_ASSERT(header.type_string == "ORM", "Type should be ORM");
    TEST_ASSERT(header.trigger_event == "O01", "Trigger should be O01");

    return true;
}

bool test_oru_builder() {
    auto msg = oru_builder::create()
                   .msh()
                   .sending_application("PACS")
                   .sending_facility("RADIOLOGY")
                   .receiving_application("HIS")
                   .receiving_facility("HOSPITAL")
                   .message_control_id("ORU001")
                   .end_msh()
                   .pid()
                   .patient_id("12345", "HOSPITAL", "MR")
                   .patient_name("DOE", "JOHN")
                   .end_pid()
                   .orc()
                   .order_control("RE")
                   .order_status("CM")
                   .end_orc()
                   .obr()
                   .set_id("1")
                   .universal_service_id("71020^CHEST XRAY^CPT")
                   .result_status("F")
                   .end_obr()
                   .obx()
                   .set_id("1")
                   .value_type("TX")
                   .observation_id("71020^CHEST XRAY^CPT")
                   .observation_value("IMPRESSION: No acute findings.")
                   .observation_status("F")
                   .end_obx()
                   .build();

    TEST_ASSERT(msg.has_segment("MSH"), "Should have MSH");
    TEST_ASSERT(msg.has_segment("PID"), "Should have PID");
    TEST_ASSERT(msg.has_segment("ORC"), "Should have ORC");
    TEST_ASSERT(msg.has_segment("OBR"), "Should have OBR");
    TEST_ASSERT(msg.has_segment("OBX"), "Should have OBX");

    auto header = msg.header();
    TEST_ASSERT(header.type_string == "ORU", "Type should be ORU");
    TEST_ASSERT(header.trigger_event == "R01", "Trigger should be R01");

    return true;
}

bool test_ack_builder() {
    // First create a message to acknowledge
    auto original = hl7_builder::create()
                        .msh()
                        .sending_application("HIS")
                        .sending_facility("HOSPITAL")
                        .receiving_application("PACS")
                        .receiving_facility("RADIOLOGY")
                        .message_type("ORM", "O01")
                        .message_control_id("ORM001")
                        .processing_id("P")
                        .version("2.4")
                        .end_msh()
                        .build();

    // Build ACK
    auto ack = hl7_builder::create_ack(original, ack_code::AA, "Message accepted");

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

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== HL7 Types Tests ===" << std::endl;
    RUN_TEST(test_encoding_characters_default);
    RUN_TEST(test_encoding_characters_to_string);
    RUN_TEST(test_hl7_error_codes);
    RUN_TEST(test_message_type_enum);
    RUN_TEST(test_ack_code_enum);
    RUN_TEST(test_hl7_timestamp);
    RUN_TEST(test_person_name);
    RUN_TEST(test_hl7_address);
    RUN_TEST(test_patient_id);
    RUN_TEST(test_message_header);

    std::cout << "\n=== HL7 Message Tests ===" << std::endl;
    RUN_TEST(test_hl7_subcomponent);
    RUN_TEST(test_hl7_component);
    RUN_TEST(test_hl7_field);
    RUN_TEST(test_hl7_segment);
    RUN_TEST(test_hl7_message_creation);
    RUN_TEST(test_hl7_message_path_access);
    RUN_TEST(test_hl7_message_header);
    RUN_TEST(test_hl7_message_serialization);

    std::cout << "\n=== HL7 Parser Tests ===" << std::endl;
    RUN_TEST(test_parser_basic);
    RUN_TEST(test_parser_error_handling);
    RUN_TEST(test_parser_encoding_detection);
    RUN_TEST(test_parser_escape_sequences);
    RUN_TEST(test_parser_options);
    RUN_TEST(test_parser_segment_iteration);

    std::cout << "\n=== HL7 Builder Tests ===" << std::endl;
    RUN_TEST(test_builder_basic);
    RUN_TEST(test_builder_add_segment);
    RUN_TEST(test_adt_builder);
    RUN_TEST(test_orm_builder);
    RUN_TEST(test_oru_builder);
    RUN_TEST(test_ack_builder);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    double coverage = (passed * 100.0) / (passed + failed);
    std::cout << "Pass Rate: " << coverage << "%" << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::hl7::test

int main() {
    return pacs::bridge::hl7::test::run_all_tests();
}
