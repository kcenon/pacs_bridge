/**
 * @file emr_adapter_test.cpp
 * @brief Unit tests for EMR Adapter functionality
 *
 * Tests the EMR adapter interface and implementations including:
 *   - Adapter error codes and strings
 *   - EMR vendor parsing
 *   - Adapter configuration validation
 *   - Adapter features
 *   - Generic FHIR adapter interface compliance
 *   - Adapter factory function
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/107
 * @see https://github.com/kcenon/pacs_bridge/issues/121
 */

#include <gtest/gtest.h>

#include "pacs/bridge/emr/emr_adapter.h"
#include "pacs/bridge/emr/adapters/generic_fhir_adapter.h"

#include <chrono>
#include <string>

using namespace pacs::bridge::emr;
using namespace std::chrono_literals;

// =============================================================================
// Adapter Error Tests
// =============================================================================

class AdapterErrorTest : public ::testing::Test {};

TEST_F(AdapterErrorTest, ErrorCodeValues) {
    EXPECT_EQ(to_error_code(adapter_error::not_initialized), -1100);
    EXPECT_EQ(to_error_code(adapter_error::connection_failed), -1101);
    EXPECT_EQ(to_error_code(adapter_error::authentication_failed), -1102);
    EXPECT_EQ(to_error_code(adapter_error::not_supported), -1103);
    EXPECT_EQ(to_error_code(adapter_error::invalid_configuration), -1104);
    EXPECT_EQ(to_error_code(adapter_error::timeout), -1105);
    EXPECT_EQ(to_error_code(adapter_error::rate_limited), -1106);
    EXPECT_EQ(to_error_code(adapter_error::invalid_vendor), -1107);
    EXPECT_EQ(to_error_code(adapter_error::health_check_failed), -1108);
    EXPECT_EQ(to_error_code(adapter_error::feature_unavailable), -1109);
}

TEST_F(AdapterErrorTest, ErrorToString) {
    EXPECT_STREQ(to_string(adapter_error::not_initialized),
                 "EMR adapter not initialized");
    EXPECT_STREQ(to_string(adapter_error::connection_failed),
                 "Connection to EMR failed");
    EXPECT_STREQ(to_string(adapter_error::authentication_failed),
                 "EMR authentication failed");
    EXPECT_STREQ(to_string(adapter_error::not_supported),
                 "Operation not supported by this adapter");
    EXPECT_STREQ(to_string(adapter_error::invalid_configuration),
                 "Invalid adapter configuration");
    EXPECT_STREQ(to_string(adapter_error::timeout),
                 "EMR operation timed out");
    EXPECT_STREQ(to_string(adapter_error::rate_limited),
                 "Rate limited by EMR system");
    EXPECT_STREQ(to_string(adapter_error::invalid_vendor),
                 "Invalid EMR vendor type");
}

// =============================================================================
// EMR Vendor Tests
// =============================================================================

class EmrVendorTest : public ::testing::Test {};

TEST_F(EmrVendorTest, VendorToString) {
    EXPECT_EQ(to_string(emr_vendor::generic_fhir), "generic");
    EXPECT_EQ(to_string(emr_vendor::epic), "epic");
    EXPECT_EQ(to_string(emr_vendor::cerner), "cerner");
    EXPECT_EQ(to_string(emr_vendor::meditech), "meditech");
    EXPECT_EQ(to_string(emr_vendor::allscripts), "allscripts");
    EXPECT_EQ(to_string(emr_vendor::unknown), "unknown");
}

TEST_F(EmrVendorTest, ParseVendorGeneric) {
    EXPECT_EQ(parse_emr_vendor("generic"), emr_vendor::generic_fhir);
    EXPECT_EQ(parse_emr_vendor("generic_fhir"), emr_vendor::generic_fhir);
    EXPECT_EQ(parse_emr_vendor("fhir"), emr_vendor::generic_fhir);
    EXPECT_EQ(parse_emr_vendor("GENERIC"), emr_vendor::generic_fhir);
    EXPECT_EQ(parse_emr_vendor("Generic"), emr_vendor::generic_fhir);
}

