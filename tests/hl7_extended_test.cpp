/**
 * @file hl7_extended_test.cpp
 * @brief Extended unit tests for HL7 v2.x message handling
 *
 * Additional tests for HL7 message parsing, encoding conversion,
 * invalid format handling, and ACK/error response generation.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/159
 * @see https://github.com/kcenon/pacs_bridge/issues/145
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"
#include "pacs/bridge/protocol/hl7/hl7_validator.h"

#include "test_helpers.h"

#include <string>
#include <string_view>

namespace pacs::bridge::hl7 {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;

// =============================================================================
// Extended Sample Messages
// =============================================================================

namespace extended_samples {

/**
 * @brief Sample SIU^S12 (New Appointment) message
 */
constexpr std::string_view SIU_S12 =
    "MSH|^~\\&|RIS|RADIOLOGY|PACS|IMAGING|20240115103000||SIU^S12|MSG010|P|2.5.1\r"
    "SCH|APPT001^RIS|APPT001^PACS||||||^^^20240120100000^^20240120|30|min^minutes|Booked\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^A||19800315|M\r"
    "RGS|1||RESOURCE_GROUP_1\r"
    "AIS|1||CT_SCAN^CT Scan^LOCAL|20240120100000|30|min\r";

/**
 * @brief Sample SIU^S15 (Cancellation) message
 */
constexpr std::string_view SIU_S15 =
    "MSH|^~\\&|RIS|RADIOLOGY|PACS|IMAGING|20240117100000||SIU^S15|MSG011|P|2.5.1\r"
    "SCH|APPT001^RIS|APPT001^PACS||||||||||^^^20240120100000^^20240120||||||||||||||||Cancelled\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^A||19800315|M\r";

/**
 * @brief Sample MDM^T02 (Original Document Notification) message
 */
constexpr std::string_view MDM_T02 =
    "MSH|^~\\&|TRANSCRIPTION|HOSPITAL|EMR|HOSPITAL|20240115140000||MDM^T02|MSG012|P|2.5.1\r"
    "EVN|T02|20240115140000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "TXA|1|HP^History and Physical|TX|20240115140000|||||||DOC12345|||||AU|||||SMITH^ROBERT^MD\r"
    "OBX|1|TX|REPORT^Report Text||History and physical examination completed.||||||F\r";

/**
 * @brief Sample QRY^A19 (Patient Query) message
 */
constexpr std::string_view QRY_A19 =
    "MSH|^~\\&|HIS|HOSPITAL|ADT|HOSPITAL|20240115150000||QRY^A19|MSG013|P|2.4\r"
    "QRD|20240115150000|R|I|QUERY001|||25^RD|12345^DOE^JOHN|DEM\r"
    "QRF|ADT||||PID|PV1\r";

/**
 * @brief Sample BAR^P01 (Add Patient Account) message
 */
constexpr std::string_view BAR_P01 =
    "MSH|^~\\&|BILLING|HOSPITAL|HIS|HOSPITAL|20240115160000||BAR^P01|MSG014|P|2.4\r"
    "EVN|P01|20240115160000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "PV1|1|I|WARD^101^A||||SMITH^ROBERT^MD\r"
    "DG1|1||J18.9^Pneumonia, unspecified organism^ICD10\r";

/**
 * @brief Sample RDE^O11 (Pharmacy Order) message
 */
constexpr std::string_view RDE_O11 =
    "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115170000||RDE^O11|MSG015|P|2.5.1\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "ORC|NW|ORD001^HIS|RX001^PHARMACY||E\r"
    "RXE|1^^^20240115170000^^E|00069015001^AMOXICILLIN 500MG^NDC|500|MG|CAP^Capsule\r";

/**
 * @brief Sample message with Korean characters (UTF-8)
 */
constexpr std::string_view MSG_WITH_KOREAN =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG016|P|2.4|||AL|NE||UNICODE UTF-8\r"
    "EVN|A01|20240115103000\r"
    "PID|1||12345^^^HOSPITAL^MR||\xEA\xB9\x80^\xEC\xB2\xA0\xEC\x88\x98||19800515|M|||123 MAIN ST^^SEOUL^KR\r"
    "PV1|1|I|WARD^101^A\r";

/**
 * @brief Sample message with special characters requiring escaping
 */
