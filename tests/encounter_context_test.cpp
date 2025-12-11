/**
 * @file encounter_context_test.cpp
 * @brief Unit tests for encounter context retrieval
 *
 * Tests the encounter context functionality including:
 *   - Encounter error codes and strings
 *   - Encounter status parsing and validation
 *   - Encounter class parsing
 *   - Encounter info structure
 *   - Location and practitioner info
 *   - FHIR Encounter JSON parsing
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/106
 * @see https://github.com/kcenon/pacs_bridge/issues/120
 */

#include <gtest/gtest.h>

#include "pacs/bridge/emr/encounter_context.h"

#include <chrono>
#include <string>

using namespace pacs::bridge::emr;
using namespace std::chrono_literals;

// =============================================================================
// Encounter Error Tests
// =============================================================================

class EncounterErrorTest : public ::testing::Test {};

TEST_F(EncounterErrorTest, ErrorCodeValues) {
    EXPECT_EQ(to_error_code(encounter_error::not_found), -1080);
    EXPECT_EQ(to_error_code(encounter_error::query_failed), -1081);
    EXPECT_EQ(to_error_code(encounter_error::multiple_active), -1082);
    EXPECT_EQ(to_error_code(encounter_error::encounter_ended), -1083);
    EXPECT_EQ(to_error_code(encounter_error::invalid_data), -1084);
    EXPECT_EQ(to_error_code(encounter_error::visit_not_found), -1085);
    EXPECT_EQ(to_error_code(encounter_error::invalid_status), -1086);
    EXPECT_EQ(to_error_code(encounter_error::location_not_found), -1087);
    EXPECT_EQ(to_error_code(encounter_error::practitioner_not_found), -1088);
    EXPECT_EQ(to_error_code(encounter_error::parse_failed), -1089);
}

TEST_F(EncounterErrorTest, ErrorToString) {
    EXPECT_STREQ(to_string(encounter_error::not_found),
                 "Encounter not found in EMR");
    EXPECT_STREQ(to_string(encounter_error::query_failed),
                 "Encounter query failed");
    EXPECT_STREQ(to_string(encounter_error::multiple_active),
                 "Multiple active encounters found");
    EXPECT_STREQ(to_string(encounter_error::encounter_ended),
                 "Encounter has ended");
    EXPECT_STREQ(to_string(encounter_error::invalid_data),
                 "Invalid encounter data in response");
    EXPECT_STREQ(to_string(encounter_error::visit_not_found),
                 "Visit number not found");
}

// =============================================================================
// Encounter Status Tests
// =============================================================================

class EncounterStatusTest : public ::testing::Test {};

TEST_F(EncounterStatusTest, StatusToString) {
    EXPECT_EQ(to_string(encounter_status::planned), "planned");
    EXPECT_EQ(to_string(encounter_status::arrived), "arrived");
    EXPECT_EQ(to_string(encounter_status::triaged), "triaged");
    EXPECT_EQ(to_string(encounter_status::in_progress), "in-progress");
    EXPECT_EQ(to_string(encounter_status::on_leave), "onleave");
    EXPECT_EQ(to_string(encounter_status::finished), "finished");
    EXPECT_EQ(to_string(encounter_status::cancelled), "cancelled");
    EXPECT_EQ(to_string(encounter_status::entered_in_error), "entered-in-error");
    EXPECT_EQ(to_string(encounter_status::unknown), "unknown");
}

TEST_F(EncounterStatusTest, ParseEncounterStatus) {
    EXPECT_EQ(parse_encounter_status("planned"), encounter_status::planned);
    EXPECT_EQ(parse_encounter_status("arrived"), encounter_status::arrived);
    EXPECT_EQ(parse_encounter_status("triaged"), encounter_status::triaged);
    EXPECT_EQ(parse_encounter_status("in-progress"), encounter_status::in_progress);
    EXPECT_EQ(parse_encounter_status("onleave"), encounter_status::on_leave);
    EXPECT_EQ(parse_encounter_status("finished"), encounter_status::finished);
    EXPECT_EQ(parse_encounter_status("cancelled"), encounter_status::cancelled);
    EXPECT_EQ(parse_encounter_status("entered-in-error"),
              encounter_status::entered_in_error);
    EXPECT_EQ(parse_encounter_status("invalid"), encounter_status::unknown);
    EXPECT_EQ(parse_encounter_status(""), encounter_status::unknown);
}