TEST_F(EmrVendorTest, ParseVendorEpic) {
    EXPECT_EQ(parse_emr_vendor("epic"), emr_vendor::epic);
    EXPECT_EQ(parse_emr_vendor("EPIC"), emr_vendor::epic);
    EXPECT_EQ(parse_emr_vendor("Epic"), emr_vendor::epic);
}

TEST_F(EmrVendorTest, ParseVendorCerner) {
    EXPECT_EQ(parse_emr_vendor("cerner"), emr_vendor::cerner);
    EXPECT_EQ(parse_emr_vendor("oracle"), emr_vendor::cerner);
    EXPECT_EQ(parse_emr_vendor("oracle_health"), emr_vendor::cerner);
    EXPECT_EQ(parse_emr_vendor("CERNER"), emr_vendor::cerner);
}

TEST_F(EmrVendorTest, ParseVendorOther) {
    EXPECT_EQ(parse_emr_vendor("meditech"), emr_vendor::meditech);
    EXPECT_EQ(parse_emr_vendor("allscripts"), emr_vendor::allscripts);
}

TEST_F(EmrVendorTest, ParseVendorUnknown) {
    EXPECT_EQ(parse_emr_vendor("invalid"), emr_vendor::unknown);
    EXPECT_EQ(parse_emr_vendor(""), emr_vendor::unknown);
    EXPECT_EQ(parse_emr_vendor("some_random_vendor"), emr_vendor::unknown);
}

// =============================================================================
// Adapter Configuration Tests
// =============================================================================

class AdapterConfigTest : public ::testing::Test {};

TEST_F(AdapterConfigTest, DefaultConfig) {
    emr_adapter_config config;

    EXPECT_EQ(config.vendor, emr_vendor::generic_fhir);
    EXPECT_TRUE(config.base_url.empty());
    EXPECT_EQ(config.auth_type, "oauth2");
    EXPECT_FALSE(config.client_id.has_value());
    EXPECT_FALSE(config.client_secret.has_value());
    EXPECT_FALSE(config.token_url.has_value());
    EXPECT_EQ(config.timeout, 30s);
    EXPECT_FALSE(config.strict_mode);
    EXPECT_FALSE(config.epic_non_production);
}

TEST_F(AdapterConfigTest, ValidationEmptyUrl) {
    emr_adapter_config config;
    // Empty base_url
    EXPECT_FALSE(config.is_valid());
}

TEST_F(AdapterConfigTest, ValidationOAuth2MissingClientId) {
    emr_adapter_config config;
    config.base_url = "https://emr.example.com/fhir";
    config.auth_type = "oauth2";
    // Missing client_id
    EXPECT_FALSE(config.is_valid());
}

TEST_F(AdapterConfigTest, ValidationOAuth2MissingTokenUrl) {
    emr_adapter_config config;
    config.base_url = "https://emr.example.com/fhir";
    config.auth_type = "oauth2";
    config.client_id = "client123";
    // Missing token_url
    EXPECT_FALSE(config.is_valid());
}

TEST_F(AdapterConfigTest, ValidationOAuth2Valid) {
    emr_adapter_config config;
    config.base_url = "https://emr.example.com/fhir";
    config.auth_type = "oauth2";
    config.client_id = "client123";
    config.token_url = "https://emr.example.com/oauth/token";

    EXPECT_TRUE(config.is_valid());
}

TEST_F(AdapterConfigTest, ValidationBasicAuthMissingUsername) {
    emr_adapter_config config;
    config.base_url = "https://emr.example.com/fhir";
    config.auth_type = "basic";
    // Missing username
    EXPECT_FALSE(config.is_valid());
}

TEST_F(AdapterConfigTest, ValidationBasicAuthValid) {
    emr_adapter_config config;
    config.base_url = "https://emr.example.com/fhir";
    config.auth_type = "basic";
    config.username = "admin";
    config.password = "secret";

    EXPECT_TRUE(config.is_valid());
}

