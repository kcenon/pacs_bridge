/**
 * @file mfn_handler_test.cpp
 * @brief Unit tests for MFN (Master File Notification) message handler
 *
 * Tests for MFN message parsing, master file update handling,
 * and record-level operations.
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
// Sample MFN Messages
// =============================================================================

namespace mfn_samples {

/**
 * @brief Sample MFN^M01 (Staff/Practitioner Master File) message
 */
constexpr std::string_view MFN_M01_STAFF =
    "MSH|^~\\&|HIS|HOSPITAL|REG|HOSPITAL|20240115100000||MFN^M01|MSG001|P|2.5.1\r"
    "MFI|STF^Staff Master File|UPD^Update|20240115100000|||NE\r"
    "MFE|MAD|20240115100000|20240115100000|DR001\r"
    "STF|DR001||SMITH^ROBERT^MD||M|19650315|A|MED^Medicine||555-123-4567||123 MEDICAL DR^^CITY^ST^12345||MD||||||||||20200101\r"
    "PRA|DR001||INTERNAL MEDICINE^Internal Medicine|Y|||20200101||||||STAFF\r";

/**
 * @brief Sample MFN^M02 (Staff/Practitioner Master File with additional info)
 */
constexpr std::string_view MFN_M02_PRACTITIONER =
    "MSH|^~\\&|HIS|HOSPITAL|REG|HOSPITAL|20240115110000||MFN^M02|MSG002|P|2.5.1\r"
    "MFI|PRA^Practitioner Master File|UPD^Update|20240115110000|||NE\r"
    "MFE|MAD|20240115110000|20240115110000|DR002\r"
    "STF|DR002||JONES^MARY^MD||F|19700520|A|RAD^Radiology\r"
    "PRA|DR002||RADIOLOGY^Radiology|Y\r"
    "ORG|1|HOSPITAL|RADIOLOGY|Y||P\r";

/**
 * @brief Sample MFN^M05 (Patient Location Master File) message
 */
constexpr std::string_view MFN_M05_LOCATION =
    "MSH|^~\\&|ADT|HOSPITAL|REG|HOSPITAL|20240115120000||MFN^M05|MSG003|P|2.5.1\r"
    "MFI|LOC^Location Master File|UPD^Update|20240115120000|||NE\r"
    "MFE|MAD|20240115120000|20240115120000|WARD101\r"
    "LOC|WARD^101^A^HOSPITAL|Medical Ward 101|N|HOSPITAL|555-100-1001||A|20\r"
    "LCH|1|OP^Operating Procedure||CAN^Can ambulate\r"
    "LRL|1|WARD^102^A^HOSPITAL|P^PARENT\r"
    "LDP|WARD^101^A^HOSPITAL|MED^Medicine|A|20240115|H\r";

/**
 * @brief Sample MFN^M08 (Test/Observation Master File) message
 */
constexpr std::string_view MFN_M08_TEST =
    "MSH|^~\\&|LAB|HOSPITAL|LIS|HOSPITAL|20240115130000||MFN^M08|MSG004|P|2.5.1\r"
    "MFI|OMC^Observation Batteries|UPD^Update|20240115130000|||NE\r"
    "MFE|MAD|20240115130000|20240115130000|CBC001\r"
    "OM1|1|CBC^Complete Blood Count^L|NM|BLOOD|N|B|Y|20231201\r"
    "OM2|1|3.5-5.5|10E3/uL|2.5|6.5|||\r"
    "OM3|1|EDTA\r"
    "OM4|1|10|mL|BLOOD\r";

/**
 * @brief Sample MFN^M10 (Charge Item Master File) message
 */
constexpr std::string_view MFN_M10_CHARGE =
    "MSH|^~\\&|BILLING|HOSPITAL|FIN|HOSPITAL|20240115140000||MFN^M10|MSG005|P|2.5.1\r"
    "MFI|CDM^Charge Description Master|UPD^Update|20240115140000|||NE\r"
    "MFE|MAD|20240115140000|20240115140000|CHG001\r"
    "CDM|CHG001|71020^CHEST XRAY^CPT|CHEST XRAY 2 VIEWS|150.00||RAD|A|20240101\r"
    "PRC|1|HOSPITAL|150.00|USD|20240101\r";

/**
 * @brief Sample MFN with delete operation
 */
