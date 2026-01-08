/**
 * @file bar_handler_test.cpp
 * @brief Unit tests for BAR (Billing Account Record) message handler
 *
 * Tests for BAR message parsing, account management events,
 * and billing information extraction.
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
// Sample BAR Messages
// =============================================================================

namespace bar_samples {

/**
 * @brief Sample BAR^P01 (Add Patient Account) message
 */
constexpr std::string_view BAR_P01_ADD_ACCOUNT =
    "MSH|^~\\&|BILLING|HOSPITAL|HIS|HOSPITAL|20240115160000||BAR^P01|MSG001|P|2.4\r"
    "EVN|P01|20240115160000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M|||123 MAIN ST^^CITY^ST^12345\r"
    "PV1|1|I|WARD^101^A^HOSPITAL||||SMITH^ROBERT^MD|||MED|||||||||V123456|BCBS|||||||||||||||||||||||||20240115\r"
    "DG1|1||J18.9^Pneumonia, unspecified organism^ICD10|||A\r"
    "GT1|1||DOE^JOHN||123 MAIN ST^^CITY^ST^12345||555-123-4567||||SELF\r"
    "IN1|1|BCBS|12345|BLUE CROSS BLUE SHIELD||||GROUP123||||||||DOE^JOHN|SELF|19800515|123 MAIN ST^^CITY^ST^12345||||||||||||||||POL123456\r";

/**
 * @brief Sample BAR^P02 (Purge Patient Account) message
 */
constexpr std::string_view BAR_P02_PURGE_ACCOUNT =
    "MSH|^~\\&|BILLING|HOSPITAL|HIS|HOSPITAL|20240115170000||BAR^P02|MSG002|P|2.4\r"
    "EVN|P02|20240115170000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "PV1|1|I||||||||||||||||||V123456\r";

/**
 * @brief Sample BAR^P05 (Update Account) message
 */
constexpr std::string_view BAR_P05_UPDATE_ACCOUNT =
    "MSH|^~\\&|BILLING|HOSPITAL|HIS|HOSPITAL|20240115180000||BAR^P05|MSG003|P|2.4\r"
    "EVN|P05|20240115180000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "PV1|1|I|WARD^102^B^HOSPITAL||||SMITH^ROBERT^MD|||MED|||||||||V123456\r"
    "DG1|1||J18.9^Pneumonia, unspecified organism^ICD10|||A\r"
    "DG1|2||I10^Essential hypertension^ICD10|||S\r";

/**
 * @brief Sample BAR^P06 (End Account) message
 */
constexpr std::string_view BAR_P06_END_ACCOUNT =
    "MSH|^~\\&|BILLING|HOSPITAL|HIS|HOSPITAL|20240115190000||BAR^P06|MSG004|P|2.4\r"
    "EVN|P06|20240115190000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "PV1|1|I||||||||||||||||||V123456|||||||||||||||||||||||||20240115|20240120\r";

/**
 * @brief Sample BAR^P10 (Transmit Ambulatory Payment Classification) message
 */
constexpr std::string_view BAR_P10_APC =
    "MSH|^~\\&|BILLING|HOSPITAL|HIS|HOSPITAL|20240115200000||BAR^P10|MSG005|P|2.4\r"
    "EVN|P10|20240115200000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "PV1|1|O|RAD^XRAY^1|||||||||||||||||V123456\r"
    "DG1|1||Z12.31^Encounter for screening mammogram^ICD10\r"
    "PR1|1||77067^Screening mammography, bilateral^CPT|20240115\r"
    "GP1|A|0.85\r"
    "GP2|1|77067|HCPCS|1.0|100.00\r";

/**
 * @brief Sample BAR message with multiple insurances
 */
constexpr std::string_view BAR_MULTIPLE_INSURANCE =
    "MSH|^~\\&|BILLING|HOSPITAL|HIS|HOSPITAL|20240115210000||BAR^P01|MSG006|P|2.4\r"
    "EVN|P01|20240115210000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "PV1|1|I|WARD^101^A|||||||||||||||||V123456\r"
    "IN1|1|BCBS|12345|BLUE CROSS BLUE SHIELD|||||||||||DOE^JOHN|SELF||||||||||||||||||POL123\r"
    "IN2|1|||||||||DOE^JOHN|SELF\r"
    "IN1|2|AETNA|67890|AETNA INSURANCE|||||||||||DOE^JANE|SPOUSE||||||||||||||||||POL456\r"
    "IN2|2|||||||||DOE^JANE|SPOUSE\r";

}  // namespace bar_samples

