/**
 * @file fhir_client_test.cpp
 * @brief Unit tests for FHIR R4 HTTP client
 *
 * Tests the FHIR client functionality including:
 *   - Search parameter building
 *   - Bundle parsing and serialization
 *   - Client operations (read, search, create, update, delete)
 *   - Error handling and retry logic
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 */

#include <gtest/gtest.h>

#include "pacs/bridge/emr/emr_types.h"
#include "pacs/bridge/emr/fhir_bundle.h"
#include "pacs/bridge/emr/fhir_client.h"
#include "pacs/bridge/emr/http_client_adapter.h"
#include "pacs/bridge/emr/search_params.h"

#include <chrono>
#include <string>

using namespace pacs::bridge::emr;
using namespace std::chrono_literals;

// =============================================================================
// EMR Types Tests
// =============================================================================

class EmrTypesTest : public ::testing::Test {};

TEST_F(EmrTypesTest, ErrorCodeValues) {
    EXPECT_EQ(to_error_code(emr_error::connection_failed), -1000);
    EXPECT_EQ(to_error_code(emr_error::timeout), -1001);
    EXPECT_EQ(to_error_code(emr_error::invalid_response), -1002);
    EXPECT_EQ(to_error_code(emr_error::resource_not_found), -1003);
    EXPECT_EQ(to_error_code(emr_error::unauthorized), -1004);
}

TEST_F(EmrTypesTest, ErrorToString) {
    EXPECT_STREQ(to_string(emr_error::connection_failed),
                 "Connection to EMR server failed");
    EXPECT_STREQ(to_string(emr_error::timeout), "Request timed out");
    EXPECT_STREQ(to_string(emr_error::resource_not_found), "Resource not found");
}

TEST_F(EmrTypesTest, HttpStatusClassification) {
    EXPECT_TRUE(is_success(http_status::ok));
    EXPECT_TRUE(is_success(http_status::created));
    EXPECT_TRUE(is_success(http_status::no_content));

    EXPECT_TRUE(is_client_error(http_status::bad_request));
    EXPECT_TRUE(is_client_error(http_status::not_found));
    EXPECT_TRUE(is_client_error(http_status::unauthorized));

    EXPECT_TRUE(is_server_error(http_status::internal_server_error));
    EXPECT_TRUE(is_server_error(http_status::service_unavailable));
}

TEST_F(EmrTypesTest, StatusToError) {
    EXPECT_EQ(status_to_error(http_status::not_found),
              emr_error::resource_not_found);
    EXPECT_EQ(status_to_error(http_status::unauthorized),
              emr_error::unauthorized);
    EXPECT_EQ(status_to_error(http_status::too_many_requests),
              emr_error::rate_limited);
    EXPECT_EQ(status_to_error(http_status::internal_server_error),
              emr_error::server_error);
}

TEST_F(EmrTypesTest, ContentTypeMime) {
    EXPECT_EQ(to_mime_type(fhir_content_type::json), "application/fhir+json");
    EXPECT_EQ(to_mime_type(fhir_content_type::xml), "application/fhir+xml");
}

TEST_F(EmrTypesTest, ResourceTypeToString) {
    EXPECT_EQ(to_string(fhir_resource_type::patient), "Patient");
    EXPECT_EQ(to_string(fhir_resource_type::service_request), "ServiceRequest");
    EXPECT_EQ(to_string(fhir_resource_type::imaging_study), "ImagingStudy");
    EXPECT_EQ(to_string(fhir_resource_type::diagnostic_report),
              "DiagnosticReport");
}

TEST_F(EmrTypesTest, ParseResourceType) {
    EXPECT_EQ(parse_resource_type("Patient"), fhir_resource_type::patient);
    EXPECT_EQ(parse_resource_type("ServiceRequest"),
              fhir_resource_type::service_request);
    EXPECT_EQ(parse_resource_type("ImagingStudy"),
              fhir_resource_type::imaging_study);
    EXPECT_FALSE(parse_resource_type("InvalidType").has_value());
}

