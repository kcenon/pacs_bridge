/**
 * @file hl7_test.cpp
 * @brief Comprehensive unit tests for HL7 v2.x message handling module
 *
 * Tests for HL7 message parsing, building, types, and utilities.
 * Uses Google Test (gtest) framework.
 * Target coverage: >= 85%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/6
 * @see https://github.com/kcenon/pacs_bridge/issues/8
 * @see https://github.com/kcenon/pacs_bridge/issues/21
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include "test_helpers.h"

#include <chrono>
#include <cstring>
#include <string>

namespace pacs::bridge::hl7 {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;

// =============================================================================
// HL7 Types Tests
// =============================================================================

class HL7TypesTest : public pacs_bridge_test {};

TEST_F(HL7TypesTest, EncodingCharactersDefault) {
    hl7_encoding_characters enc;

    EXPECT_EQ(enc.field_separator, '|');
    EXPECT_EQ(enc.component_separator, '^');
    EXPECT_EQ(enc.repetition_separator, '~');
    EXPECT_EQ(enc.escape_character, '\\');
    EXPECT_EQ(enc.subcomponent_separator, '&');
}

TEST_F(HL7TypesTest, EncodingCharactersToMsh2) {
    hl7_encoding_characters enc;
    EXPECT_EQ(enc.to_msh2(), "^~\\&");

    enc.component_separator = '#';
    EXPECT_EQ(enc.to_msh2(), "#~\\&");
}

TEST_F(HL7TypesTest, EncodingCharactersFromMsh2) {
    auto enc = hl7_encoding_characters::from_msh2("^~\\&");
    EXPECT_EQ(enc.component_separator, '^');
    EXPECT_EQ(enc.repetition_separator, '~');
    EXPECT_EQ(enc.escape_character, '\\');
    EXPECT_EQ(enc.subcomponent_separator, '&');

    // Custom encoding
    auto enc2 = hl7_encoding_characters::from_msh2("#*!@");
    EXPECT_EQ(enc2.component_separator, '#');
    EXPECT_EQ(enc2.repetition_separator, '*');
}

TEST_F(HL7TypesTest, EncodingIsDefault) {
    hl7_encoding_characters enc;
    EXPECT_TRUE(enc.is_default());

    enc.component_separator = '#';
    EXPECT_FALSE(enc.is_default());
}

TEST_F(HL7TypesTest, ErrorCodes) {
    EXPECT_EQ(to_error_code(hl7_error::empty_message), -950);
    EXPECT_EQ(to_error_code(hl7_error::missing_msh), -951);
    EXPECT_EQ(to_error_code(hl7_error::invalid_msh), -952);
    EXPECT_EQ(to_error_code(hl7_error::invalid_segment), -953);
    EXPECT_EQ(to_error_code(hl7_error::parse_error), -966);

    EXPECT_STREQ(to_string(hl7_error::invalid_segment), "Invalid segment structure");
    EXPECT_STREQ(to_string(hl7_error::missing_msh), "Missing required MSH segment");
}

TEST_F(HL7TypesTest, MessageTypeEnum) {
    // to_string tests
    EXPECT_STREQ(to_string(message_type::ADT), "ADT");
    EXPECT_STREQ(to_string(message_type::ORM), "ORM");
    EXPECT_STREQ(to_string(message_type::ORU), "ORU");
    EXPECT_STREQ(to_string(message_type::ACK), "ACK");
    EXPECT_STREQ(to_string(message_type::UNKNOWN), "UNKNOWN");

    // parse_message_type tests
    EXPECT_EQ(parse_message_type("ADT"), message_type::ADT);
    EXPECT_EQ(parse_message_type("ORM"), message_type::ORM);
    EXPECT_EQ(parse_message_type("ORU"), message_type::ORU);
    EXPECT_EQ(parse_message_type("ACK"), message_type::ACK);
    EXPECT_EQ(parse_message_type("INVALID"), message_type::UNKNOWN);
    EXPECT_EQ(parse_message_type("SIU"), message_type::SIU);
}

TEST_F(HL7TypesTest, AckCodeEnum) {
    // to_string tests
    EXPECT_STREQ(to_string(ack_code::AA), "AA");
    EXPECT_STREQ(to_string(ack_code::AE), "AE");
    EXPECT_STREQ(to_string(ack_code::AR), "AR");
    EXPECT_STREQ(to_string(ack_code::CA), "CA");
    EXPECT_STREQ(to_string(ack_code::CE), "CE");
    EXPECT_STREQ(to_string(ack_code::CR), "CR");

    // parse_ack_code tests
    EXPECT_EQ(parse_ack_code("AA"), ack_code::AA);
    EXPECT_EQ(parse_ack_code("AE"), ack_code::AE);
    EXPECT_EQ(parse_ack_code("AR"), ack_code::AR);
    EXPECT_EQ(parse_ack_code("CA"), ack_code::CA);
    EXPECT_EQ(parse_ack_code("INVALID"), ack_code::AA);

    // is_ack_success tests
    EXPECT_TRUE(is_ack_success(ack_code::AA));
    EXPECT_TRUE(is_ack_success(ack_code::CA));
    EXPECT_FALSE(is_ack_success(ack_code::AE));
    EXPECT_FALSE(is_ack_success(ack_code::AR));
}

TEST_F(HL7TypesTest, HL7Timestamp) {
    // Parse full timestamp
    auto ts = hl7_timestamp::parse("20240115103045");
    ASSERT_TRUE(ts.has_value());
    EXPECT_EQ(ts->year, 2024);
    EXPECT_EQ(ts->month, 1);
    EXPECT_EQ(ts->day, 15);
    EXPECT_EQ(ts->hour, 10);
    EXPECT_EQ(ts->minute, 30);
    EXPECT_EQ(ts->second, 45);

    // Format timestamp
    std::string formatted = ts->to_string();
    EXPECT_THAT(formatted.substr(0, 8), Eq("20240115"));

    // Now timestamp
    auto now = hl7_timestamp::now();
    EXPECT_GE(now.year, 2024);

    // Date-only parsing
    auto date_only = hl7_timestamp::parse("20240515");
    ASSERT_TRUE(date_only.has_value());
    EXPECT_EQ(date_only->year, 2024);
    EXPECT_EQ(date_only->month, 5);
    EXPECT_EQ(date_only->day, 15);

    // Invalid timestamp
    auto invalid = hl7_timestamp::parse("invalid");
    EXPECT_FALSE(invalid.has_value());
}

TEST_F(HL7TypesTest, HL7PersonName) {
    hl7_person_name name;
    name.family_name = "DOE";
    name.given_name = "JOHN";
    name.middle_name = "WILLIAM";
    name.suffix = "JR";
    name.prefix = "DR";

    EXPECT_FALSE(name.empty());

    std::string display = name.display_name();
    EXPECT_THAT(display, HasSubstr("JOHN"));
    EXPECT_THAT(display, HasSubstr("DOE"));

    std::string formatted = name.formatted_name();
    EXPECT_FALSE(formatted.empty());

    // Empty name
    hl7_person_name empty_name;
    EXPECT_TRUE(empty_name.empty());
}

TEST_F(HL7TypesTest, HL7Address) {
    hl7_address addr;
    addr.street1 = "123 MAIN ST";
    addr.city = "SPRINGFIELD";
    addr.state = "IL";
    addr.postal_code = "62701";
    addr.country = "USA";

    EXPECT_FALSE(addr.empty());

    std::string formatted = addr.formatted();
    EXPECT_THAT(formatted, HasSubstr("123 MAIN ST"));
    EXPECT_THAT(formatted, HasSubstr("SPRINGFIELD"));

    // Empty address
    hl7_address empty_addr;
    EXPECT_TRUE(empty_addr.empty());
}

TEST_F(HL7TypesTest, HL7PatientId) {
    hl7_patient_id pid;
    pid.id = "12345";
    pid.assigning_authority = "HOSPITAL";
    pid.id_type = "MR";

    EXPECT_FALSE(pid.empty());

    // Equality
    hl7_patient_id pid2;
    pid2.id = "12345";
    pid2.assigning_authority = "HOSPITAL";
    pid2.id_type = "MRN";
    EXPECT_EQ(pid, pid2);

    // Empty patient id
    hl7_patient_id empty_pid;
    EXPECT_TRUE(empty_pid.empty());
}

TEST_F(HL7TypesTest, HL7MessageHeader) {
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

    EXPECT_EQ(header.sending_application, "HIS");
    EXPECT_EQ(header.type, message_type::ADT);
    EXPECT_EQ(header.trigger_event, "A01");
    EXPECT_FALSE(header.is_ack());
    EXPECT_EQ(header.full_message_type(), "ADT^A01");
}

TEST_F(HL7TypesTest, ValidationResult) {
    validation_result result;
    EXPECT_TRUE(result.valid);
    EXPECT_FALSE(result.has_errors());
    EXPECT_EQ(result.error_count(), 0u);

    // Add error
    result.add_error(hl7_error::missing_required_field, "MSH.9", "Message type is required");
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(result.has_errors());
    EXPECT_EQ(result.error_count(), 1u);

    // Add warning
    result.add_warning(hl7_error::validation_failed, "PID.5", "Patient name is empty");
    EXPECT_EQ(result.warning_count(), 1u);
    EXPECT_EQ(result.error_count(), 1u);
}

// =============================================================================
// HL7 Message Tests
// =============================================================================

class HL7MessageTest : public pacs_bridge_test {};

TEST_F(HL7MessageTest, Subcomponent) {
    hl7_subcomponent sub("test value");
    EXPECT_EQ(sub.value(), "test value");
    EXPECT_FALSE(sub.empty());

    hl7_subcomponent empty_sub;
    EXPECT_TRUE(empty_sub.empty());

    sub.set_value("new value");
    EXPECT_EQ(sub.value(), "new value");

    // Comparison
    hl7_subcomponent sub2("new value");
    EXPECT_EQ(sub, sub2);
    EXPECT_EQ(sub, "new value");
}

TEST_F(HL7MessageTest, Component) {
    hl7_component comp("component value");
    EXPECT_EQ(comp.value(), "component value");
    EXPECT_GE(comp.subcomponent_count(), 1u);
    EXPECT_FALSE(comp.empty());

    // Access subcomponent (1-based indexing)
    const auto& sub = comp.subcomponent(1);
    EXPECT_EQ(sub.value(), "component value");

    // Mutable subcomponent access
    comp.subcomponent(2).set_value("sub2");
    EXPECT_GE(comp.subcomponent_count(), 2u);

    // Empty component
    hl7_component empty_comp;
    EXPECT_TRUE(empty_comp.empty());
}

TEST_F(HL7MessageTest, Field) {
    hl7_field field("field value");
    EXPECT_EQ(field.value(), "field value");
    EXPECT_FALSE(field.empty());

    // Access component (1-based indexing)
    const auto& comp = field.component(1);
    EXPECT_EQ(comp.value(), "field value");

    // Mutable component access
    field.component(2).set_value("comp2");
    EXPECT_GE(field.component_count(), 2u);

    // Repetitions
    EXPECT_GE(field.repetition_count(), 1u);
    field.add_repetition();
    EXPECT_GE(field.repetition_count(), 2u);

    // Empty field
    hl7_field empty_field;
    EXPECT_TRUE(empty_field.empty());
}

TEST_F(HL7MessageTest, Segment) {
    hl7_segment seg("PID");
    EXPECT_EQ(seg.segment_id(), "PID");
    EXPECT_EQ(seg.field_count(), 0u);
    EXPECT_FALSE(seg.is_msh());

    // Add fields (1-based indexing)
    seg.set_field(1, "1");
    seg.set_field(3, "12345");
    seg.set_field(5, "DOE^JOHN");

    EXPECT_GE(seg.field_count(), 5u);

    // Get field
    const auto& f3 = seg.field(3);
    EXPECT_EQ(f3.value(), "12345");

    // Get field value helper
    EXPECT_EQ(seg.field_value(3), "12345");
    EXPECT_EQ(seg.field_value(5), "DOE^JOHN");

    // Path-based access
    EXPECT_EQ(seg.get_value("3"), "12345");

    // MSH segment
    hl7_segment msh("MSH");
    EXPECT_TRUE(msh.is_msh());
}

TEST_F(HL7MessageTest, MessageCreation) {
    hl7_message msg;
    EXPECT_TRUE(msg.empty());
    EXPECT_EQ(msg.segment_count(), 0u);

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

    EXPECT_EQ(msg.segment_count(), 1u);
    EXPECT_TRUE(msg.has_segment("MSH"));
    EXPECT_FALSE(msg.empty());

    // Get segment
    auto* msh_ptr = msg.segment("MSH");
    EXPECT_NE(msh_ptr, nullptr);
}

TEST_F(HL7MessageTest, MessageParsing) {
    // Parse using hl7_message::parse directly
    auto result = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(result.has_value());

    auto& msg = *result;

    // Verify segment count
    EXPECT_EQ(msg.segment_count(), 4u);
    EXPECT_TRUE(msg.has_segment("MSH"));
    EXPECT_TRUE(msg.has_segment("EVN"));
    EXPECT_TRUE(msg.has_segment("PID"));
    EXPECT_TRUE(msg.has_segment("PV1"));

    // Path-based access
    EXPECT_EQ(msg.get_value("MSH.3"), "HIS");
    EXPECT_EQ(msg.get_value("MSH.4"), "HOSPITAL");
    EXPECT_EQ(msg.get_value("PID.5.1"), "DOE");
    EXPECT_EQ(msg.get_value("PID.5.2"), "JOHN");
    EXPECT_EQ(msg.get_value("PID.8"), "M");

    // Non-existent path
    EXPECT_TRUE(msg.get_value("ZZZ.1").empty());
    EXPECT_TRUE(msg.get_value("PID.999").empty());
}

TEST_F(HL7MessageTest, MessageParsedHeader) {
    auto result = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(result.has_value());

    auto header = result->header();
    EXPECT_EQ(header.sending_application, "HIS");
    EXPECT_EQ(header.sending_facility, "HOSPITAL");
    EXPECT_EQ(header.receiving_application, "PACS");
    EXPECT_EQ(header.receiving_facility, "RADIOLOGY");
    EXPECT_EQ(header.type_string, "ADT");
    EXPECT_EQ(header.trigger_event, "A01");
    EXPECT_EQ(header.message_control_id, "MSG001");
    EXPECT_EQ(header.processing_id, "P");
    EXPECT_EQ(header.version_id, "2.4");
}

TEST_F(HL7MessageTest, MessageSerialization) {
    auto result = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(result.has_value());

    // Serialize back
    std::string serialized = result->serialize();
    EXPECT_FALSE(serialized.empty());
    EXPECT_THAT(serialized, StartsWith("MSH|"));
    EXPECT_THAT(serialized, HasSubstr("HIS"));
    EXPECT_THAT(serialized, HasSubstr("DOE^JOHN"));

    // Re-parse the serialized message
    auto reparsed = hl7_message::parse(serialized);
    ASSERT_TRUE(reparsed.has_value());
    EXPECT_EQ(reparsed->get_value("PID.5.1"), "DOE");
}

TEST_F(HL7MessageTest, MessageModification) {
    auto result = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(result.has_value());

    auto& msg = *result;

    // Modify via set_value
    msg.set_value("PID.5.1", "SMITH");
    EXPECT_EQ(msg.get_value("PID.5.1"), "SMITH");

    // Add new segment
    auto& obx = msg.add_segment("OBX");
    obx.set_field(1, "1");
    obx.set_field(2, "TX");
    EXPECT_TRUE(msg.has_segment("OBX"));
    EXPECT_EQ(msg.segment_count(), 5u);
}

TEST_F(HL7MessageTest, MessageValidation) {
    auto result = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(result.has_value());

    auto validation = result->validate();
    EXPECT_TRUE(validation.valid);
    EXPECT_FALSE(validation.has_errors());

    // Test is_valid helper
    EXPECT_TRUE(result->is_valid());
}

TEST_F(HL7MessageTest, MessageAckCreation) {
    auto result = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(result.has_value());

    // Create ACK
    auto ack = result->create_ack(ack_code::AA, "Message accepted");
    EXPECT_TRUE(ack.has_segment("MSH"));
    EXPECT_TRUE(ack.has_segment("MSA"));

    auto header = ack.header();
    EXPECT_EQ(header.type_string, "ACK");

    // Sender/receiver should be swapped
    EXPECT_EQ(header.sending_application, "PACS");
    EXPECT_EQ(header.receiving_application, "HIS");

    // Check MSA segment
    EXPECT_EQ(ack.get_value("MSA.1"), "AA");
    EXPECT_EQ(ack.get_value("MSA.2"), "MSG001");
}

// =============================================================================
// HL7 Parser Tests
// =============================================================================

class HL7ParserTest : public pacs_bridge_test {};

TEST_F(HL7ParserTest, BasicParsing) {
    hl7_parser parser;

    auto result = parser.parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->segment_count(), 4u);

    auto result2 = parser.parse(hl7_samples::ORM_O01);
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2->segment_count(), 5u);
}

TEST_F(HL7ParserTest, ErrorHandling) {
    hl7_parser parser;

    // Empty message
    auto result = parser.parse("");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), hl7_error::empty_message);

    // Missing MSH
    auto result2 = parser.parse("PID|1||12345\r");
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), hl7_error::missing_msh);

    // Invalid MSH
    auto result3 = parser.parse("MSH\r");
    EXPECT_FALSE(result3.has_value());
}

TEST_F(HL7ParserTest, EncodingDetection) {
    hl7_parser parser;

    auto result = parser.parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(result.has_value());

    auto enc = result->encoding();
    EXPECT_EQ(enc.field_separator, '|');
    EXPECT_EQ(enc.component_separator, '^');
    EXPECT_EQ(enc.repetition_separator, '~');
    EXPECT_EQ(enc.escape_character, '\\');
    EXPECT_EQ(enc.subcomponent_separator, '&');
}

TEST_F(HL7ParserTest, WithOptions) {
    parser_options opts;
    opts.lenient_mode = false;
    opts.validate_structure = true;

    hl7_parser parser(opts);

    auto result = parser.parse(hl7_samples::ADT_A01);
    EXPECT_TRUE(result.has_value());
}

TEST_F(HL7ParserTest, WithDetails) {
    hl7_parser parser;
    parse_details details;

    auto result = parser.parse(hl7_samples::ADT_A01, &details);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(details.segment_count, 4u);
    EXPECT_GT(details.original_size, 0u);
}

TEST_F(HL7ParserTest, ExtractEncoding) {
    auto enc_result = hl7_parser::extract_encoding(hl7_samples::ADT_A01);
    ASSERT_TRUE(enc_result.has_value());
    EXPECT_EQ(enc_result->field_separator, '|');
    EXPECT_EQ(enc_result->component_separator, '^');
}

TEST_F(HL7ParserTest, ExtractHeader) {
    auto header_result = hl7_parser::extract_header(hl7_samples::ADT_A01);
    ASSERT_TRUE(header_result.has_value());
    EXPECT_EQ(header_result->sending_application, "HIS");
    EXPECT_EQ(header_result->type_string, "ADT");
}

TEST_F(HL7ParserTest, LooksLikeHL7) {
    EXPECT_TRUE(hl7_parser::looks_like_hl7(hl7_samples::ADT_A01));
    EXPECT_TRUE(hl7_parser::looks_like_hl7(hl7_samples::ORM_O01));
    EXPECT_FALSE(hl7_parser::looks_like_hl7("Hello World"));
    EXPECT_FALSE(hl7_parser::looks_like_hl7(""));
}

TEST_F(HL7ParserTest, SegmentIteration) {
    hl7_parser parser;
    auto result = parser.parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(result.has_value());

    // Get all segments of a type
    auto pid_segments = result->segments("PID");
    EXPECT_EQ(pid_segments.size(), 1u);

    auto pv1_segments = result->segments("PV1");
    EXPECT_EQ(pv1_segments.size(), 1u);

    auto zxx_segments = result->segments("ZXX");
    EXPECT_TRUE(zxx_segments.empty());
}

TEST_F(HL7ParserTest, EscapeSequences) {
    hl7_encoding_characters enc;  // Default encoding

    // Test unescape: \F\ -> |
    std::string escaped_f = "test\\F\\value";
    std::string unescaped_f = hl7_parser::unescape(escaped_f, enc);
    EXPECT_EQ(unescaped_f, "test|value");

    // Test unescape: \S\ -> ^
    std::string escaped_s = "test\\S\\value";
    std::string unescaped_s = hl7_parser::unescape(escaped_s, enc);
    EXPECT_EQ(unescaped_s, "test^value");

    // Test unescape: \T\ -> &
    std::string escaped_t = "test\\T\\value";
    std::string unescaped_t = hl7_parser::unescape(escaped_t, enc);
    EXPECT_EQ(unescaped_t, "test&value");

    // Test unescape: \R\ -> ~
    std::string escaped_r = "test\\R\\value";
    std::string unescaped_r = hl7_parser::unescape(escaped_r, enc);
    EXPECT_EQ(unescaped_r, "test~value");

    // Test unescape: \E\ -> backslash
    std::string escaped_e = "test\\E\\value";
    std::string unescaped_e = hl7_parser::unescape(escaped_e, enc);
    EXPECT_EQ(unescaped_e, "test\\value");

    // Test unescape: \.br\ -> newline
    std::string escaped_br = "test\\.br\\value";
    std::string unescaped_br = hl7_parser::unescape(escaped_br, enc);
    EXPECT_EQ(unescaped_br, "test\nvalue");

    // Test escape: pipe character to escaped form
    std::string raw_pipe = "test|value";
    std::string esc_pipe = hl7_parser::escape(raw_pipe, enc);
    EXPECT_EQ(esc_pipe, "test\\F\\value");

    // Test escape: caret to escaped form
    std::string raw_caret = "test^value";
    std::string esc_caret = hl7_parser::escape(raw_caret, enc);
    EXPECT_EQ(esc_caret, "test\\S\\value");

    // Test escape roundtrip
    std::string original = "test|with^special&chars~and\\backslash";
    std::string escaped = hl7_parser::escape(original, enc);
    std::string roundtrip = hl7_parser::unescape(escaped, enc);
    EXPECT_EQ(roundtrip, original);
}

TEST_F(HL7ParserTest, ZSegment) {
    hl7_parser parser;
    auto result = parser.parse(hl7_samples::MSG_WITH_ZDS);
    ASSERT_TRUE(result.has_value());

    // Check ZDS segment exists
    EXPECT_TRUE(result->has_segment("ZDS"));

    // Get ZDS segment
    const auto* zds = result->segment("ZDS");
    ASSERT_NE(zds, nullptr);

    // Check ZDS field values
    EXPECT_EQ(zds->field_value(1), "1.2.840.10008.5.1.4.1.1.2.1.12345");
    EXPECT_EQ(zds->field_value(3), "Custom Z-segment data");
}

TEST_F(HL7ParserTest, NonStandardDelimiters) {
    hl7_parser parser;
    auto result = parser.parse(hl7_samples::CUSTOM_DELIM_MSG);
    ASSERT_TRUE(result.has_value());

    auto enc = result->encoding();
    EXPECT_EQ(enc.field_separator, '#');
    EXPECT_EQ(enc.component_separator, '*');

    // Verify field extraction works with custom delimiters
    EXPECT_EQ(result->get_value("MSH.3"), "SENDER");
}

TEST_F(HL7ParserTest, Performance) {
    hl7_parser parser;
    parse_details details;

    // Parse message and capture timing
    auto result = parser.parse(hl7_samples::ADT_A01, &details);
    ASSERT_TRUE(result.has_value());

    // Verify parse time < 10ms (with margin for first parse)
    EXPECT_LT(details.parse_time_us, 10000);

    // Run multiple parses to get better average
    const int iterations = 100;
    int64_t avg_time = benchmark(iterations, [&parser]() {
        auto r = parser.parse(hl7_samples::ADT_A01);
        EXPECT_TRUE(r.has_value());
    });

    // Average should be < 1ms (1000 microseconds)
    EXPECT_LT(avg_time, 1000);
}

// =============================================================================
// HL7 Builder Tests
// =============================================================================

class HL7BuilderTest : public pacs_bridge_test {};

TEST_F(HL7BuilderTest, BasicBuilder) {
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

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->has_segment("MSH"));

    auto header = result->header();
    EXPECT_EQ(header.sending_application, "TEST_APP");
    EXPECT_EQ(header.receiving_application, "DEST_APP");
    EXPECT_EQ(header.type_string, "ADT");
    EXPECT_EQ(header.trigger_event, "A01");
    EXPECT_EQ(header.message_control_id, "MSG12345");
}

TEST_F(HL7BuilderTest, BuilderWithPatient) {
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

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->has_segment("MSH"));
    EXPECT_TRUE(result->has_segment("PID"));
}

TEST_F(HL7BuilderTest, CreateAck) {
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

    ASSERT_TRUE(original_result.has_value());

    // Build ACK
    auto ack = hl7_builder::create_ack(*original_result, ack_code::AA, "Message accepted");

    EXPECT_TRUE(ack.has_segment("MSH"));
    EXPECT_TRUE(ack.has_segment("MSA"));

    auto header = ack.header();
    EXPECT_EQ(header.type_string, "ACK");
    EXPECT_EQ(header.sending_application, "PACS");
    EXPECT_EQ(header.receiving_application, "HIS");

    EXPECT_EQ(ack.get_value("MSA.1"), "AA");
    EXPECT_EQ(ack.get_value("MSA.2"), "ORM001");
}

TEST_F(HL7BuilderTest, SetField) {
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

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get_value("PID.3"), "67890");
    EXPECT_EQ(result->get_value("PID.5.1"), "SMITH");
}

TEST_F(HL7BuilderTest, ADTBuilder) {
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

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->has_segment("MSH"));

    auto header = result->header();
    EXPECT_EQ(header.type_string, "ADT");
    EXPECT_EQ(header.trigger_event, "A01");
}

TEST_F(HL7BuilderTest, ORMBuilder) {
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

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->has_segment("MSH"));

    auto header = result->header();
    EXPECT_EQ(header.type_string, "ORM");
    EXPECT_EQ(header.trigger_event, "O01");
}

TEST_F(HL7BuilderTest, ORUBuilder) {
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

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->has_segment("MSH"));

    auto header = result->header();
    EXPECT_EQ(header.type_string, "ORU");
    EXPECT_EQ(header.trigger_event, "R01");
}

TEST_F(HL7BuilderTest, MessageIdGenerator) {
    auto id1 = message_id_generator::generate();
    EXPECT_FALSE(id1.empty());

    auto id2 = message_id_generator::generate();
    EXPECT_FALSE(id2.empty());

    auto uuid = message_id_generator::generate_uuid();
    EXPECT_FALSE(uuid.empty());
    EXPECT_GE(uuid.length(), 32u);

    auto prefixed = message_id_generator::generate_with_prefix("TEST");
    EXPECT_THAT(prefixed, StartsWith("TEST"));
}

}  // namespace
}  // namespace pacs::bridge::hl7
