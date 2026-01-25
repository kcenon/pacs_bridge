/**
 * @file fhir_dicom_mapper_test.cpp
 * @brief Unit tests for FHIR-DICOM mapper
 *
 * Tests the bidirectional mapping between FHIR R4 resources and DICOM
 * data structures.
 *
 * @see include/pacs/bridge/mapping/fhir_dicom_mapper.h
 * @see https://github.com/kcenon/pacs_bridge/issues/35
 */

#include "pacs/bridge/mapping/fhir_dicom_mapper.h"

#include "pacs/bridge/fhir/patient_resource.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace pacs::bridge::mapping {
namespace {

// =============================================================================
// Test Fixtures
// =============================================================================

class FhirDicomMapperTest : public ::testing::Test {
protected:
    void SetUp() override {
        mapper_ = std::make_unique<fhir_dicom_mapper>();
    }

    void TearDown() override {
        mapper_.reset();
    }

    std::unique_ptr<fhir_dicom_mapper> mapper_;
};

// =============================================================================
// DateTime Conversion Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, FhirDateTimeToDicom_BasicFormat) {
    auto result = fhir_dicom_mapper::fhir_datetime_to_dicom("2024-01-15T10:30:00Z");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().first, "20240115");
    EXPECT_EQ(result.value().second, "103000");
}

TEST_F(FhirDicomMapperTest, FhirDateTimeToDicom_WithMilliseconds) {
    auto result = fhir_dicom_mapper::fhir_datetime_to_dicom("2024-01-15T10:30:45.123Z");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().first, "20240115");
    EXPECT_EQ(result.value().second, "103045.123");
}

TEST_F(FhirDicomMapperTest, FhirDateTimeToDicom_WithTimezone) {
    auto result = fhir_dicom_mapper::fhir_datetime_to_dicom("2024-01-15T10:30:00+09:00");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().first, "20240115");
    EXPECT_EQ(result.value().second, "103000");
}

TEST_F(FhirDicomMapperTest, FhirDateTimeToDicom_DateOnly) {
    auto result = fhir_dicom_mapper::fhir_datetime_to_dicom("2024-01-15");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().first, "20240115");
    EXPECT_EQ(result.value().second, "");
}

TEST_F(FhirDicomMapperTest, FhirDateTimeToDicom_InvalidFormat) {
    auto result = fhir_dicom_mapper::fhir_datetime_to_dicom("invalid");
    ASSERT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, static_cast<int>(fhir_dicom_error::datetime_conversion_failed));
}

TEST_F(FhirDicomMapperTest, DicomDateTimeToFhir_BasicFormat) {
    auto result = fhir_dicom_mapper::dicom_datetime_to_fhir("20240115", "103000");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), "2024-01-15T10:30:00");
}

TEST_F(FhirDicomMapperTest, DicomDateTimeToFhir_WithFractionalSeconds) {
    auto result = fhir_dicom_mapper::dicom_datetime_to_fhir("20240115", "103045.123456");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), "2024-01-15T10:30:45.123");
}

TEST_F(FhirDicomMapperTest, DicomDateTimeToFhir_DateOnly) {
    auto result = fhir_dicom_mapper::dicom_datetime_to_fhir("20240115", "");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), "2024-01-15");
}

TEST_F(FhirDicomMapperTest, DicomDateTimeToFhir_InvalidDate) {
    auto result = fhir_dicom_mapper::dicom_datetime_to_fhir("2024011", "103000");
    ASSERT_FALSE(result.is_ok());
}

// =============================================================================
// Priority Conversion Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, FhirPriorityToDicom_Stat) {
    EXPECT_EQ(fhir_dicom_mapper::fhir_priority_to_dicom("stat"), "STAT");
}

TEST_F(FhirDicomMapperTest, FhirPriorityToDicom_Asap) {
    EXPECT_EQ(fhir_dicom_mapper::fhir_priority_to_dicom("asap"), "HIGH");
}

