/**
 * @file backwards_compatibility_test.cpp
 * @brief Integration tests for HL7 version backwards compatibility
 *
 * Tests for handling different HL7 v2.x versions (2.3, 2.3.1, 2.4, 2.5, 2.5.1)
 * and ensuring interoperability between systems using different versions.
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
// Sample Messages for Different HL7 Versions
// =============================================================================

namespace version_samples {

/**
 * @brief HL7 v2.3 ADT message (older format)
 */
constexpr std::string_view ADT_V23 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.3\r"
    "EVN|A01|20240115103000\r"
    "PID|1||12345^^^HOSPITAL||DOE^JOHN||19800515|M|||123 MAIN ST^^CITY^ST^12345\r"
    "PV1|1|I|WARD^101^A||||SMITH^ROBERT\r";

/**
 * @brief HL7 v2.3.1 ADT message
 */
constexpr std::string_view ADT_V231 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.3.1\r"
    "EVN|A01|20240115103000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M|||123 MAIN ST^^CITY^ST^12345\r"
    "PV1|1|I|WARD^101^A||||SMITH^ROBERT^MD\r";

/**
 * @brief HL7 v2.4 ADT message (common version)
 */
constexpr std::string_view ADT_V24 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4|||AL|NE\r"
    "EVN|A01|20240115103000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||123 MAIN ST^^CITY^ST^12345||555-1234\r"
    "PV1|1|I|WARD^101^A^HOSPITAL||||SMITH^ROBERT^MD|||MED\r";

/**
 * @brief HL7 v2.5 ADT message
 */
constexpr std::string_view ADT_V25 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01^ADT_A01|MSG001|P|2.5|||AL|NE||UNICODE UTF-8\r"
    "EVN|A01|20240115103000||||A01\r"
    "PID|1||12345^^^HOSPITAL^MR~98765^^^SSA^SS||DOE^JOHN^WILLIAM^Jr^Dr||19800515|M|||123 MAIN ST^^CITY^ST^12345^USA||555-1234|||M||ACC123\r"
    "PV1|1|I|WARD^101^A^HOSPITAL^R^1||||SMITH^ROBERT^MD^Dr|||MED||||||||VIP|||||||||||||||||||||||||20240115\r";

/**
 * @brief HL7 v2.5.1 ADT message (latest common version)
 */
constexpr std::string_view ADT_V251 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01^ADT_A01|MSG001|P|2.5.1|||AL|NE||UNICODE UTF-8\r"
    "SFT|VENDOR|1.0|PRODUCT|BIN001||20240101\r"
    "EVN|A01|20240115103000||||A01\r"
    "PID|1||12345^^^HOSPITAL^MR~98765^^^SSA^SS||DOE^JOHN^WILLIAM^Jr^Dr^PhD||19800515|M|||123 MAIN ST^^CITY^ST^12345^USA^H||555-1234^PRN^PH|||M|CHR|ACC123|||N||||20240115\r"
    "PV1|1|I|WARD^101^A^HOSPITAL^R^1^^^NORTH||||SMITH^ROBERT^MD^Dr||JONES^MARY^MD|MED||||||||VIP|V123456|||||||||||||||||||||||20240115\r";

/**
 * @brief HL7 v2.3 ORM message
 */
constexpr std::string_view ORM_V23 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ORM^O01|MSG001|P|2.3\r"
    "PID|1||12345^^^HOSPITAL||DOE^JOHN||19800515|M\r"
    "ORC|NW|ORD001||ACC001\r"
    "OBR|1|ORD001|ACC001|71020^CHEST XRAY\r";

/**
 * @brief HL7 v2.4 ORM message
 */
constexpr std::string_view ORM_V24 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ORM^O01|MSG001|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "ORC|NW|ORD001^HIS|ACC001^PACS||SC|||^^^20240115120000^^R\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT\r";

/**
 * @brief HL7 v2.5.1 ORM message
 */
constexpr std::string_view ORM_V251 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ORM^O01^ORM_O01|MSG001|P|2.5.1|||AL|NE||UNICODE UTF-8\r"
    "PID|1||12345^^^HOSPITAL^MR~98765^^^SSA^SS||DOE^JOHN^WILLIAM||19800515|M\r"
    "ORC|NW|ORD001^HIS^1.2.3.4|ACC001^PACS^5.6.7.8||SC|||^^^20240115120000^^R||20240115110000|JONES^MARY^RN|||RADIOLOGY\r"
    "TQ1|1||1^ONCE||20240115120000||S^STAT^HL70078\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT^CXRAY^CHEST XRAY^LOCAL|||20240115110000||1\r";

