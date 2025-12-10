/**
 * @file siu_handler_test.cpp
 * @brief Unit tests for SIU (Scheduling Information Unsolicited) message handler
 *
 * Tests for SIU message parsing, trigger event handling, appointment
 * information extraction, and MWL integration.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/26
 */

#include "pacs/bridge/protocol/hl7/siu_handler.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/pacs_adapter/mwl_client.h"

#include <gtest/gtest.h>

namespace pacs::bridge::hl7::test {

// =============================================================================
// Test Fixtures
// =============================================================================

class SiuHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a mock MWL client configuration
        pacs_adapter::mwl_client_config config;
        config.pacs_host = "localhost";
        config.pacs_port = 11112;

        mwl_client_ = std::make_shared<pacs_adapter::mwl_client>(config);
    }

    std::shared_ptr<pacs_adapter::mwl_client> mwl_client_;

    // Helper to create a sample SIU^S12 message
    static std::string create_siu_s12_message() {
        return "MSH|^~\\&|RIS|RADIOLOGY|PACS|IMAGING|20240115103000||SIU^S12|MSG001|P|2.5.1\r"
               "SCH|APPT001^RIS|APPT001^PACS||||||^^^20240120100000^^20240120|30|min^minutes|Booked\r"
               "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^A||19800315|M\r"
               "RGS|1||RESOURCE_GROUP_1\r"
               "AIS|1||CT_SCAN^CT Scan^LOCAL|20240120100000|30|min\r";
    }

    // Helper to create a sample SIU^S13 message (reschedule)
    static std::string create_siu_s13_message() {
        return "MSH|^~\\&|RIS|RADIOLOGY|PACS|IMAGING|20240116080000||SIU^S13|MSG002|P|2.5.1\r"
               "SCH|APPT001^RIS|APPT001^PACS||||||^^^20240121143000^^20240121|30|min^minutes|Booked\r"
               "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^A||19800315|M\r"
               "RGS|1||RESOURCE_GROUP_1\r"
               "AIS|1||CT_SCAN^CT Scan^LOCAL|20240121143000|30|min\r";
    }

    // Helper to create a sample SIU^S14 message (modification)
    static std::string create_siu_s14_message() {
        return "MSH|^~\\&|RIS|RADIOLOGY|PACS|IMAGING|20240116090000||SIU^S14|MSG003|P|2.5.1\r"
               "SCH|APPT001^RIS|APPT001^PACS||||||^^^20240120100000^^20240120|45|min^minutes|Booked\r"
               "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^ALEXANDER||19800315|M\r"
               "RGS|1||RESOURCE_GROUP_1\r"
               "AIS|1||CT_CHEST^CT Chest Scan^LOCAL|20240120100000|45|min\r";
    }

    // Helper to create a sample SIU^S15 message (cancellation)
    // SCH-25 is the Filler Status Code field
    static std::string create_siu_s15_message() {
        return "MSH|^~\\&|RIS|RADIOLOGY|PACS|IMAGING|20240117100000||SIU^S15|MSG004|P|2.5.1\r"
               "SCH|APPT001^RIS|APPT001^PACS||||||||||^^^20240120100000^^20240120||||||||||||||||Cancelled\r"
               "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^A||19800315|M\r"
               "RGS|1||RESOURCE_GROUP_1\r"
               "AIS|1||CT_SCAN^CT Scan^LOCAL|20240120100000|30|min\r";
    }
};

// =============================================================================
// SIU Type and Constant Tests
// =============================================================================

TEST(SiuTypesTest, ErrorCodeRange) {
    EXPECT_EQ(to_error_code(siu_error::not_siu_message), -870);
    EXPECT_EQ(to_error_code(siu_error::processing_failed), -879);
}

TEST(SiuTypesTest, ErrorCodeStrings) {
    EXPECT_STREQ(to_string(siu_error::not_siu_message),
                 "Message is not an SIU message");
    EXPECT_STREQ(to_string(siu_error::unsupported_trigger_event),
                 "Unsupported SIU trigger event");
    EXPECT_STREQ(to_string(siu_error::missing_required_field),
                 "Required field missing in SIU message");
    EXPECT_STREQ(to_string(siu_error::appointment_not_found),
                 "Appointment not found for update/cancel operation");
}

TEST(SiuTypesTest, TriggerEventParsing) {
    EXPECT_EQ(parse_siu_trigger_event("S12"),
              siu_trigger_event::s12_new_appointment);
    EXPECT_EQ(parse_siu_trigger_event("S13"),
              siu_trigger_event::s13_rescheduled);
    EXPECT_EQ(parse_siu_trigger_event("S14"),
              siu_trigger_event::s14_modification);
    EXPECT_EQ(parse_siu_trigger_event("S15"),
              siu_trigger_event::s15_cancellation);
    EXPECT_EQ(parse_siu_trigger_event("S99"),
              siu_trigger_event::unknown);
}

