/**
 * @file patient_lookup_test.cpp
 * @brief Unit tests for patient demographics lookup
 *
 * Tests the patient lookup functionality including:
 *   - Patient record structure
 *   - FHIR Patient parsing
 *   - Patient matching and disambiguation
 *   - Lookup service operations
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/104
 */

#include <gtest/gtest.h>

#include "pacs/bridge/emr/patient_lookup.h"
#include "pacs/bridge/emr/patient_matcher.h"
#include "pacs/bridge/emr/patient_record.h"

#include <chrono>
#include <string>

using namespace pacs::bridge::emr;
using namespace std::chrono_literals;

// =============================================================================
// Patient Error Tests
// =============================================================================

class PatientErrorTest : public ::testing::Test {};

TEST_F(PatientErrorTest, ErrorCodeValues) {
    EXPECT_EQ(to_error_code(patient_error::not_found), -1040);
    EXPECT_EQ(to_error_code(patient_error::multiple_found), -1041);
    EXPECT_EQ(to_error_code(patient_error::query_failed), -1042);
    EXPECT_EQ(to_error_code(patient_error::invalid_data), -1043);
    EXPECT_EQ(to_error_code(patient_error::merge_detected), -1044);
}

TEST_F(PatientErrorTest, ErrorToString) {
    EXPECT_STREQ(to_string(patient_error::not_found),
                 "Patient not found in EMR");
    EXPECT_STREQ(to_string(patient_error::multiple_found),
                 "Multiple patients found, disambiguation required");
    EXPECT_STREQ(to_string(patient_error::query_failed),
                 "Patient query failed");
}

// =============================================================================
// Patient Identifier Tests
// =============================================================================

class PatientIdentifierTest : public ::testing::Test {};

TEST_F(PatientIdentifierTest, MatchesSystem) {
    patient_identifier id;
    id.value = "12345";
    id.system = "http://hospital.org/mrn";

    EXPECT_TRUE(id.matches_system("http://hospital.org/mrn"));
    EXPECT_FALSE(id.matches_system("http://other.org/mrn"));
}

TEST_F(PatientIdentifierTest, IsMrn) {
    patient_identifier mrn_id;
    mrn_id.type_code = "MR";
    EXPECT_TRUE(mrn_id.is_mrn());

    patient_identifier other_id;
    other_id.type_code = "SS";
    EXPECT_FALSE(other_id.is_mrn());

    patient_identifier no_type;
    EXPECT_FALSE(no_type.is_mrn());
}

// =============================================================================
// Patient Name Tests
// =============================================================================

class PatientNameTest : public ::testing::Test {};

TEST_F(PatientNameTest, FirstGiven) {
    patient_name name;
    EXPECT_TRUE(name.first_given().empty());

    name.given = {"John"};
    EXPECT_EQ(name.first_given(), "John");

    name.given = {"John", "Andrew"};
    EXPECT_EQ(name.first_given(), "John");
}

TEST_F(PatientNameTest, MiddleNames) {
    patient_name name;
    EXPECT_TRUE(name.middle_names().empty());

    name.given = {"John"};
    EXPECT_TRUE(name.middle_names().empty());

    name.given = {"John", "Andrew"};
    EXPECT_EQ(name.middle_names(), "Andrew");

    name.given = {"John", "Andrew", "James"};
    EXPECT_EQ(name.middle_names(), "Andrew James");
}

TEST_F(PatientNameTest, ToDicomPn) {
    patient_name name;
    name.family = "Smith";
    name.given = {"John"};

    EXPECT_EQ(name.to_dicom_pn(), "Smith^John");

    name.given = {"John", "Andrew"};
    EXPECT_EQ(name.to_dicom_pn(), "Smith^John^Andrew");

    name.prefix = {"Dr."};
    name.suffix = {"Jr."};
    EXPECT_EQ(name.to_dicom_pn(), "Smith^John^Andrew^Dr.^Jr.");
}

// =============================================================================
// Patient Record Tests
// =============================================================================

