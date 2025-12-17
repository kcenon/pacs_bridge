/**
 * @file rde_handler_test.cpp
 * @brief Unit tests for RDE (Pharmacy/Treatment Encoded Order) message handler
 *
 * Tests for RDE message parsing, pharmacy order handling,
 * and medication information extraction.
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
// Sample RDE Messages
// =============================================================================

namespace rde_samples {

/**
 * @brief Sample RDE^O11 (Pharmacy Order) message
 */
constexpr std::string_view RDE_O11_NEW_ORDER =
    "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115170000||RDE^O11^RDE_O11|MSG001|P|2.5.1\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M|||123 MAIN ST^^CITY^ST^12345\r"
    "PV1|1|I|WARD^101^A||||SMITH^ROBERT^MD\r"
    "ORC|NW|ORD001^HIS|RX001^PHARMACY||E|||^^^20240115170000^^R||20240115170000|NURSE^MARY^RN|||WARD\r"
    "RXE|1^^^20240115170000^^E|00069015001^AMOXICILLIN 500MG^NDC|500|MG|CAP^Capsule||ORAL^Oral|30|CAP|3|1||||||||||SMITH^ROBERT^MD\r"
    "RXR|PO^Oral^HL70162\r"
    "RXC|B|00069015001^AMOXICILLIN^NDC|500|MG\r";

/**
 * @brief Sample RDE^O11 with IV medication
 */
constexpr std::string_view RDE_O11_IV_ORDER =
    "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115180000||RDE^O11^RDE_O11|MSG002|P|2.5.1\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "PV1|1|I|ICU^201^A\r"
    "ORC|NW|ORD002^HIS|RX002^PHARMACY||E\r"
    "RXE|1^^^20240115180000^^E|00409490101^VANCOMYCIN 1GM^NDC|1|GM|VIAL^Vial||IV^Intravenous|1|DOSE|Q12H|1||||||||||JONES^MARY^MD\r"
    "RXR|IV^Intravenous^HL70162\r"
    "TQ1|1||Q12H^Every 12 hours^HL70335|20240115180000|20240120180000\r";

/**
 * @brief Sample RDE^O11 with multiple medications
 */
constexpr std::string_view RDE_O11_MULTIPLE_MEDS =
    "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115190000||RDE^O11^RDE_O11|MSG003|P|2.5.1\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "ORC|NW|ORD003^HIS|RX003^PHARMACY||E\r"
    "RXE|1^^^20240115190000^^E|00071015525^LISINOPRIL 10MG^NDC|10|MG|TAB^Tablet||PO^Oral|30|TAB|1|1\r"
    "RXR|PO^Oral^HL70162\r"
    "ORC|NW|ORD004^HIS|RX004^PHARMACY||E\r"
    "RXE|2^^^20240115190000^^E|00378180110^METFORMIN 500MG^NDC|500|MG|TAB^Tablet||PO^Oral|60|TAB|2|1\r"
    "RXR|PO^Oral^HL70162\r";

/**
 * @brief Sample RDE^O25 (Pharmacy Refill Authorization) message
 */
constexpr std::string_view RDE_O25_REFILL =
    "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115200000||RDE^O25|MSG004|P|2.5.1\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "ORC|RF|ORD001^HIS|RX001^PHARMACY||E\r"
    "RXE|1^^^20240115200000^^E|00069015001^AMOXICILLIN 500MG^NDC|500|MG|CAP^Capsule||ORAL^Oral|30|CAP|3|1|||||2|1\r"
    "RXR|PO^Oral^HL70162\r";

/**
 * @brief Sample RDE message with allergy information
 */
constexpr std::string_view RDE_WITH_ALLERGY =
    "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115210000||RDE^O11^RDE_O11|MSG005|P|2.5.1\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "AL1|1|DA|00006074321^PENICILLIN^NDC|SV^Severe|ANAPHYLAXIS\r"
    "ORC|NW|ORD005^HIS|RX005^PHARMACY||E\r"
    "RXE|1^^^20240115210000^^E|00093311756^AZITHROMYCIN 250MG^NDC|250|MG|TAB^Tablet||PO^Oral|6|TAB|1|1\r"
    "RXR|PO^Oral^HL70162\r";

/**
 * @brief Sample RDE with discontinue order
 */
