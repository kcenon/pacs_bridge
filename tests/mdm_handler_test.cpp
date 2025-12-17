/**
 * @file mdm_handler_test.cpp
 * @brief Unit tests for MDM (Medical Document Management) message handler
 *
 * Tests for MDM message parsing, document notification handling,
 * and document content extraction.
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
// Sample MDM Messages
// =============================================================================

namespace mdm_samples {

/**
 * @brief Sample MDM^T02 (Original Document Notification) message
 */
constexpr std::string_view MDM_T02_ORIGINAL =
    "MSH|^~\\&|TRANSCRIPTION|HOSPITAL|EMR|HOSPITAL|20240115140000||MDM^T02|MSG001|P|2.5.1\r"
    "EVN|T02|20240115140000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "PV1|1|I|WARD^101^A||||SMITH^ROBERT^MD\r"
    "TXA|1|HP^History and Physical|TX|20240115140000|||||||DOC12345|||||AU|||||SMITH^ROBERT^MD\r"
    "OBX|1|TX|REPORT^Report Text||History and physical examination completed.||||||F\r";

/**
 * @brief Sample MDM^T04 (Document Status Change) message
 */
constexpr std::string_view MDM_T04_STATUS_CHANGE =
    "MSH|^~\\&|TRANSCRIPTION|HOSPITAL|EMR|HOSPITAL|20240115150000||MDM^T04|MSG002|P|2.5.1\r"
    "EVN|T04|20240115150000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "TXA|1|HP^History and Physical|TX|20240115140000||20240115150000|||||DOC12345|||||LA|||||SMITH^ROBERT^MD\r";

/**
 * @brief Sample MDM^T06 (Document Addendum) message
 */
constexpr std::string_view MDM_T06_ADDENDUM =
    "MSH|^~\\&|TRANSCRIPTION|HOSPITAL|EMR|HOSPITAL|20240115160000||MDM^T06|MSG003|P|2.5.1\r"
    "EVN|T06|20240115160000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "TXA|1|HP^History and Physical Addendum|TX|20240115160000|||||||DOC12346|DOC12345||||AU|||||JONES^MARY^MD\r"
    "OBX|1|TX|ADDENDUM^Addendum Text||Additional findings noted.||||||F\r";

/**
 * @brief Sample MDM^T08 (Document Edit) message
 */
constexpr std::string_view MDM_T08_EDIT =
    "MSH|^~\\&|TRANSCRIPTION|HOSPITAL|EMR|HOSPITAL|20240115170000||MDM^T08|MSG004|P|2.5.1\r"
    "EVN|T08|20240115170000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "TXA|1|HP^History and Physical|TX|20240115140000||20240115170000|||||DOC12345|||||AU|||||SMITH^ROBERT^MD\r"
    "OBX|1|TX|REPORT^Report Text||History and physical examination completed with corrections.||||||F\r";

/**
 * @brief Sample MDM^T10 (Document Replacement) message
 */
constexpr std::string_view MDM_T10_REPLACEMENT =
    "MSH|^~\\&|TRANSCRIPTION|HOSPITAL|EMR|HOSPITAL|20240115180000||MDM^T10|MSG005|P|2.5.1\r"
    "EVN|T10|20240115180000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "TXA|1|HP^History and Physical|TX|20240115180000|||||||DOC12347|DOC12345||||AU|||||SMITH^ROBERT^MD\r"
    "OBX|1|TX|REPORT^Report Text||Replacement document with updated findings.||||||F\r";

/**
 * @brief Sample MDM message with radiology report
 */
constexpr std::string_view MDM_RADIOLOGY_REPORT =
    "MSH|^~\\&|PACS|RADIOLOGY|EMR|HOSPITAL|20240115190000||MDM^T02|MSG006|P|2.5.1\r"
    "EVN|T02|20240115190000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
    "PV1|1|O|RAD^XRAY^1\r"
    "TXA|1|RR^Radiology Report|TX|20240115190000|||||||RAD001|||||AU|||||RADIOLOGIST^JAMES^MD\r"
    "OBR|1|ORD001|ACC001|71020^CHEST XRAY^CPT\r"
    "OBX|1|TX|IMPRESSION^Impression||No acute cardiopulmonary abnormality.||||||F\r"
    "OBX|2|TX|FINDINGS^Findings||Heart size normal. Lungs are clear.||||||F\r";

}  // namespace mdm_samples