constexpr std::string_view MFN_DELETE =
    "MSH|^~\\&|HIS|HOSPITAL|REG|HOSPITAL|20240115150000||MFN^M01|MSG006|P|2.5.1\r"
    "MFI|STF^Staff Master File|UPD^Update|20240115150000|||NE\r"
    "MFE|MDL|20240115150000|20240115150000|DR003\r";

/**
 * @brief Sample MFN with update operation
 */
constexpr std::string_view MFN_UPDATE =
    "MSH|^~\\&|HIS|HOSPITAL|REG|HOSPITAL|20240115160000||MFN^M01|MSG007|P|2.5.1\r"
    "MFI|STF^Staff Master File|UPD^Update|20240115160000|||NE\r"
    "MFE|MUP|20240115160000|20240115160000|DR001\r"
    "STF|DR001||SMITH^ROBERT^MD||M|19650315|A|CARD^Cardiology\r";

}  // namespace mfn_samples

// =============================================================================
// Test Fixture
// =============================================================================

class MfnHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;

    std::expected<hl7_message, hl7_error> parse_mfn(std::string_view raw) {
        return parser_->parse(std::string(raw));
    }

    // Extract master file identifier from MFI
    std::string extract_master_file_id(const hl7_message& msg) {
        auto mfi = msg.segment("MFI");
        if (!mfi) return "";
        return std::string(mfi->field_value(1));
    }

    // Extract record level event code from MFE
    std::string extract_record_event_code(const hl7_message& msg) {
        auto mfe = msg.segment("MFE");
        if (!mfe) return "";
        return std::string(mfe->field_value(1));
    }

    // Extract primary key value from MFE
    std::string extract_primary_key(const hl7_message& msg) {
        auto mfe = msg.segment("MFE");
        if (!mfe) return "";
        return std::string(mfe->field_value(4));
    }
};

// =============================================================================
// MFN Message Parsing Tests
// =============================================================================

TEST_F(MfnHandlerTest, ParseMfnM01Staff) {
    auto msg = parse_mfn(mfn_samples::MFN_M01_STAFF);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MFN");
    EXPECT_EQ(msg->trigger_event(), "M01");
    EXPECT_EQ(extract_primary_key(*msg), "DR001");
}

TEST_F(MfnHandlerTest, ParseMfnM02Practitioner) {
    auto msg = parse_mfn(mfn_samples::MFN_M02_PRACTITIONER);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MFN");
    EXPECT_EQ(msg->trigger_event(), "M02");

    // Should have ORG segment
    auto org = msg->segment("ORG");
    ASSERT_TRUE(org != nullptr);
}

TEST_F(MfnHandlerTest, ParseMfnM05Location) {
    auto msg = parse_mfn(mfn_samples::MFN_M05_LOCATION);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MFN");
    EXPECT_EQ(msg->trigger_event(), "M05");

    // Should have LOC segment
    auto loc = msg->segment("LOC");
    ASSERT_TRUE(loc != nullptr);
}

TEST_F(MfnHandlerTest, ParseMfnM08Test) {
    auto msg = parse_mfn(mfn_samples::MFN_M08_TEST);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MFN");
    EXPECT_EQ(msg->trigger_event(), "M08");

    // Should have OM segments
    auto om1 = msg->segment("OM1");
    ASSERT_TRUE(om1 != nullptr);
}

TEST_F(MfnHandlerTest, ParseMfnM10Charge) {
    auto msg = parse_mfn(mfn_samples::MFN_M10_CHARGE);
    ASSERT_TRUE(msg.has_value());

    EXPECT_STREQ(to_string(msg->type()), "MFN");
    EXPECT_EQ(msg->trigger_event(), "M10");

    // Should have CDM segment
    auto cdm = msg->segment("CDM");
    ASSERT_TRUE(cdm != nullptr);
}

// =============================================================================
// MFI Segment Tests
// =============================================================================

TEST_F(MfnHandlerTest, ExtractMasterFileIdentifier) {
    auto msg = parse_mfn(mfn_samples::MFN_M01_STAFF);
    ASSERT_TRUE(msg.has_value());

    std::string mf_id = extract_master_file_id(*msg);
    EXPECT_TRUE(mf_id.find("STF") != std::string::npos);
}