TEST_F(FhirDicomMapperTest, FhirPriorityToDicom_Urgent) {
    EXPECT_EQ(fhir_dicom_mapper::fhir_priority_to_dicom("urgent"), "HIGH");
}

TEST_F(FhirDicomMapperTest, FhirPriorityToDicom_Routine) {
    EXPECT_EQ(fhir_dicom_mapper::fhir_priority_to_dicom("routine"), "MEDIUM");
}

TEST_F(FhirDicomMapperTest, DicomPriorityToFhir_Stat) {
    EXPECT_EQ(fhir_dicom_mapper::dicom_priority_to_fhir("STAT"), "stat");
}

TEST_F(FhirDicomMapperTest, DicomPriorityToFhir_High) {
    EXPECT_EQ(fhir_dicom_mapper::dicom_priority_to_fhir("HIGH"), "urgent");
}

TEST_F(FhirDicomMapperTest, DicomPriorityToFhir_Medium) {
    EXPECT_EQ(fhir_dicom_mapper::dicom_priority_to_fhir("MEDIUM"), "routine");
}

// =============================================================================
// Patient Reference Parsing Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, ParsePatientReference_SimpleFormat) {
    auto result = fhir_dicom_mapper::parse_patient_reference("Patient/12345");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "12345");
}

TEST_F(FhirDicomMapperTest, ParsePatientReference_AbsoluteUrl) {
    auto result = fhir_dicom_mapper::parse_patient_reference(
        "http://hospital.local/fhir/Patient/12345");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "12345");
}

TEST_F(FhirDicomMapperTest, ParsePatientReference_WithQueryString) {
    auto result = fhir_dicom_mapper::parse_patient_reference("Patient/12345?_format=json");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "12345");
}

TEST_F(FhirDicomMapperTest, ParsePatientReference_Invalid) {
    auto result = fhir_dicom_mapper::parse_patient_reference("Organization/12345");
    ASSERT_FALSE(result.has_value());
}

// =============================================================================
// UID Generation Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, GenerateUid_NotEmpty) {
    std::string uid = mapper_->generate_uid();
    EXPECT_FALSE(uid.empty());
}

TEST_F(FhirDicomMapperTest, GenerateUid_StartsWithRoot) {
    std::string uid = mapper_->generate_uid();
    EXPECT_TRUE(uid.find(mapper_->config().uid_root) == 0);
}

TEST_F(FhirDicomMapperTest, GenerateUid_Unique) {
    std::string uid1 = mapper_->generate_uid();
    std::string uid2 = mapper_->generate_uid();
    EXPECT_NE(uid1, uid2);
}

TEST_F(FhirDicomMapperTest, GenerateUid_WithSuffix) {
    std::string uid = mapper_->generate_uid("SPS");
    EXPECT_TRUE(uid.find("SPS") != std::string::npos);
}

// =============================================================================
// Code Translation Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, LoincToDicom_KnownCode) {
    auto result = mapper_->loinc_to_dicom("24558-9");  // CT Chest
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->code, "CT");
}

TEST_F(FhirDicomMapperTest, LoincToDicom_UnknownCode) {
    auto result = mapper_->loinc_to_dicom("unknown-code");
    EXPECT_FALSE(result.has_value());
}

TEST_F(FhirDicomMapperTest, SnomedToDicom_KnownCode) {
    auto result = mapper_->snomed_to_dicom("51185008");  // Chest
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->code, "CHEST");
}

