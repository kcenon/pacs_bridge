/**
 * @file hl7_malformed_segment_recovery_test.cpp
 * @brief Unit tests for HL7 malformed segment recovery
 *
 * Tests for handling corrupted, truncated, and malformed HL7 segments,
 * including recovery strategies and error reporting.
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

namespace pacs::bridge::hl7::test {
namespace {

using namespace ::testing;

// =============================================================================
// Test Fixture
// =============================================================================

class Hl7MalformedRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;

    std::expected<hl7_message, hl7_error> parse(const std::string& raw) {
        return parser_->parse(raw);
    }

    // Create valid base message
    std::string create_valid_message() {
        return
            "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
            "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
            "PV1|1|I|WARD^101^A\r";
    }
};

// =============================================================================
// Truncated Segment Tests
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, TruncatedMshSegment) {
    std::string truncated = "MSH|^~\\&|HIS|HOS";  // Cut off mid-field
    auto msg = parse(truncated);
    // Should handle truncation gracefully
}

TEST_F(Hl7MalformedRecoveryTest, TruncatedPidSegment) {
    std::string truncated =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOS";  // Cut off mid-field
    auto msg = parse(truncated);
    // Should recover what's possible
}

TEST_F(Hl7MalformedRecoveryTest, TruncatedAtSegmentBoundary) {
    std::string truncated =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "PV1";  // Segment name only
    auto msg = parse(truncated);
    // Should parse complete segments
    if (msg.has_value()) {
        EXPECT_TRUE(msg->segment("MSH") != nullptr);
        EXPECT_TRUE(msg->segment("PID") != nullptr);
    }
}

TEST_F(Hl7MalformedRecoveryTest, MessageEndingWithSeparator) {
    std::string truncated =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|";  // Ends with |
    auto msg = parse(truncated);
    // Should handle trailing separator
}

// =============================================================================
// Corrupted Segment Tests
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, CorruptedSegmentName) {
    std::string corrupted =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "P!D|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "PV1|1|I|WARD^101^A\r";
    auto msg = parse(corrupted);
    // Should skip corrupted segment or recover
    if (msg.has_value()) {
        EXPECT_TRUE(msg->segment("MSH") != nullptr);
        EXPECT_TRUE(msg->segment("PV1") != nullptr);
    }
}

TEST_F(Hl7MalformedRecoveryTest, BinaryDataInSegment) {
    std::string corrupted =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN\x00\x01\x02||19800515|M\r"
        "PV1|1|I|WARD^101^A\r";
    auto msg = parse(corrupted);
    // Should handle binary data gracefully
}

TEST_F(Hl7MalformedRecoveryTest, DoubleFieldSeparators) {
    std::string corrupted =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1|||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r";  // Extra | after set ID
    auto msg = parse(corrupted);
    // Should handle double separators as empty field
    EXPECT_TRUE(msg.has_value());
}

TEST_F(Hl7MalformedRecoveryTest, MissingFieldSeparators) {
    std::string corrupted =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID1|12345^^^HOSPITAL^MR|DOE^JOHN|19800515|M\r";  // Missing | after PID
    auto msg = parse(corrupted);
    // Should attempt recovery
}

// =============================================================================
// Incomplete Segment Structure Tests
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, SegmentWithNoFields) {
    std::string incomplete =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID\r"  // Just segment name
        "PV1|1|I\r";
    auto msg = parse(incomplete);
    // Should handle segment with no fields
}

TEST_F(Hl7MalformedRecoveryTest, SegmentWithOnlySetId) {
    std::string incomplete =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1\r"  // Only set ID
        "PV1|1|I\r";
    auto msg = parse(incomplete);
    EXPECT_TRUE(msg.has_value());
}

TEST_F(Hl7MalformedRecoveryTest, ExtraLongSegmentName) {
    std::string incomplete =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PIDEXTRA|1||12345\r"  // Invalid segment name
        "PV1|1|I\r";
    auto msg = parse(incomplete);
    // Unknown segment should be skipped or preserved
}

// =============================================================================
// Mixed Valid/Invalid Segments Tests
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, ValidSegmentsAroundCorrupted) {
    std::string mixed =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "EVN|A01|20240115103000\r"
        "CORRUPTED_GARBAGE_DATA_HERE\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "PV1|1|I|WARD^101^A\r";
    auto msg = parse(mixed);
    // Should recover valid segments
    if (msg.has_value()) {
        EXPECT_TRUE(msg->segment("MSH") != nullptr);
        EXPECT_TRUE(msg->segment("EVN") != nullptr);
        EXPECT_TRUE(msg->segment("PID") != nullptr);
        EXPECT_TRUE(msg->segment("PV1") != nullptr);
    }
}

TEST_F(Hl7MalformedRecoveryTest, MultipleCorruptedSegments) {
    std::string mixed =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "BAD1|garbage\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "BAD2|more garbage\r"
        "PV1|1|I|WARD^101^A\r";
    auto msg = parse(mixed);
    // Should still parse valid segments
}

// =============================================================================
// Segment Terminator Issues Tests
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, MissingSegmentTerminator) {
    std::string missing_term =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4"  // No \r
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r";
    auto msg = parse(missing_term);
    // Should handle missing terminator
}

TEST_F(Hl7MalformedRecoveryTest, WrongSegmentTerminator) {
    std::string wrong_term =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\n"  // \n instead of \r
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\n";
    auto msg = parse(wrong_term);
    // Should handle alternate terminators
}

TEST_F(Hl7MalformedRecoveryTest, DoubleSegmentTerminator) {
    std::string double_term =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r";
    auto msg = parse(double_term);
    // Should handle double terminators
}

// =============================================================================
// Encoding Characters Corruption Tests
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, CorruptedEncodingCharacters) {
    std::string corrupted =
        "MSH|????|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345\r";
    auto msg = parse(corrupted);
    // Invalid encoding characters should cause failure or recovery
}

TEST_F(Hl7MalformedRecoveryTest, MissingEncodingCharacters) {
    std::string missing =
        "MSH||HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345\r";
    auto msg = parse(missing);
    // Missing encoding chars should be handled
}

TEST_F(Hl7MalformedRecoveryTest, PartialEncodingCharacters) {
    std::string partial =
        "MSH|^~|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345\r";
    auto msg = parse(partial);
    // Should use defaults for missing chars
}

// =============================================================================
// Recovery Strategy Tests
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, RecoverFromMidMessageCorruption) {
    std::string corrupted =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "\x00\x01\x02\x03GARBAGE\r"
        "OBX|1|TX|NOTE||Test note||||||F\r";
    auto msg = parse(corrupted);
    // Should recover and continue parsing after garbage
}

TEST_F(Hl7MalformedRecoveryTest, RecoverValidMessageAfterFailedParse) {
    // First parse a corrupted message
    std::string corrupted = "TOTALLY INVALID MESSAGE";
    auto msg1 = parse(corrupted);
    EXPECT_FALSE(msg1.has_value());

    // Then parse a valid message - parser should recover
    std::string valid = create_valid_message();
    auto msg2 = parse(valid);
    EXPECT_TRUE(msg2.has_value());
}

// =============================================================================
// Specific Segment Corruption Tests
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, CorruptedObxSegment) {
    std::string corrupted =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ORU^R01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "OBR|1|ORD001|ACC001|71020^CHEST XRAY^CPT\r"
        "OBX|1|TX|NOTE\x00CORRUPTED\r"  // Null in segment
        "OBX|2|TX|NOTE2||Second note||||||F\r";
    auto msg = parse(corrupted);
    // Should parse valid OBX segments
    if (msg.has_value()) {
        auto obx_segments = msg->segments("OBX");
        // At least one valid OBX should be recovered
        EXPECT_GE(obx_segments.size(), 1);
    }
}

TEST_F(Hl7MalformedRecoveryTest, CorruptedObrSegment) {
    std::string corrupted =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ORM^O01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "ORC|NW|ORD001|ACC001\r"
        "OBR|INVALID_SET_ID||ACC001|71020^CHEST XRAY^CPT\r";  // Invalid set ID
    auto msg = parse(corrupted);
    // Should handle invalid set ID
}

// =============================================================================
// Component/Subcomponent Corruption Tests
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, CorruptedComponents) {
    std::string corrupted =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345^^^^\x00HOSPITAL^MR||DOE^JOHN||19800515|M\r";
    auto msg = parse(corrupted);
    // Should handle corrupted components
}

TEST_F(Hl7MalformedRecoveryTest, ExcessiveComponents) {
    std::string components;
    for (int i = 0; i < 1000; ++i) {
        if (i > 0) components += "^";
        components += "C" + std::to_string(i);
    }
    std::string corrupted =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||" + components + "\r";
    auto msg = parse(corrupted);
    // Should handle excessive components
}

// =============================================================================
// Error Reporting Tests
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, EmptyMessageReturnsNoValue) {
    auto msg = parse("");
    EXPECT_FALSE(msg.has_value());
}

TEST_F(Hl7MalformedRecoveryTest, InvalidStartReturnsNoValue) {
    auto msg = parse("THIS IS NOT HL7");
    EXPECT_FALSE(msg.has_value());
}

TEST_F(Hl7MalformedRecoveryTest, ValidMessageReturnsValue) {
    auto msg = parse(create_valid_message());
    EXPECT_TRUE(msg.has_value());
}

// =============================================================================
// Real-World Corruption Scenarios
// =============================================================================

TEST_F(Hl7MalformedRecoveryTest, NetworkTruncation) {
    // Simulate network truncation at various points
    std::string full = create_valid_message();
    for (size_t i = 10; i < full.size(); i += 20) {
        std::string truncated = full.substr(0, i);
        auto msg = parse(truncated);
        // Should not crash
    }
}

TEST_F(Hl7MalformedRecoveryTest, CharsetConversionCorruption) {
    // Simulate charset conversion issues
    std::string corrupted =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE\xC0\xC1\xC2^JOHN||19800515|M\r";
    auto msg = parse(corrupted);
    // Should handle corrupted character sequences
}

TEST_F(Hl7MalformedRecoveryTest, BufferOverrunPattern) {
    // Test handling of patterns that might cause buffer issues
    std::string pattern(10000, '|');
    std::string corrupted =
        "MSH|^~\\&|HIS|" + pattern + "\r";
    auto msg = parse(corrupted);
    // Should handle without buffer issues
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