TEST_F(MfnHandlerTest, ExtractFileEventCode) {
    auto msg = parse_mfn(mfn_samples::MFN_M01_STAFF);
    ASSERT_TRUE(msg.has_value());

    auto mfi = msg->segment("MFI");
    ASSERT_TRUE(mfi != nullptr);

    // MFI-2 is File Level Event Code (UPD = Update)
    EXPECT_TRUE(mfi->field_value(2).find("UPD") != std::string::npos);
}

TEST_F(MfnHandlerTest, ExtractResponseLevel) {
    auto msg = parse_mfn(mfn_samples::MFN_M01_STAFF);
    ASSERT_TRUE(msg.has_value());

    auto mfi = msg->segment("MFI");
    ASSERT_TRUE(mfi != nullptr);

    // MFI-6 is Response Level Code (NE = Never)
    EXPECT_EQ(mfi->field_value(6), "NE");
}

// =============================================================================
// MFE Segment Tests
// =============================================================================

TEST_F(MfnHandlerTest, ExtractRecordEventCode) {
    auto msg = parse_mfn(mfn_samples::MFN_M01_STAFF);
    ASSERT_TRUE(msg.has_value());

    std::string event_code = extract_record_event_code(*msg);
    // MAD = Add
    EXPECT_EQ(event_code, "MAD");
}

TEST_F(MfnHandlerTest, DeleteRecordEventCode) {
    auto msg = parse_mfn(mfn_samples::MFN_DELETE);
    ASSERT_TRUE(msg.has_value());

    std::string event_code = extract_record_event_code(*msg);
    // MDL = Delete
    EXPECT_EQ(event_code, "MDL");
}

TEST_F(MfnHandlerTest, UpdateRecordEventCode) {
    auto msg = parse_mfn(mfn_samples::MFN_UPDATE);
    ASSERT_TRUE(msg.has_value());

    std::string event_code = extract_record_event_code(*msg);
    // MUP = Update
    EXPECT_EQ(event_code, "MUP");
}

TEST_F(MfnHandlerTest, ExtractEffectiveDateTime) {
    auto msg = parse_mfn(mfn_samples::MFN_M01_STAFF);
    ASSERT_TRUE(msg.has_value());

    auto mfe = msg->segment("MFE");
    ASSERT_TRUE(mfe != nullptr);

    // MFE-3 is Effective Date/Time
    EXPECT_EQ(mfe->field_value(3), "20240115100000");
}

// =============================================================================
// Staff Master File Tests
// =============================================================================

TEST_F(MfnHandlerTest, ExtractStaffInfo) {
    auto msg = parse_mfn(mfn_samples::MFN_M01_STAFF);
    ASSERT_TRUE(msg.has_value());

    auto stf = msg->segment("STF");
    ASSERT_TRUE(stf != nullptr);

    // STF-1 is Staff Identifier
    EXPECT_EQ(stf->field_value(1), "DR001");
    // STF-3 is Staff Name
    EXPECT_TRUE(stf->field_value(3).find("SMITH") != std::string::npos);
}

TEST_F(MfnHandlerTest, ExtractPractitionerInfo) {
    auto msg = parse_mfn(mfn_samples::MFN_M01_STAFF);
    ASSERT_TRUE(msg.has_value());

    auto pra = msg->segment("PRA");
    ASSERT_TRUE(pra != nullptr);

    // PRA-3 is Practitioner Group
    EXPECT_TRUE(pra->field_value(3).find("INTERNAL MEDICINE") != std::string::npos);
}

// =============================================================================
// Location Master File Tests
// =============================================================================

TEST_F(MfnHandlerTest, ExtractLocationInfo) {
    auto msg = parse_mfn(mfn_samples::MFN_M05_LOCATION);
    ASSERT_TRUE(msg.has_value());

    auto loc = msg->segment("LOC");
    ASSERT_TRUE(loc != nullptr);

    // LOC-1 is Primary Key Value - LOC
    EXPECT_TRUE(loc->field_value(1).find("WARD") != std::string::npos);
    // LOC-2 is Location Description
    EXPECT_TRUE(loc->field_value(2).find("Medical Ward") != std::string::npos);
}

TEST_F(MfnHandlerTest, ExtractLocationDepartment) {
    auto msg = parse_mfn(mfn_samples::MFN_M05_LOCATION);
    ASSERT_TRUE(msg.has_value());

    auto ldp = msg->segment("LDP");
    ASSERT_TRUE(ldp != nullptr);

    // LDP-2 is Location Department
    EXPECT_TRUE(ldp->field_value(2).find("MED") != std::string::npos);
}