// =============================================================================
// Test Fixture
// =============================================================================

class BarHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;

    Result<hl7_message> parse_bar(std::string_view raw) {
        return parser_->parse(raw);
    }

    // Extract account number from PV1
    std::string extract_account_number(const hl7_message& msg) {
        auto pv1 = msg.segment("PV1");
        if (!pv1) return "";
        return std::string(pv1->field_value(19));  // PV1-19 is Visit Number
    }

    // Extract insurance company from IN1
    std::string extract_insurance_company(const hl7_message& msg) {
        auto in1 = msg.segment("IN1");
        if (!in1) return "";
        return std::string(in1->field_value(4));  // IN1-4 is Insurance Company Name
    }
};

// =============================================================================
// BAR Message Parsing Tests
// =============================================================================

TEST_F(BarHandlerTest, ParseBarP01AddAccount) {
    auto msg = parse_bar(bar_samples::BAR_P01_ADD_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "BAR");
    EXPECT_EQ(msg.value().trigger_event(), "P01");
    EXPECT_EQ(extract_account_number(msg.value()), "V123456");
}

TEST_F(BarHandlerTest, ParseBarP02PurgeAccount) {
    auto msg = parse_bar(bar_samples::BAR_P02_PURGE_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "BAR");
    EXPECT_EQ(msg.value().trigger_event(), "P02");
}

TEST_F(BarHandlerTest, ParseBarP05UpdateAccount) {
    auto msg = parse_bar(bar_samples::BAR_P05_UPDATE_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "BAR");
    EXPECT_EQ(msg.value().trigger_event(), "P05");

    // Should have multiple diagnosis codes
    auto dg1_segments = msg.value().segments("DG1");
    EXPECT_EQ(dg1_segments.size(), 2);
}

TEST_F(BarHandlerTest, ParseBarP06EndAccount) {
    auto msg = parse_bar(bar_samples::BAR_P06_END_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "BAR");
    EXPECT_EQ(msg.value().trigger_event(), "P06");

    // PV1-45 should have discharge date
    auto pv1 = msg.value().segment("PV1");
    ASSERT_TRUE(pv1 != nullptr);
    EXPECT_FALSE(pv1->field_value(45).empty());
}

TEST_F(BarHandlerTest, ParseBarP10Apc) {
    auto msg = parse_bar(bar_samples::BAR_P10_APC);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "BAR");
    EXPECT_EQ(msg.value().trigger_event(), "P10");

    // Should have PR1 segment for procedures
    auto pr1 = msg.value().segment("PR1");
    ASSERT_TRUE(pr1 != nullptr);
}

// =============================================================================
// Insurance Information Tests
// =============================================================================