TEST_F(AdapterConfigTest, ValidationUnknownAuthType) {
    emr_adapter_config config;
    config.base_url = "https://emr.example.com/fhir";
    config.auth_type = "none";  // Not oauth2 or basic

    EXPECT_TRUE(config.is_valid());  // Other auth types pass basic validation
}

// =============================================================================
// Adapter Features Tests
// =============================================================================

class AdapterFeaturesTest : public ::testing::Test {};

TEST_F(AdapterFeaturesTest, DefaultFeatures) {
    adapter_features features;

    EXPECT_TRUE(features.patient_lookup);
    EXPECT_TRUE(features.patient_search);
    EXPECT_TRUE(features.result_posting);
    EXPECT_TRUE(features.result_updates);
    EXPECT_TRUE(features.encounter_context);
    EXPECT_TRUE(features.imaging_study);
    EXPECT_TRUE(features.service_request);
    EXPECT_FALSE(features.bulk_export);
    EXPECT_TRUE(features.smart_on_fhir);
    EXPECT_TRUE(features.oauth2_client_credentials);
    EXPECT_TRUE(features.basic_auth);
}

// =============================================================================
// Adapter Health Status Tests
// =============================================================================

class AdapterHealthStatusTest : public ::testing::Test {};

TEST_F(AdapterHealthStatusTest, DefaultStatus) {
    adapter_health_status status;

    EXPECT_FALSE(status.healthy);
    EXPECT_FALSE(status.connected);
    EXPECT_FALSE(status.authenticated);
    EXPECT_FALSE(status.last_check.has_value());
    EXPECT_FALSE(status.error_message.has_value());
    EXPECT_EQ(status.response_time, 0ms);
    EXPECT_FALSE(status.server_version.has_value());
    EXPECT_TRUE(status.supported_resources.empty());
}

TEST_F(AdapterHealthStatusTest, HealthyStatus) {
    adapter_health_status status;
    status.healthy = true;
    status.connected = true;
    status.authenticated = true;
    status.last_check = std::chrono::system_clock::now();
    status.response_time = 150ms;
    status.server_version = "4.0.1";
    status.supported_resources = {"Patient", "Encounter", "DiagnosticReport"};

    EXPECT_TRUE(status.healthy);
    EXPECT_TRUE(status.connected);
    EXPECT_TRUE(status.authenticated);
    EXPECT_TRUE(status.last_check.has_value());
    EXPECT_FALSE(status.error_message.has_value());
    EXPECT_EQ(status.response_time, 150ms);
    EXPECT_EQ(status.server_version.value(), "4.0.1");
    EXPECT_EQ(status.supported_resources.size(), 3);
}

TEST_F(AdapterHealthStatusTest, UnhealthyStatus) {
    adapter_health_status status;
    status.healthy = false;
    status.connected = false;
    status.error_message = "Connection refused";

    EXPECT_FALSE(status.healthy);
    EXPECT_FALSE(status.connected);
    EXPECT_TRUE(status.error_message.has_value());
    EXPECT_EQ(status.error_message.value(), "Connection refused");
}

// =============================================================================
// Generic FHIR Adapter Interface Tests
// =============================================================================

class GenericFhirAdapterTest : public ::testing::Test {
protected:
    emr_adapter_config create_valid_config() {
        emr_adapter_config config;
        config.vendor = emr_vendor::generic_fhir;
        config.base_url = "https://emr.example.com/fhir";
        config.auth_type = "oauth2";
        config.client_id = "test_client";
        config.token_url = "https://emr.example.com/oauth/token";
        return config;
    }
};

TEST_F(GenericFhirAdapterTest, VendorIdentification) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter(config);

    EXPECT_EQ(adapter.vendor(), emr_vendor::generic_fhir);
    EXPECT_EQ(adapter.vendor_name(), "Generic FHIR R4");
    EXPECT_EQ(adapter.version(), "1.0.0");
}