constexpr std::string_view MSG_WITH_SPECIAL_CHARS =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG017|P|2.4\r"
    "EVN|A01|20240115103000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M|||123 MAIN ST\\F\\APT 5^^CITY^ST^12345\r"
    "NTE|1||Patient notes: BP 120\\S\\80, temp 98.6\\T\\normal range\r";

/**
 * @brief Sample ACK with error (AE)
 */
constexpr std::string_view ACK_AE =
    "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115103001||ACK|ACK002|P|2.4\r"
    "MSA|AE|MSG001|Application error occurred\r"
    "ERR|^^^207&Application internal error&HL70357\r";

/**
 * @brief Sample ACK with rejection (AR)
 */
constexpr std::string_view ACK_AR =
    "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115103001||ACK|ACK003|P|2.4\r"
    "MSA|AR|MSG001|Message rejected - invalid format\r"
    "ERR|MSH^1^9^1|101^Required field missing^HL70357\r";

/**
 * @brief Malformed message - truncated MSH
 */
constexpr std::string_view MALFORMED_TRUNCATED_MSH =
    "MSH|^~\\";

/**
 * @brief Malformed message - invalid segment ID
 */
constexpr std::string_view MALFORMED_INVALID_SEGMENT =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG018|P|2.4\r"
    "X|invalid segment\r";

/**
 * @brief Message with empty segments
 */
constexpr std::string_view MSG_WITH_EMPTY_SEGMENTS =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG019|P|2.4\r"
    "EVN||\r"
    "PID|||\r";

/**
 * @brief Message with very long field
 */
inline std::string create_msg_with_long_field() {
    std::string long_value(10000, 'X');
    return "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG020|P|2.4\r"
           "EVN|A01|20240115103000\r"
           "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M|||" + long_value + "\r";
}

/**
 * @brief Message with repetitions
 */
constexpr std::string_view MSG_WITH_REPETITIONS =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG021|P|2.4\r"
    "EVN|A01|20240115103000\r"
    "PID|1||12345^^^HOSPITAL^MR~67890^^^CLINIC^MR||DOE^JOHN~SMITH^JOHN||19800515|M\r"
    "PV1|1|I|WARD^101^A||||SMITH^ROBERT^MD~JONES^MARY^MD\r";

}  // namespace extended_samples

// =============================================================================
// Extended Message Type Parsing Tests
// =============================================================================

class HL7ExtendedParsingTest : public pacs_bridge_test {};

TEST_F(HL7ExtendedParsingTest, ParseSIU_S12Message) {
    auto result = hl7_message::parse(extended_samples::SIU_S12);
    ASSERT_TRUE(result.has_value()) << "SIU^S12 message should parse successfully";

    auto header = result->header();
    EXPECT_EQ(header.type_string, "SIU");
    EXPECT_EQ(header.trigger_event, "S12");
    EXPECT_EQ(header.version_id, "2.5.1");

    EXPECT_TRUE(result->has_segment("MSH"));
    EXPECT_TRUE(result->has_segment("SCH"));
    EXPECT_TRUE(result->has_segment("PID"));
    EXPECT_TRUE(result->has_segment("RGS"));
    EXPECT_TRUE(result->has_segment("AIS"));

    // Verify SCH fields
    EXPECT_EQ(result->get_value("SCH.1.1"), "APPT001");
}

TEST_F(HL7ExtendedParsingTest, ParseSIU_S15Message) {
    auto result = hl7_message::parse(extended_samples::SIU_S15);
    ASSERT_TRUE(result.has_value()) << "SIU^S15 message should parse successfully";

    auto header = result->header();
    EXPECT_EQ(header.type_string, "SIU");
    EXPECT_EQ(header.trigger_event, "S15");
}

TEST_F(HL7ExtendedParsingTest, ParseMDM_T02Message) {
    auto result = hl7_message::parse(extended_samples::MDM_T02);
    ASSERT_TRUE(result.has_value()) << "MDM^T02 message should parse successfully";

    auto header = result->header();
    EXPECT_EQ(header.type_string, "MDM");
    EXPECT_EQ(header.trigger_event, "T02");

    EXPECT_TRUE(result->has_segment("TXA"));
    EXPECT_TRUE(result->has_segment("OBX"));
}