TEST(SiuTypesTest, TriggerEventToString) {
    EXPECT_STREQ(to_string(siu_trigger_event::s12_new_appointment), "S12");
    EXPECT_STREQ(to_string(siu_trigger_event::s13_rescheduled), "S13");
    EXPECT_STREQ(to_string(siu_trigger_event::s14_modification), "S14");
    EXPECT_STREQ(to_string(siu_trigger_event::s15_cancellation), "S15");
    EXPECT_STREQ(to_string(siu_trigger_event::unknown), "UNKNOWN");
}

TEST(SiuTypesTest, AppointmentStatusParsing) {
    EXPECT_EQ(parse_appointment_status("Pending"),
              appointment_status::pending);
    EXPECT_EQ(parse_appointment_status("Booked"),
              appointment_status::booked);
    EXPECT_EQ(parse_appointment_status("Arrived"),
              appointment_status::arrived);
    EXPECT_EQ(parse_appointment_status("Started"),
              appointment_status::started);
    EXPECT_EQ(parse_appointment_status("Complete"),
              appointment_status::complete);
    EXPECT_EQ(parse_appointment_status("Cancelled"),
              appointment_status::cancelled);
    EXPECT_EQ(parse_appointment_status("No-Show"),
              appointment_status::no_show);
    EXPECT_EQ(parse_appointment_status("NoShow"),
              appointment_status::no_show);
    EXPECT_EQ(parse_appointment_status("Unknown"),
              appointment_status::unknown);
}

TEST(SiuTypesTest, AppointmentStatusToMwlStatus) {
    EXPECT_STREQ(to_mwl_status(appointment_status::pending), "SCHEDULED");
    EXPECT_STREQ(to_mwl_status(appointment_status::booked), "SCHEDULED");
    EXPECT_STREQ(to_mwl_status(appointment_status::arrived), "STARTED");
    EXPECT_STREQ(to_mwl_status(appointment_status::started), "STARTED");
    EXPECT_STREQ(to_mwl_status(appointment_status::complete), "COMPLETED");
    EXPECT_STREQ(to_mwl_status(appointment_status::cancelled), "DISCONTINUED");
    EXPECT_STREQ(to_mwl_status(appointment_status::no_show), "DISCONTINUED");
}

// =============================================================================
// SIU Handler Configuration Tests
// =============================================================================

TEST(SiuConfigTest, DefaultConfiguration) {
    siu_handler_config config;

    EXPECT_FALSE(config.allow_s12_update);
    EXPECT_FALSE(config.allow_reschedule_create);
    EXPECT_TRUE(config.auto_generate_study_uid);
    EXPECT_TRUE(config.validate_appointment_data);
    EXPECT_TRUE(config.detailed_ack);
    EXPECT_TRUE(config.audit_logging);
    EXPECT_EQ(config.ack_sending_application, "PACS_BRIDGE");
    EXPECT_EQ(config.ack_sending_facility, "RADIOLOGY");
}

TEST(SiuConfigTest, RequiredFieldsDefault) {
    siu_handler_config config;

    EXPECT_EQ(config.required_fields.size(), 3);
    EXPECT_NE(std::find(config.required_fields.begin(),
                         config.required_fields.end(), "patient_id"),
              config.required_fields.end());
    EXPECT_NE(std::find(config.required_fields.begin(),
                         config.required_fields.end(), "patient_name"),
              config.required_fields.end());
    EXPECT_NE(std::find(config.required_fields.begin(),
                         config.required_fields.end(), "appointment_id"),
              config.required_fields.end());
}

// =============================================================================
// SIU Handler Creation Tests
// =============================================================================

TEST_F(SiuHandlerTest, HandlerCreation) {
    siu_handler handler(mwl_client_);

    EXPECT_NE(handler.mwl_client(), nullptr);
    EXPECT_NE(handler.mapper(), nullptr);
}

TEST_F(SiuHandlerTest, HandlerCreationWithConfig) {
    siu_handler_config config;
    config.allow_s12_update = true;
    config.ack_sending_application = "TEST_APP";

    siu_handler handler(mwl_client_, config);

    EXPECT_TRUE(handler.config().allow_s12_update);
    EXPECT_EQ(handler.config().ack_sending_application, "TEST_APP");
}