TEST_F(GenericFhirAdapterTest, DefaultFeatures) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter(config);

    auto features = adapter.features();
    EXPECT_TRUE(features.patient_lookup);
    EXPECT_TRUE(features.patient_search);
    EXPECT_TRUE(features.result_posting);
    EXPECT_TRUE(features.encounter_context);
    EXPECT_FALSE(features.bulk_export);  // Not yet implemented
}

TEST_F(GenericFhirAdapterTest, NotInitializedByDefault) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter(config);

    EXPECT_FALSE(adapter.is_initialized());
    EXPECT_FALSE(adapter.is_connected());
}

TEST_F(GenericFhirAdapterTest, ConfigAccess) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter(config);

    const auto& adapter_config = adapter.config();
    EXPECT_EQ(adapter_config.base_url, "https://emr.example.com/fhir");
    EXPECT_EQ(adapter_config.vendor, emr_vendor::generic_fhir);
}

TEST_F(GenericFhirAdapterTest, InitialStatistics) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter(config);

    auto stats = adapter.get_statistics();
    EXPECT_EQ(stats.total_requests, 0);
    EXPECT_EQ(stats.successful_requests, 0);
    EXPECT_EQ(stats.failed_requests, 0);
    EXPECT_EQ(stats.patient_queries, 0);
    EXPECT_EQ(stats.result_posts, 0);
    EXPECT_EQ(stats.encounter_queries, 0);
}

TEST_F(GenericFhirAdapterTest, ResetStatistics) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter(config);

    // Simulate some activity by getting stats multiple times
    auto stats = adapter.get_statistics();
    adapter.reset_statistics();

    stats = adapter.get_statistics();
    EXPECT_EQ(stats.total_requests, 0);
}

TEST_F(GenericFhirAdapterTest, InitialHealthStatus) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter(config);

    auto status = adapter.get_health_status();
    EXPECT_FALSE(status.healthy);
    EXPECT_FALSE(status.connected);
}

// =============================================================================
// Adapter Factory Tests
// =============================================================================

class AdapterFactoryTest : public ::testing::Test {
protected:
    emr_adapter_config create_valid_config(emr_vendor vendor) {
        emr_adapter_config config;
        config.vendor = vendor;
        config.base_url = "https://emr.example.com/fhir";
        config.auth_type = "oauth2";
        config.client_id = "test_client";
        config.token_url = "https://emr.example.com/oauth/token";
        return config;
    }
};

TEST_F(AdapterFactoryTest, CreateGenericAdapter) {
    auto config = create_valid_config(emr_vendor::generic_fhir);

    auto result = create_emr_adapter(config);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)->vendor(), emr_vendor::generic_fhir);
    EXPECT_EQ((*result)->vendor_name(), "Generic FHIR R4");
}

TEST_F(AdapterFactoryTest, CreateEpicAdapter) {
    auto config = create_valid_config(emr_vendor::epic);

    auto result = create_emr_adapter(config);

    // Currently falls back to generic
    ASSERT_TRUE(result.has_value());
    // Epic adapter will be implemented in Phase 5.2+
}

TEST_F(AdapterFactoryTest, CreateCernerAdapter) {
    auto config = create_valid_config(emr_vendor::cerner);

    auto result = create_emr_adapter(config);

    // Currently falls back to generic
    ASSERT_TRUE(result.has_value());
    // Cerner adapter will be implemented in Phase 5.2+
}

TEST_F(AdapterFactoryTest, CreateMeditechAdapter) {
    auto config = create_valid_config(emr_vendor::meditech);

    auto result = create_emr_adapter(config);

    // Not yet supported
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), adapter_error::not_supported);
}

TEST_F(AdapterFactoryTest, CreateAllscriptsAdapter) {
    auto config = create_valid_config(emr_vendor::allscripts);

    auto result = create_emr_adapter(config);

    // Not yet supported
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), adapter_error::not_supported);
}