TEST_F(HL7ExtendedParsingTest, ParseQRY_A19Message) {
    auto result = hl7_message::parse(extended_samples::QRY_A19);
    ASSERT_TRUE(result.has_value()) << "QRY^A19 message should parse successfully";

    auto header = result->header();
    EXPECT_EQ(header.type_string, "QRY");
    EXPECT_EQ(header.trigger_event, "A19");

    EXPECT_TRUE(result->has_segment("QRD"));
    EXPECT_TRUE(result->has_segment("QRF"));
}

TEST_F(HL7ExtendedParsingTest, ParseBAR_P01Message) {
    auto result = hl7_message::parse(extended_samples::BAR_P01);
    ASSERT_TRUE(result.has_value()) << "BAR^P01 message should parse successfully";

    auto header = result->header();
    EXPECT_EQ(header.type_string, "BAR");
    EXPECT_EQ(header.trigger_event, "P01");

    EXPECT_TRUE(result->has_segment("DG1"));
}

TEST_F(HL7ExtendedParsingTest, ParseRDE_O11Message) {
    auto result = hl7_message::parse(extended_samples::RDE_O11);
    ASSERT_TRUE(result.has_value()) << "RDE^O11 message should parse successfully";

    auto header = result->header();
    EXPECT_EQ(header.type_string, "RDE");
    EXPECT_EQ(header.trigger_event, "O11");

    EXPECT_TRUE(result->has_segment("RXE"));
}

// =============================================================================
// Encoding Conversion Tests
// =============================================================================

class HL7EncodingTest : public pacs_bridge_test {};

TEST_F(HL7EncodingTest, ParseMessageWithKoreanCharacters) {
    auto result = hl7_message::parse(extended_samples::MSG_WITH_KOREAN);
    ASSERT_TRUE(result.has_value()) << "Message with Korean characters should parse";

    // Verify patient name contains Korean characters
    auto patient_name = result->get_value("PID.5");
    EXPECT_FALSE(patient_name.empty());

    // UTF-8 Korean characters should be preserved
    EXPECT_THAT(patient_name, HasSubstr("\xEA\xB9\x80"));  // Korean family name
}

TEST_F(HL7EncodingTest, ParseMessageWithEscapedSpecialCharacters) {
    auto result = hl7_message::parse(extended_samples::MSG_WITH_SPECIAL_CHARS);
    ASSERT_TRUE(result.has_value()) << "Message with escaped special chars should parse";

    // Verify NTE segment exists
    EXPECT_TRUE(result->has_segment("NTE"));
}

TEST_F(HL7EncodingTest, EscapeSequenceRoundtrip) {
    hl7_encoding_characters enc;

    // Test various escape sequences
    std::vector<std::pair<std::string, std::string>> test_cases = {
        {"test|value", "test\\F\\value"},         // Field separator
        {"test^value", "test\\S\\value"},         // Component separator
        {"test~value", "test\\R\\value"},         // Repetition separator
        {"test&value", "test\\T\\value"},         // Subcomponent separator
        {"test\\value", "test\\E\\value"},        // Escape character
        {"line1\rline2", "line1\\.br\\line2"},    // Carriage return
    };

    for (const auto& [original, expected_escaped] : test_cases) {
        std::string escaped = hl7_parser::escape(original, enc);
        std::string unescaped = hl7_parser::unescape(escaped, enc);
        EXPECT_EQ(unescaped, original) << "Roundtrip failed for: " << original;
    }
}

TEST_F(HL7EncodingTest, HighByteCharacterPreservation) {
    // Create message with high-byte characters (Latin-1 range)
    // PID fields: 1=SetID, 2=ExtPatientID, 3=PatientIDList, 4=AltPatientID, 5=PatientName
    std::string msg = "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG|P|2.4\r"
                      "EVN|A01|20240115103000\r"
                      "PID|1||12345||M\xC3\xBCller^Hans||19800515|M\r";  // Müller in UTF-8

    auto result = hl7_message::parse(msg);
    ASSERT_TRUE(result.has_value());

    auto patient_name = result->get_value("PID.5");
    EXPECT_THAT(std::string(patient_name), HasSubstr("M\xC3\xBC"));  // UTF-8 ü
}

// =============================================================================
// Invalid Format Handling Tests
// =============================================================================

class HL7InvalidFormatTest : public pacs_bridge_test {};