class PatientRecordTest : public ::testing::Test {
protected:
    patient_record create_sample_patient() {
        patient_record patient;
        patient.id = "123";
        patient.mrn = "MRN12345";

        patient_name name;
        name.use = "official";
        name.family = "Doe";
        name.given = {"John", "Andrew"};
        patient.names.push_back(name);

        patient.birth_date = "1980-05-15";
        patient.sex = "male";
        patient.active = true;

        patient_identifier mrn_id;
        mrn_id.value = "MRN12345";
        mrn_id.system = "http://hospital.org/mrn";
        mrn_id.type_code = "MR";
        patient.identifiers.push_back(mrn_id);

        return patient;
    }
};

TEST_F(PatientRecordTest, OfficialName) {
    auto patient = create_sample_patient();

    auto* name = patient.official_name();
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->family.value_or(""), "Doe");
}

TEST_F(PatientRecordTest, FamilyAndGivenName) {
    auto patient = create_sample_patient();

    EXPECT_EQ(patient.family_name(), "Doe");
    EXPECT_EQ(patient.given_name(), "John");
    EXPECT_EQ(patient.middle_name(), "Andrew");
}

TEST_F(PatientRecordTest, DicomName) {
    auto patient = create_sample_patient();
    EXPECT_EQ(patient.dicom_name(), "Doe^John^Andrew");
}

TEST_F(PatientRecordTest, DicomBirthDate) {
    auto patient = create_sample_patient();
    EXPECT_EQ(patient.dicom_birth_date(), "19800515");

    patient.birth_date = std::nullopt;
    EXPECT_TRUE(patient.dicom_birth_date().empty());
}

TEST_F(PatientRecordTest, DicomSex) {
    auto patient = create_sample_patient();
    EXPECT_EQ(patient.dicom_sex(), "M");

    patient.sex = "female";
    EXPECT_EQ(patient.dicom_sex(), "F");

    patient.sex = "other";
    EXPECT_EQ(patient.dicom_sex(), "O");

    patient.sex = std::nullopt;
    EXPECT_TRUE(patient.dicom_sex().empty());
}

TEST_F(PatientRecordTest, IdentifierBySystem) {
    auto patient = create_sample_patient();

    auto mrn = patient.identifier_by_system("http://hospital.org/mrn");
    ASSERT_TRUE(mrn.has_value());
    EXPECT_EQ(*mrn, "MRN12345");

    auto missing = patient.identifier_by_system("http://other.org");
    EXPECT_FALSE(missing.has_value());
}

TEST_F(PatientRecordTest, IsValid) {
    patient_record patient;
    EXPECT_FALSE(patient.is_valid());

    patient.id = "123";
    EXPECT_FALSE(patient.is_valid());

    patient.mrn = "MRN12345";
    EXPECT_TRUE(patient.is_valid());
}

TEST_F(PatientRecordTest, IsMerged) {
    patient_record patient;
    EXPECT_FALSE(patient.is_merged());

    patient.link_type = "replaces";
    EXPECT_FALSE(patient.is_merged());

    patient.link_type = "replaced-by";
    EXPECT_TRUE(patient.is_merged());
}

// =============================================================================
// Patient Query Tests
// =============================================================================

class PatientQueryTest : public ::testing::Test {};

TEST_F(PatientQueryTest, IsEmpty) {
    patient_query query;
    EXPECT_TRUE(query.is_empty());

    query.patient_id = "12345";
    EXPECT_FALSE(query.is_empty());
}

TEST_F(PatientQueryTest, IsMrnLookup) {
    patient_query query;
    EXPECT_FALSE(query.is_mrn_lookup());

    query.patient_id = "12345";
    EXPECT_TRUE(query.is_mrn_lookup());

    query.family_name = "Doe";
    EXPECT_FALSE(query.is_mrn_lookup());
}

TEST_F(PatientQueryTest, ByMrn) {
    auto query = patient_query::by_mrn("MRN12345");

    EXPECT_TRUE(query.patient_id.has_value());
    EXPECT_EQ(*query.patient_id, "MRN12345");
    EXPECT_EQ(query.max_results, 1);
    EXPECT_TRUE(query.is_mrn_lookup());
}

