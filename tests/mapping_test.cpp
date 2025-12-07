/**
 * @file mapping_test.cpp
 * @brief Comprehensive unit tests for HL7 to DICOM mapping module
 *
 * Tests for HL7 to DICOM MWL conversion, data type mapping, and
 * format conversions. Target coverage: >= 85%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/21
 */

#include "pacs/bridge/mapping/hl7_dicom_mapper.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"
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

// Sample ORM O01 message for testing
const std::string SAMPLE_ORM_O01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG003|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||123 MAIN ST^^SPRINGFIELD^IL^62701||555-123-4567\r"
    "PV1|1|I|WARD^101^A^HOSPITAL||||SMITH^ROBERT^MD\r"
    "ORC|NW|ORD001^HIS|ACC001^PACS||SC|||^^^20240115120000^^R||20240115110000|JONES^MARY^RN||||RADIOLOGY\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT||20240115110000|20240115120000||||||||SMITH^ROBERT^MD||||||20240115110000|||1^ROUTINE^HL70078\r";

// =============================================================================
// DICOM Data Structure Tests
// =============================================================================

bool test_dicom_patient_structure() {
    dicom_patient patient;
    patient.patient_id = "12345";
    patient.issuer_of_patient_id = "HOSPITAL";
    patient.patient_name = "DOE^JOHN^WILLIAM";
    patient.patient_birth_date = "19800515";
    patient.patient_sex = "M";
    patient.patient_address = "123 MAIN ST, SPRINGFIELD, IL 62701";
    patient.patient_phone = "555-123-4567";

    TEST_ASSERT(patient.patient_id == "12345", "Patient ID should match");
    TEST_ASSERT(patient.patient_sex == "M", "Sex should be M");
    TEST_ASSERT(!patient.patient_name.empty(), "Name should not be empty");

    return true;
}

bool test_dicom_requested_procedure() {
    dicom_requested_procedure proc;
    proc.requested_procedure_id = "RP001";
    proc.requested_procedure_description = "Chest X-Ray 2 Views";
    proc.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    proc.accession_number = "ACC001";
    proc.requesting_physician = "SMITH^ROBERT^MD";
    proc.referring_physician = "JONES^MARY^MD";

    TEST_ASSERT(proc.requested_procedure_id == "RP001", "Procedure ID should match");
    TEST_ASSERT(!proc.study_instance_uid.empty(), "Study UID should not be empty");

    return true;
}

bool test_dicom_scheduled_procedure_step() {
    dicom_scheduled_procedure_step sps;
    sps.scheduled_procedure_step_id = "SPS001";
    sps.scheduled_procedure_step_description = "Chest PA and Lateral";
    sps.modality = "CR";
    sps.scheduled_performing_physician = "JOHNSON^LISA^RT";
    sps.scheduled_procedure_step_start_date = "20240115";
    sps.scheduled_procedure_step_start_time = "120000";
    sps.scheduled_station_ae_title = "CT_SCANNER_01";
    sps.scheduled_station_name = "Radiology Room 1";
    sps.scheduled_procedure_step_status = "SCHEDULED";

    TEST_ASSERT(sps.modality == "CR", "Modality should be CR");
    TEST_ASSERT(sps.scheduled_procedure_step_status == "SCHEDULED", "Status should be SCHEDULED");

    return true;
}

bool test_dicom_imaging_service_request() {
    dicom_imaging_service_request isr;
    isr.accession_number = "ACC001";
    isr.requesting_physician = "SMITH^ROBERT^MD";
    isr.referring_physician = "JONES^MARY^MD";
    isr.placer_order_number = "ORD001";
    isr.filler_order_number = "FILL001";
    isr.order_status = "SCHEDULED";

    TEST_ASSERT(isr.accession_number == "ACC001", "Accession number should match");
    TEST_ASSERT(isr.order_status == "SCHEDULED", "Order status should match");

    return true;
}

bool test_mwl_item_complete() {
    mwl_item item;

    // Set patient info
    item.patient.patient_id = "12345";
    item.patient.patient_name = "DOE^JOHN";
    item.patient.patient_birth_date = "19800515";
    item.patient.patient_sex = "M";

    // Set procedure info
    item.requested_procedure.requested_procedure_id = "RP001";
    item.requested_procedure.study_instance_uid = "1.2.3.4.5";
    item.requested_procedure.accession_number = "ACC001";

    // Set SPS info
    item.scheduled_procedure_step.scheduled_procedure_step_id = "SPS001";
    item.scheduled_procedure_step.modality = "CR";
    item.scheduled_procedure_step.scheduled_procedure_step_start_date = "20240115";

    // Set imaging request
    item.imaging_service_request.accession_number = "ACC001";
    item.imaging_service_request.order_status = "SCHEDULED";

    // Verify structure
    TEST_ASSERT(item.patient.patient_id == "12345", "Patient ID should match");
    TEST_ASSERT(item.requested_procedure.accession_number == "ACC001", "Accession should match");
    TEST_ASSERT(item.scheduled_procedure_step.modality == "CR", "Modality should match");

    return true;
}