TEST_F(EmrTypesTest, HttpMethodToString) {
    EXPECT_EQ(to_string(http_method::get), "GET");
    EXPECT_EQ(to_string(http_method::post), "POST");
    EXPECT_EQ(to_string(http_method::put), "PUT");
    EXPECT_EQ(to_string(http_method::delete_method), "DELETE");
}

TEST_F(EmrTypesTest, RetryPolicyBackoff) {
    retry_policy policy;
    policy.max_retries = 3;
    policy.initial_backoff = 1000ms;
    policy.backoff_multiplier = 2.0;
    policy.max_backoff = 10000ms;

    EXPECT_EQ(policy.backoff_for(0), 1000ms);
    EXPECT_EQ(policy.backoff_for(1), 2000ms);
    EXPECT_EQ(policy.backoff_for(2), 4000ms);
    // Should cap at max_backoff
    EXPECT_EQ(policy.backoff_for(10), 10000ms);
}

TEST_F(EmrTypesTest, ClientConfigValidation) {
    fhir_client_config config;
    EXPECT_FALSE(config.is_valid());  // base_url is empty

    config.base_url = "https://emr.example.com/fhir";
    EXPECT_TRUE(config.is_valid());

    config.timeout = 0s;
    EXPECT_FALSE(config.is_valid());

    config.timeout = 30s;
    config.max_connections = 0;
    EXPECT_FALSE(config.is_valid());
}

TEST_F(EmrTypesTest, ClientConfigUrlFor) {
    fhir_client_config config;
    config.base_url = "https://emr.example.com/fhir";

    EXPECT_EQ(config.url_for("/Patient/123"),
              "https://emr.example.com/fhir/Patient/123");

    config.base_url = "https://emr.example.com/fhir/";
    EXPECT_EQ(config.url_for("/Patient/123"),
              "https://emr.example.com/fhir/Patient/123");
}

TEST_F(EmrTypesTest, HttpResponseGetHeader) {
    http_response response;
    response.headers.emplace_back("Content-Type", "application/fhir+json");
    response.headers.emplace_back("ETag", "W/\"1\"");
    response.headers.emplace_back("Location", "Patient/123/_history/1");

    EXPECT_TRUE(response.get_header("Content-Type").has_value());
    EXPECT_EQ(*response.get_header("content-type"), "application/fhir+json");

    EXPECT_TRUE(response.etag().has_value());
    EXPECT_EQ(*response.etag(), "W/\"1\"");

    EXPECT_TRUE(response.location().has_value());
    EXPECT_EQ(*response.location(), "Patient/123/_history/1");

    EXPECT_FALSE(response.get_header("X-Unknown").has_value());
}

// =============================================================================
// Search Params Tests
// =============================================================================

class SearchParamsTest : public ::testing::Test {};

TEST_F(SearchParamsTest, EmptyParams) {
    search_params params;
    EXPECT_TRUE(params.empty());
    EXPECT_EQ(params.size(), 0);
    EXPECT_EQ(params.to_query_string(), "");
}

TEST_F(SearchParamsTest, SimpleParams) {
    search_params params;
    params.add("name", "Smith").add("birthdate", "1990-01-01");

    EXPECT_FALSE(params.empty());
    EXPECT_EQ(params.size(), 2);

    auto query = params.to_query_string();
    EXPECT_NE(query.find("name=Smith"), std::string::npos);
    EXPECT_NE(query.find("birthdate=1990-01-01"), std::string::npos);
}

TEST_F(SearchParamsTest, ModifierParams) {
    search_params params;
    params.add_with_modifier("name", "exact", "John");

    auto query = params.to_query_string();
    EXPECT_NE(query.find("name%3Aexact=John"), std::string::npos);
}

TEST_F(SearchParamsTest, DatePrefixParams) {
    search_params params;
    params.add_date("birthdate", search_prefix::lt, "2000-01-01")
        .add_date("birthdate", search_prefix::ge, "1990-01-01");

    auto query = params.to_query_string();
    EXPECT_NE(query.find("lt2000-01-01"), std::string::npos);
    EXPECT_NE(query.find("ge1990-01-01"), std::string::npos);
}