TEST_F(EncounterStatusTest, IsActiveStatus) {
    EXPECT_TRUE(is_active(encounter_status::planned));
    EXPECT_TRUE(is_active(encounter_status::arrived));
    EXPECT_TRUE(is_active(encounter_status::triaged));
    EXPECT_TRUE(is_active(encounter_status::in_progress));
    EXPECT_TRUE(is_active(encounter_status::on_leave));

    EXPECT_FALSE(is_active(encounter_status::finished));
    EXPECT_FALSE(is_active(encounter_status::cancelled));
    EXPECT_FALSE(is_active(encounter_status::entered_in_error));
    EXPECT_FALSE(is_active(encounter_status::unknown));
}

// =============================================================================
// Encounter Class Tests
// =============================================================================

class EncounterClassTest : public ::testing::Test {};

TEST_F(EncounterClassTest, ClassToCode) {
    EXPECT_EQ(to_code(encounter_class::inpatient), "IMP");
    EXPECT_EQ(to_code(encounter_class::outpatient), "AMB");
    EXPECT_EQ(to_code(encounter_class::emergency), "EMER");
    EXPECT_EQ(to_code(encounter_class::home_health), "HH");
    EXPECT_EQ(to_code(encounter_class::virtual_visit), "VR");
    EXPECT_EQ(to_code(encounter_class::preadmission), "PRENC");
    EXPECT_EQ(to_code(encounter_class::short_stay), "SS");
    EXPECT_EQ(to_code(encounter_class::unknown), "UNK");
}

TEST_F(EncounterClassTest, ClassToDisplay) {
    EXPECT_EQ(to_display(encounter_class::inpatient), "inpatient encounter");
    EXPECT_EQ(to_display(encounter_class::outpatient), "ambulatory");
    EXPECT_EQ(to_display(encounter_class::emergency), "emergency");
    EXPECT_EQ(to_display(encounter_class::home_health), "home health");
    EXPECT_EQ(to_display(encounter_class::virtual_visit), "virtual");
    EXPECT_EQ(to_display(encounter_class::unknown), "unknown");
}

TEST_F(EncounterClassTest, ParseEncounterClass) {
    EXPECT_EQ(parse_encounter_class("IMP"), encounter_class::inpatient);
    EXPECT_EQ(parse_encounter_class("ACUTE"), encounter_class::inpatient);
    EXPECT_EQ(parse_encounter_class("NONAC"), encounter_class::inpatient);
    EXPECT_EQ(parse_encounter_class("AMB"), encounter_class::outpatient);
    EXPECT_EQ(parse_encounter_class("EMER"), encounter_class::emergency);
    EXPECT_EQ(parse_encounter_class("HH"), encounter_class::home_health);
    EXPECT_EQ(parse_encounter_class("VR"), encounter_class::virtual_visit);
    EXPECT_EQ(parse_encounter_class("PRENC"), encounter_class::preadmission);
    EXPECT_EQ(parse_encounter_class("SS"), encounter_class::short_stay);
    EXPECT_EQ(parse_encounter_class("INVALID"), encounter_class::unknown);
    EXPECT_EQ(parse_encounter_class(""), encounter_class::unknown);
}

// =============================================================================
// Location Info Tests
// =============================================================================

class LocationInfoTest : public ::testing::Test {};

TEST_F(LocationInfoTest, DefaultConstruction) {
    location_info loc;
    EXPECT_TRUE(loc.id.empty());
    EXPECT_TRUE(loc.display.empty());
    EXPECT_TRUE(loc.type.empty());
    EXPECT_TRUE(loc.status.empty());
    EXPECT_TRUE(loc.physical_type.empty());
    EXPECT_FALSE(loc.start_time.has_value());
    EXPECT_FALSE(loc.end_time.has_value());
}

TEST_F(LocationInfoTest, WithValues) {
    location_info loc;
    loc.id = "Location/ward-3a";
    loc.display = "Ward 3A";
    loc.type = "ward";
    loc.status = "active";
    loc.physical_type = "wa";
    loc.start_time = std::chrono::system_clock::now();

    EXPECT_EQ(loc.id, "Location/ward-3a");
    EXPECT_EQ(loc.display, "Ward 3A");
    EXPECT_TRUE(loc.start_time.has_value());
    EXPECT_FALSE(loc.end_time.has_value());
}

// =============================================================================
// Practitioner Info Tests
// =============================================================================

class PractitionerInfoTest : public ::testing::Test {};

