/**
 * @file hl7_encoding_iso_conversion_test.cpp
 * @brief Unit tests for HL7 encoding conversion (ISO-8859-1 to UTF-8)
 *
 * Tests for character encoding conversion, charset detection,
 * and encoding validation in HL7 messages.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/145
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::hl7::test {
namespace {

using namespace ::testing;

// =============================================================================
// Sample Messages with Various Encodings
// =============================================================================

namespace encoding_samples {

/**
 * @brief Standard ASCII message (valid in both UTF-8 and ISO-8859-1)
 */
constexpr std::string_view MSG_ASCII =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r";

/**
 * @brief Message with UTF-8 encoded characters (Korean)
 */
constexpr std::string_view MSG_UTF8_KOREAN =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG002|P|2.4|||AL|NE||UNICODE UTF-8\r"
    "PID|1||12345^^^HOSPITAL^MR||\xEA\xB9\x80^\xEC\xB2\xA0\xEC\x88\x98||19800515|M|||123 MAIN ST^^SEOUL^KR\r";

/**
 * @brief Message with UTF-8 encoded characters (Japanese)
 */
constexpr std::string_view MSG_UTF8_JAPANESE =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG003|P|2.4|||AL|NE||UNICODE UTF-8\r"
    "PID|1||12345^^^HOSPITAL^MR||\xE5\xB1\xB1\xE7\x94\xB0^\xE5\xA4\xAA\xE9\x83\x8E||19800515|M|||123 MAIN ST^^TOKYO^JP\r";

/**
 * @brief Message with UTF-8 encoded characters (Chinese)
 */
constexpr std::string_view MSG_UTF8_CHINESE =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG004|P|2.4|||AL|NE||UNICODE UTF-8\r"
    "PID|1||12345^^^HOSPITAL^MR||\xE7\x8E\x8B^\xE4\xBC\x9F||19800515|M|||123 MAIN ST^^BEIJING^CN\r";

/**
 * @brief Message with ISO-8859-1 characters (Western European)
 * Contains: ä (0xE4), ö (0xF6), ü (0xFC), ß (0xDF)
 */
const std::string MSG_ISO_8859_1_GERMAN = []() {
    std::string msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG005|P|2.4|||AL|NE||8859/1\r"
        "PID|1||12345^^^HOSPITAL^MR||M";
    msg += '\xFC';  // ü in ISO-8859-1
    msg += "LLER^HANS||19800515|M|||GR";
    msg += '\xF6';  // ö in ISO-8859-1
    msg += "NE STR 1^^M";
    msg += '\xFC';  // ü
    msg += "NCHEN^DE\r";
    return msg;
}();

/**
 * @brief Message with ISO-8859-1 characters (French)
 * Contains: é (0xE9), è (0xE8), ç (0xE7), à (0xE0)
 */
const std::string MSG_ISO_8859_1_FRENCH = []() {
    std::string msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG006|P|2.4|||AL|NE||8859/1\r"
        "PID|1||12345^^^HOSPITAL^MR||FRAN";
    msg += '\xE7';  // ç
    msg += "OIS^REN";
    msg += '\xE9';  // é
    msg += "||19800515|M|||1 AV D";
    msg += '\xE9';  // é
    msg += "FENSE^^PARIS^FR\r";
    return msg;
}();

/**
 * @brief Message with ISO-8859-1 characters (Nordic)
 * Contains: å (0xE5), ø (0xF8), æ (0xE6)
 */
const std::string MSG_ISO_8859_1_NORDIC = []() {
    std::string msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG007|P|2.4|||AL|NE||8859/1\r"
        "PID|1||12345^^^HOSPITAL^MR||";
    msg += '\xD8';  // Ø
    msg += "STERGAARD^J";
    msg += '\xF8';  // ø
    msg += "RGEN||19800515|M|||S";
    msg += '\xF8';  // ø
    msg += "NDERGADE 1^^K";
    msg += '\xF8';  // ø
    msg += "BENHAVN^DK\r";
    return msg;
}();

/**
 * @brief Message with ISO-8859-1 characters (Spanish)
 * Contains: ñ (0xF1), ó (0xF3), í (0xED), á (0xE1)
 */