// =============================================================================
// Mapping Error Tests
// =============================================================================

bool test_mapping_error_codes() {
    TEST_ASSERT(to_error_code(mapping_error::invalid_message_type) == -940,
                "invalid_message_type should be -940");
    TEST_ASSERT(to_error_code(mapping_error::uid_generation_failed) == -948,
                "uid_generation_failed should be -948");

    TEST_ASSERT(std::string(to_string(mapping_error::missing_patient_id)) ==
                    "Missing required patient identifier",
                "Error message should match");
    TEST_ASSERT(std::string(to_string(mapping_error::missing_order_info)) ==
                    "Missing required order information",
                "Error message should match");

    return true;
}

// =============================================================================
// HL7 to DICOM Mapper Tests
// =============================================================================

bool test_mapper_default_config() {
    hl7_dicom_mapper mapper;

    auto config = mapper.config();
    TEST_ASSERT(config.generate_study_uid, "Should generate study UIDs by default");
    TEST_ASSERT(!config.uid_root.empty(), "UID root should not be empty");
    TEST_ASSERT(config.default_modality == "OT", "Default modality should be OT");
    TEST_ASSERT(config.default_station_ae.empty(), "Default station AE should be empty");

    return true;
}

bool test_mapper_custom_config() {
    mapper_config config;
    config.uid_root = "1.2.3.4.5";
    config.generate_study_uid = false;
    config.default_modality = "CR";
    config.default_station_ae = "RADIOLOGY_01";

    hl7_dicom_mapper mapper(config);

    auto retrieved = mapper.config();
    TEST_ASSERT(retrieved.uid_root == "1.2.3.4.5", "UID root should match");
    TEST_ASSERT(!retrieved.generate_study_uid, "Should not generate UIDs");
    TEST_ASSERT(retrieved.default_modality == "CR", "Default modality should be CR");
    TEST_ASSERT(retrieved.default_station_ae == "RADIOLOGY_01", "Station AE should match");

    return true;
}

bool test_mapper_orm_to_mwl() {
    hl7::hl7_parser parser;
    auto parse_result = parser.parse(SAMPLE_ORM_O01);
    TEST_ASSERT(parse_result.has_value(), "Should parse ORM O01 successfully");

    hl7_dicom_mapper mapper;
    auto map_result = mapper.map_to_mwl(*parse_result);
    TEST_ASSERT(map_result.has_value(), "Should map to MWL successfully");

    const auto& mwl = *map_result;

    // Check patient info
    TEST_ASSERT(mwl.patient.patient_id == "12345", "Patient ID should be 12345");
    TEST_ASSERT(mwl.patient.patient_name == "DOE^JOHN^WILLIAM", "Patient name should match");
    TEST_ASSERT(mwl.patient.patient_birth_date == "19800515", "Birth date should match");
    TEST_ASSERT(mwl.patient.patient_sex == "M", "Sex should be M");

    // Check requested procedure
    TEST_ASSERT(mwl.requested_procedure.accession_number == "ACC001",
                "Accession number should be ACC001");

    // Check scheduled procedure step
    TEST_ASSERT(!mwl.scheduled_procedure_step.scheduled_procedure_step_id.empty(),
                "SPS ID should not be empty");

    // Check imaging service request
    TEST_ASSERT(mwl.imaging_service_request.placer_order_number == "ORD001",
                "Placer order should be ORD001");

    return true;
}

bool test_mapper_patient_extraction() {
    hl7::hl7_parser parser;
    auto parse_result = parser.parse(SAMPLE_ORM_O01);
    TEST_ASSERT(parse_result.has_value(), "Should parse successfully");

    hl7_dicom_mapper mapper;
    auto patient = mapper.extract_patient(*parse_result);
    TEST_ASSERT(patient.has_value(), "Should extract patient successfully");

    TEST_ASSERT(patient->patient_id == "12345", "Patient ID should match");
    TEST_ASSERT(patient->issuer_of_patient_id == "HOSPITAL", "Issuer should match");
    TEST_ASSERT(patient->patient_name == "DOE^JOHN^WILLIAM", "Name should match");
    TEST_ASSERT(patient->patient_birth_date == "19800515", "Birth date should match");
    TEST_ASSERT(patient->patient_sex == "M", "Sex should be M");

    return true;
}