TEST_F(HL7InvalidFormatTest, EmptyMessage) {
    auto result = hl7_message::parse("");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), hl7_error::empty_message);
}

TEST_F(HL7InvalidFormatTest, WhitespaceOnlyMessage) {
    auto result = hl7_message::parse("   \t\n  ");
    EXPECT_FALSE(result.has_value());
}

TEST_F(HL7InvalidFormatTest, TruncatedMSHSegment) {
    auto result = hl7_message::parse(extended_samples::MALFORMED_TRUNCATED_MSH);
    EXPECT_FALSE(result.has_value());
}

TEST_F(HL7InvalidFormatTest, MissingMSHSegment) {
    auto result = hl7_message::parse("PID|1||12345\r");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), hl7_error::missing_msh);
}

TEST_F(HL7InvalidFormatTest, InvalidSegmentIdLength) {
    auto result = hl7_message::parse(extended_samples::MALFORMED_INVALID_SEGMENT);
    // Parser might accept or reject single-char segment IDs depending on strictness
    if (result.has_value()) {
        // If parsed, verify MSH is present
        EXPECT_TRUE(result->has_segment("MSH"));
    }
}

TEST_F(HL7InvalidFormatTest, MessageWithEmptySegments) {
    auto result = hl7_message::parse(extended_samples::MSG_WITH_EMPTY_SEGMENTS);
    ASSERT_TRUE(result.has_value()) << "Message with empty segments should still parse";

    EXPECT_TRUE(result->has_segment("EVN"));
    EXPECT_TRUE(result->has_segment("PID"));
}

TEST_F(HL7InvalidFormatTest, MessageWithVeryLongField) {
    std::string msg = extended_samples::create_msg_with_long_field();
    auto result = hl7_message::parse(msg);

    // Should either parse or fail gracefully
    if (result.has_value()) {
        EXPECT_TRUE(result->has_segment("PID"));
    }
}

TEST_F(HL7InvalidFormatTest, MessageWithOnlyMSH) {
    auto result = hl7_message::parse(hl7_samples::MINIMAL_MSG);
    EXPECT_TRUE(result.has_value());
}

TEST_F(HL7InvalidFormatTest, NullBytesInMessage) {
    std::string msg_with_null = std::string(hl7_samples::ADT_A01);
    msg_with_null[50] = '\0';  // Insert null byte

    auto result = hl7_message::parse(msg_with_null);
    // Should handle gracefully - either parse up to null or reject
}

TEST_F(HL7InvalidFormatTest, InvalidVersionId) {
    const char* msg_invalid_version =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|9.9\r"
        "PID|1||12345\r";

    auto result = hl7_message::parse(msg_invalid_version);
    // Should parse but validation might flag the version
    if (result.has_value()) {
        EXPECT_EQ(result->header().version_id, "9.9");
    }
}

// =============================================================================
// Message Repetition Tests
// =============================================================================

class HL7RepetitionTest : public pacs_bridge_test {};

TEST_F(HL7RepetitionTest, ParseMessageWithRepetitions) {
    auto result = hl7_message::parse(extended_samples::MSG_WITH_REPETITIONS);
    ASSERT_TRUE(result.has_value());

    // PID-3 should have repetitions (two MRNs)
    auto* pid = result->segment("PID");
    ASSERT_NE(pid, nullptr);

    const auto& pid3 = pid->field(3);
    EXPECT_GE(pid3.repetition_count(), 2u);
}

TEST_F(HL7RepetitionTest, AccessRepetitionValues) {
    auto result = hl7_message::parse(extended_samples::MSG_WITH_REPETITIONS);
    ASSERT_TRUE(result.has_value());

    auto* pid = result->segment("PID");
    ASSERT_NE(pid, nullptr);

    // Access first repetition
    const auto& pid3 = pid->field(3);
    EXPECT_GE(pid3.repetition_count(), 1u);
}

// =============================================================================
// ACK and Error Response Tests
// =============================================================================

class HL7AckResponseTest : public pacs_bridge_test {};

