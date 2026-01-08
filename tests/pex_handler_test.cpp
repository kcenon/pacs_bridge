/**
 * @file pex_handler_test.cpp
 * @brief Unit tests for PEX (Product Experience) message handler
 *
 * Tests for PEX message parsing, adverse event reporting,
 * and product experience data extraction.
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
// Sample PEX Messages
// =============================================================================

namespace pex_samples {

/**
 * @brief Sample PEX^P07 (Unsolicited Initial Individual Product Experience Report)
 */
constexpr std::string_view PEX_P07_INITIAL =
    "MSH|^~\\&|SAFETY|HOSPITAL|FDA|GOVERNMENT|20240115100000||PEX^P07|MSG001|P|2.5.1\r"
    "EVN|P07|20240115100000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M|||123 MAIN ST^^CITY^ST^12345\r"
    "PV1|1|I|WARD^101^A||||SMITH^ROBERT^MD\r"
    "PES|HOSPITAL|SMITH^ROBERT^MD|20240115100000|20240110|C^Confirmed\r"
    "PEO|1|NAUSEA|20240110|20240111|M^Moderate|I^Initial\r"
    "PCR|1|00069015001^AMOXICILLIN 500MG^NDC|500MG|TID|PO^Oral|20240108|20240110|D^Drug|P^Probable\r"
    "RXE|1||00069015001^AMOXICILLIN 500MG^NDC|500|MG|CAP^Capsule\r";

/**
 * @brief Sample PEX^P08 (Unsolicited Update Individual Product Experience Report)
 */
constexpr std::string_view PEX_P08_UPDATE =
    "MSH|^~\\&|SAFETY|HOSPITAL|FDA|GOVERNMENT|20240115110000||PEX^P08|MSG002|P|2.5.1\r"
    "EVN|P08|20240115110000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "PES|HOSPITAL|SMITH^ROBERT^MD|20240115110000|20240110|C^Confirmed\r"
    "PEO|1|NAUSEA|20240110|20240112|R^Resolved|F^Follow-up\r"
    "PCR|1|00069015001^AMOXICILLIN 500MG^NDC|500MG|TID|PO^Oral|20240108|20240110|D^Drug|P^Probable\r";

/**
 * @brief Sample PEX with serious adverse event
 */
constexpr std::string_view PEX_SERIOUS_EVENT =
    "MSH|^~\\&|SAFETY|HOSPITAL|FDA|GOVERNMENT|20240115120000||PEX^P07|MSG003|P|2.5.1\r"
    "EVN|P07|20240115120000\r"
    "PID|1||54321^^^HOSPITAL^MR||SMITH^JANE||19750620|F\r"
    "PV1|1|E|ER^101^A||||JONES^MARY^MD\r"
    "PES|HOSPITAL|JONES^MARY^MD|20240115120000|20240114|C^Confirmed|Y^Yes\r"
    "PEO|1|ANAPHYLAXIS|20240114|20240114|S^Severe|I^Initial|Y^Life-Threatening\r"
    "PCR|1|00006074321^PENICILLIN^NDC|250MG|QID|IV^Intravenous|20240114|20240114|D^Drug|D^Definite\r"
    "RXE|1||00006074321^PENICILLIN^NDC|250|MG|VIAL^Vial\r"
    "NK1|1|SMITH^JOHN||555-123-4567||EC^Emergency Contact\r";

/**
 * @brief Sample PEX with device malfunction
 */
constexpr std::string_view PEX_DEVICE_EVENT =
    "MSH|^~\\&|SAFETY|HOSPITAL|FDA|GOVERNMENT|20240115130000||PEX^P07|MSG004|P|2.5.1\r"
    "EVN|P07|20240115130000\r"
    "PID|1||67890^^^HOSPITAL^MR||WILSON^BOB||19600101|M\r"
    "PES|HOSPITAL|JONES^MARY^MD|20240115130000|20240113|C^Confirmed\r"
    "PEO|1|DEVICE MALFUNCTION|20240113|20240113|L^Low|I^Initial\r"
    "PCR|1|DEV001^INFUSION PUMP MODEL X^UDI||N/A||20240101|20240113|M^Medical Device|P^Probable\r"
    "PSH|1|INFUSION PUMP MODEL X|MANUFACTURER_X|LOT123|SN456789|2023\r";

/**
 * @brief Sample PEX with multiple products
 */
