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
    "RXE|1^^^20240115170000^^E|00069015001^AMOXICILLIN 500MG^NDC|500|MG|CAP^Capsule|ORAL^Oral||||30|CAP|3|1|SMITH^ROBERT^MD\r"
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
    "RXE|1^^^20240115180000^^E|00409490101^VANCOMYCIN 1GM^NDC|1|GM|VIAL^Vial|IV^Intravenous||||1|DOSE|Q12H|1|JONES^MARY^MD\r"
    "RXR|IV^Intravenous^HL70162\r"
    "TQ1|1||Q12H^Every 12 hours^HL70335|20240115180000|20240120180000\r";

/**
 * @brief Sample RDE^O11 with multiple medications
 */
constexpr std::string_view RDE_O11_MULTIPLE_MEDS =
    "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115190000||RDE^O11^RDE_O11|MSG003|P|2.5.1\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "ORC|NW|ORD003^HIS|RX003^PHARMACY||E\r"
    "RXE|1^^^20240115190000^^E|00071015525^LISINOPRIL 10MG^NDC|10|MG|TAB^Tablet|PO^Oral||||30|TAB|1|1\r"
    "RXR|PO^Oral^HL70162\r"
    "ORC|NW|ORD004^HIS|RX004^PHARMACY||E\r"
    "RXE|2^^^20240115190000^^E|00378180110^METFORMIN 500MG^NDC|500|MG|TAB^Tablet|PO^Oral||||60|TAB|2|1\r"
    "RXR|PO^Oral^HL70162\r";

/**
 * @brief Sample RDE^O25 (Pharmacy Refill Authorization) message
 */
constexpr std::string_view RDE_O25_REFILL =
    "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115200000||RDE^O25|MSG004|P|2.5.1\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "ORC|RF|ORD001^HIS|RX001^PHARMACY||E\r"
    "RXE|1^^^20240115200000^^E|00069015001^AMOXICILLIN 500MG^NDC|500|MG|CAP^Capsule|ORAL^Oral||||30|CAP|3|1|||2|1\r"
    "RXR|PO^Oral^HL70162\r";

/**
 * @brief Sample RDE message with allergy information
 */
constexpr std::string_view RDE_WITH_ALLERGY =
    "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115210000||RDE^O11^RDE_O11|MSG005|P|2.5.1\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "AL1|1|DA|00006074321^PENICILLIN^NDC|SV^Severe|ANAPHYLAXIS\r"
    "ORC|NW|ORD005^HIS|RX005^PHARMACY||E\r"
    "RXE|1^^^20240115210000^^E|00093311756^AZITHROMYCIN 250MG^NDC|250|MG|TAB^Tablet|PO^Oral||||6|TAB|1|1\r"
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

    Result<hl7_message> parse_rde(std::string_view raw) {
        return parser_->parse(raw);
    }

    // Extract medication code from RXE
    std::string extract_medication_code(const hl7_message& msg) {
        auto rxe = msg.segment("RXE");
        if (!rxe) return "";
        return std::string(rxe->field_value(2));
    }

    // Extract order control from ORC
    std::string extract_order_control(const hl7_message& msg) {
        auto orc = msg.segment("ORC");
        if (!orc) return "";
        return std::string(orc->field_value(1));
    }
};

// =============================================================================
// RDE Message Parsing Tests
// =============================================================================

TEST_F(RdeHandlerTest, ParseRdeO11NewOrder) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "RDE");
    EXPECT_EQ(msg.value().trigger_event(), "O11");
    EXPECT_EQ(extract_order_control(msg.value()), "NW");
}

TEST_F(RdeHandlerTest, ParseRdeO11IvOrder) {
    auto msg = parse_rde(rde_samples::RDE_O11_IV_ORDER);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "RDE");

    auto rxr = msg.value().segment("RXR");
    ASSERT_TRUE(rxr != nullptr);
    EXPECT_TRUE(rxr->field_value(1).find("IV") != std::string::npos);
}

TEST_F(RdeHandlerTest, ParseRdeMultipleMeds) {
    auto msg = parse_rde(rde_samples::RDE_O11_MULTIPLE_MEDS);
    ASSERT_TRUE(msg.is_ok());

    auto orc_segments = msg.value().segments("ORC");
    EXPECT_EQ(orc_segments.size(), 2);

    auto rxe_segments = msg.value().segments("RXE");
    EXPECT_EQ(rxe_segments.size(), 2);
}

TEST_F(RdeHandlerTest, ParseRdeO25Refill) {
    auto msg = parse_rde(rde_samples::RDE_O25_REFILL);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "RDE");
    EXPECT_EQ(msg.value().trigger_event(), "O25");
    EXPECT_EQ(extract_order_control(msg.value()), "RF");
}

TEST_F(RdeHandlerTest, ParseRdeDiscontinue) {
    auto msg = parse_rde(rde_samples::RDE_DISCONTINUE);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_EQ(extract_order_control(msg.value()), "DC");
}

// =============================================================================
// Medication Information Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractMedicationCode) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.is_ok());

    auto rxe = msg.value().segment("RXE");
    ASSERT_TRUE(rxe != nullptr);
    // RXE-2 component 2 contains medication name
    auto med_name = rxe->field(2).component(2).value();
    EXPECT_TRUE(med_name.find("AMOXICILLIN") != std::string::npos);
}

TEST_F(RdeHandlerTest, ExtractDosageInfo) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.is_ok());

    auto rxe = msg.value().segment("RXE");
    ASSERT_TRUE(rxe != nullptr);

    // RXE-3 is Give Amount - Minimum
    EXPECT_EQ(rxe->field_value(3), "500");
    // RXE-4 is Give Units
    EXPECT_EQ(rxe->field_value(4), "MG");
    // RXE-5 is Give Dosage Form
    EXPECT_TRUE(rxe->field_value(5).find("CAP") != std::string::npos);
}