TEST_F(AdapterFactoryTest, CreateUnknownVendorAdapter) {
    auto config = create_valid_config(emr_vendor::unknown);

    auto result = create_emr_adapter(config);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), adapter_error::invalid_vendor);
}

TEST_F(AdapterFactoryTest, InvalidConfiguration) {
    emr_adapter_config config;
    // Empty base_url makes it invalid
    config.vendor = emr_vendor::generic_fhir;

    auto result = create_emr_adapter(config);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), adapter_error::invalid_configuration);
}

TEST_F(AdapterFactoryTest, CreateWithVendorAndUrl) {
    auto result = create_emr_adapter(emr_vendor::generic_fhir,
                                     "https://emr.example.com/fhir");

    // This will fail because minimal config doesn't have auth
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), adapter_error::invalid_configuration);
}

// =============================================================================
// Adapter Interface Compliance Tests
// =============================================================================

class AdapterInterfaceTest : public ::testing::Test {
protected:
    std::unique_ptr<emr_adapter> create_adapter() {
        emr_adapter_config config;
        config.vendor = emr_vendor::generic_fhir;
        config.base_url = "https://emr.example.com/fhir";
        config.auth_type = "oauth2";
        config.client_id = "test_client";
        config.token_url = "https://emr.example.com/oauth/token";

        auto result = create_emr_adapter(config);
        if (result) {
            return std::move(*result);
        }
        return nullptr;
    }
};

TEST_F(AdapterInterfaceTest, VirtualMethodsCalled) {
    auto adapter = create_adapter();
    ASSERT_NE(adapter, nullptr);

    // These methods should work via interface
    EXPECT_EQ(adapter->vendor(), emr_vendor::generic_fhir);
    EXPECT_FALSE(adapter->vendor_name().empty());
    EXPECT_FALSE(adapter->version().empty());
    EXPECT_FALSE(adapter->is_initialized());
}

TEST_F(AdapterInterfaceTest, StatisticsViaInterface) {
    auto adapter = create_adapter();
    ASSERT_NE(adapter, nullptr);

    auto stats = adapter->get_statistics();
    EXPECT_EQ(stats.total_requests, 0);

    adapter->reset_statistics();
    stats = adapter->get_statistics();
    EXPECT_EQ(stats.total_requests, 0);
}

TEST_F(AdapterInterfaceTest, HealthStatusViaInterface) {
    auto adapter = create_adapter();
    ASSERT_NE(adapter, nullptr);

    auto status = adapter->get_health_status();
    EXPECT_FALSE(status.healthy);
}

// =============================================================================
// Configuration Update Tests
// =============================================================================

class AdapterConfigUpdateTest : public ::testing::Test {
protected:
    emr_adapter_config create_valid_config() {
        emr_adapter_config config;
        config.vendor = emr_vendor::generic_fhir;
        config.base_url = "https://emr.example.com/fhir";
        config.auth_type = "oauth2";
        config.client_id = "test_client";
        config.token_url = "https://emr.example.com/oauth/token";
        return config;
    }
};

TEST_F(AdapterConfigUpdateTest, SetConfigInvalidFails) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter(config);

    emr_adapter_config invalid_config;
    // Empty base_url
    auto result = adapter.set_config(invalid_config);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), adapter_error::invalid_configuration);
}

TEST_F(AdapterConfigUpdateTest, SetConfigValid) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter(config);

    emr_adapter_config new_config = config;
    new_config.base_url = "https://new-emr.example.com/fhir";

    auto result = adapter.set_config(new_config);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(adapter.config().base_url, "https://new-emr.example.com/fhir");
}

// =============================================================================
// Move Semantics Tests
// =============================================================================

class AdapterMoveSemanticsTest : public ::testing::Test {
protected:
    emr_adapter_config create_valid_config() {
        emr_adapter_config config;
        config.vendor = emr_vendor::generic_fhir;
        config.base_url = "https://emr.example.com/fhir";
        config.auth_type = "oauth2";
        config.client_id = "test_client";
        config.token_url = "https://emr.example.com/oauth/token";
        return config;
    }
};