// =============================================================================
// Test Fixture
// =============================================================================

class MdmHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;

    // Helper to parse MDM message
    std::expected<hl7_message, hl7_error> parse_mdm(std::string_view raw) {
        return parser_->parse(std::string(raw));
    }

    // Extract document unique ID from TXA segment
    std::string extract_document_id(const hl7_message& msg) {
        auto txa = msg.segment("TXA");
        if (!txa) return "";
        return std::string(txa->field_value(12));
    }

    // Extract document type from TXA segment
    std::string extract_document_type(const hl7_message& msg) {
        auto txa = msg.segment("TXA");
        if (!txa) return "";
        return std::string(txa->field_value(2));
    }

    // Extract document status from TXA segment
    std::string extract_document_status(const hl7_message& msg) {
        auto txa = msg.segment("TXA");
        if (!txa) return "";
        return std::string(txa->field_value(17));
    }
};

// =============================================================================
// MDM Message Parsing Tests
// =============================================================================

TEST_F(MdmHandlerTest, ParseMdmT02Original) {
    auto msg = parse_mdm(mdm_samples::MDM_T02_ORIGINAL);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MDM");
    EXPECT_EQ(msg->trigger_event(), "T02");
    EXPECT_EQ(extract_document_id(*msg), "DOC12345");
    EXPECT_EQ(extract_document_status(*msg), "AU");
}

TEST_F(MdmHandlerTest, ParseMdmT04StatusChange) {
    auto msg = parse_mdm(mdm_samples::MDM_T04_STATUS_CHANGE);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MDM");
    EXPECT_EQ(msg->trigger_event(), "T04");
    EXPECT_EQ(extract_document_status(*msg), "LA");
}

TEST_F(MdmHandlerTest, ParseMdmT06Addendum) {
    auto msg = parse_mdm(mdm_samples::MDM_T06_ADDENDUM);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MDM");
    EXPECT_EQ(msg->trigger_event(), "T06");

    auto txa = msg->segment("TXA");
    ASSERT_TRUE(txa != nullptr);
    // TXA-13 contains parent document ID
    EXPECT_EQ(txa->field_value(13), "DOC12345");
}

TEST_F(MdmHandlerTest, ParseMdmT08Edit) {
    auto msg = parse_mdm(mdm_samples::MDM_T08_EDIT);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MDM");
    EXPECT_EQ(msg->trigger_event(), "T08");
}

TEST_F(MdmHandlerTest, ParseMdmT10Replacement) {
    auto msg = parse_mdm(mdm_samples::MDM_T10_REPLACEMENT);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MDM");
    EXPECT_EQ(msg->trigger_event(), "T10");

    auto txa = msg->segment("TXA");
    ASSERT_TRUE(txa != nullptr);
    // Replacement should reference original document
    EXPECT_EQ(txa->field_value(13), "DOC12345");
}

TEST_F(MdmHandlerTest, ParseRadiologyReport) {
    auto msg = parse_mdm(mdm_samples::MDM_RADIOLOGY_REPORT);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MDM");

    // Should have OBR segment for radiology
    auto obr = msg->segment("OBR");
    ASSERT_TRUE(obr != nullptr);

    // Should have multiple OBX segments
    auto obx_segments = msg->segments("OBX");
    EXPECT_GE(obx_segments.size(), 2);
}

// =============================================================================
// Document Type Tests
// =============================================================================

TEST_F(MdmHandlerTest, DocumentTypeHistoryPhysical) {
    auto msg = parse_mdm(mdm_samples::MDM_T02_ORIGINAL);
    ASSERT_TRUE(msg.has_value());

    std::string doc_type = extract_document_type(*msg);
    EXPECT_TRUE(doc_type.find("HP") != std::string::npos ||
                doc_type.find("History and Physical") != std::string::npos);
}

TEST_F(MdmHandlerTest, DocumentTypeRadiologyReport) {
    auto msg = parse_mdm(mdm_samples::MDM_RADIOLOGY_REPORT);
    ASSERT_TRUE(msg.has_value());

    std::string doc_type = extract_document_type(*msg);
    EXPECT_TRUE(doc_type.find("RR") != std::string::npos ||
                doc_type.find("Radiology Report") != std::string::npos);
}