TEST_F(SearchParamsTest, TokenParams) {
    search_params params;
    params.add_token("identifier", "http://hospital.org/mrn", "123456");

    auto query = params.to_query_string();
    // URL encoded pipe character
    EXPECT_NE(query.find("%7C123456"), std::string::npos);
}

TEST_F(SearchParamsTest, ReferenceParams) {
    search_params params;
    params.add_reference("patient", "Patient", "123");

    auto query = params.to_query_string();
    EXPECT_NE(query.find("patient=Patient%2F123"), std::string::npos);
}

TEST_F(SearchParamsTest, CommonParams) {
    search_params params;
    params.id("123").count(20).offset(40).sort("birthdate", true);

    auto query = params.to_query_string();
    EXPECT_NE(query.find("_id=123"), std::string::npos);
    EXPECT_NE(query.find("_count=20"), std::string::npos);
    EXPECT_NE(query.find("_offset=40"), std::string::npos);
    EXPECT_NE(query.find("_sort=-birthdate"), std::string::npos);
}

TEST_F(SearchParamsTest, PatientSearchBuilder) {
    auto params = search_params::for_patient()
                      .identifier("http://hospital.org/mrn", "123456")
                      .name("Smith")
                      .birthdate_before("2000-01-01")
                      .active(true)
                      .count(10);

    EXPECT_EQ(params.size(), 5);
    auto query = params.to_query_string();
    EXPECT_NE(query.find("active=true"), std::string::npos);
}

TEST_F(SearchParamsTest, ServiceRequestSearchBuilder) {
    auto params = search_params::for_service_request()
                      .patient("patient123")
                      .status("active")
                      .category("http://snomed.info/sct", "363679005");

    EXPECT_EQ(params.size(), 3);
}

TEST_F(SearchParamsTest, ImagingStudySearchBuilder) {
    auto params = search_params::for_imaging_study()
                      .patient("patient123")
                      .study_uid("1.2.3.4.5")
                      .modality("CT")
                      .started("2024-01-01");

    EXPECT_EQ(params.size(), 4);
}

TEST_F(SearchParamsTest, IncludeRevInclude) {
    search_params params;
    params.include("Patient", "organization")
        .rev_include("Observation", "subject");

    auto query = params.to_query_string();
    EXPECT_NE(query.find("_include=Patient%3Aorganization"), std::string::npos);
    EXPECT_NE(query.find("_revinclude=Observation%3Asubject"),
              std::string::npos);
}

TEST_F(SearchParamsTest, UrlEncoding) {
    search_params params;
    params.add("name", "John Doe & Jane");

    auto query = params.to_query_string();
    // Space should be encoded as +
    EXPECT_NE(query.find("John+Doe"), std::string::npos);
    // & should be encoded
    EXPECT_NE(query.find("%26"), std::string::npos);
}

// =============================================================================
// FHIR Bundle Tests
// =============================================================================

class FhirBundleTest : public ::testing::Test {};

TEST_F(FhirBundleTest, BundleTypeToString) {
    EXPECT_EQ(to_string(bundle_type::searchset), "searchset");
    EXPECT_EQ(to_string(bundle_type::transaction), "transaction");
    EXPECT_EQ(to_string(bundle_type::batch), "batch");
}

TEST_F(FhirBundleTest, ParseBundleType) {
    EXPECT_EQ(parse_bundle_type("searchset"), bundle_type::searchset);
    EXPECT_EQ(parse_bundle_type("transaction"), bundle_type::transaction);
    EXPECT_EQ(parse_bundle_type("batch"), bundle_type::batch);
    EXPECT_FALSE(parse_bundle_type("invalid").has_value());
}

TEST_F(FhirBundleTest, LinkRelationToString) {
    EXPECT_EQ(to_string(link_relation::self), "self");
    EXPECT_EQ(to_string(link_relation::next), "next");
    EXPECT_EQ(to_string(link_relation::previous), "previous");
}