TEST_F(HL7AckResponseTest, CreateAckForADTMessage) {
    auto adt = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(adt.has_value());

    auto ack = adt->create_ack(ack_code::AA, "Message accepted successfully");

    EXPECT_TRUE(ack.has_segment("MSH"));
    EXPECT_TRUE(ack.has_segment("MSA"));

    auto header = ack.header();
    EXPECT_EQ(header.type_string, "ACK");

    // Verify MSA fields
    EXPECT_EQ(ack.get_value("MSA.1"), "AA");
    EXPECT_EQ(ack.get_value("MSA.2"), adt->header().message_control_id);
    EXPECT_EQ(ack.get_value("MSA.3"), "Message accepted successfully");
}

TEST_F(HL7AckResponseTest, CreateAckForORMMessage) {
    auto orm = hl7_message::parse(hl7_samples::ORM_O01);
    ASSERT_TRUE(orm.has_value());

    auto ack = orm->create_ack(ack_code::AA, "Order received");

    EXPECT_TRUE(ack.has_segment("MSH"));
    EXPECT_TRUE(ack.has_segment("MSA"));
    EXPECT_EQ(ack.get_value("MSA.1"), "AA");
}

TEST_F(HL7AckResponseTest, CreateErrorAck_AE) {
    auto original = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(original.has_value());

    auto ack = original->create_ack(ack_code::AE, "Application error: database unavailable");

    EXPECT_EQ(ack.get_value("MSA.1"), "AE");
    EXPECT_THAT(ack.get_value("MSA.3"), HasSubstr("database unavailable"));
}

TEST_F(HL7AckResponseTest, CreateErrorAck_AR) {
    auto original = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(original.has_value());

    auto ack = original->create_ack(ack_code::AR, "Message rejected: invalid patient ID");

    EXPECT_EQ(ack.get_value("MSA.1"), "AR");
    EXPECT_THAT(ack.get_value("MSA.3"), HasSubstr("invalid patient ID"));
}

TEST_F(HL7AckResponseTest, CreateCommitAck_CA) {
    auto original = hl7_message::parse(hl7_samples::ORM_O01);
    ASSERT_TRUE(original.has_value());

    auto ack = original->create_ack(ack_code::CA, "Commit accept");

    EXPECT_EQ(ack.get_value("MSA.1"), "CA");
}

TEST_F(HL7AckResponseTest, CreateCommitError_CE) {
    auto original = hl7_message::parse(hl7_samples::ORM_O01);
    ASSERT_TRUE(original.has_value());

    auto ack = original->create_ack(ack_code::CE, "Commit error: transaction failed");

    EXPECT_EQ(ack.get_value("MSA.1"), "CE");
}

TEST_F(HL7AckResponseTest, CreateCommitReject_CR) {
    auto original = hl7_message::parse(hl7_samples::ORM_O01);
    ASSERT_TRUE(original.has_value());

    auto ack = original->create_ack(ack_code::CR, "Commit reject: invalid sequence number");

    EXPECT_EQ(ack.get_value("MSA.1"), "CR");
}

TEST_F(HL7AckResponseTest, ParseAckWithError) {
    auto result = hl7_message::parse(extended_samples::ACK_AE);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->get_value("MSA.1"), "AE");
    EXPECT_TRUE(result->has_segment("ERR"));
}

TEST_F(HL7AckResponseTest, ParseAckWithRejection) {
    auto result = hl7_message::parse(extended_samples::ACK_AR);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->get_value("MSA.1"), "AR");
    EXPECT_TRUE(result->has_segment("ERR"));
}

TEST_F(HL7AckResponseTest, AckSwapsApplications) {
    auto original = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(original.has_value());

    auto ack = original->create_ack(ack_code::AA, "OK");

    auto orig_header = original->header();
    auto ack_header = ack.header();

    // Sender becomes receiver and vice versa
    EXPECT_EQ(ack_header.sending_application, orig_header.receiving_application);
    EXPECT_EQ(ack_header.sending_facility, orig_header.receiving_facility);
    EXPECT_EQ(ack_header.receiving_application, orig_header.sending_application);
    EXPECT_EQ(ack_header.receiving_facility, orig_header.sending_facility);
}

TEST_F(HL7AckResponseTest, AckSerializesCorrectly) {
    auto original = hl7_message::parse(hl7_samples::ADT_A01);
    ASSERT_TRUE(original.has_value());

    auto ack = original->create_ack(ack_code::AA, "Message processed");

    std::string serialized = ack.serialize();
    EXPECT_FALSE(serialized.empty());
    EXPECT_THAT(serialized, StartsWith("MSH|"));
    EXPECT_THAT(serialized, HasSubstr("MSA|AA"));

    // Re-parse to verify
    auto reparsed = hl7_message::parse(serialized);
    ASSERT_TRUE(reparsed.has_value());
    EXPECT_EQ(reparsed->get_value("MSA.1"), "AA");
}