TEST_F(PatientQueryTest, ByNameDob) {
    auto query = patient_query::by_name_dob("Doe", "John", "1980-05-15");

    EXPECT_TRUE(query.family_name.has_value());
    EXPECT_EQ(*query.family_name, "Doe");
    EXPECT_TRUE(query.given_name.has_value());
    EXPECT_EQ(*query.given_name, "John");
    EXPECT_TRUE(query.birth_date.has_value());
    EXPECT_EQ(*query.birth_date, "1980-05-15");
    EXPECT_FALSE(query.is_mrn_lookup());
}

TEST_F(PatientQueryTest, ByIdentifier) {
    auto query = patient_query::by_identifier(
        "http://hospital.org/mrn", "12345");

    EXPECT_TRUE(query.identifier_system.has_value());
    EXPECT_EQ(*query.identifier_system, "http://hospital.org/mrn");
    EXPECT_TRUE(query.patient_id.has_value());
    EXPECT_EQ(*query.patient_id, "12345");
}

// =============================================================================
// FHIR Patient Parsing Tests
// =============================================================================

class FhirPatientParseTest : public ::testing::Test {
protected:
    const std::string valid_patient_json = R"({
        "resourceType": "Patient",
        "id": "patient-123",
        "meta": {
            "versionId": "1",
            "lastUpdated": "2024-01-15T10:30:00Z"
        },
        "identifier": [
            {
                "use": "usual",
                "type": {
                    "coding": [
                        {
                            "system": "http://terminology.hl7.org/CodeSystem/v2-0203",
                            "code": "MR",
                            "display": "Medical Record Number"
                        }
                    ]
                },
                "system": "http://hospital.org/mrn",
                "value": "MRN12345"
            }
        ],
        "active": true,
        "name": [
            {
                "use": "official",
                "family": "Doe",
                "given": ["John", "Andrew"]
            }
        ],
        "telecom": [
            {
                "system": "phone",
                "value": "555-1234",
                "use": "home"
            },
            {
                "system": "email",
                "value": "john.doe@example.com"
            }
        ],
        "gender": "male",
        "birthDate": "1980-05-15",
        "address": [
            {
                "use": "home",
                "line": ["123 Main St", "Apt 4"],
                "city": "Boston",
                "state": "MA",
                "postalCode": "02101",
                "country": "USA"
            }
        ]
    })";
};

TEST_F(FhirPatientParseTest, ValidPatient) {
    auto result = parse_fhir_patient(valid_patient_json);

    ASSERT_TRUE(result.has_value());

    auto& patient = *result;
    EXPECT_EQ(patient.id, "patient-123");
    EXPECT_EQ(patient.mrn, "MRN12345");
    EXPECT_EQ(patient.version_id.value_or(""), "1");
    EXPECT_TRUE(patient.active);

    // Check name
    ASSERT_EQ(patient.names.size(), 1);
    EXPECT_EQ(patient.family_name(), "Doe");
    EXPECT_EQ(patient.given_name(), "John");

    // Check birth date and gender
    EXPECT_EQ(patient.birth_date.value_or(""), "1980-05-15");
    EXPECT_EQ(patient.sex.value_or(""), "male");

    // Check identifiers
    ASSERT_EQ(patient.identifiers.size(), 1);
    EXPECT_EQ(patient.identifiers[0].value, "MRN12345");
    EXPECT_TRUE(patient.identifiers[0].is_mrn());

    // Check telecom
    ASSERT_EQ(patient.telecom.size(), 2);
    EXPECT_EQ(patient.home_phone(), "555-1234");

    // Check address
    ASSERT_EQ(patient.addresses.size(), 1);
    auto* addr = patient.home_address();
    ASSERT_NE(addr, nullptr);
    EXPECT_EQ(addr->city.value_or(""), "Boston");
}