TEST_F(FhirBundleTest, ParseLinkRelation) {
    EXPECT_EQ(parse_link_relation("self"), link_relation::self);
    EXPECT_EQ(parse_link_relation("next"), link_relation::next);
    EXPECT_EQ(parse_link_relation("previous"), link_relation::previous);
    EXPECT_EQ(parse_link_relation("prev"), link_relation::previous);
    EXPECT_FALSE(parse_link_relation("invalid").has_value());
}

TEST_F(FhirBundleTest, ParseSearchBundle) {
    std::string json = R"({
        "resourceType": "Bundle",
        "id": "test-bundle",
        "type": "searchset",
        "total": 2,
        "link": [
            {"relation": "self", "url": "http://example.com/Patient?name=Smith"},
            {"relation": "next", "url": "http://example.com/Patient?name=Smith&_offset=20"}
        ],
        "entry": [
            {
                "fullUrl": "http://example.com/Patient/1",
                "resource": {"resourceType": "Patient", "id": "1"},
                "search": {"mode": "match"}
            },
            {
                "fullUrl": "http://example.com/Patient/2",
                "resource": {"resourceType": "Patient", "id": "2"},
                "search": {"mode": "match"}
            }
        ]
    })";

    auto bundle = fhir_bundle::parse(json);
    ASSERT_TRUE(bundle.has_value());

    EXPECT_EQ(bundle->id, "test-bundle");
    EXPECT_EQ(bundle->type, bundle_type::searchset);
    EXPECT_EQ(bundle->total, 2);

    EXPECT_EQ(bundle->links.size(), 2);
    EXPECT_TRUE(bundle->has_next());
    EXPECT_FALSE(bundle->has_previous());

    EXPECT_EQ(bundle->entries.size(), 2);
    EXPECT_EQ(bundle->entries[0].resource_type, "Patient");
    EXPECT_EQ(bundle->entries[0].resource_id, "1");
}

TEST_F(FhirBundleTest, ParseInvalidBundle) {
    // Not a Bundle
    auto result1 = fhir_bundle::parse(R"({"resourceType": "Patient"})");
    EXPECT_FALSE(result1.has_value());

    // Invalid JSON
    auto result2 = fhir_bundle::parse("invalid json");
    EXPECT_FALSE(result2.has_value());
}

TEST_F(FhirBundleTest, BundleGetLink) {
    fhir_bundle bundle;
    bundle.links.push_back({link_relation::self, "http://example.com/self"});
    bundle.links.push_back({link_relation::next, "http://example.com/next"});

    EXPECT_TRUE(bundle.get_link(link_relation::self).has_value());
    EXPECT_EQ(*bundle.get_link(link_relation::self), "http://example.com/self");

    EXPECT_TRUE(bundle.next_url().has_value());
    EXPECT_FALSE(bundle.previous_url().has_value());
}

TEST_F(FhirBundleTest, BundleBuilder) {
    bundle_builder builder(bundle_type::transaction);

    std::string patient_json = R"({"resourceType":"Patient","name":[{"family":"Smith"}]})";

    builder.add_create("Patient", patient_json)
        .add_update("Patient/123", patient_json)
        .add_delete("Patient/456")
        .add_read("Patient/789");

    EXPECT_EQ(builder.size(), 4);
    EXPECT_FALSE(builder.empty());

    auto bundle = builder.build();
    EXPECT_EQ(bundle.type, bundle_type::transaction);
    EXPECT_EQ(bundle.entries.size(), 4);

    // Check request methods
    EXPECT_EQ(bundle.entries[0].request->method, http_method::post);
    EXPECT_EQ(bundle.entries[1].request->method, http_method::put);
    EXPECT_EQ(bundle.entries[2].request->method, http_method::delete_method);
    EXPECT_EQ(bundle.entries[3].request->method, http_method::get);
}