// =============================================================================
// Test Master File Tests
// =============================================================================

TEST_F(MfnHandlerTest, ExtractTestInfo) {
    auto msg = parse_mfn(mfn_samples::MFN_M08_TEST);
    ASSERT_TRUE(msg.has_value());

    auto om1 = msg->segment("OM1");
    ASSERT_TRUE(om1 != nullptr);

    // OM1-2 is Producer's Test/Observation ID
    EXPECT_TRUE(om1->field_value(2).find("CBC") != std::string::npos);
}

TEST_F(MfnHandlerTest, ExtractTestReferenceRange) {
    auto msg = parse_mfn(mfn_samples::MFN_M08_TEST);
    ASSERT_TRUE(msg.has_value());

    auto om2 = msg->segment("OM2");
    ASSERT_TRUE(om2 != nullptr);

    // OM2-2 is Reference Range
    EXPECT_TRUE(om2->field_value(2).find("3.5-5.5") != std::string::npos);
}

// =============================================================================
// Charge Master File Tests
// =============================================================================

TEST_F(MfnHandlerTest, ExtractChargeInfo) {
    auto msg = parse_mfn(mfn_samples::MFN_M10_CHARGE);
    ASSERT_TRUE(msg.has_value());

    auto cdm = msg->segment("CDM");
    ASSERT_TRUE(cdm != nullptr);

    // CDM-1 is Primary Key Value - CDM
    EXPECT_EQ(cdm->field_value(1), "CHG001");
    // CDM-2 is Charge Code Alias
    EXPECT_TRUE(cdm->field_value(2).find("71020") != std::string::npos);
}

TEST_F(MfnHandlerTest, ExtractPriceInfo) {
    auto msg = parse_mfn(mfn_samples::MFN_M10_CHARGE);
    ASSERT_TRUE(msg.has_value());

    auto prc = msg->segment("PRC");
    ASSERT_TRUE(prc != nullptr);

    // PRC-3 is Price
    EXPECT_EQ(prc->field_value(3), "150.00");
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(MfnHandlerTest, MissingMfiSegment) {
    std::string invalid_mfn =
        "MSH|^~\\&|HIS|HOSPITAL|REG|HOSPITAL|20240115100000||MFN^M01|MSG001|P|2.5.1\r"
        "MFE|MAD|20240115100000|20240115100000|DR001\r";

    auto msg = parse_mfn(invalid_mfn);
    ASSERT_TRUE(msg.has_value());

    auto mfi = msg->segment("MFI");
    EXPECT_TRUE(mfi == nullptr);
}

TEST_F(MfnHandlerTest, MissingMfeSegment) {
    std::string mfn_no_mfe =
        "MSH|^~\\&|HIS|HOSPITAL|REG|HOSPITAL|20240115100000||MFN^M01|MSG001|P|2.5.1\r"
        "MFI|STF^Staff Master File|UPD^Update|20240115100000|||NE\r";

    auto msg = parse_mfn(mfn_no_mfe);
    ASSERT_TRUE(msg.has_value());

    auto mfe = msg->segment("MFE");
    EXPECT_TRUE(mfe == nullptr);
}

// =============================================================================
// ACK Response Tests
// =============================================================================

TEST_F(MfnHandlerTest, BuildMfkResponse) {
    auto msg = parse_mfn(mfn_samples::MFN_M01_STAFF);
    ASSERT_TRUE(msg.has_value());

    auto ack = hl7_builder::create_ack(*msg, ack_code::AA, "Master file update accepted");

    // ack is hl7_message directly, no has_value check needed
    EXPECT_STREQ(to_string(ack.type()), "ACK");
}

TEST_F(MfnHandlerTest, BuildErrorAckForMfn) {
    auto msg = parse_mfn(mfn_samples::MFN_M01_STAFF);
    ASSERT_TRUE(msg.has_value());

    auto ack = hl7_builder::create_ack(*msg, ack_code::AE, "Invalid master file record");

    // ack is hl7_message directly, no has_value check needed
    auto msa = ack.segment("MSA");
    ASSERT_TRUE(msa != nullptr);
    EXPECT_EQ(msa->field_value(1), "AE");
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