TEST_F(FhirPatientParseTest, InvalidResourceType) {
    std::string invalid_json = R"({
        "resourceType": "Observation",
        "id": "123"
    })";

    auto result = parse_fhir_patient(invalid_json);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), patient_error::invalid_data);
}

TEST_F(FhirPatientParseTest, MalformedJson) {
    std::string malformed = "{ not valid json }";

    auto result = parse_fhir_patient(malformed);
    EXPECT_FALSE(result.has_value());
    // Malformed JSON without resourceType field returns invalid_data
    EXPECT_EQ(result.error(), patient_error::invalid_data);
}

TEST_F(FhirPatientParseTest, MinimalPatient) {
    std::string minimal_json = R"({
        "resourceType": "Patient",
        "id": "minimal-123"
    })";

    auto result = parse_fhir_patient(minimal_json);
    ASSERT_TRUE(result.has_value());

    auto& patient = *result;
    EXPECT_EQ(patient.id, "minimal-123");
    EXPECT_TRUE(patient.active);  // Default
    EXPECT_TRUE(patient.names.empty());
    EXPECT_FALSE(patient.birth_date.has_value());
}

TEST_F(FhirPatientParseTest, DeceasedPatient) {
    std::string deceased_json = R"({
        "resourceType": "Patient",
        "id": "deceased-123",
        "deceasedDateTime": "2023-06-15T14:30:00Z"
    })";

    auto result = parse_fhir_patient(deceased_json);
    ASSERT_TRUE(result.has_value());

    auto& patient = *result;
    EXPECT_TRUE(patient.deceased.value_or(false));
    EXPECT_TRUE(patient.deceased_datetime.has_value());
}

TEST_F(FhirPatientParseTest, MergedPatient) {
    std::string merged_json = R"({
        "resourceType": "Patient",
        "id": "merged-123",
        "link": [
            {
                "other": {
                    "reference": "Patient/master-456"
                },
                "type": "replaced-by"
            }
        ]
    })";

    auto result = parse_fhir_patient(merged_json);
    ASSERT_TRUE(result.has_value());

    auto& patient = *result;
    EXPECT_TRUE(patient.is_merged());
    EXPECT_EQ(patient.link_reference.value_or(""), "Patient/master-456");
}

// =============================================================================
// Patient Matcher Tests
// =============================================================================

class PatientMatcherTest : public ::testing::Test {
protected:
    patient_record create_patient(
        const std::string& mrn,
        const std::string& family,
        const std::string& given,
        const std::string& birthdate) {

        patient_record patient;
        patient.id = "id-" + mrn;
        patient.mrn = mrn;

        patient_name name;
        name.family = family;
        name.given = {given};
        patient.names.push_back(name);

        patient.birth_date = birthdate;

        return patient;
    }
};

TEST_F(PatientMatcherTest, StringSimilarity) {
    // Exact match
    EXPECT_DOUBLE_EQ(patient_matcher::string_similarity("John", "John"), 1.0);

    // Similar strings
    double sim = patient_matcher::string_similarity("John", "Jon");
    EXPECT_GT(sim, 0.8);

    // Different strings
    sim = patient_matcher::string_similarity("John", "Mary");
    EXPECT_LT(sim, 0.5);

    // Empty strings
    EXPECT_DOUBLE_EQ(patient_matcher::string_similarity("", ""), 1.0);
    EXPECT_DOUBLE_EQ(patient_matcher::string_similarity("John", ""), 0.0);
}

TEST_F(PatientMatcherTest, EditDistance) {
    EXPECT_EQ(patient_matcher::edit_distance("John", "John"), 0);
    EXPECT_EQ(patient_matcher::edit_distance("John", "Jon"), 1);
    EXPECT_EQ(patient_matcher::edit_distance("John", "Jonn"), 1);
    EXPECT_EQ(patient_matcher::edit_distance("John", "Joan"), 1);
    EXPECT_EQ(patient_matcher::edit_distance("kitten", "sitting"), 3);
}