TEST_F(FhirBundleTest, BundleToJson) {
    fhir_bundle bundle;
    bundle.id = "test";
    bundle.type = bundle_type::transaction;

    bundle_entry entry;
    entry.full_url = "urn:uuid:12345";
    entry.resource = R"({"resourceType":"Patient","id":"123"})";
    entry.resource_type = "Patient";
    entry_request req;
    req.method = http_method::post;
    req.url = "Patient";
    entry.request = req;
    bundle.entries.push_back(entry);

    auto json = bundle.to_json();
    EXPECT_NE(json.find("\"resourceType\":\"Bundle\""), std::string::npos);
    EXPECT_NE(json.find("\"type\":\"transaction\""), std::string::npos);
    EXPECT_NE(json.find("\"entry\""), std::string::npos);
}

// =============================================================================
// HTTP Client Adapter Tests
// =============================================================================

class HttpClientAdapterTest : public ::testing::Test {};

TEST_F(HttpClientAdapterTest, CallbackHttpClient) {
    auto callback = [](const http_request& req)
        -> Result<http_response> {
        http_response response;
        response.status = http_status::ok;
        response.body = R"({"resourceType":"Patient","id":"123"})";
        response.headers.emplace_back("Content-Type", "application/fhir+json");
        return kcenon::common::ok(response);
    };

    callback_http_client client(callback);

    http_request request;
    request.method = http_method::get;
    request.url = "http://example.com/Patient/123";

    auto result = client.execute(request);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().status, http_status::ok);
    EXPECT_FALSE(result.value().body.empty());
}

TEST_F(HttpClientAdapterTest, CallbackHttpClientError) {
    auto callback = [](const http_request&)
        -> Result<http_response> {
        return Result<http_response>(kcenon::common::error_info{
            to_error_code(emr_error::timeout), to_string(emr_error::timeout), "emr"});
    };

    callback_http_client client(callback);

    http_request request;
    request.method = http_method::get;
    request.url = "http://example.com/Patient/123";

    auto result = client.execute(request);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, to_error_code(emr_error::timeout));
}

TEST_F(HttpClientAdapterTest, ConvenienceMethods) {
    auto callback = [](const http_request& req)
        -> Result<http_response> {
        http_response response;
        response.status = http_status::ok;

        // Echo back method
        std::string method_str(to_string(req.method));
        response.body = "{\"method\":\"" + method_str + "\"}";
        return kcenon::common::ok(response);
    };

    callback_http_client client(callback);

    // Test GET
    auto get_result = client.get("http://example.com/test");
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_NE(get_result.value().body.find("GET"), std::string::npos);

    // Test POST
    auto post_result =
        client.post("http://example.com/test", "{}", "application/fhir+json");
    ASSERT_TRUE(post_result.is_ok());
    EXPECT_NE(post_result.value().body.find("POST"), std::string::npos);

    // Test DELETE
    auto del_result = client.del("http://example.com/test");
    ASSERT_TRUE(del_result.is_ok());
    EXPECT_NE(del_result.value().body.find("DELETE"), std::string::npos);
}

TEST_F(HttpClientAdapterTest, CreateHttpClientWithCallback) {
    auto callback = [](const http_request&)
        -> Result<http_response> {
        http_response response;
        response.status = http_status::ok;
        return kcenon::common::ok(response);
    };

    auto client = create_http_client(callback);
    EXPECT_NE(client, nullptr);

    http_request request;
    auto result = client->execute(request);
    EXPECT_TRUE(result.is_ok());
}

// =============================================================================
// FHIR Client Tests
// =============================================================================

class FhirClientTest : public ::testing::Test {
protected:
    std::unique_ptr<fhir_client> create_mock_client(
        callback_http_client::execute_callback callback) {
        fhir_client_config config;
        config.base_url = "https://emr.example.com/fhir";
        config.timeout = 30s;

        auto http_client = create_http_client(callback);
        return std::make_unique<fhir_client>(config, std::move(http_client));
    }
};