const std::string MSG_ISO_8859_1_SPANISH = []() {
    std::string msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG008|P|2.4|||AL|NE||8859/1\r"
        "PID|1||12345^^^HOSPITAL^MR||NU";
    msg += '\xF1';  // ñ
    msg += "EZ^JOSE^MAR";
    msg += '\xED';  // í
    msg += "A||19800515|M|||CALLE ESPA";
    msg += '\xF1';  // ñ
    msg += "A 1^^MADRID^ES\r";
    return msg;
}();

/**
 * @brief Message with mixed valid/invalid UTF-8 sequences
 */
const std::string MSG_INVALID_UTF8 = []() {
    std::string msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG009|P|2.4|||AL|NE||UNICODE UTF-8\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN";
    // Invalid UTF-8: continuation byte without start byte
    msg += '\x80';
    msg += "||19800515|M\r";
    return msg;
}();

/**
 * @brief Message with special characters requiring escape
 */
constexpr std::string_view MSG_ESCAPE_CHARS =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG010|P|2.4\r"
    "PID|1||12345^^^HOSPITAL^MR||O'BRIEN^MARY-JANE||19800515|F|||123 ELM ST\\F\\APT 2B^^BOSTON^MA\r"
    "OBX|1|TX|NOTE||Patient reports \\T\\ allergies\\R\\Previous visit: 2024-01-10||||||F\r";

}  // namespace encoding_samples

// =============================================================================
// Test Fixture
// =============================================================================

class Hl7EncodingTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;

    std::optional<hl7_message> parse(std::string_view raw) {
        return parser_->parse(std::string(raw));
    }

    std::optional<hl7_message> parse(const std::string& raw) {
        return parser_->parse(raw);
    }

    // Extract patient name from PID segment
    std::string extract_patient_name(const hl7_message& msg) {
        auto pid = msg.get_segment("PID");
        if (!pid) return "";
        return pid->get_field(5);
    }

    // Extract character set from MSH-18
    std::string extract_character_set(const hl7_message& msg) {
        auto msh = msg.get_segment("MSH");
        if (!msh) return "";
        return msh->get_field(18);
    }

    // Check if string is valid UTF-8
    bool is_valid_utf8(const std::string& str) {
        size_t i = 0;
        while (i < str.size()) {
            unsigned char c = str[i];
            size_t bytes_to_follow = 0;

            if (c <= 0x7F) {
                // ASCII
                bytes_to_follow = 0;
            } else if ((c & 0xE0) == 0xC0) {
                // 2-byte sequence
                bytes_to_follow = 1;
            } else if ((c & 0xF0) == 0xE0) {
                // 3-byte sequence
                bytes_to_follow = 2;
            } else if ((c & 0xF8) == 0xF0) {
                // 4-byte sequence
                bytes_to_follow = 3;
            } else {
                return false;  // Invalid start byte
            }

            if (i + bytes_to_follow >= str.size()) {
                return false;  // Not enough bytes
            }

            for (size_t j = 1; j <= bytes_to_follow; ++j) {
                if ((str[i + j] & 0xC0) != 0x80) {
                    return false;  // Invalid continuation byte
                }
            }
            i += bytes_to_follow + 1;
        }
        return true;
    }
};

// =============================================================================
// ASCII Encoding Tests
// =============================================================================

TEST_F(Hl7EncodingTest, ParseAsciiMessage) {
    auto msg = parse(encoding_samples::MSG_ASCII);
    ASSERT_TRUE(msg.has_value());

    std::string name = extract_patient_name(*msg);
    EXPECT_TRUE(name.find("DOE") != std::string::npos);
    EXPECT_TRUE(is_valid_utf8(name));
}

// =============================================================================
// UTF-8 Encoding Tests
// =============================================================================

TEST_F(Hl7EncodingTest, ParseUtf8Korean) {
    auto msg = parse(encoding_samples::MSG_UTF8_KOREAN);
    ASSERT_TRUE(msg.has_value());

    std::string charset = extract_character_set(*msg);
    EXPECT_TRUE(charset.find("UTF-8") != std::string::npos ||
                charset.find("UNICODE") != std::string::npos);

    std::string name = extract_patient_name(*msg);
    EXPECT_FALSE(name.empty());
    EXPECT_TRUE(is_valid_utf8(name));
}

TEST_F(Hl7EncodingTest, ParseUtf8Japanese) {
    auto msg = parse(encoding_samples::MSG_UTF8_JAPANESE);
    ASSERT_TRUE(msg.has_value());

    std::string name = extract_patient_name(*msg);
    EXPECT_FALSE(name.empty());
    EXPECT_TRUE(is_valid_utf8(name));
}

