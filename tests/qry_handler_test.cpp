/**
 * @file qry_handler_test.cpp
 * @brief Unit tests for QRY (Query) message handler
 *
 * Tests for QRY message parsing, query parameter extraction,
 * and response building.
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
// Sample QRY Messages
// =============================================================================

namespace qry_samples {

/**
 * @brief Sample QRY^A19 (Patient Query) message
 */
constexpr std::string_view QRY_A19_PATIENT =
    "MSH|^~\\&|HIS|HOSPITAL|ADT|HOSPITAL|20240115150000||QRY^A19|MSG001|P|2.4\r"
    "QRD|20240115150000|R|I|QUERY001|||25^RD|12345^DOE^JOHN|DEM\r"
    "QRF|ADT||||PID|PV1\r";

/**
 * @brief Sample QRY^Q01 (Query by Parameter) message
 */
constexpr std::string_view QRY_Q01_PARAMETER =
    "MSH|^~\\&|HIS|HOSPITAL|LAB|HOSPITAL|20240115160000||QRY^Q01|MSG002|P|2.4\r"
    "QRD|20240115160000|R|I|QUERY002|||10^RD||RES\r"
    "QRF|LAB|20240101|20240115||OBX\r";

/**
 * @brief Sample QRY^R02 (Query for Results) message
 */
constexpr std::string_view QRY_R02_RESULTS =
    "MSH|^~\\&|EMR|HOSPITAL|LAB|HOSPITAL|20240115170000||QRY^R02|MSG003|P|2.4\r"
    "QRD|20240115170000|R|I|QUERY003|||50^RD|12345|RES\r"
    "QRF|LAB|20240110|20240115||OBR|OBX\r";

/**
 * @brief Sample QRY^PC4 (Patient Problem Query) message
 */
constexpr std::string_view QRY_PC4_PROBLEM =
    "MSH|^~\\&|EMR|HOSPITAL|PM|HOSPITAL|20240115180000||QRY^PC4|MSG004|P|2.4\r"
    "QRD|20240115180000|R|I|QUERY004|||100^RD|12345|PRB\r"
    "QRF|PM||||PRB|GOL\r";

/**
 * @brief Sample QRY^T12 (Document Query) message
 */
constexpr std::string_view QRY_T12_DOCUMENT =
    "MSH|^~\\&|EMR|HOSPITAL|DOC|HOSPITAL|20240115190000||QRY^T12|MSG005|P|2.5.1\r"
    "QRD|20240115190000|R|I|QUERY005|||20^RD|12345|DOC\r"
    "QRF|DOC|20240101|20240115|HP^History and Physical\r";

/**
 * @brief Sample QRY with date range
 */
constexpr std::string_view QRY_DATE_RANGE =
    "MSH|^~\\&|RAD|RADIOLOGY|PACS|IMAGING|20240115200000||QRY^A19|MSG006|P|2.4\r"
    "QRD|20240115200000|R|I|QUERY006|||25^RD||RAD\r"
    "QRF|RAD|20240101000000|20240115235959||OBR|OBX\r";

/**
 * @brief Sample QRY with multiple criteria
 */
constexpr std::string_view QRY_MULTI_CRITERIA =
    "MSH|^~\\&|HIS|HOSPITAL|ADT|HOSPITAL|20240115210000||QRY^A19|MSG007|P|2.4\r"
    "QRD|20240115210000|R|I|QUERY007|||50^RD||DEM\r"
    "QRF|ADT||||PID|PV1|NK1|IN1\r";

}  // namespace qry_samples

// =============================================================================
// Test Fixture
// =============================================================================

class QryHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;

    std::optional<hl7_message> parse_qry(std::string_view raw) {
        return parser_->parse(std::string(raw));
    }

    // Extract query ID from QRD
    std::string extract_query_id(const hl7_message& msg) {
        auto qrd = msg.get_segment("QRD");
        if (!qrd) return "";
        return qrd->get_field(4);
    }

    // Extract what subject filter from QRD
    std::string extract_subject_filter(const hl7_message& msg) {
        auto qrd = msg.get_segment("QRD");
        if (!qrd) return "";
        return qrd->get_field(8);
    }

    // Extract quantity limited request from QRD
    std::string extract_quantity_limit(const hl7_message& msg) {
        auto qrd = msg.get_segment("QRD");
        if (!qrd) return "";
        return qrd->get_field(7);
    }
};

// =============================================================================
// QRY Message Parsing Tests
// =============================================================================

TEST_F(QryHandlerTest, ParseQryA19Patient) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->get_message_type(), "QRY");
    EXPECT_EQ(msg->get_trigger_event(), "A19");
    EXPECT_EQ(extract_query_id(*msg), "QUERY001");
}