TEST_F(PractitionerInfoTest, DefaultConstruction) {
    practitioner_info pract;
    EXPECT_TRUE(pract.id.empty());
    EXPECT_TRUE(pract.display.empty());
    EXPECT_TRUE(pract.type.empty());
    EXPECT_FALSE(pract.start_time.has_value());
    EXPECT_FALSE(pract.end_time.has_value());
}

TEST_F(PractitionerInfoTest, WithValues) {
    practitioner_info pract;
    pract.id = "Practitioner/dr-smith";
    pract.display = "Dr. John Smith";
    pract.type = "ATND";

    EXPECT_EQ(pract.id, "Practitioner/dr-smith");
    EXPECT_EQ(pract.display, "Dr. John Smith");
    EXPECT_EQ(pract.type, "ATND");
}

// =============================================================================
// Encounter Info Tests
// =============================================================================

class EncounterInfoTest : public ::testing::Test {
protected:
    encounter_info create_sample_encounter() {
        encounter_info enc;
        enc.id = "enc-12345";
        enc.visit_number = "VN-2025-001";
        enc.status = encounter_status::in_progress;
        enc.enc_class = encounter_class::inpatient;
        enc.class_display = "inpatient encounter";
        enc.patient_reference = "Patient/12345";
        enc.start_time = std::chrono::system_clock::now() - 24h;
        enc.service_provider = "Organization/hospital-main";
        enc.service_provider_display = "Main Hospital";
        return enc;
    }
};

TEST_F(EncounterInfoTest, DefaultConstruction) {
    encounter_info enc;
    EXPECT_TRUE(enc.id.empty());
    EXPECT_TRUE(enc.visit_number.empty());
    EXPECT_EQ(enc.status, encounter_status::unknown);
    EXPECT_EQ(enc.enc_class, encounter_class::unknown);
    EXPECT_FALSE(enc.start_time.has_value());
    EXPECT_FALSE(enc.end_time.has_value());
}

TEST_F(EncounterInfoTest, ToReference) {
    encounter_info enc;
    enc.id = "enc-12345";
    EXPECT_EQ(enc.to_reference(), "Encounter/enc-12345");
}

TEST_F(EncounterInfoTest, IsActive) {
    encounter_info enc;

    enc.status = encounter_status::in_progress;
    EXPECT_TRUE(enc.is_active());

    enc.status = encounter_status::arrived;
    EXPECT_TRUE(enc.is_active());

    enc.status = encounter_status::finished;
    EXPECT_FALSE(enc.is_active());

    enc.status = encounter_status::cancelled;
    EXPECT_FALSE(enc.is_active());
}

TEST_F(EncounterInfoTest, CurrentLocationEmpty) {
    encounter_info enc;
    auto loc = enc.current_location();
    EXPECT_FALSE(loc.has_value());
}

TEST_F(EncounterInfoTest, CurrentLocationSingle) {
    encounter_info enc;
    location_info loc;
    loc.id = "Location/room-101";
    loc.display = "Room 101";
    enc.locations.push_back(loc);

    auto current = enc.current_location();
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->id, "Location/room-101");
}

TEST_F(EncounterInfoTest, CurrentLocationMultiple) {
    encounter_info enc;

    // First location (ended)
    location_info loc1;
    loc1.id = "Location/er";
    loc1.display = "Emergency Room";
    loc1.start_time = std::chrono::system_clock::now() - 48h;
    loc1.end_time = std::chrono::system_clock::now() - 24h;
    enc.locations.push_back(loc1);

    // Second location (current, no end time)
    location_info loc2;
    loc2.id = "Location/ward-3a";
    loc2.display = "Ward 3A";
    loc2.start_time = std::chrono::system_clock::now() - 24h;
    enc.locations.push_back(loc2);

    auto current = enc.current_location();
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->id, "Location/ward-3a");
}

TEST_F(EncounterInfoTest, AttendingPhysicianNotFound) {
    encounter_info enc;
    auto attending = enc.attending_physician();
    EXPECT_FALSE(attending.has_value());
}

TEST_F(EncounterInfoTest, AttendingPhysicianFound) {
    encounter_info enc;

    practitioner_info pract1;
    pract1.id = "Practitioner/nurse-1";
    pract1.display = "Nurse Jane";
    pract1.type = "NURSE";
    enc.participants.push_back(pract1);

    practitioner_info pract2;
    pract2.id = "Practitioner/dr-smith";
    pract2.display = "Dr. Smith";
    pract2.type = "ATND";
    enc.participants.push_back(pract2);

    auto attending = enc.attending_physician();
    ASSERT_TRUE(attending.has_value());
    EXPECT_EQ(attending->id, "Practitioner/dr-smith");
    EXPECT_EQ(attending->display, "Dr. Smith");
}

