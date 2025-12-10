/**
 * @file oru_generator_test.cpp
 * @brief Unit tests for ORU^R01 message generator
 *
 * Tests for ORU message generation including report status handling,
 * text encoding, and message structure validation.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/25
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/protocol/hl7/oru_generator.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include "test_helpers.h"

namespace pacs::bridge::hl7 {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;

// =============================================================================
// Test Fixtures
// =============================================================================

class ORUGeneratorTest : public pacs_bridge_test {
protected:
    oru_study_info create_sample_study() {
        oru_study_info study;
        study.patient_id = "12345";
        study.patient_id_authority = "HOSPITAL";
        study.patient_family_name = "DOE";
        study.patient_given_name = "JOHN";
        study.patient_birth_date = "19800515";
        study.patient_sex = "M";
        study.placer_order_number = "ORD001";
        study.accession_number = "ACC001";
        study.procedure_code = "71020";
        study.procedure_description = "CHEST XRAY PA AND LAT";
        study.procedure_coding_system = "CPT";
        study.referring_physician_id = "DR001";
        study.referring_physician_family_name = "SMITH";
        study.referring_physician_given_name = "ROBERT";
        study.radiologist_id = "RAD001";
        study.radiologist_family_name = "JONES";
        study.radiologist_given_name = "MARY";
        return study;
    }
};

// =============================================================================
// Report Status Tests
// =============================================================================

TEST_F(ORUGeneratorTest, ReportStatusToString) {
    EXPECT_STREQ(to_string(report_status::preliminary), "P");
    EXPECT_STREQ(to_string(report_status::final_report), "F");
    EXPECT_STREQ(to_string(report_status::corrected), "C");
    EXPECT_STREQ(to_string(report_status::cancelled), "X");
}

TEST_F(ORUGeneratorTest, ReportStatusToDescription) {
    EXPECT_STREQ(to_description(report_status::preliminary), "Preliminary");
    EXPECT_STREQ(to_description(report_status::final_report), "Final");
    EXPECT_STREQ(to_description(report_status::corrected), "Corrected");
    EXPECT_STREQ(to_description(report_status::cancelled), "Cancelled");
}

TEST_F(ORUGeneratorTest, ParseReportStatus) {
    auto p = parse_report_status('P');
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(*p, report_status::preliminary);

    auto f = parse_report_status('F');
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(*f, report_status::final_report);

    auto c = parse_report_status('C');
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(*c, report_status::corrected);

    auto x = parse_report_status('X');
    ASSERT_TRUE(x.has_value());
    EXPECT_EQ(*x, report_status::cancelled);

    // Invalid status
    auto invalid = parse_report_status('Z');
    EXPECT_FALSE(invalid.has_value());
}

// =============================================================================
// Study Info Validation Tests
// =============================================================================

TEST_F(ORUGeneratorTest, StudyInfoValid) {
    auto study = create_sample_study();
    EXPECT_TRUE(study.is_valid());
}

TEST_F(ORUGeneratorTest, StudyInfoInvalidMissingPatientId) {
    auto study = create_sample_study();
    study.patient_id = "";
    EXPECT_FALSE(study.is_valid());
}

TEST_F(ORUGeneratorTest, StudyInfoInvalidMissingAccessionNumber) {
    auto study = create_sample_study();
    study.accession_number = "";
    EXPECT_FALSE(study.is_valid());
}

// =============================================================================
// ORU Generator Basic Tests
// =============================================================================

TEST_F(ORUGeneratorTest, GenerateFinalReport) {
    oru_generator gen;
    auto study = create_sample_study();
    std::string report_text = "Normal chest radiograph. No acute cardiopulmonary disease.";

    auto result = gen.generate_final(study, report_text);
    ASSERT_TRUE(result.has_value());

    // Verify message structure
    EXPECT_TRUE(result->has_segment("MSH"));
    EXPECT_TRUE(result->has_segment("PID"));
    EXPECT_TRUE(result->has_segment("ORC"));
    EXPECT_TRUE(result->has_segment("OBR"));
    EXPECT_TRUE(result->has_segment("OBX"));

    // Verify message type
    auto header = result->header();
    EXPECT_EQ(header.type_string, "ORU");
    EXPECT_EQ(header.trigger_event, "R01");

    // Verify patient info
    EXPECT_EQ(result->get_value("PID.3.1"), "12345");
    EXPECT_EQ(result->get_value("PID.5.1"), "DOE");
    EXPECT_EQ(result->get_value("PID.5.2"), "JOHN");

    // Verify order info
    EXPECT_EQ(result->get_value("ORC.1"), "RE");
    EXPECT_EQ(result->get_value("OBR.4.1"), "71020");

    // Verify result status
    EXPECT_EQ(result->get_value("OBR.25"), "F");
    EXPECT_EQ(result->get_value("OBX.11"), "F");
}

TEST_F(ORUGeneratorTest, GeneratePreliminaryReport) {
    oru_generator gen;
    auto study = create_sample_study();
    std::string report_text = "Preliminary findings: Possible nodule in right lower lobe.";

    auto result = gen.generate_preliminary(study, report_text);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->get_value("OBR.25"), "P");
    EXPECT_EQ(result->get_value("OBX.11"), "P");
}

TEST_F(ORUGeneratorTest, GenerateCorrectedReport) {
    oru_generator gen;
    auto study = create_sample_study();
    std::string report_text = "CORRECTED REPORT: Previous nodule identified as artifact.";

    auto result = gen.generate_corrected(study, report_text);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->get_value("OBR.25"), "C");
    EXPECT_EQ(result->get_value("OBX.11"), "C");
}

TEST_F(ORUGeneratorTest, GenerateCancelledReport) {
    oru_generator gen;
    auto study = create_sample_study();

    auto result = gen.generate_cancelled(study, "Study cancelled by ordering physician");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->get_value("OBR.25"), "X");
    EXPECT_EQ(result->get_value("OBX.11"), "X");
}

TEST_F(ORUGeneratorTest, GenerateCancelledReportDefaultReason) {
    oru_generator gen;
    auto study = create_sample_study();

    auto result = gen.generate_cancelled(study);
    ASSERT_TRUE(result.has_value());

    // Should have default cancellation message
    EXPECT_EQ(result->get_value("OBX.11"), "X");
}

// =============================================================================
// Configuration Tests
// =============================================================================

TEST_F(ORUGeneratorTest, CustomConfiguration) {
    oru_generator_config config;
    config.sending_application = "CUSTOM_PACS";
    config.sending_facility = "CUSTOM_RAD";
    config.receiving_application = "CUSTOM_RIS";
    config.receiving_facility = "CUSTOM_HOSP";

    oru_generator gen(config);
    auto study = create_sample_study();

    auto result = gen.generate_final(study, "Test report");
    ASSERT_TRUE(result.has_value());

    auto header = result->header();
    EXPECT_EQ(header.sending_application, "CUSTOM_PACS");
    EXPECT_EQ(header.sending_facility, "CUSTOM_RAD");
    EXPECT_EQ(header.receiving_application, "CUSTOM_RIS");
    EXPECT_EQ(header.receiving_facility, "CUSTOM_HOSP");
}

TEST_F(ORUGeneratorTest, LOINCCodesEnabled) {
    oru_generator_config config;
    config.use_loinc_codes = true;
    config.loinc_report_code = "18782-3";
    config.loinc_report_description = "Radiology Study observation";
    config.loinc_coding_system = "LN";

    oru_generator gen(config);
    auto study = create_sample_study();

    auto result = gen.generate_final(study, "Test report");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->get_value("OBX.3.1"), "18782-3");
    EXPECT_EQ(result->get_value("OBX.3.2"), "Radiology Study observation");
    EXPECT_EQ(result->get_value("OBX.3.3"), "LN");
}

TEST_F(ORUGeneratorTest, LOINCCodesDisabled) {
    oru_generator_config config;
    config.use_loinc_codes = false;

    oru_generator gen(config);
    auto study = create_sample_study();

    auto result = gen.generate_final(study, "Test report");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->get_value("OBX.3.1"), "REPORT");
    EXPECT_EQ(result->get_value("OBX.3.2"), "Radiology Report");
}

TEST_F(ORUGeneratorTest, GetConfig) {
    oru_generator_config config;
    config.sending_application = "TEST_APP";

    oru_generator gen(config);
    EXPECT_EQ(gen.config().sending_application, "TEST_APP");
}

TEST_F(ORUGeneratorTest, SetConfig) {
    oru_generator gen;

    oru_generator_config new_config;
    new_config.sending_application = "NEW_APP";
    gen.set_config(new_config);

    EXPECT_EQ(gen.config().sending_application, "NEW_APP");
}

// =============================================================================
// Text Encoding Tests
// =============================================================================

TEST_F(ORUGeneratorTest, EncodeReportTextBasic) {
    std::string text = "Normal chest radiograph.";
    std::string encoded = oru_generator::encode_report_text(text);
    EXPECT_EQ(encoded, text);  // No special characters to encode
}

TEST_F(ORUGeneratorTest, EncodeReportTextWithPipe) {
    std::string text = "Patient | Doctor";
    std::string encoded = oru_generator::encode_report_text(text);
    EXPECT_EQ(encoded, "Patient \\F\\ Doctor");
}

TEST_F(ORUGeneratorTest, EncodeReportTextWithCaret) {
    std::string text = "A^B^C";
    std::string encoded = oru_generator::encode_report_text(text);
    EXPECT_EQ(encoded, "A\\S\\B\\S\\C");
}

TEST_F(ORUGeneratorTest, EncodeReportTextWithAmpersand) {
    std::string text = "Smith & Jones";
    std::string encoded = oru_generator::encode_report_text(text);
    EXPECT_EQ(encoded, "Smith \\T\\ Jones");
}

TEST_F(ORUGeneratorTest, EncodeReportTextWithTilde) {
    std::string text = "Option1~Option2";
    std::string encoded = oru_generator::encode_report_text(text);
    EXPECT_EQ(encoded, "Option1\\R\\Option2");
}

TEST_F(ORUGeneratorTest, EncodeReportTextWithBackslash) {
    std::string text = "C:\\Path\\File";
    std::string encoded = oru_generator::encode_report_text(text);
    EXPECT_EQ(encoded, "C:\\E\\Path\\E\\File");
}

TEST_F(ORUGeneratorTest, EncodeReportTextWithNewlines) {
    std::string text = "Line 1\nLine 2\nLine 3";
    std::string encoded = oru_generator::encode_report_text(text);
    EXPECT_EQ(encoded, "Line 1\\.br\\Line 2\\.br\\Line 3");
}

TEST_F(ORUGeneratorTest, EncodeReportTextWithCRLF) {
    std::string text = "Line 1\r\nLine 2";
    std::string encoded = oru_generator::encode_report_text(text);
    EXPECT_EQ(encoded, "Line 1\\.br\\Line 2");
}

TEST_F(ORUGeneratorTest, EncodeReportTextWithCROnly) {
    std::string text = "Line 1\rLine 2";
    std::string encoded = oru_generator::encode_report_text(text);
    EXPECT_EQ(encoded, "Line 1\\.br\\Line 2");
}

TEST_F(ORUGeneratorTest, DecodeReportTextBasic) {
    std::string encoded = "Normal chest radiograph.";
    std::string decoded = oru_generator::decode_report_text(encoded);
    EXPECT_EQ(decoded, encoded);
}

TEST_F(ORUGeneratorTest, DecodeReportTextWithPipe) {
    std::string encoded = "Patient \\F\\ Doctor";
    std::string decoded = oru_generator::decode_report_text(encoded);
    EXPECT_EQ(decoded, "Patient | Doctor");
}

TEST_F(ORUGeneratorTest, DecodeReportTextWithNewlines) {
    std::string encoded = "Line 1\\.br\\Line 2\\.br\\Line 3";
    std::string decoded = oru_generator::decode_report_text(encoded);
    EXPECT_EQ(decoded, "Line 1\nLine 2\nLine 3");
}

TEST_F(ORUGeneratorTest, EncodeDecodeRoundTrip) {
    std::string original = "Patient: John Doe | Age: 45\nFindings: Normal chest radiograph.\nImpression: No acute cardiopulmonary disease.";
    std::string encoded = oru_generator::encode_report_text(original);
    std::string decoded = oru_generator::decode_report_text(encoded);
    EXPECT_EQ(decoded, original);
}

TEST_F(ORUGeneratorTest, EncodeComplexReportText) {
    std::string report =
        "EXAMINATION: Chest X-ray PA and Lateral\n"
        "\n"
        "CLINICAL HISTORY: Cough & fever | Rule out pneumonia\n"
        "\n"
        "FINDINGS:\n"
        "Lungs are clear bilaterally. No consolidation or pleural effusion.\n"
        "Heart size is normal.\n"
        "\n"
        "IMPRESSION:\n"
        "1. Normal chest radiograph.\n"
        "2. No acute cardiopulmonary disease.";

    std::string encoded = oru_generator::encode_report_text(report);

    // Should not contain raw delimiters
    EXPECT_EQ(encoded.find('|'), std::string::npos);

    // Verify roundtrip
    std::string decoded = oru_generator::decode_report_text(encoded);
    EXPECT_EQ(decoded, report);
}

// =============================================================================
// Static Generation Tests
// =============================================================================

TEST_F(ORUGeneratorTest, GenerateStringStatic) {
    auto study = create_sample_study();
    auto result = oru_generator::generate_string(
        study, "Test report", report_status::final_report);

    ASSERT_TRUE(result.has_value());
    EXPECT_THAT(*result, StartsWith("MSH|"));
    EXPECT_THAT(*result, HasSubstr("ORU^R01"));
    EXPECT_THAT(*result, HasSubstr("DOE^JOHN"));
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(ORUGeneratorTest, GenerateWithInvalidStudy) {
    oru_generator gen;
    oru_study_info invalid_study;
    // Missing required fields

    auto result = gen.generate_final(invalid_study, "Test report");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), hl7_error::missing_required_field);
}

// =============================================================================
// OBX Segment Structure Tests
// =============================================================================

TEST_F(ORUGeneratorTest, OBXSegmentStructure) {
    oru_generator gen;
    auto study = create_sample_study();

    auto result = gen.generate_final(study, "Test report content");
    ASSERT_TRUE(result.has_value());

    // OBX-1: Set ID
    EXPECT_EQ(result->get_value("OBX.1"), "1");

    // OBX-2: Value Type (FT = Formatted Text)
    EXPECT_EQ(result->get_value("OBX.2"), "FT");

    // OBX-3: Observation Identifier (covered in LOINC tests)

    // OBX-5: Observation Value
    EXPECT_FALSE(result->get_value("OBX.5").empty());

    // OBX-11: Observation Result Status
    EXPECT_EQ(result->get_value("OBX.11"), "F");

    // OBX-14: Date/Time of Observation
    EXPECT_FALSE(result->get_value("OBX.14").empty());
}

// =============================================================================
// Radiologist Information Tests
// =============================================================================

TEST_F(ORUGeneratorTest, RadiologistInOBR32) {
    oru_generator gen;
    auto study = create_sample_study();

    auto result = gen.generate_final(study, "Test report");
    ASSERT_TRUE(result.has_value());

    // OBR-32: Principal Result Interpreter
    EXPECT_EQ(result->get_value("OBR.32.1"), "RAD001");
    EXPECT_EQ(result->get_value("OBR.32.2"), "JONES");
    EXPECT_EQ(result->get_value("OBR.32.3"), "MARY");
}

TEST_F(ORUGeneratorTest, RadiologistMissing) {
    oru_generator gen;
    auto study = create_sample_study();
    study.radiologist_id = "";
    study.radiologist_family_name = "";
    study.radiologist_given_name = "";

    auto result = gen.generate_final(study, "Test report");
    ASSERT_TRUE(result.has_value());

    // OBR-32 should be empty
    EXPECT_TRUE(result->get_value("OBR.32.1").empty());
}

// =============================================================================
// Message Parsing Verification Tests
// =============================================================================

TEST_F(ORUGeneratorTest, GeneratedMessageParses) {
    oru_generator gen;
    auto study = create_sample_study();

    auto result = gen.generate_final(study, "Normal chest radiograph.");
    ASSERT_TRUE(result.has_value());

    // Serialize and re-parse
    std::string serialized = result->serialize();
    auto reparsed = hl7_message::parse(serialized);

    ASSERT_TRUE(reparsed.has_value());
    EXPECT_EQ(reparsed->segment_count(), result->segment_count());
    EXPECT_EQ(reparsed->get_value("PID.5.1"), "DOE");
}

// =============================================================================
// Timestamp Tests
// =============================================================================

TEST_F(ORUGeneratorTest, ObservationDateTimeProvided) {
    oru_generator gen;
    auto study = create_sample_study();

    hl7_timestamp ts;
    ts.year = 2024;
    ts.month = 6;
    ts.day = 15;
    ts.hour = 14;
    ts.minute = 30;
    ts.second = 0;
    study.observation_datetime = ts;

    auto result = gen.generate_final(study, "Test report");
    ASSERT_TRUE(result.has_value());

    std::string obr7 = std::string(result->get_value("OBR.7"));
    EXPECT_THAT(obr7, StartsWith("20240615"));
}

TEST_F(ORUGeneratorTest, ObservationDateTimeDefault) {
    oru_generator gen;
    auto study = create_sample_study();
    study.observation_datetime = std::nullopt;

    auto result = gen.generate_final(study, "Test report");
    ASSERT_TRUE(result.has_value());

    // OBR-7 should have current timestamp
    EXPECT_FALSE(result->get_value("OBR.7").empty());
}

}  // namespace
}  // namespace pacs::bridge::hl7