TEST_F(FhirClientTest, ReadResource) {
    auto callback = [](const http_request& req)
        -> Result<http_response> {
        // Verify request
        EXPECT_EQ(req.method, http_method::get);
        EXPECT_NE(req.url.find("Patient/123"), std::string::npos);

        http_response response;
        response.status = http_status::ok;
        response.body = R"({
            "resourceType": "Patient",
            "id": "123",
            "name": [{"family": "Smith", "given": ["John"]}]
        })";
        response.headers.emplace_back("ETag", "W/\"1\"");
        return kcenon::common::ok(response);
    };

    auto client = create_mock_client(callback);
    auto result = client->read("Patient", "123");

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().status, http_status::ok);
    EXPECT_EQ(result.value().value.resource_type, "Patient");
    EXPECT_EQ(result.value().value.id, "123");
    EXPECT_TRUE(result.value().etag.has_value());
    EXPECT_EQ(*result.value().etag, "W/\"1\"");
}

TEST_F(FhirClientTest, ReadResourceNotFound) {
    auto callback = [](const http_request&)
        -> Result<http_response> {
        http_response response;
        response.status = http_status::not_found;
        response.body = R"({"resourceType":"OperationOutcome"})";
        return kcenon::common::ok(response);
    };

    auto client = create_mock_client(callback);
    auto result = client->read("Patient", "999");

    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, to_error_code(emr_error::resource_not_found));
}

TEST_F(FhirClientTest, SearchResources) {
    auto callback = [](const http_request& req)
        -> Result<http_response> {
        // Verify search parameters
        EXPECT_NE(req.url.find("name=Smith"), std::string::npos);

        http_response response;
        response.status = http_status::ok;
        response.body = R"({
            "resourceType": "Bundle",
            "type": "searchset",
            "total": 1,
            "entry": [{
                "resource": {"resourceType": "Patient", "id": "123"}
            }]
        })";
        return kcenon::common::ok(response);
    };

    auto client = create_mock_client(callback);
    auto params = search_params::for_patient().name("Smith");
    auto result = client->search("Patient", params);

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().value.type, bundle_type::searchset);
    EXPECT_EQ(result.value().value.total, 1);
    EXPECT_EQ(result.value().value.entries.size(), 1);
}

TEST_F(FhirClientTest, CreateResource) {
    auto callback = [](const http_request& req)
        -> Result<http_response> {
        EXPECT_EQ(req.method, http_method::post);
        EXPECT_FALSE(req.body.empty());

        http_response response;
        response.status = http_status::created;
        response.body = R"({"resourceType":"Patient","id":"new-123"})";
        response.headers.emplace_back("Location", "Patient/new-123/_history/1");
        return kcenon::common::ok(response);
    };

    auto client = create_mock_client(callback);
    std::string patient_json = R"({"resourceType":"Patient"})";
    auto result = client->create("Patient", patient_json);

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().status, http_status::created);
    EXPECT_TRUE(result.value().location.has_value());
}

TEST_F(FhirClientTest, UpdateResource) {
    auto callback = [](const http_request& req)
        -> Result<http_response> {
        EXPECT_EQ(req.method, http_method::put);
        EXPECT_NE(req.url.find("Patient/123"), std::string::npos);

        http_response response;
        response.status = http_status::ok;
        response.body = R"({"resourceType":"Patient","id":"123"})";
        response.headers.emplace_back("ETag", "W/\"2\"");
        return kcenon::common::ok(response);
    };

    auto client = create_mock_client(callback);
    auto result = client->update("Patient", "123", R"({"resourceType":"Patient","id":"123"})");

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(*result.value().etag, "W/\"2\"");
}

TEST_F(FhirClientTest, DeleteResource) {
    auto callback = [](const http_request& req)
        -> Result<http_response> {
        EXPECT_EQ(req.method, http_method::delete_method);

        http_response response;
        response.status = http_status::no_content;
        return kcenon::common::ok(response);
    };

    auto client = create_mock_client(callback);
    auto result = client->remove("Patient", "123");

    EXPECT_TRUE(result.is_ok());
}

