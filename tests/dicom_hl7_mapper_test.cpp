/**
 * @file dicom_hl7_mapper_test.cpp
 * @brief Comprehensive unit tests for DICOM to HL7 mapper module
 *
 * Tests for MPPS to ORM conversion, date/time format conversion, and
 * name format conversion. Target coverage: >= 85%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/24
 */

#include "pacs/bridge/mapping/dicom_hl7_mapper.h"
#include "pacs/bridge/pacs_adapter/mpps_handler.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <cassert>
#include <iostream>
#include <string>

namespace pacs::bridge::mapping::test {

// =============================================================================
// Test Utilities
// =============================================================================

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_TEST(test_func)                                                    \
    do {                                                                       \
        std::cout << "Running " << #test_func << "..." << std::endl;           \
        if (test_func()) {                                                     \
            std::cout << "  PASSED" << std::endl;                              \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "  FAILED" << std::endl;                              \
            failed++;                                                          \
        }                                                                      \
    } while (0)

/**
 * @brief Create a sample MPPS dataset for testing
 */
pacs_adapter::mpps_dataset create_sample_mpps() {
    pacs_adapter::mpps_dataset mpps;

    // SOP Instance
    mpps.sop_instance_uid = "1.2.3.4.5.6.7.8.9.10";

    // Procedure Step Relationship
    mpps.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    mpps.accession_number = "ACC001";
    mpps.scheduled_procedure_step_id = "SPS001";
    mpps.performed_procedure_step_id = "PPS001";

    // Patient Information
    mpps.patient_id = "12345";
    mpps.patient_name = "DOE^JOHN^WILLIAM";

    // Status
    mpps.status = pacs_adapter::mpps_event::in_progress;
    mpps.performed_procedure_description = "Chest X-Ray PA and Lateral";

    // Timing
    mpps.start_date = "20240115";
    mpps.start_time = "120000";

    // Modality and Station
    mpps.modality = "CR";
    mpps.station_ae_title = "CR_SCANNER_01";
    mpps.station_name = "CR Room 1";

    // Referring Physician
    mpps.referring_physician = "SMITH^ROBERT^MD";
    mpps.requested_procedure_id = "RP001";

    return mpps;
}

/**
 * @brief Create MPPS for completed status
 */
pacs_adapter::mpps_dataset create_completed_mpps() {
    auto mpps = create_sample_mpps();
    mpps.status = pacs_adapter::mpps_event::completed;
    mpps.end_date = "20240115";
    mpps.end_time = "123500";

    // Add performed series
    pacs_adapter::mpps_performed_series series;
    series.series_instance_uid = "1.2.3.4.5.6.7.8.9.1";
    series.series_description = "PA View";
    series.modality = "CR";
    series.number_of_instances = 1;
    mpps.performed_series.push_back(series);

    series.series_instance_uid = "1.2.3.4.5.6.7.8.9.2";
    series.series_description = "Lateral View";
    series.number_of_instances = 1;
    mpps.performed_series.push_back(series);

    return mpps;
}

/**
 * @brief Create MPPS for discontinued status
 */
pacs_adapter::mpps_dataset create_discontinued_mpps() {
    auto mpps = create_sample_mpps();
    mpps.status = pacs_adapter::mpps_event::discontinued;
    mpps.end_date = "20240115";
    mpps.end_time = "121500";
    mpps.discontinuation_reason = "Patient refused examination";
    return mpps;
}

// =============================================================================
// Error Code Tests
// =============================================================================

bool test_error_codes() {
    // Verify error code range
    TEST_ASSERT(to_error_code(dicom_hl7_error::missing_required_attribute) == -930,
                "missing_required_attribute should be -930");
    TEST_ASSERT(to_error_code(dicom_hl7_error::serialization_failed) == -938,
                "serialization_failed should be -938");

    // Verify error messages
    TEST_ASSERT(std::string(to_string(dicom_hl7_error::missing_required_attribute)) ==
                    "Missing required MPPS attribute",
                "missing_required_attribute message");
    TEST_ASSERT(std::string(to_string(dicom_hl7_error::datetime_conversion_failed)) ==
                    "Date/time format conversion failed",
                "datetime_conversion_failed message");

    return true;
}

// =============================================================================
// Configuration Tests
// =============================================================================

bool test_mapper_config_defaults() {
    dicom_hl7_mapper_config config;

    TEST_ASSERT(config.sending_application == "PACS_BRIDGE",
                "Default sending application should be PACS_BRIDGE");
    TEST_ASSERT(config.receiving_application == "HIS",
                "Default receiving application should be HIS");
    TEST_ASSERT(config.hl7_version == "2.5.1",
                "Default HL7 version should be 2.5.1");
    TEST_ASSERT(config.processing_id == "P",
                "Default processing ID should be P");
    TEST_ASSERT(config.include_timing_details == true,
                "Include timing details should default to true");
    TEST_ASSERT(config.include_series_info == true,
                "Include series info should default to true");
    TEST_ASSERT(config.auto_generate_control_id == true,
                "Auto generate control ID should default to true");

    return true;
}

bool test_mapper_config_custom() {
    dicom_hl7_mapper_config config;
    config.sending_application = "RADIOLOGY_PACS";
    config.sending_facility = "HOSPITAL_A";
    config.receiving_application = "EPIC_HIS";
    config.receiving_facility = "INTEGRATION";

    dicom_hl7_mapper mapper(config);

    const auto& result_config = mapper.config();
    TEST_ASSERT(result_config.sending_application == "RADIOLOGY_PACS",
                "Sending application should match");
    TEST_ASSERT(result_config.sending_facility == "HOSPITAL_A",
                "Sending facility should match");
    TEST_ASSERT(result_config.receiving_application == "EPIC_HIS",
                "Receiving application should match");

    return true;
}

// =============================================================================
// MPPS Status Mapping Tests
// =============================================================================

bool test_status_to_order_status_mapping() {
    TEST_ASSERT(dicom_hl7_mapper::mpps_status_to_hl7_order_status(
                    pacs_adapter::mpps_event::in_progress) == "IP",
                "IN PROGRESS should map to IP");
    TEST_ASSERT(dicom_hl7_mapper::mpps_status_to_hl7_order_status(
                    pacs_adapter::mpps_event::completed) == "CM",
                "COMPLETED should map to CM");
    TEST_ASSERT(dicom_hl7_mapper::mpps_status_to_hl7_order_status(
                    pacs_adapter::mpps_event::discontinued) == "CA",
                "DISCONTINUED should map to CA");

    return true;
}

bool test_status_to_order_control_mapping() {
    TEST_ASSERT(dicom_hl7_mapper::mpps_status_to_hl7_order_control(
                    pacs_adapter::mpps_event::in_progress) == "SC",
                "IN PROGRESS should map to SC");
    TEST_ASSERT(dicom_hl7_mapper::mpps_status_to_hl7_order_control(
                    pacs_adapter::mpps_event::completed) == "SC",
                "COMPLETED should map to SC");
    TEST_ASSERT(dicom_hl7_mapper::mpps_status_to_hl7_order_control(
                    pacs_adapter::mpps_event::discontinued) == "DC",
                "DISCONTINUED should map to DC");

    return true;
}

// =============================================================================
// Date/Time Conversion Tests
// =============================================================================

bool test_dicom_date_to_hl7() {
    // Valid date
    auto result = dicom_hl7_mapper::dicom_date_to_hl7("20240115");
    TEST_ASSERT(result.has_value(), "Valid date should convert");
    TEST_ASSERT(*result == "20240115", "Date should be preserved");

    // Invalid date (wrong length)
    result = dicom_hl7_mapper::dicom_date_to_hl7("2024011");
    TEST_ASSERT(!result.has_value(), "Short date should fail");

    result = dicom_hl7_mapper::dicom_date_to_hl7("202401150");
    TEST_ASSERT(!result.has_value(), "Long date should fail");

    // Empty date
    result = dicom_hl7_mapper::dicom_date_to_hl7("");
    TEST_ASSERT(!result.has_value(), "Empty date should fail");

    // Non-numeric date
    result = dicom_hl7_mapper::dicom_date_to_hl7("2024AB15");
    TEST_ASSERT(!result.has_value(), "Non-numeric date should fail");

    return true;
}

bool test_dicom_time_to_hl7() {
    // Simple time without fractional seconds
    auto result = dicom_hl7_mapper::dicom_time_to_hl7("120000");
    TEST_ASSERT(result.has_value(), "Simple time should convert");
    TEST_ASSERT(*result == "120000", "Time should be preserved");

    // Time with fractional seconds
    result = dicom_hl7_mapper::dicom_time_to_hl7("120000.123456");
    TEST_ASSERT(result.has_value(), "Fractional time should convert");
    TEST_ASSERT(*result == "120000.1234", "Fractional should be truncated to 4 digits");

    // Time with short fractional seconds
    result = dicom_hl7_mapper::dicom_time_to_hl7("120000.12");
    TEST_ASSERT(result.has_value(), "Short fractional should convert");
    TEST_ASSERT(*result == "120000.12", "Short fractional should be preserved");

    // Short time (just hours)
    result = dicom_hl7_mapper::dicom_time_to_hl7("12");
    TEST_ASSERT(result.has_value(), "Short time should convert");
    TEST_ASSERT(*result == "12", "Short time should be preserved");

    // Empty time
    result = dicom_hl7_mapper::dicom_time_to_hl7("");
    TEST_ASSERT(!result.has_value(), "Empty time should fail");

    return true;
}

bool test_dicom_datetime_to_hl7_timestamp() {
    // Full datetime
    auto result = dicom_hl7_mapper::dicom_datetime_to_hl7_timestamp("20240115", "120000");
    TEST_ASSERT(result.has_value(), "Full datetime should convert");
    TEST_ASSERT(result->year == 2024, "Year should be 2024");
    TEST_ASSERT(result->month == 1, "Month should be 1");
    TEST_ASSERT(result->day == 15, "Day should be 15");
    TEST_ASSERT(result->hour == 12, "Hour should be 12");
    TEST_ASSERT(result->minute == 0, "Minute should be 0");
    TEST_ASSERT(result->second == 0, "Second should be 0");

    // With fractional seconds
    result = dicom_hl7_mapper::dicom_datetime_to_hl7_timestamp("20240115", "120030.123");
    TEST_ASSERT(result.has_value(), "Datetime with milliseconds should convert");
    TEST_ASSERT(result->second == 30, "Second should be 30");
    TEST_ASSERT(result->millisecond == 123, "Millisecond should be 123");

    // Invalid date
    result = dicom_hl7_mapper::dicom_datetime_to_hl7_timestamp("2024", "120000");
    TEST_ASSERT(!result.has_value(), "Short date should fail");

    return true;
}

// =============================================================================
// Name Conversion Tests
// =============================================================================

bool test_dicom_name_to_hl7() {
    // Full name with all components
    auto result = dicom_hl7_mapper::dicom_name_to_hl7("DOE^JOHN^WILLIAM^Dr^Jr");
    TEST_ASSERT(result.family_name == "DOE", "Family name should be DOE");
    TEST_ASSERT(result.given_name == "JOHN", "Given name should be JOHN");
    TEST_ASSERT(result.middle_name == "WILLIAM", "Middle name should be WILLIAM");
    TEST_ASSERT(result.prefix == "Dr", "Prefix should be Dr");
    TEST_ASSERT(result.suffix == "Jr", "Suffix should be Jr");

    // Simple name
    result = dicom_hl7_mapper::dicom_name_to_hl7("SMITH^JANE");
    TEST_ASSERT(result.family_name == "SMITH", "Family name should be SMITH");
    TEST_ASSERT(result.given_name == "JANE", "Given name should be JANE");
    TEST_ASSERT(result.middle_name.empty(), "Middle name should be empty");

    // Single component (family name only)
    result = dicom_hl7_mapper::dicom_name_to_hl7("DOE");
    TEST_ASSERT(result.family_name == "DOE", "Family name should be DOE");
    TEST_ASSERT(result.given_name.empty(), "Given name should be empty");

    // Empty name
    result = dicom_hl7_mapper::dicom_name_to_hl7("");
    TEST_ASSERT(result.family_name.empty(), "Empty input should give empty name");

    return true;
}

// =============================================================================
// MPPS to ORM Mapping Tests
// =============================================================================

bool test_mpps_in_progress_to_orm() {
    dicom_hl7_mapper mapper;
    auto mpps = create_sample_mpps();

    auto result = mapper.mpps_in_progress_to_orm(mpps);
    TEST_ASSERT(result.has_value(), "IN PROGRESS mapping should succeed");

    TEST_ASSERT(result->order_control == "SC",
                "Order control should be SC");
    TEST_ASSERT(result->order_status == "IP",
                "Order status should be IP");
    TEST_ASSERT(result->mpps_status == pacs_adapter::mpps_event::in_progress,
                "MPPS status should be in_progress");
    TEST_ASSERT(result->accession_number == "ACC001",
                "Accession number should match");
    TEST_ASSERT(!result->control_id.empty(),
                "Control ID should be generated");

    // Verify message structure
    const auto& msg = result->message;
    TEST_ASSERT(msg.get_value("MSH.9.1") == "ORM",
                "Message type should be ORM");
    TEST_ASSERT(msg.get_value("MSH.9.2") == "O01",
                "Trigger event should be O01");
    TEST_ASSERT(msg.get_value("ORC.1") == "SC",
                "ORC-1 should be SC");
    TEST_ASSERT(msg.get_value("ORC.5") == "IP",
                "ORC-5 should be IP");
    TEST_ASSERT(msg.get_value("OBR.24") == "CR",
                "OBR-24 should be modality");

    return true;
}

bool test_mpps_completed_to_orm() {
    dicom_hl7_mapper mapper;
    auto mpps = create_completed_mpps();

    auto result = mapper.mpps_completed_to_orm(mpps);
    TEST_ASSERT(result.has_value(), "COMPLETED mapping should succeed");

    TEST_ASSERT(result->order_control == "SC",
                "Order control should be SC");
    TEST_ASSERT(result->order_status == "CM",
                "Order status should be CM");
    TEST_ASSERT(result->mpps_status == pacs_adapter::mpps_event::completed,
                "MPPS status should be completed");

    // Verify message structure
    const auto& msg = result->message;
    TEST_ASSERT(msg.get_value("ORC.5") == "CM",
                "ORC-5 should be CM");
    TEST_ASSERT(msg.get_value("OBR.25") == "CM",
                "OBR-25 result status should be CM");

    // End datetime should be present
    auto obr27 = msg.get_value("OBR.27.4");
    TEST_ASSERT(!obr27.empty(), "OBR-27 should have end datetime");

    return true;
}

bool test_mpps_discontinued_to_orm() {
    dicom_hl7_mapper mapper;
    auto mpps = create_discontinued_mpps();

    auto result = mapper.mpps_discontinued_to_orm(mpps);
    TEST_ASSERT(result.has_value(), "DISCONTINUED mapping should succeed");

    TEST_ASSERT(result->order_control == "DC",
                "Order control should be DC for discontinued");
    TEST_ASSERT(result->order_status == "CA",
                "Order status should be CA");
    TEST_ASSERT(result->mpps_status == pacs_adapter::mpps_event::discontinued,
                "MPPS status should be discontinued");

    // Verify message structure
    const auto& msg = result->message;
    TEST_ASSERT(msg.get_value("ORC.1") == "DC",
                "ORC-1 should be DC");
    TEST_ASSERT(msg.get_value("ORC.5") == "CA",
                "ORC-5 should be CA");

    // Discontinuation reason should be in OBR-31
    auto obr31 = msg.get_value("OBR.31");
    TEST_ASSERT(obr31 == "Patient refused examination",
                "OBR-31 should have discontinuation reason");

    return true;
}

bool test_mpps_to_orm_generic() {
    dicom_hl7_mapper mapper;
    auto mpps = create_sample_mpps();

    // Test IN PROGRESS
    mpps.status = pacs_adapter::mpps_event::in_progress;
    auto result = mapper.mpps_to_orm(mpps, pacs_adapter::mpps_event::in_progress);
    TEST_ASSERT(result.has_value(), "Generic mapping should succeed");
    TEST_ASSERT(result->order_status == "IP", "Status should be IP");

    // Test COMPLETED
    mpps.status = pacs_adapter::mpps_event::completed;
    mpps.end_date = "20240115";
    mpps.end_time = "123500";
    result = mapper.mpps_to_orm(mpps, pacs_adapter::mpps_event::completed);
    TEST_ASSERT(result.has_value(), "Generic completed mapping should succeed");
    TEST_ASSERT(result->order_status == "CM", "Status should be CM");

    return true;
}

// =============================================================================
// Validation Tests
// =============================================================================

bool test_mpps_validation() {
    dicom_hl7_mapper mapper;
    pacs_adapter::mpps_dataset mpps;

    // Empty MPPS should have validation errors
    auto errors = mapper.validate_mpps(mpps);
    TEST_ASSERT(!errors.empty(), "Empty MPPS should have validation errors");

    // Valid MPPS
    auto valid_mpps = create_sample_mpps();
    errors = mapper.validate_mpps(valid_mpps);
    TEST_ASSERT(errors.empty(), "Valid MPPS should not have validation errors");

    // MPPS without accession number
    auto no_accession = create_sample_mpps();
    no_accession.accession_number = "";
    errors = mapper.validate_mpps(no_accession);
    TEST_ASSERT(!errors.empty(), "MPPS without accession should have errors");

    return true;
}

bool test_mpps_mapping_with_warnings() {
    dicom_hl7_mapper_config config;
    config.validate_before_build = false;  // Allow partial mapping

    dicom_hl7_mapper mapper(config);

    pacs_adapter::mpps_dataset mpps;
    mpps.accession_number = "ACC001";
    mpps.start_date = "20240115";
    // Missing patient_id - should generate warning

    auto result = mapper.mpps_in_progress_to_orm(mpps);
    TEST_ASSERT(result.has_value(), "Partial mapping should succeed");
    TEST_ASSERT(result->has_warnings(), "Should have warnings for missing fields");

    return true;
}

// =============================================================================
// Series Information Tests
// =============================================================================

bool test_series_info_in_orm() {
    dicom_hl7_mapper_config config;
    config.include_series_info = true;

    dicom_hl7_mapper mapper(config);
    auto mpps = create_completed_mpps();

    auto result = mapper.mpps_completed_to_orm(mpps);
    TEST_ASSERT(result.has_value(), "Mapping with series info should succeed");

    // Check for OBX segments with series information
    const auto& msg = result->message;
    auto segments = msg.get_segments("OBX");
    TEST_ASSERT(!segments.empty(), "Should have OBX segments");

    return true;
}

bool test_no_series_info_when_disabled() {
    dicom_hl7_mapper_config config;
    config.include_series_info = false;

    dicom_hl7_mapper mapper(config);
    auto mpps = create_completed_mpps();

    auto result = mapper.mpps_completed_to_orm(mpps);
    TEST_ASSERT(result.has_value(), "Mapping without series info should succeed");

    // Should not have OBX segments for series
    const auto& msg = result->message;
    auto segments = msg.get_segments("OBX");
    TEST_ASSERT(segments.empty(), "Should not have OBX segments when disabled");

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== DICOM to HL7 Mapper Error Code Tests ===" << std::endl;
    RUN_TEST(test_error_codes);

    std::cout << "\n=== Configuration Tests ===" << std::endl;
    RUN_TEST(test_mapper_config_defaults);
    RUN_TEST(test_mapper_config_custom);

    std::cout << "\n=== Status Mapping Tests ===" << std::endl;
    RUN_TEST(test_status_to_order_status_mapping);
    RUN_TEST(test_status_to_order_control_mapping);

    std::cout << "\n=== Date/Time Conversion Tests ===" << std::endl;
    RUN_TEST(test_dicom_date_to_hl7);
    RUN_TEST(test_dicom_time_to_hl7);
    RUN_TEST(test_dicom_datetime_to_hl7_timestamp);

    std::cout << "\n=== Name Conversion Tests ===" << std::endl;
    RUN_TEST(test_dicom_name_to_hl7);

    std::cout << "\n=== MPPS to ORM Mapping Tests ===" << std::endl;
    RUN_TEST(test_mpps_in_progress_to_orm);
    RUN_TEST(test_mpps_completed_to_orm);
    RUN_TEST(test_mpps_discontinued_to_orm);
    RUN_TEST(test_mpps_to_orm_generic);

    std::cout << "\n=== Validation Tests ===" << std::endl;
    RUN_TEST(test_mpps_validation);
    RUN_TEST(test_mpps_mapping_with_warnings);

    std::cout << "\n=== Series Information Tests ===" << std::endl;
    RUN_TEST(test_series_info_in_orm);
    RUN_TEST(test_no_series_info_when_disabled);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::mapping::test

int main() {
    return pacs::bridge::mapping::test::run_all_tests();
}