constexpr std::string_view PEX_MULTI_PRODUCT =
    "MSH|^~\\&|SAFETY|HOSPITAL|FDA|GOVERNMENT|20240115140000||PEX^P07|MSG005|P|2.5.1\r"
    "EVN|P07|20240115140000\r"
    "PID|1||11111^^^HOSPITAL^MR||BROWN^ALICE||19850301|F\r"
    "PES|HOSPITAL|SMITH^ROBERT^MD|20240115140000|20240112|C^Confirmed\r"
    "PEO|1|HEPATOTOXICITY|20240112|20240115|S^Severe|I^Initial\r"
    "PCR|1|00378180110^METFORMIN 500MG^NDC|500MG|BID|PO^Oral|20231201|20240112|D^Drug|P^Probable\r"
    "PCR|2|00071015525^LISINOPRIL 10MG^NDC|10MG|QD|PO^Oral|20231215|20240112|D^Drug|S^Suspect\r"
    "PCR|3|00456123456^ATORVASTATIN 20MG^NDC|20MG|QD|PO^Oral|20231001|20240112|D^Drug|S^Suspect\r";

/**
 * @brief Sample PEX with observation results
 */
constexpr std::string_view PEX_WITH_RESULTS =
    "MSH|^~\\&|SAFETY|HOSPITAL|FDA|GOVERNMENT|20240115150000||PEX^P07|MSG006|P|2.5.1\r"
    "EVN|P07|20240115150000\r"
    "PID|1||22222^^^HOSPITAL^MR||GREEN^TOM||19700815|M\r"
    "PES|HOSPITAL|SMITH^ROBERT^MD|20240115150000|20240113|C^Confirmed\r"
    "PEO|1|THROMBOCYTOPENIA|20240113|20240115|M^Moderate|I^Initial\r"
    "PCR|1|00012345678^HEPARIN^NDC|5000UNITS|Q12H|IV^Intravenous|20240110|20240113|D^Drug|P^Probable\r"
    "OBX|1|NM|PLT^Platelet Count^L|1|45|10E3/uL|150-400|L|||F|20240113\r"
    "OBX|2|NM|PLT^Platelet Count^L|2|120|10E3/uL|150-400|L|||F|20240115\r";

}  // namespace pex_samples

// =============================================================================
// Test Fixture
// =============================================================================

class PexHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;

    Result<hl7_message> parse_pex(std::string_view raw) {
        return parser_->parse(raw);
    }

    // Extract event description from PEO
    std::string extract_event_description(const hl7_message& msg) {
        auto peo = msg.segment("PEO");
        if (!peo) return "";
        return std::string(peo->field_value(2));
    }

    // Extract product code from PCR
    std::string extract_product_code(const hl7_message& msg) {
        auto pcr = msg.segment("PCR");
        if (!pcr) return "";
        return std::string(pcr->field_value(2));
    }

    // Extract causality assessment from PCR
    std::string extract_causality(const hl7_message& msg) {
        auto pcr = msg.segment("PCR");
        if (!pcr) return "";
        return std::string(pcr->field_value(9));  // PCR-9 is Causality Assessment
    }
};

// =============================================================================
// PEX Message Parsing Tests
// =============================================================================

TEST_F(PexHandlerTest, ParsePexP07Initial) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "PEX");
    EXPECT_EQ(msg.value().trigger_event(), "P07");
}

TEST_F(PexHandlerTest, ParsePexP08Update) {
    auto msg = parse_pex(pex_samples::PEX_P08_UPDATE);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "PEX");
    EXPECT_EQ(msg.value().trigger_event(), "P08");
}

TEST_F(PexHandlerTest, ParsePexSeriousEvent) {
    auto msg = parse_pex(pex_samples::PEX_SERIOUS_EVENT);
    ASSERT_TRUE(msg.is_ok());

    EXPECT_STREQ(to_string(msg.value().type()), "PEX");
}

TEST_F(PexHandlerTest, ParsePexDeviceEvent) {
    auto msg = parse_pex(pex_samples::PEX_DEVICE_EVENT);
    ASSERT_TRUE(msg.is_ok());

    // Should have PSH segment for device info
    auto psh = msg.value().segment("PSH");
    ASSERT_TRUE(psh != nullptr);
}

TEST_F(PexHandlerTest, ParsePexMultiProduct) {
    auto msg = parse_pex(pex_samples::PEX_MULTI_PRODUCT);
    ASSERT_TRUE(msg.is_ok());

    // Should have multiple PCR segments
    auto pcr_segments = msg.value().segments("PCR");
    EXPECT_EQ(pcr_segments.size(), 3);
}

// =============================================================================
// PES Segment Tests (Product Experience Sender)
// =============================================================================