TEST_F(FhirDicomMapperTest, SnomedToDicom_UnknownCode) {
    auto result = mapper_->snomed_to_dicom("unknown-code");
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// ServiceRequest to MWL Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, ServiceRequestToMwl_Basic) {
    fhir_service_request request;
    request.id = "order-123";
    request.status = "active";
    request.intent = "order";
    request.priority = "routine";

    // Set procedure code
    fhir_coding coding;
    coding.system = "http://loinc.org";
    coding.code = "24558-9";
    coding.display = "CT Chest";
    request.code.coding.push_back(coding);

    // Set subject
    request.subject.reference = "Patient/patient-123";

    // Set occurrence
    request.occurrence_date_time = "2024-01-15T10:00:00Z";

    // Create patient
    dicom_patient patient;
    patient.patient_id = "patient-123";
    patient.patient_name = "Doe^John";
    patient.patient_birth_date = "19800101";
    patient.patient_sex = "M";

    auto result = mapper_->service_request_to_mwl(request, patient);
    ASSERT_TRUE(result.is_ok());

    // Verify patient data
    EXPECT_EQ(result.value().patient.patient_id, "patient-123");
    EXPECT_EQ(result.value().patient.patient_name, "Doe^John");

    // Verify scheduled procedure step
    ASSERT_FALSE(result.value().scheduled_steps.empty());
    const auto& sps = result.value().scheduled_steps[0];
    EXPECT_EQ(sps.scheduled_start_date, "20240115");
    EXPECT_EQ(sps.scheduled_start_time, "100000");
    EXPECT_EQ(sps.modality, "CT");  // Mapped from LOINC
    EXPECT_EQ(sps.scheduled_step_description, "CT Chest");

    // Verify requested procedure
    EXPECT_FALSE(result.value().requested_procedure.study_instance_uid.empty());
    EXPECT_EQ(result.value().requested_procedure.procedure_code_value, "24558-9");
    EXPECT_EQ(result.value().requested_procedure.requested_procedure_priority, "MEDIUM");
}

TEST_F(FhirDicomMapperTest, ServiceRequestToMwl_WithIdentifiers) {
    fhir_service_request request;
    request.id = "order-456";
    request.identifiers.push_back({"http://hospital/accession", "ACSN-001"});
    request.identifiers.push_back({"http://hospital/placer", "PLACER-001"});

    fhir_coding coding;
    coding.system = "http://local";
    coding.code = "XR-CHEST";
    coding.display = "Chest X-Ray";
    request.code.coding.push_back(coding);

    request.subject.reference = "Patient/patient-456";

    dicom_patient patient;
    patient.patient_id = "patient-456";

    auto result = mapper_->service_request_to_mwl(request, patient);
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(result.value().imaging_service_request.accession_number, "ACSN-001");
    EXPECT_EQ(result.value().imaging_service_request.placer_order_number, "PLACER-001");
}

TEST_F(FhirDicomMapperTest, ServiceRequestToMwl_ValidationFails) {
    fhir_service_request request;
    request.status = "invalid-status";  // Invalid status

    dicom_patient patient;
    patient.patient_id = "test";

    // With validation enabled (default), this should fail
    auto result = mapper_->service_request_to_mwl(request, patient);
    EXPECT_FALSE(result.is_ok());
}

TEST_F(FhirDicomMapperTest, ServiceRequestToMwl_WithPerformer) {
    fhir_service_request request;
    request.id = "order-789";

    fhir_coding coding;
    coding.system = "http://local";
    coding.code = "MRI";
    coding.display = "MRI Brain";
    request.code.coding.push_back(coding);

    request.subject.reference = "Patient/patient-789";

    // Set performer
    fhir_reference performer;
    performer.reference = "AE_TITLE_1";
    performer.display = "Dr. Smith";
    request.performer.push_back(performer);

    dicom_patient patient;
    patient.patient_id = "patient-789";

    auto result = mapper_->service_request_to_mwl(request, patient);
    ASSERT_TRUE(result.is_ok());

    ASSERT_FALSE(result.value().scheduled_steps.empty());
    EXPECT_EQ(result.value().scheduled_steps[0].scheduled_station_ae_title, "AE_TITLE_1");
    EXPECT_EQ(result.value().scheduled_steps[0].scheduled_performing_physician, "Dr. Smith");
}