TEST_F(PatientMatcherTest, NormalizeName) {
    EXPECT_EQ(patient_matcher::normalize_name("John Doe"), "johndoe");
    EXPECT_EQ(patient_matcher::normalize_name("O'Brien"), "obrien");
    EXPECT_EQ(patient_matcher::normalize_name("Mary-Jane"), "maryjane");
}

TEST_F(PatientMatcherTest, CompareDates) {
    // Exact match
    EXPECT_DOUBLE_EQ(patient_matcher::compare_dates(
        "1980-05-15", "1980-05-15"), 1.0);

    // Year and month match
    EXPECT_DOUBLE_EQ(patient_matcher::compare_dates(
        "1980-05-15", "1980-05-20"), 0.8);

    // Year only match
    EXPECT_DOUBLE_EQ(patient_matcher::compare_dates(
        "1980-05-15", "1980-10-20"), 0.5);

    // No match
    EXPECT_DOUBLE_EQ(patient_matcher::compare_dates(
        "1980-05-15", "1990-05-15"), 0.0);
}

TEST_F(PatientMatcherTest, CalculateScoreExactMatch) {
    patient_matcher matcher;

    auto patient = create_patient("MRN123", "Doe", "John", "1980-05-15");

    match_criteria criteria;
    criteria.mrn = "MRN123";
    criteria.family_name = "Doe";
    criteria.given_name = "John";
    criteria.birth_date = "1980-05-15";

    double score = matcher.calculate_score(patient, criteria);
    EXPECT_GT(score, 0.95);  // Should be very high for exact match
}

TEST_F(PatientMatcherTest, CalculateScorePartialMatch) {
    patient_matcher matcher;

    auto patient = create_patient("MRN123", "Doe", "John", "1980-05-15");

    match_criteria criteria;
    criteria.family_name = "Doe";
    criteria.given_name = "Jonathan";  // Different given name
    criteria.birth_date = "1980-05-15";

    double score = matcher.calculate_score(patient, criteria);
    EXPECT_GT(score, 0.5);    // Should still be decent due to other matches
    EXPECT_LE(score, 0.96);   // But not as high as exact match
}

TEST_F(PatientMatcherTest, FindBestMatchDefinitive) {
    patient_matcher matcher;

    std::vector<patient_record> candidates = {
        create_patient("MRN001", "Smith", "Jane", "1990-01-01"),
        create_patient("MRN123", "Doe", "John", "1980-05-15"),
        create_patient("MRN002", "Johnson", "Bob", "1975-12-20")
    };

    match_criteria criteria;
    criteria.mrn = "MRN123";
    criteria.family_name = "Doe";
    criteria.given_name = "John";
    criteria.birth_date = "1980-05-15";

    auto result = matcher.find_best_match(candidates, criteria);

    EXPECT_TRUE(result.is_definitive);
    EXPECT_FALSE(result.needs_disambiguation);
    // best_match_index is the index in the sorted candidates list
    EXPECT_GE(result.best_match_index, 0);
    EXPECT_GT(result.best_match_score, 0.95);
    // Verify the best match is actually John Doe
    ASSERT_NE(result.best_patient(), nullptr);
    EXPECT_EQ(result.best_patient()->mrn, "MRN123");
}

TEST_F(PatientMatcherTest, FindBestMatchAmbiguous) {
    patient_matcher matcher;

    // Two similar patients
    std::vector<patient_record> candidates = {
        create_patient("MRN001", "Doe", "John", "1980-05-15"),
        create_patient("MRN002", "Doe", "John", "1980-05-20")
    };

    match_criteria criteria;
    criteria.family_name = "Doe";
    criteria.given_name = "John";
    // No birth date - makes it ambiguous

    auto result = matcher.find_best_match(candidates, criteria);

    EXPECT_FALSE(result.is_definitive);
    // Both match equally well without birthdate
}