TEST_F(QryHandlerTest, ParseQryQ01Parameter) {
    auto msg = parse_qry(qry_samples::QRY_Q01_PARAMETER);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->get_message_type(), "QRY");
    EXPECT_EQ(msg->get_trigger_event(), "Q01");
}

TEST_F(QryHandlerTest, ParseQryR02Results) {
    auto msg = parse_qry(qry_samples::QRY_R02_RESULTS);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->get_message_type(), "QRY");
    EXPECT_EQ(msg->get_trigger_event(), "R02");
}

TEST_F(QryHandlerTest, ParseQryPC4Problem) {
    auto msg = parse_qry(qry_samples::QRY_PC4_PROBLEM);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->get_message_type(), "QRY");
    EXPECT_EQ(msg->get_trigger_event(), "PC4");
}

TEST_F(QryHandlerTest, ParseQryT12Document) {
    auto msg = parse_qry(qry_samples::QRY_T12_DOCUMENT);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->get_message_type(), "QRY");
    EXPECT_EQ(msg->get_trigger_event(), "T12");
}

// =============================================================================
// QRD Segment Tests
// =============================================================================

TEST_F(QryHandlerTest, ExtractQueryDateTime) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    auto qrd = msg->get_segment("QRD");
    ASSERT_TRUE(qrd != nullptr);

    // QRD-1 is Query Date/Time
    EXPECT_EQ(qrd->get_field(1), "20240115150000");
}

TEST_F(QryHandlerTest, ExtractQueryFormatCode) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    auto qrd = msg->get_segment("QRD");
    ASSERT_TRUE(qrd != nullptr);

    // QRD-2 is Query Format Code (R = Response)
    EXPECT_EQ(qrd->get_field(2), "R");
}

TEST_F(QryHandlerTest, ExtractQueryPriority) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    auto qrd = msg->get_segment("QRD");
    ASSERT_TRUE(qrd != nullptr);

    // QRD-3 is Query Priority (I = Immediate)
    EXPECT_EQ(qrd->get_field(3), "I");
}

TEST_F(QryHandlerTest, ExtractQuantityLimit) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    std::string limit = extract_quantity_limit(*msg);
    // QRD-7 contains quantity (25^RD means 25 records)
    EXPECT_TRUE(limit.find("25") != std::string::npos);
}

TEST_F(QryHandlerTest, ExtractSubjectFilter) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    std::string filter = extract_subject_filter(*msg);
    // QRD-8 contains subject filter (patient ID and name)
    EXPECT_TRUE(filter.find("12345") != std::string::npos);
    EXPECT_TRUE(filter.find("DOE") != std::string::npos);
}

TEST_F(QryHandlerTest, ExtractWhatDataCodeSubject) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    auto qrd = msg->get_segment("QRD");
    ASSERT_TRUE(qrd != nullptr);

    // QRD-9 is What Data Code Subject (DEM = Demographics)
    EXPECT_EQ(qrd->get_field(9), "DEM");
}

// =============================================================================
// QRF Segment Tests
// =============================================================================

TEST_F(QryHandlerTest, ExtractWhereSubjectFilter) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    auto qrf = msg->get_segment("QRF");
    ASSERT_TRUE(qrf != nullptr);

    // QRF-1 is Where Subject Filter
    EXPECT_EQ(qrf->get_field(1), "ADT");
}

TEST_F(QryHandlerTest, ExtractDateRange) {
    auto msg = parse_qry(qry_samples::QRY_DATE_RANGE);
    ASSERT_TRUE(msg.has_value());

    auto qrf = msg->get_segment("QRF");
    ASSERT_TRUE(qrf != nullptr);

    // QRF-2 is When Data Start Date/Time
    EXPECT_EQ(qrf->get_field(2), "20240101000000");
    // QRF-3 is When Data End Date/Time
    EXPECT_EQ(qrf->get_field(3), "20240115235959");
}

TEST_F(QryHandlerTest, ExtractWhatUserQualifier) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    auto qrf = msg->get_segment("QRF");
    ASSERT_TRUE(qrf != nullptr);

    // QRF-5 contains what data codes requested (PID|PV1)
    std::string qualifiers = qrf->get_field(5);
    EXPECT_TRUE(qualifiers.find("PID") != std::string::npos);
}

TEST_F(QryHandlerTest, ExtractMultipleQualifiers) {
    auto msg = parse_qry(qry_samples::QRY_MULTI_CRITERIA);
    ASSERT_TRUE(msg.has_value());

    auto qrf = msg->get_segment("QRF");
    ASSERT_TRUE(qrf != nullptr);

    std::string qualifiers = qrf->get_field(5);
    // Should have multiple segment requests
    EXPECT_TRUE(qualifiers.find("PID") != std::string::npos);
}