// =============================================================================
// DICOM Study to ImagingStudy Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, StudyToImagingStudy_Basic) {
    dicom_study study;
    study.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    study.study_date = "20240115";
    study.study_time = "103000";
    study.accession_number = "ACSN-001";
    study.study_description = "CT Chest with contrast";
    study.patient_id = "patient-123";
    study.patient_name = "Doe^John";
    study.referring_physician_name = "Dr. Smith";
    study.number_of_series = 3;
    study.number_of_instances = 150;
    study.status = "available";

    auto result = mapper_->study_to_imaging_study(study);
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(result.value().status, "available");
    EXPECT_EQ(result.value().study_instance_uid, "1.2.3.4.5.6.7.8.9");
    EXPECT_EQ(result.value().number_of_series, 3u);
    EXPECT_EQ(result.value().number_of_instances, 150u);
    EXPECT_EQ(*result.value().description, "CT Chest with contrast");

    // Check identifiers
    EXPECT_FALSE(result.value().identifiers.empty());
    EXPECT_EQ(result.value().identifiers[0].second, "1.2.3.4.5.6.7.8.9");

    // Check started
    ASSERT_TRUE(result.value().started.has_value());
    EXPECT_EQ(*result.value().started, "2024-01-15T10:30:00");

    // Check subject
    ASSERT_TRUE(result.value().subject.reference.has_value());
    EXPECT_EQ(*result.value().subject.reference, "Patient/patient-123");
}

TEST_F(FhirDicomMapperTest, StudyToImagingStudy_WithSeries) {
    dicom_study study;
    study.study_instance_uid = "1.2.3.4.5";
    study.study_date = "20240115";

    dicom_series series1;
    series1.series_instance_uid = "1.2.3.4.5.1";
    series1.series_number = 1;
    series1.modality = "CT";
    series1.series_description = "Axial images";
    series1.number_of_instances = 50;
    study.series.push_back(series1);

    dicom_series series2;
    series2.series_instance_uid = "1.2.3.4.5.2";
    series2.series_number = 2;
    series2.modality = "CT";
    series2.series_description = "Coronal MPR";
    series2.number_of_instances = 30;
    study.series.push_back(series2);

    auto result = mapper_->study_to_imaging_study(study);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().series.size(), 2u);

    EXPECT_EQ(result.value().series[0].uid, "1.2.3.4.5.1");
    EXPECT_EQ(result.value().series[0].modality.code, "CT");
    EXPECT_EQ(*result.value().series[0].description, "Axial images");
    EXPECT_EQ(*result.value().series[0].number_of_instances, 50u);

    EXPECT_EQ(result.value().series[1].uid, "1.2.3.4.5.2");
    EXPECT_EQ(*result.value().series[1].number, 2u);
}

TEST_F(FhirDicomMapperTest, StudyToImagingStudy_CustomPatientReference) {
    dicom_study study;
    study.study_instance_uid = "1.2.3.4.5";
    study.study_date = "20240115";
    study.patient_id = "original-id";

    auto result = mapper_->study_to_imaging_study(study, "Patient/custom-ref-123");
    ASSERT_TRUE(result.is_ok());

    // Custom reference should be used
    EXPECT_EQ(*result.value().subject.reference, "Patient/custom-ref-123");
}

// =============================================================================
// Validation Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, ValidateServiceRequest_Valid) {
    fhir_service_request request;
    request.status = "active";
    request.intent = "order";

    fhir_coding coding;
    coding.system = "http://local";
    coding.code = "TEST";
    coding.display = "Test procedure";
    request.code.coding.push_back(coding);

    request.subject.reference = "Patient/123";

    auto errors = mapper_->validate_service_request(request);
    EXPECT_TRUE(errors.empty());
}

TEST_F(FhirDicomMapperTest, ValidateServiceRequest_MissingCode) {
    fhir_service_request request;
    request.status = "active";
    request.intent = "order";
    request.subject.reference = "Patient/123";
    // Missing code

    auto errors = mapper_->validate_service_request(request);
    EXPECT_FALSE(errors.empty());
    EXPECT_TRUE(errors[0].find("code") != std::string::npos);
}