bool test_mapper_invalid_message_type() {
    // Create an ADT message (not ORM)
    std::string adt_msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345|||DOE^JOHN||19800515|M\r";

    hl7::hl7_parser parser;
    auto parse_result = parser.parse(adt_msg);
    TEST_ASSERT(parse_result.has_value(), "Should parse ADT successfully");

    hl7_dicom_mapper mapper;
    auto map_result = mapper.map_to_mwl(*parse_result);
    TEST_ASSERT(!map_result.has_value(), "Should fail for ADT message");
    TEST_ASSERT(map_result.error() == mapping_error::invalid_message_type,
                "Error should be invalid_message_type");

    return true;
}

bool test_mapper_missing_patient() {
    // ORM without PID segment
    std::string orm_no_pid =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG|P|2.4\r"
        "ORC|NW|ORD001||ACC001||SC\r"
        "OBR|1|ORD001||71020^CHEST XRAY^CPT\r";

    hl7::hl7_parser parser;
    auto parse_result = parser.parse(orm_no_pid);
    TEST_ASSERT(parse_result.has_value(), "Should parse ORM successfully");

    hl7_dicom_mapper mapper;
    auto map_result = mapper.map_to_mwl(*parse_result);
    TEST_ASSERT(!map_result.has_value(), "Should fail without PID");
    TEST_ASSERT(map_result.error() == mapping_error::missing_patient_id,
                "Error should be missing_patient_id");

    return true;
}

bool test_mapper_missing_order() {
    // ORM without ORC/OBR segments
    std::string orm_no_order =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG|P|2.4\r"
        "PID|1||12345|||DOE^JOHN||19800515|M\r";

    hl7::hl7_parser parser;
    auto parse_result = parser.parse(orm_no_order);
    TEST_ASSERT(parse_result.has_value(), "Should parse ORM successfully");

    hl7_dicom_mapper mapper;
    auto map_result = mapper.map_to_mwl(*parse_result);
    TEST_ASSERT(!map_result.has_value(), "Should fail without ORC");
    TEST_ASSERT(map_result.error() == mapping_error::missing_order_info,
                "Error should be missing_order_info");

    return true;
}

// =============================================================================
// Format Conversion Tests
// =============================================================================

bool test_name_format_conversion() {
    hl7_dicom_mapper mapper;

    // HL7 format: FAMILY^GIVEN^MIDDLE^SUFFIX^PREFIX
    std::string hl7_name = "DOE^JOHN^WILLIAM^JR^DR";
    std::string dicom_name = mapper.convert_name_format(hl7_name);

    // DICOM format should preserve HL7 format for Person Name
    TEST_ASSERT(dicom_name == "DOE^JOHN^WILLIAM^JR^DR", "DICOM name should match");

    // Simple name
    std::string simple = mapper.convert_name_format("SMITH^JANE");
    TEST_ASSERT(simple == "SMITH^JANE", "Simple name should convert");

    // Single name
    std::string single = mapper.convert_name_format("WILSON");
    TEST_ASSERT(single == "WILSON", "Single name should convert");

    return true;
}

bool test_date_format_conversion() {
    hl7_dicom_mapper mapper;

    // HL7 format: YYYYMMDD or YYYYMMDDHHMMSS
    std::string hl7_date = "20240115";
    std::string dicom_date = mapper.convert_date_format(hl7_date);
    TEST_ASSERT(dicom_date == "20240115", "Date should be 8 characters");

    // With time
    std::string hl7_datetime = "20240115103045";
    std::string date_part = mapper.convert_date_format(hl7_datetime);
    TEST_ASSERT(date_part == "20240115", "Should extract date part only");

    return true;
}

bool test_time_format_conversion() {
    hl7_dicom_mapper mapper;

    // HL7 format: HHMMSS or YYYYMMDDHHMMSS
    std::string hl7_time = "103045";
    std::string dicom_time = mapper.convert_time_format(hl7_time);
    TEST_ASSERT(dicom_time == "103045", "Time should be 6 characters");

    // With date prefix
    std::string hl7_datetime = "20240115103045";
    std::string time_part = mapper.convert_time_format(hl7_datetime);
    TEST_ASSERT(time_part == "103045", "Should extract time part only");

    return true;
}