TEST_F(FhirClientTest, Capabilities) {
    auto callback = [](const http_request& req)
        -> Result<http_response> {
        EXPECT_NE(req.url.find("metadata"), std::string::npos);

        http_response response;
        response.status = http_status::ok;
        response.body = R"({
            "resourceType": "CapabilityStatement",
            "rest": [{"resource": [{"type": "Patient"}]}]
        })";
        return kcenon::common::ok(response);
    };

    auto client = create_mock_client(callback);
    auto result = client->capabilities();

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().value.resource_type, "CapabilityStatement");
}

TEST_F(FhirClientTest, SupportsResource) {
    auto callback = [](const http_request&)
        -> Result<http_response> {
        http_response response;
        response.status = http_status::ok;
        // Need actual "type":"Patient" pattern for the simple substring search
        response.body = R"({
            "resourceType": "CapabilityStatement",
            "rest": [{"resource": [{"type":"Patient"}, {"type":"Observation"}]}]
        })";
        return kcenon::common::ok(response);
    };

    auto client = create_mock_client(callback);

    auto patient_result = client->supports_resource("Patient");
    ASSERT_TRUE(patient_result.is_ok());
    EXPECT_TRUE(patient_result.value());

    auto unknown_result = client->supports_resource("UnknownResource");
    ASSERT_TRUE(unknown_result.is_ok());
    EXPECT_FALSE(unknown_result.value());
}

TEST_F(FhirClientTest, NextPage) {
    int call_count = 0;
    auto callback = [&call_count](const http_request& req)
        -> Result<http_response> {
        call_count++;

        http_response response;
        response.status = http_status::ok;

        if (call_count == 1) {
            // First page with next link
            response.body = R"({
                "resourceType": "Bundle",
                "type": "searchset",
                "link": [{"relation": "next", "url": "http://example.com/page2"}],
                "entry": [{"resource": {"resourceType": "Patient", "id": "1"}}]
            })";
        } else {
            // Second page without next link
            response.body = R"({
                "resourceType": "Bundle",
                "type": "searchset",
                "entry": [{"resource": {"resourceType": "Patient", "id": "2"}}]
            })";
        }
        return kcenon::common::ok(response);
    };

    auto client = create_mock_client(callback);
    auto result = client->search("Patient", search_params{});

    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().value.has_next());

    auto next_result = client->next_page(result.value().value);
    ASSERT_TRUE(next_result.is_ok());
    EXPECT_FALSE(next_result.value().value.has_next());

    EXPECT_EQ(call_count, 2);
}

TEST_F(FhirClientTest, Statistics) {
    auto callback = [](const http_request&)
        -> Result<http_response> {
        http_response response;
        response.status = http_status::ok;
        response.body = R"({"resourceType":"Patient","id":"1"})";
        return kcenon::common::ok(response);
    };

    auto client = create_mock_client(callback);

    // Reset and check initial state
    client->reset_statistics();
    auto stats = client->get_statistics();
    EXPECT_EQ(stats.total_requests, 0);

    // Make some requests
    (void)client->read("Patient", "1");
    (void)client->read("Patient", "2");

    stats = client->get_statistics();
    EXPECT_EQ(stats.total_requests, 2);
    EXPECT_EQ(stats.successful_requests, 2);
    EXPECT_EQ(stats.failed_requests, 0);
}

TEST_F(FhirClientTest, Configuration) {
    fhir_client_config config;
    config.base_url = "https://emr.example.com/fhir";
    config.timeout = 30s;

    auto http_client = create_http_client([](const http_request&)
                                              -> Result<http_response> {
        return Result<http_response>(kcenon::common::error_info{
            to_error_code(emr_error::not_supported), to_string(emr_error::not_supported), "emr"});
    });

    fhir_client client(config, std::move(http_client));

    EXPECT_EQ(client.base_url(), "https://emr.example.com/fhir");
    EXPECT_EQ(client.config().timeout, 30s);

    // Modify timeout
    client.set_timeout(60s);
    EXPECT_EQ(client.config().timeout, 60s);
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