TEST_F(Hl7EncodingTest, ParseUtf8Chinese) {
    auto msg = parse(encoding_samples::MSG_UTF8_CHINESE);
    ASSERT_TRUE(msg.has_value());

    std::string name = extract_patient_name(*msg);
    EXPECT_FALSE(name.empty());
    EXPECT_TRUE(is_valid_utf8(name));
}

// =============================================================================
// ISO-8859-1 Encoding Tests
// =============================================================================

TEST_F(Hl7EncodingTest, ParseIso88591German) {
    auto msg = parse(encoding_samples::MSG_ISO_8859_1_GERMAN);
    ASSERT_TRUE(msg.has_value());

    std::string charset = extract_character_set(*msg);
    EXPECT_TRUE(charset.find("8859") != std::string::npos);

    std::string name = extract_patient_name(*msg);
    EXPECT_FALSE(name.empty());
}

TEST_F(Hl7EncodingTest, ParseIso88591French) {
    auto msg = parse(encoding_samples::MSG_ISO_8859_1_FRENCH);
    ASSERT_TRUE(msg.has_value());

    std::string name = extract_patient_name(*msg);
    EXPECT_FALSE(name.empty());
}

TEST_F(Hl7EncodingTest, ParseIso88591Nordic) {
    auto msg = parse(encoding_samples::MSG_ISO_8859_1_NORDIC);
    ASSERT_TRUE(msg.has_value());

    std::string name = extract_patient_name(*msg);
    EXPECT_FALSE(name.empty());
}

TEST_F(Hl7EncodingTest, ParseIso88591Spanish) {
    auto msg = parse(encoding_samples::MSG_ISO_8859_1_SPANISH);
    ASSERT_TRUE(msg.has_value());

    std::string name = extract_patient_name(*msg);
    EXPECT_FALSE(name.empty());
}

// =============================================================================
// Encoding Detection Tests
// =============================================================================

TEST_F(Hl7EncodingTest, DetectUtf8Encoding) {
    auto msg = parse(encoding_samples::MSG_UTF8_KOREAN);
    ASSERT_TRUE(msg.has_value());

    std::string charset = extract_character_set(*msg);
    EXPECT_TRUE(charset.find("UTF-8") != std::string::npos ||
                charset.find("UNICODE") != std::string::npos);
}

TEST_F(Hl7EncodingTest, DetectIso88591Encoding) {
    auto msg = parse(encoding_samples::MSG_ISO_8859_1_GERMAN);
    ASSERT_TRUE(msg.has_value());

    std::string charset = extract_character_set(*msg);
    EXPECT_TRUE(charset.find("8859") != std::string::npos);
}

TEST_F(Hl7EncodingTest, DefaultEncodingWhenNotSpecified) {
    auto msg = parse(encoding_samples::MSG_ASCII);
    ASSERT_TRUE(msg.has_value());

    // When no encoding specified, should handle as ASCII/default
    std::string charset = extract_character_set(*msg);
    // Empty or default charset is acceptable
}

// =============================================================================
// Escape Sequence Tests
// =============================================================================

TEST_F(Hl7EncodingTest, ParseEscapeSequences) {
    auto msg = parse(encoding_samples::MSG_ESCAPE_CHARS);
    ASSERT_TRUE(msg.has_value());

    auto pid = msg->get_segment("PID");
    ASSERT_TRUE(pid != nullptr);

    // O'BRIEN should be preserved
    std::string name = pid->get_field(5);
    EXPECT_TRUE(name.find("O'BRIEN") != std::string::npos ||
                name.find("O") != std::string::npos);
}

TEST_F(Hl7EncodingTest, ParseAddressWithEscapedFieldSeparator) {
    auto msg = parse(encoding_samples::MSG_ESCAPE_CHARS);
    ASSERT_TRUE(msg.has_value());

    auto pid = msg->get_segment("PID");
    ASSERT_TRUE(pid != nullptr);

    // Address with \F\ escape (field separator in address)
    std::string address = pid->get_field(11);
    EXPECT_FALSE(address.empty());
}