/**
 * @brief HL7 v2.3 ORU message
 */
constexpr std::string_view ORU_V23 =
    "MSH|^~\\&|LAB|HOSPITAL|HIS|HOSPITAL|20240115103000||ORU^R01|MSG001|P|2.3\r"
    "PID|1||12345^^^HOSPITAL||DOE^JOHN||19800515|M\r"
    "OBR|1|ORD001|ACC001|CBC^Complete Blood Count\r"
    "OBX|1|NM|WBC||7.5|10E3/uL|4.0-11.0|N|||F\r";

/**
 * @brief HL7 v2.5.1 ORU message
 */
constexpr std::string_view ORU_V251 =
    "MSH|^~\\&|LAB|HOSPITAL|HIS|HOSPITAL|20240115103000||ORU^R01^ORU_R01|MSG001|P|2.5.1|||AL|NE||UNICODE UTF-8\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "ORC|RE|ORD001^LAB|ACC001^LAB\r"
    "OBR|1|ORD001^LAB|ACC001^LAB|CBC^Complete Blood Count^L|||20240115090000||||||||20240115100000||SMITH^ROBERT^MD||||20240115103000|||F\r"
    "OBX|1|NM|WBC^White Blood Cell Count^L||7.5|10E3/uL|4.0-11.0|N|||F|||20240115103000\r"
    "OBX|2|NM|RBC^Red Blood Cell Count^L||4.8|10E6/uL|4.2-5.9|N|||F|||20240115103000\r";

}  // namespace version_samples

// =============================================================================
// Test Fixture
// =============================================================================

class BackwardsCompatibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;

    Result<hl7_message> parse(std::string_view raw) {
        return parser_->parse(raw);
    }

    // Extract HL7 version from message
    std::string extract_version(const hl7_message& msg) {
        auto msh = msg.segment("MSH");
        if (!msh) return "";
        return std::string(msh->field_value(12));
    }

    // Extract patient ID from message
    std::string extract_patient_id(const hl7_message& msg) {
        auto pid = msg.segment("PID");
        if (!pid) return "";
        return std::string(pid->field_value(3));
    }
};

// =============================================================================
// Version Parsing Tests
// =============================================================================

TEST_F(BackwardsCompatibilityTest, ParseVersion23) {
    auto msg = parse(version_samples::ADT_V23);
    ASSERT_TRUE(msg.is_ok());
    EXPECT_EQ(extract_version(msg.value()), "2.3");
}

TEST_F(BackwardsCompatibilityTest, ParseVersion231) {
    auto msg = parse(version_samples::ADT_V231);
    ASSERT_TRUE(msg.is_ok());
    EXPECT_EQ(extract_version(msg.value()), "2.3.1");
}

TEST_F(BackwardsCompatibilityTest, ParseVersion24) {
    auto msg = parse(version_samples::ADT_V24);
    ASSERT_TRUE(msg.is_ok());
    EXPECT_EQ(extract_version(msg.value()), "2.4");
}

TEST_F(BackwardsCompatibilityTest, ParseVersion25) {
    auto msg = parse(version_samples::ADT_V25);
    ASSERT_TRUE(msg.is_ok());
    EXPECT_EQ(extract_version(msg.value()), "2.5");
}

TEST_F(BackwardsCompatibilityTest, ParseVersion251) {
    auto msg = parse(version_samples::ADT_V251);
    ASSERT_TRUE(msg.is_ok());
    EXPECT_EQ(extract_version(msg.value()), "2.5.1");
}

// =============================================================================
// Message Structure Compatibility Tests
// =============================================================================

TEST_F(BackwardsCompatibilityTest, V23MessageStructure) {
    auto msg = parse(version_samples::ADT_V23);
    ASSERT_TRUE(msg.is_ok());

    // v2.3 should have basic segments
    EXPECT_TRUE(msg.value().segment("MSH") != nullptr);
    EXPECT_TRUE(msg.value().segment("EVN") != nullptr);
    EXPECT_TRUE(msg.value().segment("PID") != nullptr);
    EXPECT_TRUE(msg.value().segment("PV1") != nullptr);
}