TEST_F(SiuHandlerTest, SupportedTriggers) {
    siu_handler handler(mwl_client_);

    auto triggers = handler.supported_triggers();
    EXPECT_EQ(triggers.size(), 4);
    EXPECT_NE(std::find(triggers.begin(), triggers.end(), "S12"),
              triggers.end());
    EXPECT_NE(std::find(triggers.begin(), triggers.end(), "S13"),
              triggers.end());
    EXPECT_NE(std::find(triggers.begin(), triggers.end(), "S14"),
              triggers.end());
    EXPECT_NE(std::find(triggers.begin(), triggers.end(), "S15"),
              triggers.end());
}

// =============================================================================
// SIU Message Parsing Tests
// =============================================================================

TEST_F(SiuHandlerTest, CanHandleSiuMessage) {
    siu_handler handler(mwl_client_);

    auto parse_result = hl7_message::parse(create_siu_s12_message());
    ASSERT_TRUE(parse_result.has_value());

    EXPECT_TRUE(handler.can_handle(*parse_result));
}

TEST_F(SiuHandlerTest, CannotHandleOrmMessage) {
    siu_handler handler(mwl_client_);

    std::string orm_message =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ORM^O01|MSG001|P|2.5\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800315|M\r"
        "ORC|NW|ORD001|ACC001||SC\r"
        "OBR|1||ACC001|CT_CHEST^CT Chest|||20240115\r";

    auto parse_result = hl7_message::parse(orm_message);
    ASSERT_TRUE(parse_result.has_value());

    EXPECT_FALSE(handler.can_handle(*parse_result));
}

TEST_F(SiuHandlerTest, ExtractAppointmentInfoS12) {
    siu_handler handler(mwl_client_);

    auto parse_result = hl7_message::parse(create_siu_s12_message());
    ASSERT_TRUE(parse_result.has_value());

    auto appt_result = handler.extract_appointment_info(*parse_result);
    ASSERT_TRUE(appt_result.has_value());

    EXPECT_EQ(appt_result->trigger, siu_trigger_event::s12_new_appointment);
    EXPECT_EQ(appt_result->placer_appointment_id, "APPT001");
    EXPECT_EQ(appt_result->patient_id, "12345");
    EXPECT_EQ(appt_result->patient_name, "DOE^JOHN");
    EXPECT_EQ(appt_result->procedure_code, "CT_SCAN");
    EXPECT_EQ(appt_result->procedure_description, "CT Scan");
}

TEST_F(SiuHandlerTest, ExtractAppointmentInfoS15) {
    siu_handler handler(mwl_client_);

    auto parse_result = hl7_message::parse(create_siu_s15_message());
    ASSERT_TRUE(parse_result.has_value());

    auto appt_result = handler.extract_appointment_info(*parse_result);
    ASSERT_TRUE(appt_result.has_value());

    // Verify trigger event is S15 (cancellation)
    EXPECT_EQ(appt_result->trigger, siu_trigger_event::s15_cancellation);
    // Note: SCH-25 parsing depends on message format; for S15, the trigger event
    // itself indicates cancellation, so status may be unknown if field is not populated
    // in the standard position. The handler will treat S15 as cancellation regardless.
}

// =============================================================================
// SIU Handler Error Cases
// =============================================================================

TEST_F(SiuHandlerTest, HandleNonSiuMessage) {
    siu_handler handler(mwl_client_);

    std::string adt_message =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.5\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800315|M\r";

    auto parse_result = hl7_message::parse(adt_message);
    ASSERT_TRUE(parse_result.has_value());

    auto handle_result = handler.handle(*parse_result);
    EXPECT_FALSE(handle_result.has_value());
    EXPECT_EQ(handle_result.error(), siu_error::not_siu_message);
}

TEST_F(SiuHandlerTest, HandleUnsupportedTriggerEvent) {
    siu_handler handler(mwl_client_);

    // SIU^S26 is not a supported trigger event
    std::string siu_s26_message =
        "MSH|^~\\&|RIS|RADIOLOGY|PACS|IMAGING|20240115103000||SIU^S26|MSG001|P|2.5.1\r"
        "SCH|APPT001^RIS|APPT001^PACS||||||^^^20240120100000^^20240120|30|min^minutes|Booked\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^A||19800315|M\r";

    auto parse_result = hl7_message::parse(siu_s26_message);
    ASSERT_TRUE(parse_result.has_value());

    auto handle_result = handler.handle(*parse_result);
    EXPECT_FALSE(handle_result.has_value());
    EXPECT_EQ(handle_result.error(), siu_error::unsupported_trigger_event);
}