TEST_F(PatientMatcherTest, ScoreCandidatesSorted) {
    patient_matcher matcher;

    std::vector<patient_record> candidates = {
        create_patient("MRN001", "Smith", "Jane", "1990-01-01"),
        create_patient("MRN123", "Doe", "John", "1980-05-15"),
        create_patient("MRN002", "Johnson", "Bob", "1975-12-20")
    };

    match_criteria criteria;
    criteria.family_name = "Doe";
    criteria.given_name = "John";

    auto matches = matcher.score_candidates(candidates, criteria);

    ASSERT_EQ(matches.size(), 3);

    // Should be sorted by score descending
    for (size_t i = 1; i < matches.size(); ++i) {
        EXPECT_GE(matches[i - 1].score, matches[i].score);
    }

    // Best match should be John Doe
    EXPECT_EQ(matches[0].patient.mrn, "MRN123");
}

TEST_F(PatientMatcherTest, ComparePatients) {
    patient_matcher matcher;

    auto patient1 = create_patient("MRN123", "Doe", "John", "1980-05-15");
    auto patient2 = create_patient("MRN123", "Doe", "John", "1980-05-15");
    auto patient3 = create_patient("MRN456", "Smith", "Jane", "1990-01-01");

    // Same patients should have high similarity
    double same = matcher.compare_patients(patient1, patient2);
    EXPECT_GT(same, 0.95);

    // Different patients should have low similarity
    double different = matcher.compare_patients(patient1, patient3);
    EXPECT_LT(different, 0.5);
}

// =============================================================================
// Disambiguation Strategy Tests
// =============================================================================

class DisambiguationTest : public ::testing::Test {
protected:
    match_result create_ambiguous_result() {
        match_result result;
        result.is_definitive = false;
        result.needs_disambiguation = true;

        patient_match m1;
        m1.score = 0.85;
        patient_match m2;
        m2.score = 0.80;

        result.candidates = {m1, m2};
        result.best_match_index = 0;
        result.best_match_score = 0.85;

        return result;
    }
};

TEST_F(DisambiguationTest, HighestScoreStrategy) {
    auto result = create_ambiguous_result();

    auto resolved = apply_disambiguation_strategy(
        result, disambiguation_strategy::highest_score, 0.8);

    EXPECT_TRUE(resolved.is_definitive);
    EXPECT_FALSE(resolved.needs_disambiguation);
}

TEST_F(DisambiguationTest, ManualOnlyStrategy) {
    auto result = create_ambiguous_result();
    result.is_definitive = true;  // Even if definitive

    auto resolved = apply_disambiguation_strategy(
        result, disambiguation_strategy::manual_only, 0.8);

    EXPECT_FALSE(resolved.is_definitive);
    EXPECT_TRUE(resolved.needs_disambiguation);
}

// =============================================================================
// Matcher Configuration Tests
// =============================================================================

class MatcherConfigTest : public ::testing::Test {};

TEST_F(MatcherConfigTest, DefaultConfig) {
    matcher_config config;

    EXPECT_DOUBLE_EQ(config.mrn_weight, 1.0);
    EXPECT_DOUBLE_EQ(config.min_match_score, 0.5);
    EXPECT_DOUBLE_EQ(config.definitive_threshold, 0.95);
    EXPECT_TRUE(config.fuzzy_name_matching);
    EXPECT_TRUE(config.normalize_names);
}

TEST_F(MatcherConfigTest, CustomConfig) {
    matcher_config config;
    config.mrn_weight = 0.5;
    config.birth_date_weight = 0.8;
    config.definitive_threshold = 0.90;

    patient_matcher matcher(config);

    EXPECT_DOUBLE_EQ(matcher.config().mrn_weight, 0.5);
    EXPECT_DOUBLE_EQ(matcher.config().birth_date_weight, 0.8);
    EXPECT_DOUBLE_EQ(matcher.config().definitive_threshold, 0.90);
}

// =============================================================================
// Lookup Configuration Tests
// =============================================================================

class LookupConfigTest : public ::testing::Test {};

TEST_F(LookupConfigTest, DefaultConfig) {
    patient_lookup_config config;

    EXPECT_TRUE(config.enable_cache);
    EXPECT_EQ(config.cache_ttl, 3600s);
    EXPECT_EQ(config.negative_cache_ttl, 300s);
    EXPECT_TRUE(config.auto_disambiguate);
}