TEST_F(FhirDicomMapperTest, ValidateServiceRequest_MissingSubject) {
    fhir_service_request request;
    request.status = "active";
    request.intent = "order";

    fhir_coding coding;
    coding.code = "TEST";
    request.code.coding.push_back(coding);
    // Missing subject

    auto errors = mapper_->validate_service_request(request);
    EXPECT_FALSE(errors.empty());
    EXPECT_TRUE(errors[0].find("subject") != std::string::npos);
}

TEST_F(FhirDicomMapperTest, ValidateServiceRequest_InvalidStatus) {
    fhir_service_request request;
    request.status = "invalid";
    request.intent = "order";

    fhir_coding coding;
    coding.code = "TEST";
    request.code.coding.push_back(coding);

    request.subject.reference = "Patient/123";

    auto errors = mapper_->validate_service_request(request);
    EXPECT_FALSE(errors.empty());
    EXPECT_TRUE(errors[0].find("status") != std::string::npos);
}

TEST_F(FhirDicomMapperTest, ValidateMwl_Valid) {
    mwl_item item;
    item.patient.patient_id = "patient-123";
    item.requested_procedure.study_instance_uid = "1.2.3.4.5";

    dicom_scheduled_procedure_step sps;
    sps.modality = "CT";
    item.scheduled_steps.push_back(sps);

    auto errors = mapper_->validate_mwl(item);
    EXPECT_TRUE(errors.empty());
}

TEST_F(FhirDicomMapperTest, ValidateMwl_MissingPatientId) {
    mwl_item item;
    // Missing patient_id
    item.requested_procedure.study_instance_uid = "1.2.3.4.5";

    dicom_scheduled_procedure_step sps;
    sps.modality = "CT";
    item.scheduled_steps.push_back(sps);

    auto errors = mapper_->validate_mwl(item);
    EXPECT_FALSE(errors.empty());
}

TEST_F(FhirDicomMapperTest, ValidateMwl_MissingModality) {
    mwl_item item;
    item.patient.patient_id = "patient-123";
    item.requested_procedure.study_instance_uid = "1.2.3.4.5";

    dicom_scheduled_procedure_step sps;
    // Missing modality
    item.scheduled_steps.push_back(sps);

    auto errors = mapper_->validate_mwl(item);
    EXPECT_FALSE(errors.empty());
}

// =============================================================================
// JSON Serialization Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, ImagingStudyToJson_Basic) {
    fhir_imaging_study study;
    study.id = "study-123";
    study.status = "available";
    study.study_instance_uid = "1.2.3.4.5";
    study.identifiers.push_back({"urn:dicom:uid", "1.2.3.4.5"});
    study.subject.reference = "Patient/patient-123";
    study.started = "2024-01-15T10:30:00";
    study.number_of_series = 3;
    study.number_of_instances = 150;
    study.description = "CT Chest";

    std::string json = imaging_study_to_json(study);

    EXPECT_TRUE(json.find("\"resourceType\": \"ImagingStudy\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"id\": \"study-123\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"status\": \"available\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"numberOfSeries\": 3") != std::string::npos);
    EXPECT_TRUE(json.find("\"numberOfInstances\": 150") != std::string::npos);
}

TEST_F(FhirDicomMapperTest, ServiceRequestToJson_Basic) {
    fhir_service_request request;
    request.id = "order-123";
    request.status = "active";
    request.intent = "order";
    request.priority = "routine";

    fhir_coding coding;
    coding.system = "http://loinc.org";
    coding.code = "24558-9";
    coding.display = "CT Chest";
    request.code.coding.push_back(coding);

    request.subject.reference = "Patient/patient-123";
    request.occurrence_date_time = "2024-01-15T10:00:00Z";

    std::string json = service_request_to_json(request);

    EXPECT_TRUE(json.find("\"resourceType\": \"ServiceRequest\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"id\": \"order-123\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"status\": \"active\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"intent\": \"order\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"priority\": \"routine\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"occurrenceDateTime\"") != std::string::npos);
}