bool test_sex_format_conversion() {
    hl7_dicom_mapper mapper;

    TEST_ASSERT(mapper.convert_sex_format("M") == "M", "M should stay M");
    TEST_ASSERT(mapper.convert_sex_format("F") == "F", "F should stay F");
    TEST_ASSERT(mapper.convert_sex_format("O") == "O", "O should stay O");
    TEST_ASSERT(mapper.convert_sex_format("U") == "O", "U should convert to O (Other)");
    TEST_ASSERT(mapper.convert_sex_format("") == "O", "Empty should default to O");
    TEST_ASSERT(mapper.convert_sex_format("X") == "O", "Unknown should default to O");

    return true;
}

bool test_priority_format_conversion() {
    hl7_dicom_mapper mapper;

    // HL7 priority codes to DICOM
    TEST_ASSERT(mapper.convert_priority_format("S") == "STAT", "S should be STAT");
    TEST_ASSERT(mapper.convert_priority_format("A") == "HIGH", "A should be HIGH");
    TEST_ASSERT(mapper.convert_priority_format("R") == "ROUTINE", "R should be ROUTINE");
    TEST_ASSERT(mapper.convert_priority_format("P") == "LOW", "P should be LOW");
    TEST_ASSERT(mapper.convert_priority_format("C") == "STAT", "C should be STAT");
    TEST_ASSERT(mapper.convert_priority_format("") == "MEDIUM", "Empty should be MEDIUM");

    return true;
}

// =============================================================================
// UID Generation Tests
// =============================================================================

bool test_uid_generation() {
    mapper_config config;
    config.uid_root = "1.2.840.99999";
    config.generate_study_uid = true;

    hl7_dicom_mapper mapper(config);

    auto uid1 = mapper.generate_uid();
    auto uid2 = mapper.generate_uid();

    TEST_ASSERT(!uid1.empty(), "UID 1 should not be empty");
    TEST_ASSERT(!uid2.empty(), "UID 2 should not be empty");
    TEST_ASSERT(uid1 != uid2, "UIDs should be unique");
    TEST_ASSERT(uid1.find("1.2.840.99999") == 0, "UID should start with root");
    TEST_ASSERT(uid2.find("1.2.840.99999") == 0, "UID should start with root");

    // Verify format (digits and dots only)
    for (char c : uid1) {
        TEST_ASSERT(c == '.' || (c >= '0' && c <= '9'),
                    "UID should contain only digits and dots");
    }

    return true;
}

bool test_uid_generation_in_mapping() {
    mapper_config config;
    config.uid_root = "1.2.840.12345";
    config.generate_study_uid = true;

    hl7_dicom_mapper mapper(config);

    hl7::hl7_parser parser;
    auto parse_result = parser.parse(SAMPLE_ORM_O01);
    TEST_ASSERT(parse_result.has_value(), "Should parse successfully");

    auto map_result = mapper.map_to_mwl(*parse_result);
    TEST_ASSERT(map_result.has_value(), "Should map successfully");

    // Check that Study Instance UID was generated
    TEST_ASSERT(!map_result->requested_procedure.study_instance_uid.empty(),
                "Study UID should be generated");
    TEST_ASSERT(map_result->requested_procedure.study_instance_uid.find("1.2.840.12345") == 0,
                "Study UID should start with root");

    return true;
}

// =============================================================================
// Modality Mapping Tests
// =============================================================================

bool test_modality_from_procedure_code() {
    hl7_dicom_mapper mapper;

    // Test common procedure code prefixes
    TEST_ASSERT(mapper.determine_modality("71020", "CHEST XRAY") == "CR" ||
                mapper.determine_modality("71020", "CHEST XRAY") == "DX",
                "Chest X-ray should be CR or DX");

    TEST_ASSERT(mapper.determine_modality("74150", "CT ABDOMEN") == "CT",
                "CT should map to CT modality");

    TEST_ASSERT(mapper.determine_modality("70553", "MRI BRAIN") == "MR",
                "MRI should map to MR modality");

    TEST_ASSERT(mapper.determine_modality("76856", "US PELVIS") == "US",
                "Ultrasound should map to US modality");

    TEST_ASSERT(mapper.determine_modality("93000", "ECG") == "ECG",
                "ECG should map to ECG modality");

    return true;
}

bool test_default_modality() {
    mapper_config config;
    config.default_modality = "OT";

    hl7_dicom_mapper mapper(config);

    // Unknown procedure code should use default
    std::string modality = mapper.determine_modality("99999", "UNKNOWN PROCEDURE");
    TEST_ASSERT(modality == "OT", "Unknown procedure should use default modality");

    return true;
}