// =============================================================================
// Query Response Building Tests
// =============================================================================

TEST_F(QryHandlerTest, BuildQueryResponseAdr) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    // Build ADR (ADT Response) message
    hl7_builder builder;

    // Start with MSH for response
    builder.set_sending_application("ADT")
           .set_sending_facility("HOSPITAL")
           .set_receiving_application("HIS")
           .set_receiving_facility("HOSPITAL")
           .set_message_type("ADR")
           .set_trigger_event("A19");

    auto response = builder.build();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->get_message_type(), "ADR");
}

// =============================================================================
// Special Query Types Tests
// =============================================================================

TEST_F(QryHandlerTest, DocumentQueryWithType) {
    auto msg = parse_qry(qry_samples::QRY_T12_DOCUMENT);
    ASSERT_TRUE(msg.has_value());

    auto qrf = msg->get_segment("QRF");
    ASSERT_TRUE(qrf != nullptr);

    // QRF-4 should contain document type filter
    std::string doc_type = qrf->get_field(4);
    EXPECT_TRUE(doc_type.find("HP") != std::string::npos ||
                doc_type.find("History and Physical") != std::string::npos);
}

TEST_F(QryHandlerTest, ResultsQueryWithDateRange) {
    auto msg = parse_qry(qry_samples::QRY_R02_RESULTS);
    ASSERT_TRUE(msg.has_value());

    auto qrf = msg->get_segment("QRF");
    ASSERT_TRUE(qrf != nullptr);

    // Should have start and end dates
    EXPECT_FALSE(qrf->get_field(2).empty());
    EXPECT_FALSE(qrf->get_field(3).empty());
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(QryHandlerTest, MissingQrdSegment) {
    std::string invalid_qry =
        "MSH|^~\\&|HIS|HOSPITAL|ADT|HOSPITAL|20240115150000||QRY^A19|MSG001|P|2.4\r";

    auto msg = parse_qry(invalid_qry);
    ASSERT_TRUE(msg.has_value());

    auto qrd = msg->get_segment("QRD");
    EXPECT_TRUE(qrd == nullptr);
}

TEST_F(QryHandlerTest, EmptyQueryId) {
    std::string qry_no_id =
        "MSH|^~\\&|HIS|HOSPITAL|ADT|HOSPITAL|20240115150000||QRY^A19|MSG001|P|2.4\r"
        "QRD|20240115150000|R|I|||||DEM\r";

    auto msg = parse_qry(qry_no_id);
    ASSERT_TRUE(msg.has_value());

    EXPECT_TRUE(extract_query_id(*msg).empty());
}

TEST_F(QryHandlerTest, MissingQrfSegment) {
    std::string qry_no_qrf =
        "MSH|^~\\&|HIS|HOSPITAL|ADT|HOSPITAL|20240115150000||QRY^A19|MSG001|P|2.4\r"
        "QRD|20240115150000|R|I|QUERY001|||25^RD|12345|DEM\r";

    auto msg = parse_qry(qry_no_qrf);
    ASSERT_TRUE(msg.has_value());

    // QRF is optional, so parsing should succeed
    auto qrf = msg->get_segment("QRF");
    EXPECT_TRUE(qrf == nullptr);
}

// =============================================================================
// ACK Response Tests
// =============================================================================

TEST_F(QryHandlerTest, BuildAckForQuery) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    hl7_builder builder;
    auto ack = builder.build_ack(*msg, "AA", "Query accepted");

    ASSERT_TRUE(ack.has_value());
    EXPECT_EQ(ack->get_message_type(), "ACK");
}

TEST_F(QryHandlerTest, BuildErrorAckForInvalidQuery) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    hl7_builder builder;
    auto ack = builder.build_ack(*msg, "AE", "Invalid query parameters");

    ASSERT_TRUE(ack.has_value());
    auto msa = ack->get_segment("MSA");
    ASSERT_TRUE(msa != nullptr);
    EXPECT_EQ(msa->get_field(1), "AE");
}

TEST_F(QryHandlerTest, BuildNoDataAck) {
    auto msg = parse_qry(qry_samples::QRY_A19_PATIENT);
    ASSERT_TRUE(msg.has_value());

    hl7_builder builder;
    auto ack = builder.build_ack(*msg, "AA", "No matching records found");

    ASSERT_TRUE(ack.has_value());
    // Should indicate successful query but no data
    auto msa = ack->get_segment("MSA");
    ASSERT_TRUE(msa != nullptr);
    EXPECT_EQ(msa->get_field(1), "AA");
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