constexpr std::string_view RDE_DISCONTINUE =
    "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115220000||RDE^O11^RDE_O11|MSG006|P|2.5.1\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "ORC|DC|ORD001^HIS|RX001^PHARMACY||DC|||^^^20240115220000\r"
    "RXE|1^^^20240115170000^^E|00069015001^AMOXICILLIN 500MG^NDC|500|MG|CAP^Capsule\r";

}  // namespace rde_samples

// =============================================================================
// Test Fixture
// =============================================================================

class RdeHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;

    std::optional<hl7_message> parse_rde(std::string_view raw) {
        return parser_->parse(std::string(raw));
    }

    // Extract medication code from RXE
    std::string extract_medication_code(const hl7_message& msg) {
        auto rxe = msg.get_segment("RXE");
        if (!rxe) return "";
        return rxe->get_field(2);
    }

    // Extract order control from ORC
    std::string extract_order_control(const hl7_message& msg) {
        auto orc = msg.get_segment("ORC");
        if (!orc) return "";
        return orc->get_field(1);
    }
};

// =============================================================================
// RDE Message Parsing Tests
// =============================================================================

TEST_F(RdeHandlerTest, ParseRdeO11NewOrder) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->get_message_type(), "RDE");
    EXPECT_EQ(msg->get_trigger_event(), "O11");
    EXPECT_EQ(extract_order_control(*msg), "NW");
}

TEST_F(RdeHandlerTest, ParseRdeO11IvOrder) {
    auto msg = parse_rde(rde_samples::RDE_O11_IV_ORDER);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->get_message_type(), "RDE");

    auto rxr = msg->get_segment("RXR");
    ASSERT_TRUE(rxr != nullptr);
    EXPECT_TRUE(rxr->get_field(1).find("IV") != std::string::npos);
}

TEST_F(RdeHandlerTest, ParseRdeMultipleMeds) {
    auto msg = parse_rde(rde_samples::RDE_O11_MULTIPLE_MEDS);
    ASSERT_TRUE(msg.has_value());

    auto orc_segments = msg->get_segments("ORC");
    EXPECT_EQ(orc_segments.size(), 2);

    auto rxe_segments = msg->get_segments("RXE");
    EXPECT_EQ(rxe_segments.size(), 2);
}

TEST_F(RdeHandlerTest, ParseRdeO25Refill) {
    auto msg = parse_rde(rde_samples::RDE_O25_REFILL);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->get_message_type(), "RDE");
    EXPECT_EQ(msg->get_trigger_event(), "O25");
    EXPECT_EQ(extract_order_control(*msg), "RF");
}

TEST_F(RdeHandlerTest, ParseRdeDiscontinue) {
    auto msg = parse_rde(rde_samples::RDE_DISCONTINUE);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(extract_order_control(*msg), "DC");
}

// =============================================================================
// Medication Information Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractMedicationCode) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.has_value());

    std::string med_code = extract_medication_code(*msg);
    EXPECT_TRUE(med_code.find("AMOXICILLIN") != std::string::npos);
}

TEST_F(RdeHandlerTest, ExtractDosageInfo) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.has_value());

    auto rxe = msg->get_segment("RXE");
    ASSERT_TRUE(rxe != nullptr);

    // RXE-3 is Give Amount - Minimum
    EXPECT_EQ(rxe->get_field(3), "500");
    // RXE-4 is Give Units
    EXPECT_EQ(rxe->get_field(4), "MG");
    // RXE-5 is Give Dosage Form
    EXPECT_TRUE(rxe->get_field(5).find("CAP") != std::string::npos);
}

TEST_F(RdeHandlerTest, ExtractRouteOfAdministration) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.has_value());

    auto rxr = msg->get_segment("RXR");
    ASSERT_TRUE(rxr != nullptr);

    EXPECT_TRUE(rxr->get_field(1).find("PO") != std::string::npos ||
                rxr->get_field(1).find("Oral") != std::string::npos);
}

TEST_F(RdeHandlerTest, IvRouteOfAdministration) {
    auto msg = parse_rde(rde_samples::RDE_O11_IV_ORDER);
    ASSERT_TRUE(msg.has_value());

    auto rxr = msg->get_segment("RXR");
    ASSERT_TRUE(rxr != nullptr);

    EXPECT_TRUE(rxr->get_field(1).find("IV") != std::string::npos);
}

