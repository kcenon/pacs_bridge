/**
 * @file result_poster_test.cpp
 * @brief Unit tests for EMR Result Poster
 *
 * Tests DiagnosticReport posting, result tracking, and builder functionality.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/105
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/emr/result_poster.h"
#include "pacs/bridge/emr/diagnostic_report_builder.h"
#include "pacs/bridge/emr/result_tracker.h"

namespace pacs::bridge::emr {
namespace {

// =============================================================================
// Test Fixtures
// =============================================================================

class DiagnosticReportBuilderTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

class ResultTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        result_tracker_config config;
        config.max_entries = 100;
        config.ttl = std::chrono::hours{24};
        tracker_ = std::make_unique<in_memory_result_tracker>(config);
    }

    void TearDown() override {
        tracker_.reset();
    }

    std::unique_ptr<in_memory_result_tracker> tracker_;
};

class ResultStatusTest : public ::testing::Test {};

// =============================================================================
// Result Status Tests
// =============================================================================

TEST_F(ResultStatusTest, ToStringConvertsAllStatuses) {
    EXPECT_EQ(to_string(result_status::registered), "registered");
    EXPECT_EQ(to_string(result_status::partial), "partial");
    EXPECT_EQ(to_string(result_status::preliminary), "preliminary");
    EXPECT_EQ(to_string(result_status::final_report), "final");
    EXPECT_EQ(to_string(result_status::amended), "amended");
    EXPECT_EQ(to_string(result_status::corrected), "corrected");
    EXPECT_EQ(to_string(result_status::appended), "appended");
    EXPECT_EQ(to_string(result_status::cancelled), "cancelled");
    EXPECT_EQ(to_string(result_status::entered_in_error), "entered-in-error");
    EXPECT_EQ(to_string(result_status::unknown), "unknown");
}

TEST_F(ResultStatusTest, ParseResultStatusValidStrings) {
    EXPECT_EQ(parse_result_status("registered"), result_status::registered);
    EXPECT_EQ(parse_result_status("partial"), result_status::partial);
    EXPECT_EQ(parse_result_status("preliminary"), result_status::preliminary);
    EXPECT_EQ(parse_result_status("final"), result_status::final_report);
    EXPECT_EQ(parse_result_status("amended"), result_status::amended);
    EXPECT_EQ(parse_result_status("corrected"), result_status::corrected);
    EXPECT_EQ(parse_result_status("appended"), result_status::appended);
    EXPECT_EQ(parse_result_status("cancelled"), result_status::cancelled);
    EXPECT_EQ(parse_result_status("entered-in-error"), result_status::entered_in_error);
    EXPECT_EQ(parse_result_status("unknown"), result_status::unknown);
}

TEST_F(ResultStatusTest, ParseResultStatusInvalidString) {
    EXPECT_FALSE(parse_result_status("invalid").has_value());
    EXPECT_FALSE(parse_result_status("").has_value());
    EXPECT_FALSE(parse_result_status("FINAL").has_value());  // Case sensitive
}

// =============================================================================
// Result Error Tests
// =============================================================================

TEST_F(ResultStatusTest, ResultErrorToString) {
    EXPECT_STREQ(to_string(result_error::post_failed),
                 "Failed to post result to EMR");
    EXPECT_STREQ(to_string(result_error::update_failed),
                 "Failed to update existing result");
    EXPECT_STREQ(to_string(result_error::duplicate),
                 "Duplicate result detected");
    EXPECT_STREQ(to_string(result_error::invalid_data),
                 "Invalid result data");
    EXPECT_STREQ(to_string(result_error::rejected),
                 "EMR rejected the result");
    EXPECT_STREQ(to_string(result_error::not_found),
                 "Result not found");
    EXPECT_STREQ(to_string(result_error::invalid_status_transition),
                 "Invalid status transition");
    EXPECT_STREQ(to_string(result_error::missing_reference),
                 "Missing required reference");
    EXPECT_STREQ(to_string(result_error::build_failed),
                 "Failed to build DiagnosticReport");
    EXPECT_STREQ(to_string(result_error::tracker_error),
                 "Result tracker operation failed");
}

TEST_F(ResultStatusTest, ResultErrorCodes) {
    EXPECT_EQ(to_error_code(result_error::post_failed), -1060);
    EXPECT_EQ(to_error_code(result_error::update_failed), -1061);
    EXPECT_EQ(to_error_code(result_error::duplicate), -1062);
    EXPECT_EQ(to_error_code(result_error::invalid_data), -1063);
    EXPECT_EQ(to_error_code(result_error::rejected), -1064);
    EXPECT_EQ(to_error_code(result_error::not_found), -1065);
    EXPECT_EQ(to_error_code(result_error::invalid_status_transition), -1066);
    EXPECT_EQ(to_error_code(result_error::missing_reference), -1067);
    EXPECT_EQ(to_error_code(result_error::build_failed), -1068);
    EXPECT_EQ(to_error_code(result_error::tracker_error), -1069);
}

// =============================================================================
// Study Result Tests
// =============================================================================

TEST_F(ResultStatusTest, StudyResultIsValid) {
    study_result result;
    EXPECT_FALSE(result.is_valid());

    result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    EXPECT_FALSE(result.is_valid());

    result.patient_id = "MRN12345";
    EXPECT_FALSE(result.is_valid());

    result.modality = "CT";
    EXPECT_FALSE(result.is_valid());

    result.study_datetime = "2025-01-15T10:30:00Z";
    EXPECT_TRUE(result.is_valid());
}

TEST_F(ResultStatusTest, StudyResultOptionalFields) {
    study_result result;
    result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    result.patient_id = "MRN12345";
    result.modality = "CT";
    result.study_datetime = "2025-01-15T10:30:00Z";

    // All optional fields should be empty
    EXPECT_FALSE(result.patient_reference.has_value());
    EXPECT_FALSE(result.accession_number.has_value());
    EXPECT_FALSE(result.study_description.has_value());
    EXPECT_FALSE(result.performing_physician.has_value());
    EXPECT_FALSE(result.performer_reference.has_value());
    EXPECT_FALSE(result.conclusion.has_value());
    EXPECT_FALSE(result.conclusion_code.has_value());
    EXPECT_FALSE(result.imaging_study_reference.has_value());
    EXPECT_FALSE(result.based_on_reference.has_value());
    EXPECT_FALSE(result.encounter_reference.has_value());
}

// =============================================================================
// Diagnostic Report Builder Tests
// =============================================================================

TEST_F(DiagnosticReportBuilderTest, RequiredFieldsValidation) {
    diagnostic_report_builder builder;

    // Empty builder should not be valid
    EXPECT_FALSE(builder.is_valid());

    auto errors = builder.validation_errors();
    EXPECT_EQ(errors.size(), 3);  // status, code, subject

    // Add required fields one by one
    builder.status(result_status::final_report);
    errors = builder.validation_errors();
    EXPECT_EQ(errors.size(), 2);  // code, subject

    builder.code_imaging_study();
    errors = builder.validation_errors();
    EXPECT_EQ(errors.size(), 1);  // subject

    builder.subject("Patient/123");
    EXPECT_TRUE(builder.is_valid());
    EXPECT_TRUE(builder.validation_errors().empty());
}

TEST_F(DiagnosticReportBuilderTest, BuildMinimalReport) {
    auto json = diagnostic_report_builder()
                    .status(result_status::final_report)
                    .code_imaging_study()
                    .subject("Patient/123")
                    .build();

    ASSERT_TRUE(json.has_value());
    EXPECT_TRUE(json->find("\"resourceType\":\"DiagnosticReport\"") !=
                std::string::npos);
    EXPECT_TRUE(json->find("\"status\":\"final\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"subject\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"Patient/123\"") != std::string::npos);
}

TEST_F(DiagnosticReportBuilderTest, BuildReportWithAllFields) {
    auto json = diagnostic_report_builder()
                    .status(result_status::final_report)
                    .category_radiology()
                    .code_imaging_study()
                    .subject("Patient/123", "John Doe")
                    .effective_datetime("2025-01-15T10:30:00Z")
                    .issued("2025-01-15T10:35:00Z")
                    .performer("Practitioner/456", "Dr. Smith")
                    .imaging_study("ImagingStudy/789")
                    .study_instance_uid("1.2.3.4.5.6.7.8.9")
                    .accession_number("ACC12345")
                    .conclusion("No acute findings.")
                    .conclusion_code_snomed("260385009", "Negative")
                    .build();

    ASSERT_TRUE(json.has_value());

    // Check required fields
    EXPECT_TRUE(json->find("\"resourceType\":\"DiagnosticReport\"") !=
                std::string::npos);
    EXPECT_TRUE(json->find("\"status\":\"final\"") != std::string::npos);

    // Check category
    EXPECT_TRUE(json->find("\"category\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"RAD\"") != std::string::npos);

    // Check timing
    EXPECT_TRUE(json->find("\"effectiveDateTime\":\"2025-01-15T10:30:00Z\"") !=
                std::string::npos);
    EXPECT_TRUE(json->find("\"issued\":\"2025-01-15T10:35:00Z\"") !=
                std::string::npos);

    // Check performer
    EXPECT_TRUE(json->find("\"performer\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"Practitioner/456\"") != std::string::npos);

    // Check imaging study reference
    EXPECT_TRUE(json->find("\"imagingStudy\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"ImagingStudy/789\"") != std::string::npos);

    // Check identifiers
    EXPECT_TRUE(json->find("\"identifier\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"urn:dicom:uid\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"1.2.3.4.5.6.7.8.9\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"ACC12345\"") != std::string::npos);

    // Check conclusion
    EXPECT_TRUE(json->find("\"conclusion\":\"No acute findings.\"") !=
                std::string::npos);
    EXPECT_TRUE(json->find("\"conclusionCode\"") != std::string::npos);
}

TEST_F(DiagnosticReportBuilderTest, BuildFromStudyResult) {
    study_result result;
    result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    result.patient_id = "MRN12345";
    result.patient_reference = "Patient/123";
    result.accession_number = "ACC12345";
    result.modality = "CT";
    result.study_description = "CT Chest";
    result.performing_physician = "Dr. Smith";
    result.study_datetime = "2025-01-15T10:30:00Z";
    result.status = result_status::final_report;
    result.conclusion = "No acute findings.";

    auto json = diagnostic_report_builder::from_study_result(result).build();

    ASSERT_TRUE(json.has_value());
    EXPECT_TRUE(json->find("\"status\":\"final\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"Patient/123\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"1.2.3.4.5.6.7.8.9\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"ACC12345\"") != std::string::npos);
    EXPECT_TRUE(json->find("\"No acute findings.\"") != std::string::npos);
}

TEST_F(DiagnosticReportBuilderTest, BuildValidatedSuccess) {
    auto result = diagnostic_report_builder()
                      .status(result_status::final_report)
                      .code_imaging_study()
                      .subject("Patient/123")
                      .build_validated();

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
}

TEST_F(DiagnosticReportBuilderTest, BuildValidatedFailure) {
    auto result = diagnostic_report_builder().build_validated();

    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("Validation failed") != std::string::npos);
}

TEST_F(DiagnosticReportBuilderTest, Reset) {
    diagnostic_report_builder builder;
    builder.status(result_status::final_report)
        .code_imaging_study()
        .subject("Patient/123");

    EXPECT_TRUE(builder.is_valid());

    builder.reset();
    EXPECT_FALSE(builder.is_valid());
}

TEST_F(DiagnosticReportBuilderTest, SpecialCharactersInConclusion) {
    auto json = diagnostic_report_builder()
                    .status(result_status::final_report)
                    .code_imaging_study()
                    .subject("Patient/123")
                    .conclusion("Test with \"quotes\" and \\ backslash")
                    .build();

    ASSERT_TRUE(json.has_value());
    // Check proper escaping
    EXPECT_TRUE(json->find("\\\"quotes\\\"") != std::string::npos);
    EXPECT_TRUE(json->find("\\\\") != std::string::npos);
}

// =============================================================================
// FHIR Coding Tests
// =============================================================================

TEST_F(DiagnosticReportBuilderTest, FhirCodingFactoryMethods) {
    auto loinc = fhir_coding::loinc("18748-4", "Diagnostic imaging study");
    EXPECT_EQ(loinc.system, "http://loinc.org");
    EXPECT_EQ(loinc.code, "18748-4");
    EXPECT_EQ(loinc.display.value_or(""), "Diagnostic imaging study");

    auto snomed = fhir_coding::snomed("260385009", "Negative");
    EXPECT_EQ(snomed.system, "http://snomed.info/sct");
    EXPECT_EQ(snomed.code, "260385009");
    EXPECT_EQ(snomed.display.value_or(""), "Negative");

    auto hl7 = fhir_coding::hl7v2("0074", "RAD", "Radiology");
    EXPECT_EQ(hl7.system, "http://terminology.hl7.org/CodeSystem/v2-0074");
    EXPECT_EQ(hl7.code, "RAD");
    EXPECT_EQ(hl7.display.value_or(""), "Radiology");

    auto dicom = fhir_coding::dicom("CT", "Computed Tomography");
    EXPECT_EQ(dicom.system, "http://dicom.nema.org/resources/ontology/DCM");
    EXPECT_EQ(dicom.code, "CT");
    EXPECT_EQ(dicom.display.value_or(""), "Computed Tomography");
}

// =============================================================================
// FHIR Reference Tests
// =============================================================================

TEST_F(DiagnosticReportBuilderTest, FhirReferenceFactoryMethods) {
    auto ref = fhir_reference::from_id("Patient", "123");
    EXPECT_EQ(ref.reference.value_or(""), "Patient/123");
    EXPECT_EQ(ref.type.value_or(""), "Patient");

    auto ref2 = fhir_reference::from_string("Organization/456");
    EXPECT_EQ(ref2.reference.value_or(""), "Organization/456");
}

// =============================================================================
// Result Tracker Tests
// =============================================================================

TEST_F(ResultTrackerTest, TrackNewResult) {
    posted_result result;
    result.report_id = "report-123";
    result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    result.accession_number = "ACC12345";
    result.status = result_status::final_report;
    result.posted_at = std::chrono::system_clock::now();

    EXPECT_TRUE(tracker_->track(result));
    EXPECT_EQ(tracker_->size(), 1);
    EXPECT_TRUE(tracker_->exists("1.2.3.4.5.6.7.8.9"));
}

TEST_F(ResultTrackerTest, GetByStudyUid) {
    posted_result result;
    result.report_id = "report-123";
    result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    result.accession_number = "ACC12345";
    result.status = result_status::final_report;
    result.posted_at = std::chrono::system_clock::now();

    EXPECT_TRUE(tracker_->track(result));

    auto retrieved = tracker_->get_by_study_uid("1.2.3.4.5.6.7.8.9");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->report_id, "report-123");
    EXPECT_EQ(retrieved->study_instance_uid, "1.2.3.4.5.6.7.8.9");
    EXPECT_EQ(retrieved->accession_number.value_or(""), "ACC12345");
}

TEST_F(ResultTrackerTest, GetByAccessionNumber) {
    posted_result result;
    result.report_id = "report-123";
    result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    result.accession_number = "ACC12345";
    result.status = result_status::final_report;
    result.posted_at = std::chrono::system_clock::now();

    EXPECT_TRUE(tracker_->track(result));

    auto retrieved = tracker_->get_by_accession("ACC12345");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->report_id, "report-123");
}

TEST_F(ResultTrackerTest, GetByReportId) {
    posted_result result;
    result.report_id = "report-123";
    result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    result.accession_number = "ACC12345";
    result.status = result_status::final_report;
    result.posted_at = std::chrono::system_clock::now();

    EXPECT_TRUE(tracker_->track(result));

    auto retrieved = tracker_->get_by_report_id("report-123");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->study_instance_uid, "1.2.3.4.5.6.7.8.9");
}

TEST_F(ResultTrackerTest, GetNonExistent) {
    EXPECT_FALSE(tracker_->get_by_study_uid("nonexistent").has_value());
    EXPECT_FALSE(tracker_->get_by_accession("nonexistent").has_value());
    EXPECT_FALSE(tracker_->get_by_report_id("nonexistent").has_value());
    EXPECT_FALSE(tracker_->exists("nonexistent"));
}

TEST_F(ResultTrackerTest, UpdateExisting) {
    posted_result result;
    result.report_id = "report-123";
    result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    result.status = result_status::preliminary;
    result.posted_at = std::chrono::system_clock::now();

    EXPECT_TRUE(tracker_->track(result));

    // Update status
    result.status = result_status::final_report;
    result.updated_at = std::chrono::system_clock::now();

    EXPECT_TRUE(tracker_->update("1.2.3.4.5.6.7.8.9", result));

    auto retrieved = tracker_->get_by_study_uid("1.2.3.4.5.6.7.8.9");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->status, result_status::final_report);
}

TEST_F(ResultTrackerTest, UpdateNonExistent) {
    posted_result result;
    result.report_id = "report-123";
    result.study_instance_uid = "nonexistent";
    result.status = result_status::final_report;
    result.posted_at = std::chrono::system_clock::now();

    EXPECT_FALSE(tracker_->update("nonexistent", result));
}

TEST_F(ResultTrackerTest, Remove) {
    posted_result result;
    result.report_id = "report-123";
    result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    result.accession_number = "ACC12345";
    result.status = result_status::final_report;
    result.posted_at = std::chrono::system_clock::now();

    EXPECT_TRUE(tracker_->track(result));
    EXPECT_EQ(tracker_->size(), 1);

    EXPECT_TRUE(tracker_->remove("1.2.3.4.5.6.7.8.9"));
    EXPECT_EQ(tracker_->size(), 0);
    EXPECT_FALSE(tracker_->exists("1.2.3.4.5.6.7.8.9"));

    // Also check indices are cleaned
    EXPECT_FALSE(tracker_->get_by_accession("ACC12345").has_value());
    EXPECT_FALSE(tracker_->get_by_report_id("report-123").has_value());
}

TEST_F(ResultTrackerTest, Clear) {
    for (int i = 0; i < 10; ++i) {
        posted_result result;
        result.report_id = "report-" + std::to_string(i);
        result.study_instance_uid = "1.2.3.4.5.6.7.8." + std::to_string(i);
        result.status = result_status::final_report;
        result.posted_at = std::chrono::system_clock::now();
        EXPECT_TRUE(tracker_->track(result));
    }

    EXPECT_EQ(tracker_->size(), 10);

    tracker_->clear();
    EXPECT_EQ(tracker_->size(), 0);
}

TEST_F(ResultTrackerTest, Keys) {
    for (int i = 0; i < 5; ++i) {
        posted_result result;
        result.report_id = "report-" + std::to_string(i);
        result.study_instance_uid = "1.2.3.4.5.6.7.8." + std::to_string(i);
        result.status = result_status::final_report;
        result.posted_at = std::chrono::system_clock::now();
        EXPECT_TRUE(tracker_->track(result));
    }

    auto keys = tracker_->keys();
    EXPECT_EQ(keys.size(), 5);
}

TEST_F(ResultTrackerTest, MaxEntriesEviction) {
    result_tracker_config config;
    config.max_entries = 5;
    config.ttl = std::chrono::hours{24};
    tracker_ = std::make_unique<in_memory_result_tracker>(config);

    // Add more entries than max
    for (int i = 0; i < 10; ++i) {
        posted_result result;
        result.report_id = "report-" + std::to_string(i);
        result.study_instance_uid = "1.2.3.4.5.6.7.8." + std::to_string(i);
        result.status = result_status::final_report;
        result.posted_at = std::chrono::system_clock::now();
        EXPECT_TRUE(tracker_->track(result));
    }

    // Should have max_entries entries due to eviction
    EXPECT_EQ(tracker_->size(), 5);

    auto stats = tracker_->get_statistics();
    EXPECT_EQ(stats.evictions, 5);
}

TEST_F(ResultTrackerTest, Statistics) {
    auto stats = tracker_->get_statistics();
    EXPECT_EQ(stats.total_tracked, 0);
    EXPECT_EQ(stats.current_size, 0);
    EXPECT_EQ(stats.expired_cleaned, 0);
    EXPECT_EQ(stats.evictions, 0);

    posted_result result;
    result.report_id = "report-123";
    result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    result.status = result_status::final_report;
    result.posted_at = std::chrono::system_clock::now();

    EXPECT_TRUE(tracker_->track(result));

    stats = tracker_->get_statistics();
    EXPECT_EQ(stats.total_tracked, 1);
    EXPECT_EQ(stats.current_size, 1);
}

// =============================================================================
// Posted Result Tests
// =============================================================================

TEST_F(ResultStatusTest, PostedResultDefaults) {
    posted_result result;
    EXPECT_TRUE(result.report_id.empty());
    EXPECT_TRUE(result.study_instance_uid.empty());
    EXPECT_FALSE(result.accession_number.has_value());
    EXPECT_EQ(result.status, result_status::final_report);
    EXPECT_FALSE(result.etag.has_value());
    EXPECT_FALSE(result.updated_at.has_value());
}

// =============================================================================
// Result Poster Config Tests
// =============================================================================

TEST_F(ResultStatusTest, ResultPosterConfigDefaults) {
    result_poster_config config;
    EXPECT_TRUE(config.check_duplicates);
    EXPECT_TRUE(config.enable_tracking);
    EXPECT_FALSE(config.auto_create_imaging_study_ref);
    EXPECT_TRUE(config.auto_lookup_patient);
    EXPECT_EQ(config.default_loinc_code, "18748-4");
    EXPECT_EQ(config.default_loinc_display, "Diagnostic imaging study");
    EXPECT_FALSE(config.issuing_organization.has_value());
    EXPECT_EQ(config.post_timeout.count(), 30);
}

// =============================================================================
// Result Tracker Config Tests
// =============================================================================

TEST_F(ResultTrackerTest, ConfigDefaults) {
    result_tracker_config config;
    EXPECT_EQ(config.max_entries, 10000);
    EXPECT_EQ(config.ttl.count(), 24 * 7);
    EXPECT_TRUE(config.auto_cleanup);
    EXPECT_EQ(config.cleanup_interval.count(), 60);
}

}  // namespace
}  // namespace pacs::bridge::emr