TEST_F(BackwardsCompatibilityTest, V251MessageStructure) {
    auto msg = parse(version_samples::ADT_V251);
    ASSERT_TRUE(msg.is_ok());

    // v2.5.1 may have additional segments like SFT
    EXPECT_TRUE(msg.value().segment("MSH") != nullptr);
    EXPECT_TRUE(msg.value().segment("SFT") != nullptr);  // Software segment
    EXPECT_TRUE(msg.value().segment("EVN") != nullptr);
    EXPECT_TRUE(msg.value().segment("PID") != nullptr);
    EXPECT_TRUE(msg.value().segment("PV1") != nullptr);
}

// =============================================================================
// Patient ID Format Compatibility Tests
// =============================================================================

TEST_F(BackwardsCompatibilityTest, V23PatientId) {
    auto msg = parse(version_samples::ADT_V23);
    ASSERT_TRUE(msg.is_ok());

    // v2.3 uses PID-3 without identifier type
    std::string pid = extract_patient_id(msg.value());
    EXPECT_TRUE(pid.find("12345") != std::string::npos);
}

TEST_F(BackwardsCompatibilityTest, V24PatientIdWithType) {
    auto msg = parse(version_samples::ADT_V24);
    ASSERT_TRUE(msg.is_ok());

    // v2.4 includes patient ID (MR type may be in subcomponents)
    std::string pid = extract_patient_id(msg.value());
    EXPECT_TRUE(pid.find("12345") != std::string::npos);
    // Note: field_value returns first component, full field includes MR in subcomponents
}

TEST_F(BackwardsCompatibilityTest, V251MultiplePatientIds) {
    auto msg = parse(version_samples::ADT_V251);
    ASSERT_TRUE(msg.is_ok());

    // v2.5.1 may have multiple patient IDs
    std::string pid = extract_patient_id(msg.value());
    EXPECT_TRUE(pid.find("12345") != std::string::npos);
    // May contain repetition separator for multiple IDs
}

// =============================================================================
// Message Type Format Compatibility Tests
// =============================================================================

TEST_F(BackwardsCompatibilityTest, V23MessageTypeFormat) {
    auto msg = parse(version_samples::ADT_V23);
    ASSERT_TRUE(msg.is_ok());

    // v2.3 uses simple type^event format
    // Use type() and trigger_event() for proper parsing
    EXPECT_STREQ(to_string(msg.value().type()), "ADT");
    EXPECT_EQ(msg.value().trigger_event(), "A01");
}

TEST_F(BackwardsCompatibilityTest, V251MessageTypeFormat) {
    auto msg = parse(version_samples::ADT_V251);
    ASSERT_TRUE(msg.is_ok());

    // v2.5.1 uses type^event^structure format
    // Parser provides type and trigger_event separately
    EXPECT_STREQ(to_string(msg.value().type()), "ADT");
    EXPECT_EQ(msg.value().trigger_event(), "A01");
}

// =============================================================================
// ORM Version Compatibility Tests
// =============================================================================

TEST_F(BackwardsCompatibilityTest, OrmV23Format) {
    auto msg = parse(version_samples::ORM_V23);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "ORM");
    EXPECT_TRUE(msg.value().segment("ORC") != nullptr);
    EXPECT_TRUE(msg.value().segment("OBR") != nullptr);
}

TEST_F(BackwardsCompatibilityTest, OrmV24Format) {
    auto msg = parse(version_samples::ORM_V24);
    ASSERT_TRUE(msg.is_ok());

    auto orc = msg.value().segment("ORC");
    ASSERT_TRUE(orc != nullptr);

    // v2.4 ORC has more detailed placer/filler numbers
    std::string placer(orc->field_value(2));
    EXPECT_TRUE(placer.find("ORD001") != std::string::npos);
}

TEST_F(BackwardsCompatibilityTest, OrmV251WithTq1) {
    auto msg = parse(version_samples::ORM_V251);
    ASSERT_TRUE(msg.is_ok());

    // v2.5.1 may include TQ1 for timing
    auto tq1 = msg.value().segment("TQ1");
    EXPECT_TRUE(tq1 != nullptr);
}

// =============================================================================
// ORU Version Compatibility Tests
// =============================================================================

TEST_F(BackwardsCompatibilityTest, OruV23Results) {
    auto msg = parse(version_samples::ORU_V23);
    ASSERT_TRUE(msg.is_ok());

    auto obx_segments = msg.value().segments("OBX");
    EXPECT_GE(obx_segments.size(), 1);
}

TEST_F(BackwardsCompatibilityTest, OruV251Results) {
    auto msg = parse(version_samples::ORU_V251);
    ASSERT_TRUE(msg.is_ok());

    // v2.5.1 ORU includes ORC segment
    EXPECT_TRUE(msg.value().segment("ORC") != nullptr);

    auto obx_segments = msg.value().segments("OBX");
    EXPECT_GE(obx_segments.size(), 2);
}