// =============================================================================
// Timing/Quantity Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractTimingQuantity) {
    auto msg = parse_rde(rde_samples::RDE_O11_IV_ORDER);
    ASSERT_TRUE(msg.has_value());

    auto tq1 = msg->get_segment("TQ1");
    ASSERT_TRUE(tq1 != nullptr);

    // TQ1-3 is Repeat Pattern (Q12H)
    EXPECT_TRUE(tq1->get_field(3).find("Q12H") != std::string::npos);
}

TEST_F(RdeHandlerTest, ExtractDispenseQuantity) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.has_value());

    auto rxe = msg->get_segment("RXE");
    ASSERT_TRUE(rxe != nullptr);

    // RXE-10 is Dispense Amount
    EXPECT_EQ(rxe->get_field(10), "30");
    // RXE-11 is Dispense Units
    EXPECT_EQ(rxe->get_field(11), "CAP");
}

// =============================================================================
// Allergy Information Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractAllergyInfo) {
    auto msg = parse_rde(rde_samples::RDE_WITH_ALLERGY);
    ASSERT_TRUE(msg.has_value());

    auto al1 = msg->get_segment("AL1");
    ASSERT_TRUE(al1 != nullptr);

    // AL1-2 is Allergen Type (DA = Drug Allergy)
    EXPECT_EQ(al1->get_field(2), "DA");
    // AL1-3 is Allergen Code
    EXPECT_TRUE(al1->get_field(3).find("PENICILLIN") != std::string::npos);
    // AL1-4 is Allergy Severity
    EXPECT_TRUE(al1->get_field(4).find("SV") != std::string::npos);
}

// =============================================================================
// Component Information Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractComponentInfo) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.has_value());

    auto rxc = msg->get_segment("RXC");
    ASSERT_TRUE(rxc != nullptr);

    // RXC-1 is Component Type (B = Base)
    EXPECT_EQ(rxc->get_field(1), "B");
    // RXC-2 is Component Code
    EXPECT_TRUE(rxc->get_field(2).find("AMOXICILLIN") != std::string::npos);
}

// =============================================================================
// Prescriber Information Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractPrescriberInfo) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.has_value());

    auto rxe = msg->get_segment("RXE");
    ASSERT_TRUE(rxe != nullptr);

    // RXE-13 is Ordering Provider
    EXPECT_TRUE(rxe->get_field(14).find("SMITH") != std::string::npos);
}

// =============================================================================
// Patient Information Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractPatientFromRde) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.has_value());

    auto pid = msg->get_segment("PID");
    ASSERT_TRUE(pid != nullptr);

    EXPECT_TRUE(pid->get_field(3).find("12345") != std::string::npos);
    EXPECT_TRUE(pid->get_field(5).find("DOE") != std::string::npos);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(RdeHandlerTest, MissingRxeSegment) {
    std::string invalid_rde =
        "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115170000||RDE^O11|MSG001|P|2.5.1\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "ORC|NW|ORD001^HIS|RX001^PHARMACY||E\r";

    auto msg = parse_rde(invalid_rde);
    ASSERT_TRUE(msg.has_value());

    auto rxe = msg->get_segment("RXE");
    EXPECT_TRUE(rxe == nullptr);
}

TEST_F(RdeHandlerTest, EmptyMedicationCode) {
    std::string rde_no_med =
        "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115170000||RDE^O11|MSG001|P|2.5.1\r"
        "ORC|NW|ORD001^HIS|RX001^PHARMACY||E\r"
        "RXE|1||||\r";

    auto msg = parse_rde(rde_no_med);
    ASSERT_TRUE(msg.has_value());

    EXPECT_TRUE(extract_medication_code(*msg).empty());
}

// =============================================================================
// ACK Response Tests
// =============================================================================

TEST_F(RdeHandlerTest, BuildAckForRde) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.has_value());

    hl7_builder builder;
    auto ack = builder.build_ack(*msg, "AA", "Order received");

    ASSERT_TRUE(ack.has_value());
    EXPECT_EQ(ack->get_message_type(), "ACK");
}

TEST_F(RdeHandlerTest, BuildAckForInvalidOrder) {
    auto msg = parse_rde(rde_samples::RDE_WITH_ALLERGY);
    ASSERT_TRUE(msg.has_value());

    hl7_builder builder;
    auto ack = builder.build_ack(*msg, "AR", "Drug allergy conflict detected");

    ASSERT_TRUE(ack.has_value());
    auto msa = ack->get_segment("MSA");
    ASSERT_TRUE(msa != nullptr);
    EXPECT_EQ(msa->get_field(1), "AR");
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