// =============================================================================
// Builder Extended Tests
// =============================================================================

class HL7BuilderExtendedTest : public pacs_bridge_test {};

TEST_F(HL7BuilderExtendedTest, CreateAckWithBuilder) {
    auto original = hl7_builder::create()
                        .sending_app("HIS")
                        .sending_facility("HOSPITAL")
                        .receiving_app("PACS")
                        .receiving_facility("RADIOLOGY")
                        .message_type("ORM", "O01")
                        .control_id("ORM001")
                        .processing_id("P")
                        .version("2.4")
                        .build();

    ASSERT_TRUE(original.has_value());

    auto ack = hl7_builder::create_ack(*original, ack_code::AA, "Order accepted");

    EXPECT_TRUE(ack.has_segment("MSH"));
    EXPECT_TRUE(ack.has_segment("MSA"));
    EXPECT_EQ(ack.header().type_string, "ACK");
}

TEST_F(HL7BuilderExtendedTest, CreateNackWithBuilder) {
    auto original = hl7_builder::create()
                        .sending_app("HIS")
                        .sending_facility("HOSPITAL")
                        .receiving_app("PACS")
                        .receiving_facility("RADIOLOGY")
                        .message_type("ADT", "A01")
                        .control_id("ADT001")
                        .processing_id("P")
                        .version("2.4")
                        .build();

    ASSERT_TRUE(original.has_value());

    auto nack = hl7_builder::create_ack(*original, ack_code::AE, "Patient not found");

    EXPECT_EQ(nack.get_value("MSA.1"), "AE");
    EXPECT_THAT(nack.get_value("MSA.3"), HasSubstr("Patient not found"));
}

// =============================================================================
// Edge Case Tests
// =============================================================================

class HL7EdgeCaseTest : public pacs_bridge_test {};

TEST_F(HL7EdgeCaseTest, ConsecutiveDelimiters) {
    const char* msg =
        "MSH|^~\\&|HIS||PACS||20240115103000||ADT^A01|MSG|P|2.4\r"
        "PID||||||||M\r";

    auto result = hl7_message::parse(msg);
    EXPECT_TRUE(result.has_value());
}

TEST_F(HL7EdgeCaseTest, TrailingCarriageReturn) {
    std::string msg(hl7_samples::ADT_A01);
    msg += "\r\r\r";  // Extra CRs

    auto result = hl7_message::parse(msg);
    EXPECT_TRUE(result.has_value());
}

TEST_F(HL7EdgeCaseTest, MixedLineEndings) {
    std::string msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG|P|2.4\r\n"
        "EVN|A01|20240115103000\r\n"
        "PID|1||12345||||\r\n";

    auto result = hl7_message::parse(msg);
    // Parser should handle CRLF endings
    if (result.has_value()) {
        EXPECT_TRUE(result->has_segment("MSH"));
    }
}

TEST_F(HL7EdgeCaseTest, MaximumFieldDepth) {
    // Test deeply nested components and subcomponents
    const char* msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG|P|2.4\r"
        "PID|1||12345&SUB1&SUB2&SUB3^^^HOSPITAL^MR||DOE^JOHN^M^JR^DR||19800515|M\r";

    auto result = hl7_message::parse(msg);
    ASSERT_TRUE(result.has_value());

    // Access deeply nested values
    auto* pid = result->segment("PID");
    ASSERT_NE(pid, nullptr);
}

TEST_F(HL7EdgeCaseTest, EmptyMessageControlId) {
    const char* msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01||P|2.4\r";

    auto result = hl7_message::parse(msg);
    if (result.has_value()) {
        EXPECT_TRUE(result->header().message_control_id.empty());
    }
}

TEST_F(HL7EdgeCaseTest, SingleSegmentMessage) {
    const char* msg = "MSH|^~\\&|HIS|FAC|DEST|DFAC|20240115||ADT^A01|MSG|P|2.4\r";

    auto result = hl7_message::parse(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->segment_count(), 1u);
}

}  // namespace
}  // namespace pacs::bridge::hl7