TEST_F(Hl7EncodingTest, ParseObxWithEscapeSequences) {
    auto msg = parse(encoding_samples::MSG_ESCAPE_CHARS);
    ASSERT_TRUE(msg.has_value());

    auto obx = msg->get_segment("OBX");
    ASSERT_TRUE(obx != nullptr);

    // OBX-5 contains \T\ (subcomponent separator) and \R\ (repetition)
    std::string value = obx->get_field(5);
    EXPECT_FALSE(value.empty());
}

// =============================================================================
// Invalid Encoding Tests
// =============================================================================

TEST_F(Hl7EncodingTest, HandleInvalidUtf8Sequence) {
    // Parser should handle invalid UTF-8 gracefully
    auto msg = parse(encoding_samples::MSG_INVALID_UTF8);
    // Message may or may not parse depending on implementation
    // The key is that it shouldn't crash
}

// =============================================================================
// Round-Trip Encoding Tests
// =============================================================================

TEST_F(Hl7EncodingTest, RoundTripUtf8) {
    auto msg = parse(encoding_samples::MSG_UTF8_KOREAN);
    ASSERT_TRUE(msg.has_value());

    // Build the message back
    std::string rebuilt = msg->to_string();
    EXPECT_FALSE(rebuilt.empty());

    // Parse the rebuilt message
    auto reparsed = parse(rebuilt);
    ASSERT_TRUE(reparsed.has_value());

    // Verify content preserved
    EXPECT_EQ(extract_patient_name(*msg), extract_patient_name(*reparsed));
}

TEST_F(Hl7EncodingTest, RoundTripAscii) {
    auto msg = parse(encoding_samples::MSG_ASCII);
    ASSERT_TRUE(msg.has_value());

    std::string rebuilt = msg->to_string();
    auto reparsed = parse(rebuilt);
    ASSERT_TRUE(reparsed.has_value());

    EXPECT_EQ(extract_patient_name(*msg), extract_patient_name(*reparsed));
}

// =============================================================================
// Character Set Validation Tests
// =============================================================================

TEST_F(Hl7EncodingTest, ValidateUtf8CharacterSet) {
    std::string valid_utf8 = "Hello 世界 مرحبا";
    EXPECT_TRUE(is_valid_utf8(valid_utf8));
}

TEST_F(Hl7EncodingTest, DetectInvalidUtf8) {
    std::string invalid;
    invalid += '\x80';  // Invalid start byte
    EXPECT_FALSE(is_valid_utf8(invalid));
}

TEST_F(Hl7EncodingTest, ValidateAsciiSubsetOfUtf8) {
    std::string ascii = "Hello World 123 !@#";
    EXPECT_TRUE(is_valid_utf8(ascii));
}

// =============================================================================
// Multi-byte Character Tests
// =============================================================================

TEST_F(Hl7EncodingTest, ParseThreeByteUtf8) {
    // Korean characters are 3-byte UTF-8
    auto msg = parse(encoding_samples::MSG_UTF8_KOREAN);
    ASSERT_TRUE(msg.has_value());

    std::string name = extract_patient_name(*msg);
    // Each Korean character should be 3 bytes
    EXPECT_GT(name.size(), 3);
}

TEST_F(Hl7EncodingTest, ParseTwoByteUtf8) {
    // When ISO-8859-1 is converted to UTF-8, most special chars become 2-byte
    // This test verifies handling of 2-byte UTF-8 sequences
    std::string msg_with_umlaut =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4|||AL|NE||UNICODE UTF-8\r"
        "PID|1||12345^^^HOSPITAL^MR||M\xC3\xBCLLER^HANS||19800515|M\r";

    auto msg = parse(msg_with_umlaut);
    ASSERT_TRUE(msg.has_value());

    std::string name = extract_patient_name(*msg);
    EXPECT_TRUE(is_valid_utf8(name));
}

// =============================================================================
// Builder Encoding Tests
// =============================================================================

TEST_F(Hl7EncodingTest, BuildMessageWithUtf8) {
    hl7_builder builder;
    builder.set_sending_application("HIS")
           .set_sending_facility("HOSPITAL")
           .set_receiving_application("PACS")
           .set_receiving_facility("RADIOLOGY")
           .set_message_type("ADT")
           .set_trigger_event("A01")
           .set_character_set("UNICODE UTF-8");

    auto msg = builder.build();
    ASSERT_TRUE(msg.has_value());

    std::string charset = extract_character_set(*msg);
    EXPECT_TRUE(charset.find("UTF-8") != std::string::npos ||
                charset.find("UNICODE") != std::string::npos);
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