TEST_F(PexHandlerTest, ExtractSenderInfo) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    auto pes = msg.value().segment("PES");
    ASSERT_TRUE(pes != nullptr);

    // PES-1 is Sender Organization Name
    EXPECT_EQ(pes->field_value(1), "HOSPITAL");
    // PES-2 is Sender Individual Name
    EXPECT_TRUE(pes->field_value(2).find("SMITH") != std::string::npos);
}

TEST_F(PexHandlerTest, ExtractEventDateTime) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    auto pes = msg.value().segment("PES");
    ASSERT_TRUE(pes != nullptr);

    // PES-4 is Event Date/Time
    EXPECT_EQ(pes->field_value(4), "20240110");
}

TEST_F(PexHandlerTest, ExtractEventConfirmation) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    auto pes = msg.value().segment("PES");
    ASSERT_TRUE(pes != nullptr);

    // PES-5 is Event Qualification (C = Confirmed)
    EXPECT_TRUE(pes->field_value(5).find("C") != std::string::npos);
}

// =============================================================================
// PEO Segment Tests (Product Experience Observation)
// =============================================================================

TEST_F(PexHandlerTest, ExtractEventDescription) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    std::string event_desc = extract_event_description(msg.value());
    EXPECT_EQ(event_desc, "NAUSEA");
}

TEST_F(PexHandlerTest, ExtractEventSeverity) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    auto peo = msg.value().segment("PEO");
    ASSERT_TRUE(peo != nullptr);

    // PEO-5 is Event Severity (M = Moderate)
    EXPECT_TRUE(peo->field_value(5).find("M") != std::string::npos);
}

TEST_F(PexHandlerTest, ExtractSeriousEvent) {
    auto msg = parse_pex(pex_samples::PEX_SERIOUS_EVENT);
    ASSERT_TRUE(msg.is_ok());

    auto peo = msg.value().segment("PEO");
    ASSERT_TRUE(peo != nullptr);

    // PEO-5 should indicate severe
    EXPECT_TRUE(peo->field_value(5).find("S") != std::string::npos);
    // PEO-7 should indicate life-threatening
    EXPECT_TRUE(peo->field_value(7).find("Y") != std::string::npos);
}

TEST_F(PexHandlerTest, ExtractEventOutcome) {
    auto msg = parse_pex(pex_samples::PEX_P08_UPDATE);
    ASSERT_TRUE(msg.is_ok());

    auto peo = msg.value().segment("PEO");
    ASSERT_TRUE(peo != nullptr);

    // PEO-5 should indicate resolved
    EXPECT_TRUE(peo->field_value(5).find("R") != std::string::npos);
}

// =============================================================================
// PCR Segment Tests (Possible Causal Relationship)
// =============================================================================

TEST_F(PexHandlerTest, ExtractProductCode) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    auto pcr = msg.value().segment("PCR");
    ASSERT_TRUE(pcr != nullptr);

    // PCR-2 is product code (compound field: NDC^Name^Coding System)
    // Component 1 is NDC code, component 2 is product name
    std::string ndc_code = std::string(pcr->field_value(2));  // First component only
    EXPECT_TRUE(ndc_code.find("00069015001") != std::string::npos);

    auto product_name = pcr->field(2).component(2).value();
    EXPECT_TRUE(product_name.find("AMOXICILLIN") != std::string::npos);
}

TEST_F(PexHandlerTest, ExtractProductDosage) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    auto pcr = msg.value().segment("PCR");
    ASSERT_TRUE(pcr != nullptr);

    // PCR-3 is Dose
    EXPECT_EQ(pcr->field_value(3), "500MG");
    // PCR-4 is Dose Frequency
    EXPECT_EQ(pcr->field_value(4), "TID");
}

TEST_F(PexHandlerTest, ExtractRouteOfAdmin) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    auto pcr = msg.value().segment("PCR");
    ASSERT_TRUE(pcr != nullptr);

    // PCR-5 is Route of Administration
    EXPECT_TRUE(pcr->field_value(5).find("PO") != std::string::npos ||
                pcr->field_value(5).find("Oral") != std::string::npos);
}

TEST_F(PexHandlerTest, ExtractCausality) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    std::string causality = extract_causality(msg.value());
    // P = Probable
    EXPECT_TRUE(causality.find("P") != std::string::npos);
}

TEST_F(PexHandlerTest, ExtractDefiniteCausality) {
    auto msg = parse_pex(pex_samples::PEX_SERIOUS_EVENT);
    ASSERT_TRUE(msg.is_ok());

    std::string causality = extract_causality(msg.value());
    // D = Definite
    EXPECT_TRUE(causality.find("D") != std::string::npos);
}

// =============================================================================
// Device Information Tests
// =============================================================================