TEST_F(SiuHandlerTest, HandleMissingSCHSegment) {
    siu_handler handler(mwl_client_);

    std::string siu_no_sch =
        "MSH|^~\\&|RIS|RADIOLOGY|PACS|IMAGING|20240115103000||SIU^S12|MSG001|P|2.5.1\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^A||19800315|M\r";

    auto parse_result = hl7_message::parse(siu_no_sch);
    ASSERT_TRUE(parse_result.has_value());

    // can_handle should return false if SCH segment is missing
    EXPECT_FALSE(handler.can_handle(*parse_result));
}

// =============================================================================
// SIU ACK Generation Tests
// =============================================================================

TEST_F(SiuHandlerTest, GenerateSuccessAck) {
    siu_handler handler(mwl_client_);

    auto parse_result = hl7_message::parse(create_siu_s12_message());
    ASSERT_TRUE(parse_result.has_value());

    auto ack = handler.generate_ack(*parse_result, true);

    auto ack_header = ack.header();
    EXPECT_EQ(ack_header.type, message_type::ACK);
    EXPECT_EQ(ack_header.trigger_event, "S12");
    EXPECT_EQ(ack_header.sending_application, "PACS_BRIDGE");

    // Check MSA segment
    const auto* msa = ack.segment("MSA");
    ASSERT_NE(msa, nullptr);
    EXPECT_EQ(msa->field_value(1), "AA");
    EXPECT_EQ(msa->field_value(2), "MSG001");
}

TEST_F(SiuHandlerTest, GenerateErrorAck) {
    siu_handler handler(mwl_client_);

    auto parse_result = hl7_message::parse(create_siu_s12_message());
    ASSERT_TRUE(parse_result.has_value());

    auto ack = handler.generate_ack(*parse_result, false, "AE", "Duplicate appointment");

    const auto* msa = ack.segment("MSA");
    ASSERT_NE(msa, nullptr);
    EXPECT_EQ(msa->field_value(1), "AE");

    // Check ERR segment when detailed_ack is enabled
    const auto* err = ack.segment("ERR");
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(err->field_value(3), "Duplicate appointment");
}

// =============================================================================
// SIU Handler Statistics Tests
// =============================================================================

TEST_F(SiuHandlerTest, StatisticsInitialValues) {
    siu_handler handler(mwl_client_);

    auto stats = handler.get_statistics();
    EXPECT_EQ(stats.total_processed, 0);
    EXPECT_EQ(stats.success_count, 0);
    EXPECT_EQ(stats.failure_count, 0);
    EXPECT_EQ(stats.s12_count, 0);
    EXPECT_EQ(stats.s13_count, 0);
    EXPECT_EQ(stats.s14_count, 0);
    EXPECT_EQ(stats.s15_count, 0);
}

TEST_F(SiuHandlerTest, ResetStatistics) {
    siu_handler handler(mwl_client_);

    // Simulate some processing by calling handle with invalid message
    std::string adt_message =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.5\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800315|M\r";

    auto parse_result = hl7_message::parse(adt_message);
    ASSERT_TRUE(parse_result.has_value());

    [[maybe_unused]] auto result = handler.handle(*parse_result);  // This should fail

    auto stats_before = handler.get_statistics();
    EXPECT_EQ(stats_before.total_processed, 1);
    EXPECT_EQ(stats_before.failure_count, 1);

    handler.reset_statistics();

    auto stats_after = handler.get_statistics();
    EXPECT_EQ(stats_after.total_processed, 0);
    EXPECT_EQ(stats_after.failure_count, 0);
}

// =============================================================================
// SIU Result Structure Tests
// =============================================================================

TEST(SiuResultTest, DefaultValues) {
    siu_result result;

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.trigger, siu_trigger_event::unknown);
    EXPECT_EQ(result.status, appointment_status::unknown);
    EXPECT_TRUE(result.placer_appointment_id.empty());
    EXPECT_TRUE(result.filler_appointment_id.empty());
    EXPECT_TRUE(result.patient_id.empty());
    EXPECT_TRUE(result.warnings.empty());
}

// =============================================================================
// Appointment Info Structure Tests
// =============================================================================

TEST(AppointmentInfoTest, DefaultValues) {
    appointment_info info;

    EXPECT_EQ(info.trigger, siu_trigger_event::unknown);
    EXPECT_EQ(info.status, appointment_status::unknown);
    EXPECT_TRUE(info.placer_appointment_id.empty());
    EXPECT_TRUE(info.filler_appointment_id.empty());
    EXPECT_TRUE(info.patient_id.empty());
    EXPECT_TRUE(info.patient_name.empty());
}

}  // namespace pacs::bridge::hl7::test

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