// =============================================================================
// Complete Workflow Tests
// =============================================================================

bool test_complete_orm_workflow() {
    // Parse ORM message
    hl7::hl7_parser parser;
    auto parse_result = parser.parse(SAMPLE_ORM_O01);
    TEST_ASSERT(parse_result.has_value(), "Should parse ORM successfully");

    // Map to MWL
    mapper_config config;
    config.uid_root = "1.2.840.99999";
    config.generate_study_uid = true;
    config.default_modality = "CR";

    hl7_dicom_mapper mapper(config);
    auto mwl_result = mapper.map_to_mwl(*parse_result);
    TEST_ASSERT(mwl_result.has_value(), "Should create MWL item");

    const auto& mwl = *mwl_result;

    // Verify all required DICOM fields are populated
    TEST_ASSERT(!mwl.patient.patient_id.empty(), "Patient ID required");
    TEST_ASSERT(!mwl.patient.patient_name.empty(), "Patient name required");
    TEST_ASSERT(!mwl.requested_procedure.study_instance_uid.empty(), "Study UID required");
    TEST_ASSERT(!mwl.requested_procedure.accession_number.empty(), "Accession number required");
    TEST_ASSERT(!mwl.scheduled_procedure_step.scheduled_procedure_step_id.empty(), "SPS ID required");
    TEST_ASSERT(!mwl.scheduled_procedure_step.modality.empty(), "Modality required");

    return true;
}

bool test_multiple_orders_in_message() {
    // ORM with multiple ORC/OBR groups
    std::string multi_order_orm =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG|P|2.4\r"
        "PID|1||12345|||DOE^JOHN||19800515|M\r"
        "ORC|NW|ORD001||ACC001||SC\r"
        "OBR|1|ORD001||71020^CHEST XRAY^CPT\r"
        "ORC|NW|ORD002||ACC002||SC\r"
        "OBR|2|ORD002||74150^CT ABDOMEN^CPT\r";

    hl7::hl7_parser parser;
    auto parse_result = parser.parse(multi_order_orm);
    TEST_ASSERT(parse_result.has_value(), "Should parse multi-order ORM");

    hl7_dicom_mapper mapper;
    auto mwl_result = mapper.map_to_mwl(*parse_result);
    TEST_ASSERT(mwl_result.has_value(), "Should map first order");

    // Note: Current implementation maps first order only
    // Future enhancement could return vector of mwl_items
    TEST_ASSERT(mwl_result->imaging_service_request.placer_order_number == "ORD001",
                "Should map first order");

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== DICOM Data Structure Tests ===" << std::endl;
    RUN_TEST(test_dicom_patient_structure);
    RUN_TEST(test_dicom_requested_procedure);
    RUN_TEST(test_dicom_scheduled_procedure_step);
    RUN_TEST(test_dicom_imaging_service_request);
    RUN_TEST(test_mwl_item_complete);

    std::cout << "\n=== Mapping Error Tests ===" << std::endl;
    RUN_TEST(test_mapping_error_codes);

    std::cout << "\n=== HL7 to DICOM Mapper Tests ===" << std::endl;
    RUN_TEST(test_mapper_default_config);
    RUN_TEST(test_mapper_custom_config);
    RUN_TEST(test_mapper_orm_to_mwl);
    RUN_TEST(test_mapper_patient_extraction);
    RUN_TEST(test_mapper_invalid_message_type);
    RUN_TEST(test_mapper_missing_patient);
    RUN_TEST(test_mapper_missing_order);

    std::cout << "\n=== Format Conversion Tests ===" << std::endl;
    RUN_TEST(test_name_format_conversion);
    RUN_TEST(test_date_format_conversion);
    RUN_TEST(test_time_format_conversion);
    RUN_TEST(test_sex_format_conversion);
    RUN_TEST(test_priority_format_conversion);

    std::cout << "\n=== UID Generation Tests ===" << std::endl;
    RUN_TEST(test_uid_generation);
    RUN_TEST(test_uid_generation_in_mapping);

    std::cout << "\n=== Modality Mapping Tests ===" << std::endl;
    RUN_TEST(test_modality_from_procedure_code);
    RUN_TEST(test_default_modality);

    std::cout << "\n=== Complete Workflow Tests ===" << std::endl;
    RUN_TEST(test_complete_orm_workflow);
    RUN_TEST(test_multiple_orders_in_message);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    double coverage = (passed * 100.0) / (passed + failed);
    std::cout << "Pass Rate: " << coverage << "%" << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::mapping::test

int main() {
    return pacs::bridge::mapping::test::run_all_tests();
}