// =============================================================================
// Document Status Tests
// =============================================================================

TEST_F(MdmHandlerTest, DocumentStatusAuthenticated) {
    auto msg = parse_mdm(mdm_samples::MDM_T02_ORIGINAL);
    ASSERT_TRUE(msg.has_value());

    // AU = Authenticated
    EXPECT_EQ(extract_document_status(*msg), "AU");
}

TEST_F(MdmHandlerTest, DocumentStatusLegallyAuthenticated) {
    auto msg = parse_mdm(mdm_samples::MDM_T04_STATUS_CHANGE);
    ASSERT_TRUE(msg.has_value());

    // LA = Legally Authenticated
    EXPECT_EQ(extract_document_status(*msg), "LA");
}

// =============================================================================
// Patient Extraction Tests
// =============================================================================

TEST_F(MdmHandlerTest, ExtractPatientFromMdm) {
    auto msg = parse_mdm(mdm_samples::MDM_T02_ORIGINAL);
    ASSERT_TRUE(msg.has_value());

    auto pid = msg->segment("PID");
    ASSERT_TRUE(pid != nullptr);

    // Patient ID
    EXPECT_TRUE(pid->field_value(3).find("12345") != std::string::npos);
    // Patient Name
    EXPECT_TRUE(pid->field_value(5).find("DOE") != std::string::npos);
}

// =============================================================================
// OBX Content Tests
// =============================================================================

TEST_F(MdmHandlerTest, ExtractObxContent) {
    auto msg = parse_mdm(mdm_samples::MDM_T02_ORIGINAL);
    ASSERT_TRUE(msg.has_value());

    auto obx_segments = msg->segments("OBX");
    ASSERT_GE(obx_segments.size(), 1);

    // OBX-5 contains the observation value (report text)
    std::string content = std::string(obx_segments[0]->field_value(5));
    EXPECT_FALSE(content.empty());
}

TEST_F(MdmHandlerTest, MultipleObxSegments) {
    auto msg = parse_mdm(mdm_samples::MDM_RADIOLOGY_REPORT);
    ASSERT_TRUE(msg.has_value());

    auto obx_segments = msg->segments("OBX");
    EXPECT_EQ(obx_segments.size(), 2);

    // First OBX should be impression
    EXPECT_TRUE(obx_segments[0]->field_value(3).find("IMPRESSION") != std::string::npos);
    // Second OBX should be findings
    EXPECT_TRUE(obx_segments[1]->field_value(3).find("FINDINGS") != std::string::npos);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(MdmHandlerTest, MissingTxaSegment) {
    std::string invalid_mdm =
        "MSH|^~\\&|TRANSCRIPTION|HOSPITAL|EMR|HOSPITAL|20240115140000||MDM^T02|MSG001|P|2.5.1\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r";

    auto msg = parse_mdm(invalid_mdm);
    ASSERT_TRUE(msg.has_value());

    // Should parse but TXA should be missing
    auto txa = msg->segment("TXA");
    EXPECT_TRUE(txa == nullptr);
}

TEST_F(MdmHandlerTest, EmptyDocumentId) {
    std::string mdm_no_doc_id =
        "MSH|^~\\&|TRANSCRIPTION|HOSPITAL|EMR|HOSPITAL|20240115140000||MDM^T02|MSG001|P|2.5.1\r"
        "TXA|1|HP^History and Physical|TX|20240115140000|||||||||||AU\r";

    auto msg = parse_mdm(mdm_no_doc_id);
    ASSERT_TRUE(msg.has_value());

    EXPECT_TRUE(extract_document_id(*msg).empty());
}

// =============================================================================
// ACK Response Tests
// =============================================================================

TEST_F(MdmHandlerTest, BuildAckForMdm) {
    auto msg = parse_mdm(mdm_samples::MDM_T02_ORIGINAL);
    ASSERT_TRUE(msg.has_value());

    auto ack = hl7_builder::create_ack(*msg, ack_code::AA, "Message accepted");

    // ack is hl7_message directly, no has_value check needed
    EXPECT_STREQ(to_string(ack.type()), "ACK");

    auto msa = ack.segment("MSA");
    ASSERT_TRUE(msa != nullptr);
    EXPECT_EQ(msa->field_value(1), "AA");
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