TEST_F(EncounterInfoTest, PerformingPhysicianFound) {
    encounter_info enc;

    practitioner_info pract;
    pract.id = "Practitioner/dr-jones";
    pract.display = "Dr. Jones";
    pract.type = "PPRF";
    enc.participants.push_back(pract);

    auto performing = enc.performing_physician();
    ASSERT_TRUE(performing.has_value());
    EXPECT_EQ(performing->id, "Practitioner/dr-jones");
}

// =============================================================================
// FHIR JSON Parsing Tests
// =============================================================================

// Note: JSON parsing tests require PACS_BRIDGE_HAS_NLOHMANN_JSON to be defined.
// Without nlohmann/json, parse_encounter_json returns parse_failed.
// These tests verify the parsing behavior based on library availability.

class EncounterJsonParsingTest : public ::testing::Test {
protected:
    std::string create_minimal_encounter_json() {
        return R"({
            "resourceType": "Encounter",
            "id": "enc-minimal",
            "status": "in-progress",
            "class": {
                "system": "http://terminology.hl7.org/CodeSystem/v3-ActCode",
                "code": "IMP",
                "display": "inpatient encounter"
            },
            "subject": {
                "reference": "Patient/12345"
            }
        })";
    }

    std::string create_full_encounter_json() {
        return R"({
            "resourceType": "Encounter",
            "id": "enc-12345",
            "status": "in-progress",
            "identifier": [{
                "type": {
                    "coding": [{
                        "system": "http://terminology.hl7.org/CodeSystem/v2-0203",
                        "code": "VN"
                    }]
                },
                "value": "VN-2025-001"
            }],
            "class": {
                "system": "http://terminology.hl7.org/CodeSystem/v3-ActCode",
                "code": "IMP",
                "display": "inpatient encounter"
            },
            "type": [{
                "coding": [{
                    "system": "http://snomed.info/sct",
                    "code": "183452005",
                    "display": "Emergency hospital admission"
                }],
                "text": "Emergency Admission"
            }],
            "subject": {
                "reference": "Patient/12345"
            },
            "period": {
                "start": "2025-12-10T08:00:00Z"
            },
            "location": [{
                "location": {
                    "reference": "Location/ward-3a",
                    "display": "Ward 3A"
                },
                "status": "active",
                "physicalType": {
                    "coding": [{
                        "code": "wa"
                    }]
                }
            }],
            "participant": [{
                "type": [{
                    "coding": [{
                        "system": "http://terminology.hl7.org/CodeSystem/v3-ParticipationType",
                        "code": "ATND"
                    }]
                }],
                "individual": {
                    "reference": "Practitioner/dr-smith",
                    "display": "Dr. John Smith"
                }
            }],
            "serviceProvider": {
                "reference": "Organization/hospital-main",
                "display": "Main Hospital"
            },
            "reasonCode": [{
                "text": "Chest pain"
            }],
            "diagnosis": [{
                "condition": {
                    "reference": "Condition/heart-condition-1"
                }
            }]
        })";
    }

    // Check if JSON parsing is available (nlohmann/json)
    bool is_json_parsing_available() {
        auto result = parse_encounter_json(create_minimal_encounter_json());
        // If nlohmann/json is not available, it returns parse_failed
        if (std::holds_alternative<encounter_error>(result)) {
            return std::get<encounter_error>(result) != encounter_error::parse_failed;
        }
        return true;
    }
};

TEST_F(EncounterJsonParsingTest, ParseMinimalEncounter) {
    auto result = parse_encounter_json(create_minimal_encounter_json());

    if (!is_json_parsing_available()) {
        GTEST_SKIP() << "JSON parsing not available (nlohmann/json not configured)";
    }

    ASSERT_TRUE(std::holds_alternative<encounter_info>(result));
    auto& enc = std::get<encounter_info>(result);

    EXPECT_EQ(enc.id, "enc-minimal");
    EXPECT_EQ(enc.status, encounter_status::in_progress);
    EXPECT_EQ(enc.enc_class, encounter_class::inpatient);
    EXPECT_EQ(enc.patient_reference, "Patient/12345");
}