TEST_F(AdapterMoveSemanticsTest, MoveConstructor) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter1(config);

    generic_fhir_adapter adapter2(std::move(adapter1));

    EXPECT_EQ(adapter2.vendor(), emr_vendor::generic_fhir);
    EXPECT_EQ(adapter2.config().base_url, "https://emr.example.com/fhir");
}

TEST_F(AdapterMoveSemanticsTest, MoveAssignment) {
    auto config = create_valid_config();
    generic_fhir_adapter adapter1(config);

    emr_adapter_config config2 = config;
    config2.base_url = "https://other.example.com/fhir";
    generic_fhir_adapter adapter2(config2);

    adapter2 = std::move(adapter1);

    EXPECT_EQ(adapter2.config().base_url, "https://emr.example.com/fhir");
}

// =============================================================================
// Vendor-Specific Configuration Tests
// =============================================================================

class VendorConfigTest : public ::testing::Test {};

TEST_F(VendorConfigTest, EpicNonProductionFlag) {
    emr_adapter_config config;
    config.vendor = emr_vendor::epic;
    config.base_url = "https://epic.example.com/fhir";
    config.auth_type = "oauth2";
    config.client_id = "epic_client";
    config.token_url = "https://epic.example.com/oauth/token";
    config.epic_non_production = true;

    EXPECT_TRUE(config.is_valid());
    EXPECT_TRUE(config.epic_non_production);
}

TEST_F(VendorConfigTest, CernerTenantId) {
    emr_adapter_config config;
    config.vendor = emr_vendor::cerner;
    config.base_url = "https://cerner.example.com/fhir";
    config.auth_type = "oauth2";
    config.client_id = "cerner_client";
    config.token_url = "https://cerner.example.com/oauth/token";
    config.cerner_tenant_id = "tenant-12345";

    EXPECT_TRUE(config.is_valid());
    EXPECT_TRUE(config.cerner_tenant_id.has_value());
    EXPECT_EQ(config.cerner_tenant_id.value(), "tenant-12345");
}

TEST_F(VendorConfigTest, MrnSystemConfiguration) {
    emr_adapter_config config;
    config.vendor = emr_vendor::generic_fhir;
    config.base_url = "https://emr.example.com/fhir";
    config.auth_type = "basic";
    config.username = "admin";
    config.mrn_system = "http://hospital.org/mrn";
    config.organization_id = "org-12345";

    EXPECT_TRUE(config.is_valid());
    EXPECT_EQ(config.mrn_system.value(), "http://hospital.org/mrn");
    EXPECT_EQ(config.organization_id.value(), "org-12345");
}

TEST_F(VendorConfigTest, ScopesConfiguration) {
    emr_adapter_config config;
    config.vendor = emr_vendor::generic_fhir;
    config.base_url = "https://emr.example.com/fhir";
    config.auth_type = "oauth2";
    config.client_id = "client";
    config.token_url = "https://emr.example.com/oauth/token";
    config.scopes = {"patient/*.read", "user/*.read", "launch"};

    EXPECT_TRUE(config.is_valid());
    EXPECT_EQ(config.scopes.size(), 3);
    EXPECT_EQ(config.scopes[0], "patient/*.read");
}

TEST_F(VendorConfigTest, RetryPolicyConfiguration) {
    emr_adapter_config config;
    config.vendor = emr_vendor::generic_fhir;
    config.base_url = "https://emr.example.com/fhir";
    config.auth_type = "basic";
    config.username = "admin";
    config.retry.max_retries = 5;
    config.retry.initial_backoff = 500ms;
    config.retry.backoff_multiplier = 2.0;
    config.retry.max_backoff = 30000ms;

    EXPECT_TRUE(config.is_valid());
    EXPECT_EQ(config.retry.max_retries, 5);
    EXPECT_EQ(config.retry.initial_backoff, 500ms);
}