TEST_F(RdeHandlerTest, ExtractRouteOfAdministration) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.is_ok());

    auto rxr = msg.value().segment("RXR");
    ASSERT_TRUE(rxr != nullptr);

    EXPECT_TRUE(rxr->field_value(1).find("PO") != std::string::npos ||
                rxr->field_value(1).find("Oral") != std::string::npos);
}

TEST_F(RdeHandlerTest, IvRouteOfAdministration) {
    auto msg = parse_rde(rde_samples::RDE_O11_IV_ORDER);
    ASSERT_TRUE(msg.is_ok());

    auto rxr = msg.value().segment("RXR");
    ASSERT_TRUE(rxr != nullptr);

    EXPECT_TRUE(rxr->field_value(1).find("IV") != std::string::npos);
}

// =============================================================================
// Timing/Quantity Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractTimingQuantity) {
    auto msg = parse_rde(rde_samples::RDE_O11_IV_ORDER);
    ASSERT_TRUE(msg.is_ok());

    auto tq1 = msg.value().segment("TQ1");
    ASSERT_TRUE(tq1 != nullptr);

    // TQ1-3 is Repeat Pattern (Q12H)
    EXPECT_TRUE(tq1->field_value(3).find("Q12H") != std::string::npos);
}

TEST_F(RdeHandlerTest, ExtractDispenseQuantity) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.is_ok());

    auto rxe = msg.value().segment("RXE");
    ASSERT_TRUE(rxe != nullptr);

    // RXE-10 is Dispense Amount
    EXPECT_EQ(rxe->field_value(10), "30");
    // RXE-11 is Dispense Units
    EXPECT_EQ(rxe->field_value(11), "CAP");
}

// =============================================================================
// Allergy Information Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractAllergyInfo) {
    auto msg = parse_rde(rde_samples::RDE_WITH_ALLERGY);
    ASSERT_TRUE(msg.is_ok());

    auto al1 = msg.value().segment("AL1");
    ASSERT_TRUE(al1 != nullptr);

    // AL1-2 is Allergen Type (DA = Drug Allergy)
    EXPECT_EQ(al1->field_value(2), "DA");
    // AL1-3 component 2 contains allergen name
    auto allergen = al1->field(3).component(2).value();
    EXPECT_TRUE(allergen.find("PENICILLIN") != std::string::npos);
    // AL1-4 is Allergy Severity (first component is code)
    EXPECT_TRUE(al1->field_value(4).find("SV") != std::string::npos);
}

// =============================================================================
// Component Information Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractComponentInfo) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.is_ok());

    auto rxc = msg.value().segment("RXC");
    ASSERT_TRUE(rxc != nullptr);

    // RXC-1 is Component Type (B = Base)
    EXPECT_EQ(rxc->field_value(1), "B");
    // RXC-2 component 2 contains component name
    auto component = rxc->field(2).component(2).value();
    EXPECT_TRUE(component.find("AMOXICILLIN") != std::string::npos);
}

// =============================================================================
// Prescriber Information Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractPrescriberInfo) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.is_ok());

    auto rxe = msg.value().segment("RXE");
    ASSERT_TRUE(rxe != nullptr);

    // RXE-13 is Ordering Provider
    EXPECT_TRUE(rxe->field_value(14).find("SMITH") != std::string::npos);
}

// =============================================================================
// Patient Information Tests
// =============================================================================

TEST_F(RdeHandlerTest, ExtractPatientFromRde) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.is_ok());

    auto pid = msg.value().segment("PID");
    ASSERT_TRUE(pid != nullptr);

    EXPECT_TRUE(pid->field_value(3).find("12345") != std::string::npos);
    EXPECT_TRUE(pid->field_value(5).find("DOE") != std::string::npos);
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
    ASSERT_TRUE(msg.is_ok());

    auto rxe = msg.value().segment("RXE");
    EXPECT_TRUE(rxe == nullptr);
}

TEST_F(RdeHandlerTest, EmptyMedicationCode) {
    std::string rde_no_med =
        "MSH|^~\\&|HIS|HOSPITAL|PHARMACY|HOSPITAL|20240115170000||RDE^O11|MSG001|P|2.5.1\r"
        "ORC|NW|ORD001^HIS|RX001^PHARMACY||E\r"
        "RXE|1||||\r";

    auto msg = parse_rde(rde_no_med);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_TRUE(extract_medication_code(msg.value()).empty());
}

// =============================================================================
// ACK Response Tests
// =============================================================================

TEST_F(RdeHandlerTest, BuildAckForRde) {
    auto msg = parse_rde(rde_samples::RDE_O11_NEW_ORDER);
    ASSERT_TRUE(msg.is_ok());

    auto ack = hl7_builder::create_ack(msg.value(), ack_code::AA, "Order received");

    // ack is hl7_message directly, no has_value check needed
    EXPECT_STREQ(to_string(ack.type()), "ACK");
}

TEST_F(RdeHandlerTest, BuildAckForInvalidOrder) {
    auto msg = parse_rde(rde_samples::RDE_WITH_ALLERGY);
    ASSERT_TRUE(msg.is_ok());

    auto ack = hl7_builder::create_ack(msg.value(), ack_code::AR, "Drug allergy conflict detected");

    // ack is hl7_message directly, no has_value check needed
    auto msa = ack.segment("MSA");
    ASSERT_TRUE(msa != nullptr);
    EXPECT_EQ(msa->field_value(1), "AR");
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