TEST_F(EncounterJsonParsingTest, ParseFullEncounter) {
    if (!is_json_parsing_available()) {
        GTEST_SKIP() << "JSON parsing not available (nlohmann/json not configured)";
    }

    auto result = parse_encounter_json(create_full_encounter_json());

    ASSERT_TRUE(std::holds_alternative<encounter_info>(result));
    auto& enc = std::get<encounter_info>(result);

    EXPECT_EQ(enc.id, "enc-12345");
    EXPECT_EQ(enc.visit_number, "VN-2025-001");
    EXPECT_EQ(enc.status, encounter_status::in_progress);
    EXPECT_EQ(enc.enc_class, encounter_class::inpatient);
    EXPECT_EQ(enc.class_display, "inpatient encounter");
    EXPECT_EQ(enc.patient_reference, "Patient/12345");
    EXPECT_TRUE(enc.start_time.has_value());
    EXPECT_FALSE(enc.end_time.has_value());

    // Type
    ASSERT_FALSE(enc.type_codes.empty());
    EXPECT_EQ(enc.type_codes[0], "183452005");
    EXPECT_EQ(enc.type_display, "Emergency Admission");

    // Location
    ASSERT_EQ(enc.locations.size(), 1);
    EXPECT_EQ(enc.locations[0].id, "Location/ward-3a");
    EXPECT_EQ(enc.locations[0].display, "Ward 3A");
    EXPECT_EQ(enc.locations[0].status, "active");
    EXPECT_EQ(enc.locations[0].physical_type, "wa");

    // Participant
    ASSERT_EQ(enc.participants.size(), 1);
    EXPECT_EQ(enc.participants[0].id, "Practitioner/dr-smith");
    EXPECT_EQ(enc.participants[0].display, "Dr. John Smith");
    EXPECT_EQ(enc.participants[0].type, "ATND");

    // Service provider
    EXPECT_EQ(enc.service_provider, "Organization/hospital-main");
    EXPECT_EQ(enc.service_provider_display, "Main Hospital");

    // Reason
    EXPECT_EQ(enc.reason_text, "Chest pain");

    // Diagnosis
    ASSERT_EQ(enc.diagnosis_references.size(), 1);
    EXPECT_EQ(enc.diagnosis_references[0], "Condition/heart-condition-1");
}

TEST_F(EncounterJsonParsingTest, ParseInvalidJson) {
    auto result = parse_encounter_json("not valid json");
    ASSERT_TRUE(std::holds_alternative<encounter_error>(result));
    EXPECT_EQ(std::get<encounter_error>(result), encounter_error::parse_failed);
}

TEST_F(EncounterJsonParsingTest, ParseWrongResourceType) {
    if (!is_json_parsing_available()) {
        GTEST_SKIP() << "JSON parsing not available (nlohmann/json not configured)";
    }

    auto result = parse_encounter_json(R"({
        "resourceType": "Patient",
        "id": "12345"
    })");

    ASSERT_TRUE(std::holds_alternative<encounter_error>(result));
    EXPECT_EQ(std::get<encounter_error>(result), encounter_error::invalid_data);
}

TEST_F(EncounterJsonParsingTest, ParseMissingId) {
    if (!is_json_parsing_available()) {
        GTEST_SKIP() << "JSON parsing not available (nlohmann/json not configured)";
    }

    auto result = parse_encounter_json(R"({
        "resourceType": "Encounter",
        "status": "in-progress"
    })");

    ASSERT_TRUE(std::holds_alternative<encounter_error>(result));
    EXPECT_EQ(std::get<encounter_error>(result), encounter_error::invalid_data);
}

TEST_F(EncounterJsonParsingTest, ParseAllStatusValues) {
    if (!is_json_parsing_available()) {
        GTEST_SKIP() << "JSON parsing not available (nlohmann/json not configured)";
    }

    std::vector<std::pair<std::string, encounter_status>> status_tests = {
        {"planned", encounter_status::planned},
        {"arrived", encounter_status::arrived},
        {"triaged", encounter_status::triaged},
        {"in-progress", encounter_status::in_progress},
        {"onleave", encounter_status::on_leave},
        {"finished", encounter_status::finished},
        {"cancelled", encounter_status::cancelled},
        {"entered-in-error", encounter_status::entered_in_error},
    };

    for (const auto& [status_str, expected] : status_tests) {
        std::string json = R"({
            "resourceType": "Encounter",
            "id": "test",
            "status": ")" + status_str +
                           R"("
        })";

        auto result = parse_encounter_json(json);
        ASSERT_TRUE(std::holds_alternative<encounter_info>(result))
            << "Failed for status: " << status_str;
        EXPECT_EQ(std::get<encounter_info>(result).status, expected)
            << "Status mismatch for: " << status_str;
    }
}