// =============================================================================
// Configuration Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, Configuration_Default) {
    const auto& config = mapper_->config();
    EXPECT_FALSE(config.uid_root.empty());
    EXPECT_TRUE(config.auto_generate_study_uid);
    EXPECT_TRUE(config.auto_generate_sps_id);
    EXPECT_EQ(config.default_modality, "OT");
}

TEST_F(FhirDicomMapperTest, Configuration_Custom) {
    fhir_dicom_mapper_config config;
    config.uid_root = "1.2.840.custom";
    config.default_modality = "CT";
    config.default_station_ae_title = "CT_SCANNER";
    config.auto_generate_study_uid = false;

    mapper_->set_config(config);

    const auto& updated = mapper_->config();
    EXPECT_EQ(updated.uid_root, "1.2.840.custom");
    EXPECT_EQ(updated.default_modality, "CT");
    EXPECT_EQ(updated.default_station_ae_title, "CT_SCANNER");
    EXPECT_FALSE(updated.auto_generate_study_uid);
}

// =============================================================================
// Patient Lookup Tests
// =============================================================================

TEST_F(FhirDicomMapperTest, PatientLookup_NotConfigured) {
    fhir_service_request request;
    request.subject.reference = "Patient/123";

    fhir_coding coding;
    coding.code = "TEST";
    request.code.coding.push_back(coding);

    // Without patient lookup configured
    auto result = mapper_->service_request_to_mwl(request);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, static_cast<int>(fhir_dicom_error::patient_not_found));
}

TEST_F(FhirDicomMapperTest, PatientLookup_Configured) {
    // Configure patient lookup
    mapper_->set_patient_lookup([](const std::string& ref)
        -> Result<dicom_patient> {
        if (ref == "Patient/123") {
            dicom_patient patient;
            patient.patient_id = "123";
            patient.patient_name = "Test^Patient";
            return Result<dicom_patient>::ok(patient);
        }
        return Result<dicom_patient>::err(error_info(static_cast<int>(fhir_dicom_error::patient_not_found), "Patient not found", "fhir_dicom_mapper"));
    });

    fhir_service_request request;
    request.subject.reference = "Patient/123";

    fhir_coding coding;
    coding.code = "TEST";
    request.code.coding.push_back(coding);

    auto result = mapper_->service_request_to_mwl(request);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().patient.patient_id, "123");
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(FhirDicomMapperTest, EmptyStrings) {
    fhir_service_request request;
    request.id = "";  // Empty ID
    request.status = "active";
    request.intent = "order";

    fhir_coding coding;
    coding.system = "";  // Empty system
    coding.code = "TEST";
    coding.display = "";  // Empty display
    request.code.coding.push_back(coding);

    request.subject.reference = "Patient/123";

    dicom_patient patient;
    patient.patient_id = "123";

    // Should handle empty strings gracefully
    auto result = mapper_->service_request_to_mwl(request, patient);
    ASSERT_TRUE(result.is_ok());
}

TEST_F(FhirDicomMapperTest, SpecialCharactersInNames) {
    fhir_service_request request;

    fhir_coding coding;
    coding.code = "TEST";
    coding.display = "Test with \"quotes\" and \\ backslash";
    request.code.coding.push_back(coding);

    request.subject.reference = "Patient/123";

    dicom_patient patient;
    patient.patient_id = "123";
    patient.patient_name = "O'Brien^Mary^Jane";  // Special char in name

    auto result = mapper_->service_request_to_mwl(request, patient);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().patient.patient_name, "O'Brien^Mary^Jane");
}

}  // namespace
}  // namespace pacs::bridge::mapping