TEST_F(BarHandlerTest, ExtractInsuranceCompany) {
    auto msg = parse_bar(bar_samples::BAR_P01_ADD_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    std::string insurance = extract_insurance_company(msg.value());
    EXPECT_TRUE(insurance.find("BLUE CROSS") != std::string::npos);
}

TEST_F(BarHandlerTest, MultipleInsurancePlans) {
    auto msg = parse_bar(bar_samples::BAR_MULTIPLE_INSURANCE);
    ASSERT_TRUE(msg.is_ok());

    auto in1_segments = msg.value().segments("IN1");
    EXPECT_EQ(in1_segments.size(), 2);

    // Primary insurance
    EXPECT_TRUE(in1_segments[0]->field_value(4).find("BLUE CROSS") != std::string::npos);
    // Secondary insurance
    EXPECT_TRUE(in1_segments[1]->field_value(4).find("AETNA") != std::string::npos);
}

TEST_F(BarHandlerTest, InsuranceSubscriberInfo) {
    auto msg = parse_bar(bar_samples::BAR_P01_ADD_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    auto in1 = msg.value().segment("IN1");
    ASSERT_TRUE(in1 != nullptr);

    // IN1-16 is Insured's Name
    EXPECT_TRUE(in1->field_value(16).find("DOE") != std::string::npos);
    // IN1-17 is Insured's Relationship to Patient
    EXPECT_EQ(in1->field_value(17), "SELF");
}

// =============================================================================
// Diagnosis Code Tests
// =============================================================================

TEST_F(BarHandlerTest, ExtractDiagnosisCodes) {
    auto msg = parse_bar(bar_samples::BAR_P01_ADD_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    auto dg1 = msg.value().segment("DG1");
    ASSERT_TRUE(dg1 != nullptr);

    // DG1-3 contains diagnosis code
    std::string dx_code = std::string(dg1->field_value(3));
    EXPECT_TRUE(dx_code.find("J18.9") != std::string::npos);
}

TEST_F(BarHandlerTest, MultipleDiagnoses) {
    auto msg = parse_bar(bar_samples::BAR_P05_UPDATE_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    auto dg1_segments = msg.value().segments("DG1");
    EXPECT_EQ(dg1_segments.size(), 2);

    // Primary diagnosis
    EXPECT_TRUE(dg1_segments[0]->field_value(6).find("A") != std::string::npos);
    // Secondary diagnosis
    EXPECT_TRUE(dg1_segments[1]->field_value(6).find("S") != std::string::npos);
}

// =============================================================================
// Guarantor Tests
// =============================================================================

TEST_F(BarHandlerTest, ExtractGuarantor) {
    auto msg = parse_bar(bar_samples::BAR_P01_ADD_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    auto gt1 = msg.value().segment("GT1");
    ASSERT_TRUE(gt1 != nullptr);

    // GT1-3 is Guarantor Name
    EXPECT_TRUE(gt1->field_value(3).find("DOE") != std::string::npos);
    // GT1-11 is Guarantor Relationship
    EXPECT_EQ(gt1->field_value(11), "SELF");
}

// =============================================================================
// Patient Information Tests
// =============================================================================

TEST_F(BarHandlerTest, ExtractPatientFromBar) {
    auto msg = parse_bar(bar_samples::BAR_P01_ADD_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    auto pid = msg.value().segment("PID");
    ASSERT_TRUE(pid != nullptr);

    // Patient ID
    EXPECT_TRUE(pid->field_value(3).find("12345") != std::string::npos);
    // Patient Address
    EXPECT_TRUE(pid->field_value(11).find("MAIN ST") != std::string::npos);
}

// =============================================================================
// Visit Information Tests
// =============================================================================

TEST_F(BarHandlerTest, ExtractVisitInfo) {
    auto msg = parse_bar(bar_samples::BAR_P01_ADD_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    auto pv1 = msg.value().segment("PV1");
    ASSERT_TRUE(pv1 != nullptr);

    // PV1-2 is Patient Class (I = Inpatient)
    EXPECT_EQ(pv1->field_value(2), "I");
    // PV1-3 is Assigned Patient Location
    EXPECT_TRUE(pv1->field_value(3).find("WARD") != std::string::npos);
}

TEST_F(BarHandlerTest, OutpatientVisit) {
    auto msg = parse_bar(bar_samples::BAR_P10_APC);
    ASSERT_TRUE(msg.is_ok());

    auto pv1 = msg.value().segment("PV1");
    ASSERT_TRUE(pv1 != nullptr);

    // PV1-2 is Patient Class (O = Outpatient)
    EXPECT_EQ(pv1->field_value(2), "O");
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(BarHandlerTest, MissingPv1Segment) {
    std::string invalid_bar =
        "MSH|^~\\&|BILLING|HOSPITAL|HIS|HOSPITAL|20240115160000||BAR^P01|MSG001|P|2.4\r"
        "EVN|P01|20240115160000\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r";

    auto msg = parse_bar(invalid_bar);
    ASSERT_TRUE(msg.is_ok());

    auto pv1 = msg.value().segment("PV1");
    EXPECT_TRUE(pv1 == nullptr);
}

TEST_F(BarHandlerTest, EmptyAccountNumber) {
    std::string bar_no_account =
        "MSH|^~\\&|BILLING|HOSPITAL|HIS|HOSPITAL|20240115160000||BAR^P01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "PV1|1|I|WARD^101^A\r";

    auto msg = parse_bar(bar_no_account);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_TRUE(extract_account_number(msg.value()).empty());
}

// =============================================================================
// ACK Response Tests
// =============================================================================

TEST_F(BarHandlerTest, BuildAckForBar) {
    auto msg = parse_bar(bar_samples::BAR_P01_ADD_ACCOUNT);
    ASSERT_TRUE(msg.is_ok());

    auto ack = hl7_builder::create_ack(msg.value(), ack_code::AA, "Account created successfully");

    // ack is hl7_message directly, no has_value check needed
    EXPECT_STREQ(to_string(ack.type()), "ACK");

    auto msa = ack.segment("MSA");
    ASSERT_TRUE(msa != nullptr);
    EXPECT_EQ(msa->field_value(1), "AA");
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