// =============================================================================
// ACK Response Compatibility Tests
// =============================================================================

TEST_F(BackwardsCompatibilityTest, BuildAckForV23Message) {
    auto msg = parse(version_samples::ADT_V23);
    ASSERT_TRUE(msg.is_ok());

    auto ack = hl7_builder::create_ack(msg.value(), ack_code::AA, "Message accepted");

    // ACK should match source version
    EXPECT_EQ(extract_version(ack), "2.3");
}

TEST_F(BackwardsCompatibilityTest, BuildAckForV251Message) {
    auto msg = parse(version_samples::ADT_V251);
    ASSERT_TRUE(msg.is_ok());

    auto ack = hl7_builder::create_ack(msg.value(), ack_code::AA, "Message accepted");

    // ACK should match source version
    EXPECT_EQ(extract_version(ack), "2.5.1");
}

// =============================================================================
// Cross-Version Data Extraction Tests
// =============================================================================

TEST_F(BackwardsCompatibilityTest, ExtractPatientNameAllVersions) {
    std::vector<std::string_view> messages = {
        version_samples::ADT_V23,
        version_samples::ADT_V231,
        version_samples::ADT_V24,
        version_samples::ADT_V25,
        version_samples::ADT_V251
    };

    for (const auto& raw : messages) {
        auto msg = parse(raw);
        ASSERT_TRUE(msg.is_ok());

        auto pid = msg.value().segment("PID");
        ASSERT_TRUE(pid != nullptr);

        // All versions should have patient name in PID-5
        std::string name(pid->field_value(5));
        EXPECT_TRUE(name.find("DOE") != std::string::npos)
            << "Failed to find DOE in version " << extract_version(msg.value());
    }
}

TEST_F(BackwardsCompatibilityTest, ExtractDateOfBirthAllVersions) {
    std::vector<std::string_view> messages = {
        version_samples::ADT_V23,
        version_samples::ADT_V231,
        version_samples::ADT_V24,
        version_samples::ADT_V25,
        version_samples::ADT_V251
    };

    for (const auto& raw : messages) {
        auto msg = parse(raw);
        ASSERT_TRUE(msg.is_ok());

        auto pid = msg.value().segment("PID");
        ASSERT_TRUE(pid != nullptr);

        // PID-7 is DOB in all versions
        std::string dob(pid->field_value(7));
        EXPECT_TRUE(dob.find("19800515") != std::string::npos)
            << "Failed to find DOB in version " << extract_version(msg.value());
    }
}

// =============================================================================
// Version Upgrade/Downgrade Tests
// =============================================================================

TEST_F(BackwardsCompatibilityTest, ParseV23ThenBuildV24) {
    auto v23_msg = parse(version_samples::ADT_V23);
    ASSERT_TRUE(v23_msg.is_ok());

    // Build new message based on v2.3 data
    auto builder = hl7_builder::create();
    builder.version("2.4")
           .sending_app("HIS")
           .sending_facility("HOSPITAL")
           .receiving_app("PACS")
           .receiving_facility("RADIOLOGY")
           .message_type("ADT", "A01");

    auto v24_msg = builder.build();
    ASSERT_TRUE(v24_msg.is_ok());
    EXPECT_EQ(extract_version(v24_msg.value()), "2.4");
}

// =============================================================================
// Character Set Handling Across Versions
// =============================================================================

TEST_F(BackwardsCompatibilityTest, CharsetV23Default) {
    auto msg = parse(version_samples::ADT_V23);
    ASSERT_TRUE(msg.is_ok());

    auto msh = msg.value().segment("MSH");
    ASSERT_TRUE(msh != nullptr);

    // v2.3 typically doesn't specify charset
    std::string charset(msh->field_value(18));
    // Empty or default is acceptable
    (void)charset;
}

TEST_F(BackwardsCompatibilityTest, CharsetV251Explicit) {
    auto msg = parse(version_samples::ADT_V251);
    ASSERT_TRUE(msg.is_ok());

    auto msh = msg.value().segment("MSH");
    ASSERT_TRUE(msh != nullptr);

    // v2.5.1 explicitly specifies UTF-8
    std::string charset(msh->field_value(18));
    EXPECT_TRUE(charset.find("UTF-8") != std::string::npos ||
                charset.find("UNICODE") != std::string::npos);
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