TEST_F(EncounterJsonParsingTest, ParseAllClassValues) {
    if (!is_json_parsing_available()) {
        GTEST_SKIP() << "JSON parsing not available (nlohmann/json not configured)";
    }

    std::vector<std::pair<std::string, encounter_class>> class_tests = {
        {"IMP", encounter_class::inpatient},
        {"AMB", encounter_class::outpatient},
        {"EMER", encounter_class::emergency},
        {"HH", encounter_class::home_health},
        {"VR", encounter_class::virtual_visit},
        {"PRENC", encounter_class::preadmission},
        {"SS", encounter_class::short_stay},
    };

    for (const auto& [class_code, expected] : class_tests) {
        std::string json = R"({
            "resourceType": "Encounter",
            "id": "test",
            "status": "in-progress",
            "class": {
                "code": ")" + class_code +
                           R"("
            }
        })";

        auto result = parse_encounter_json(json);
        ASSERT_TRUE(std::holds_alternative<encounter_info>(result))
            << "Failed for class: " << class_code;
        EXPECT_EQ(std::get<encounter_info>(result).enc_class, expected)
            << "Class mismatch for: " << class_code;
    }
}

// =============================================================================
// Encounter Context Provider Tests (with mock client)
// =============================================================================

class EncounterContextProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Provider without client (will fail queries but test interface)
        encounter_context_config config;
        config.cache_ttl = std::chrono::seconds{60};
        config.max_cache_size = 100;
        provider_ = std::make_unique<encounter_context_provider>(std::move(config));
    }

    std::unique_ptr<encounter_context_provider> provider_;
};

TEST_F(EncounterContextProviderTest, GetEncounterWithoutClient) {
    auto result = provider_->get_encounter("enc-12345");

    ASSERT_TRUE(std::holds_alternative<encounter_error>(result));
    EXPECT_EQ(std::get<encounter_error>(result), encounter_error::query_failed);
}

TEST_F(EncounterContextProviderTest, FindByVisitNumberWithoutClient) {
    auto result = provider_->find_by_visit_number("VN-2025-001");

    ASSERT_TRUE(std::holds_alternative<encounter_error>(result));
    EXPECT_EQ(std::get<encounter_error>(result), encounter_error::query_failed);
}

TEST_F(EncounterContextProviderTest, FindActiveEncounterWithoutClient) {
    auto result = provider_->find_active_encounter("patient-123");

    ASSERT_TRUE(std::holds_alternative<encounter_error>(result));
    EXPECT_EQ(std::get<encounter_error>(result), encounter_error::query_failed);
}

TEST_F(EncounterContextProviderTest, FindEncountersWithoutClient) {
    auto result = provider_->find_encounters("patient-123");

    ASSERT_TRUE(std::holds_alternative<encounter_error>(result));
    EXPECT_EQ(std::get<encounter_error>(result), encounter_error::query_failed);
}

TEST_F(EncounterContextProviderTest, CacheOperations) {
    // Clear should not throw
    EXPECT_NO_THROW(provider_->clear_cache());

    // Get stats
    auto stats = provider_->get_cache_stats();
    EXPECT_EQ(stats.total_entries, 0);
    EXPECT_EQ(stats.cache_hits, 0);
    // Cache misses should be > 0 after failed queries above
}

TEST_F(EncounterContextProviderTest, MoveConstruction) {
    encounter_context_config config;
    encounter_context_provider provider1(std::move(config));

    // Move construction
    encounter_context_provider provider2(std::move(provider1));

    // provider2 should be functional
    auto result = provider2.get_encounter("test");
    EXPECT_TRUE(std::holds_alternative<encounter_error>(result));
}

TEST_F(EncounterContextProviderTest, MoveAssignment) {
    encounter_context_config config1;
    encounter_context_provider provider1(std::move(config1));

    encounter_context_config config2;
    encounter_context_provider provider2(std::move(config2));

    // Move assignment
    provider2 = std::move(provider1);

    // provider2 should be functional
    auto result = provider2.get_encounter("test");
    EXPECT_TRUE(std::holds_alternative<encounter_error>(result));
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