TEST_F(PexHandlerTest, ExtractDeviceInfo) {
    auto msg = parse_pex(pex_samples::PEX_DEVICE_EVENT);
    ASSERT_TRUE(msg.is_ok());

    auto psh = msg.value().segment("PSH");
    ASSERT_TRUE(psh != nullptr);

    // PSH-2 is Product Name
    EXPECT_TRUE(psh->field_value(2).find("INFUSION PUMP") != std::string::npos);
    // PSH-3 is Manufacturer Name
    EXPECT_TRUE(psh->field_value(3).find("MANUFACTURER") != std::string::npos);
}

TEST_F(PexHandlerTest, ExtractDeviceLotSerial) {
    auto msg = parse_pex(pex_samples::PEX_DEVICE_EVENT);
    ASSERT_TRUE(msg.is_ok());

    auto psh = msg.value().segment("PSH");
    ASSERT_TRUE(psh != nullptr);

    // PSH-4 is Lot Number
    EXPECT_EQ(psh->field_value(4), "LOT123");
    // PSH-5 is Serial Number
    EXPECT_EQ(psh->field_value(5), "SN456789");
}

// =============================================================================
// Observation Results Tests
// =============================================================================

TEST_F(PexHandlerTest, ExtractLabResults) {
    auto msg = parse_pex(pex_samples::PEX_WITH_RESULTS);
    ASSERT_TRUE(msg.is_ok());

    auto obx_segments = msg.value().segments("OBX");
    EXPECT_EQ(obx_segments.size(), 2);

    // First OBX should show low platelet count
    EXPECT_TRUE(obx_segments[0]->field_value(3).find("PLT") != std::string::npos);
    EXPECT_EQ(obx_segments[0]->field_value(5), "45");
}

// =============================================================================
// Patient Information Tests
// =============================================================================

TEST_F(PexHandlerTest, ExtractPatientFromPex) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    auto pid = msg.value().segment("PID");
    ASSERT_TRUE(pid != nullptr);

    EXPECT_TRUE(pid->field_value(3).find("12345") != std::string::npos);
    EXPECT_TRUE(pid->field_value(5).find("DOE") != std::string::npos);
}

TEST_F(PexHandlerTest, ExtractEmergencyContact) {
    auto msg = parse_pex(pex_samples::PEX_SERIOUS_EVENT);
    ASSERT_TRUE(msg.is_ok());

    auto nk1 = msg.value().segment("NK1");
    ASSERT_TRUE(nk1 != nullptr);

    // NK1-2 is Contact Name
    EXPECT_TRUE(nk1->field_value(2).find("SMITH") != std::string::npos);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(PexHandlerTest, MissingPesSegment) {
    std::string invalid_pex =
        "MSH|^~\\&|SAFETY|HOSPITAL|FDA|GOVERNMENT|20240115100000||PEX^P07|MSG001|P|2.5.1\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "PEO|1|NAUSEA|20240110|20240111|M^Moderate\r";

    auto msg = parse_pex(invalid_pex);
    ASSERT_TRUE(msg.is_ok());

    auto pes = msg.value().segment("PES");
    EXPECT_TRUE(pes == nullptr);
}

TEST_F(PexHandlerTest, MissingPeoSegment) {
    std::string pex_no_event =
        "MSH|^~\\&|SAFETY|HOSPITAL|FDA|GOVERNMENT|20240115100000||PEX^P07|MSG001|P|2.5.1\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
        "PES|HOSPITAL|SMITH^ROBERT^MD|20240115100000|20240110|C^Confirmed\r";

    auto msg = parse_pex(pex_no_event);
    ASSERT_TRUE(msg.is_ok());

    auto peo = msg.value().segment("PEO");
    EXPECT_TRUE(peo == nullptr);
}

// =============================================================================
// ACK Response Tests
// =============================================================================

TEST_F(PexHandlerTest, BuildAckForPex) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    auto ack = hl7_builder::create_ack(msg.value(), ack_code::AA, "Product experience report received");

    // ack is hl7_message directly, no has_value check needed
    EXPECT_STREQ(to_string(ack.type()), "ACK");
}

TEST_F(PexHandlerTest, BuildErrorAckForPex) {
    auto msg = parse_pex(pex_samples::PEX_P07_INITIAL);
    ASSERT_TRUE(msg.is_ok());

    auto ack = hl7_builder::create_ack(msg.value(), ack_code::AE, "Invalid event report");

    // ack is hl7_message directly, no has_value check needed
    auto msa = ack.segment("MSA");
    ASSERT_TRUE(msa != nullptr);
    EXPECT_EQ(msa->field_value(1), "AE");
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
