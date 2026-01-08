/**
 * @file crtp_handler_test.cpp
 * @brief Unit tests for CRTP-based HL7 handler infrastructure
 *
 * Tests for the CRTP pattern implementation including:
 * - HL7HandlerBase static dispatch
 * - HL7HandlerConcept validation
 * - HL7HandlerWrapper type erasure
 * - HL7HandlerRegistry functionality
 * - Performance validation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/202
 * @see https://github.com/kcenon/pacs_bridge/issues/262
 */

#include "pacs/bridge/cache/patient_cache.h"
#include "pacs/bridge/pacs_adapter/mwl_client.h"
#include "pacs/bridge/protocol/hl7/adt_handler.h"
#include "pacs/bridge/protocol/hl7/hl7_handler_base.h"
#include "pacs/bridge/protocol/hl7/hl7_handler_registry.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/orm_handler.h"
#include "pacs/bridge/protocol/hl7/siu_handler.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace pacs::bridge::hl7::test {

// =============================================================================
// Helper Functions
// =============================================================================

inline std::shared_ptr<pacs_adapter::mwl_client> create_test_mwl_client() {
    pacs_adapter::mwl_client_config config;
    config.pacs_host = "localhost";
    config.pacs_port = 11112;
    return std::make_shared<pacs_adapter::mwl_client>(config);
}

// =============================================================================
// Sample Messages
// =============================================================================

const std::string SAMPLE_ADT_A01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4|||AL|NE\r"
    "EVN|A01|20240115103000|||OPERATOR^JOHN\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||123 MAIN ST^^SPRINGFIELD^IL^62701||555-123-4567\r"
    "PV1|1|I|WARD^101^A^HOSPITAL||||SMITH^ROBERT^MD\r";

const std::string SAMPLE_ORM_O01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG002|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "ORC|NW|ORD001^HIS|ACC001^PACS||SC\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT\r";

const std::string SAMPLE_SIU_S12 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115120000||SIU^S12|MSG003|P|2.4|||AL|NE\r"
    "SCH|SCH001^HIS|REQ001|||||^^APT|15|MIN|^^^20240120090000|^^^20240120091500||||SMITH^ROBERT^MD|||||NW||SCHED001^SCHEDULER^JANE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "RGS|1||ROOM1^EXAM^RADIOLOGY\r"
    "AIS|1||71020^CHEST XRAY^CPT|20240120090000|15|MIN|15|MIN\r";

// =============================================================================
// CRTP Concept Tests
// =============================================================================

class CrtpConceptTest : public ::testing::Test {
protected:
    std::shared_ptr<cache::patient_cache> cache_;
    std::shared_ptr<pacs_adapter::mwl_client> mwl_client_;

    void SetUp() override {
        cache_ = std::make_shared<cache::patient_cache>();
        mwl_client_ = create_test_mwl_client();
    }
};

TEST_F(CrtpConceptTest, AdtHandlerSatisfiesConcept) {
    static_assert(HL7HandlerConcept<adt_handler>,
                  "adt_handler must satisfy HL7HandlerConcept");
}

TEST_F(CrtpConceptTest, OrmHandlerSatisfiesConcept) {
    static_assert(HL7HandlerConcept<orm_handler>,
                  "orm_handler must satisfy HL7HandlerConcept");
}

TEST_F(CrtpConceptTest, SiuHandlerSatisfiesConcept) {
    static_assert(HL7HandlerConcept<siu_handler>,
                  "siu_handler must satisfy HL7HandlerConcept");
}

TEST_F(CrtpConceptTest, HandlerHasTypeName) {
    EXPECT_EQ(adt_handler::type_name, "ADT");
    EXPECT_EQ(orm_handler::type_name, "ORM");
    EXPECT_EQ(siu_handler::type_name, "SIU");
}

TEST_F(CrtpConceptTest, HandlerTypeMethodMatchesTypeName) {
    adt_handler adt(cache_);
    orm_handler orm(mwl_client_);
    siu_handler siu(mwl_client_);

    EXPECT_EQ(adt.handler_type(), adt_handler::type_name);
    EXPECT_EQ(orm.handler_type(), orm_handler::type_name);
    EXPECT_EQ(siu.handler_type(), siu_handler::type_name);
}

// =============================================================================
// CRTP Static Dispatch Tests
// =============================================================================

class CrtpStaticDispatchTest : public ::testing::Test {
protected:
    std::shared_ptr<cache::patient_cache> cache_;

    void SetUp() override {
        cache_ = std::make_shared<cache::patient_cache>();
    }
};

TEST_F(CrtpStaticDispatchTest, CanHandleUsesStaticDispatch) {
    adt_handler handler(cache_);

    auto adt_msg = hl7_message::parse(SAMPLE_ADT_A01);
    ASSERT_TRUE(adt_msg.has_value());

    auto orm_msg = hl7_message::parse(SAMPLE_ORM_O01);
    ASSERT_TRUE(orm_msg.has_value());

    EXPECT_TRUE(handler.can_handle(*adt_msg));
    EXPECT_FALSE(handler.can_handle(*orm_msg));
}

TEST_F(CrtpStaticDispatchTest, MultipleHandlersCanHandleOwnMessages) {
    adt_handler adt(cache_);
    auto mwl_client = create_test_mwl_client();
    orm_handler orm(mwl_client);
    siu_handler siu(mwl_client);

    auto adt_msg = hl7_message::parse(SAMPLE_ADT_A01);
    auto orm_msg = hl7_message::parse(SAMPLE_ORM_O01);
    auto siu_msg = hl7_message::parse(SAMPLE_SIU_S12);

    ASSERT_TRUE(adt_msg.has_value());
    ASSERT_TRUE(orm_msg.has_value());
    ASSERT_TRUE(siu_msg.has_value());

    // ADT handler
    EXPECT_TRUE(adt.can_handle(*adt_msg));
    EXPECT_FALSE(adt.can_handle(*orm_msg));
    EXPECT_FALSE(adt.can_handle(*siu_msg));

    // ORM handler
    EXPECT_FALSE(orm.can_handle(*adt_msg));
    EXPECT_TRUE(orm.can_handle(*orm_msg));
    EXPECT_FALSE(orm.can_handle(*siu_msg));

    // SIU handler
    EXPECT_FALSE(siu.can_handle(*adt_msg));
    EXPECT_FALSE(siu.can_handle(*orm_msg));
    EXPECT_TRUE(siu.can_handle(*siu_msg));
}

// =============================================================================
// Type Erasure Wrapper Tests
// =============================================================================

class TypeErasureWrapperTest : public ::testing::Test {
protected:
    std::shared_ptr<cache::patient_cache> cache_;

    void SetUp() override {
        cache_ = std::make_shared<cache::patient_cache>();
    }
};

TEST_F(TypeErasureWrapperTest, WrapperCreation) {
    auto wrapper = make_handler_wrapper<adt_handler>(cache_);
    ASSERT_NE(wrapper, nullptr);
    EXPECT_EQ(wrapper->handler_type(), "ADT");
}

TEST_F(TypeErasureWrapperTest, WrapperCanHandle) {
    auto wrapper = make_handler_wrapper<adt_handler>(cache_);

    auto adt_msg = hl7_message::parse(SAMPLE_ADT_A01);
    ASSERT_TRUE(adt_msg.has_value());

    EXPECT_TRUE(wrapper->can_handle(*adt_msg));
}

TEST_F(TypeErasureWrapperTest, WrapperProcess) {
    auto wrapper = make_handler_wrapper<adt_handler>(cache_);

    auto adt_msg = hl7_message::parse(SAMPLE_ADT_A01);
    ASSERT_TRUE(adt_msg.has_value());

    auto result = wrapper->process(*adt_msg);
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().success);
    EXPECT_EQ(result.value().handler_type, "ADT");
}

TEST_F(TypeErasureWrapperTest, WrapperWithConfiguredHandler) {
    adt_handler_config config;
    config.allow_a08_create = true;
    config.detailed_ack = false;

    auto wrapper = std::make_unique<HL7HandlerWrapper<adt_handler>>(cache_, config);
    EXPECT_EQ(wrapper->handler_type(), "ADT");

    // Access underlying handler
    EXPECT_EQ(wrapper->handler().config().allow_a08_create, true);
    EXPECT_EQ(wrapper->handler().config().detailed_ack, false);
}

TEST_F(TypeErasureWrapperTest, MultipleWrapperTypes) {
    auto mwl_client = create_test_mwl_client();

    std::vector<std::unique_ptr<IHL7Handler>> handlers;
    handlers.push_back(make_handler_wrapper<adt_handler>(cache_));
    handlers.push_back(make_handler_wrapper<orm_handler>(mwl_client));
    handlers.push_back(make_handler_wrapper<siu_handler>(mwl_client));

    EXPECT_EQ(handlers[0]->handler_type(), "ADT");
    EXPECT_EQ(handlers[1]->handler_type(), "ORM");
    EXPECT_EQ(handlers[2]->handler_type(), "SIU");
}

// =============================================================================
// Handler Registry Tests
// =============================================================================

class RegistryTest : public ::testing::Test {
protected:
    std::shared_ptr<cache::patient_cache> cache_;
    std::shared_ptr<pacs_adapter::mwl_client> mwl_client_;
    hl7_handler_registry registry_;

    void SetUp() override {
        cache_ = std::make_shared<cache::patient_cache>();
        mwl_client_ = create_test_mwl_client();
    }
};

TEST_F(RegistryTest, RegisterHandler) {
    auto result = registry_.register_handler<adt_handler>(cache_);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(registry_.handler_count(), 1);
}

TEST_F(RegistryTest, RegisterMultipleHandlers) {
    EXPECT_TRUE(registry_.register_handler<adt_handler>(cache_).is_ok());
    EXPECT_TRUE(registry_.register_handler<orm_handler>(mwl_client_).is_ok());
    EXPECT_TRUE(registry_.register_handler<siu_handler>(mwl_client_).is_ok());

    EXPECT_EQ(registry_.handler_count(), 3);
}

TEST_F(RegistryTest, RegisterDuplicateHandlerFails) {
    EXPECT_TRUE(registry_.register_handler<adt_handler>(cache_).is_ok());

    auto result = registry_.register_handler<adt_handler>(cache_);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, to_error_code(registry_error::handler_exists));
}

TEST_F(RegistryTest, HasHandler) {
    registry_.register_handler<adt_handler>(cache_);

    EXPECT_TRUE(registry_.has_handler("ADT"));
    EXPECT_FALSE(registry_.has_handler("ORM"));
}

TEST_F(RegistryTest, RegisteredTypes) {
    registry_.register_handler<adt_handler>(cache_);
    registry_.register_handler<orm_handler>(mwl_client_);

    auto types = registry_.registered_types();
    EXPECT_EQ(types.size(), 2);
    EXPECT_TRUE(std::find(types.begin(), types.end(), "ADT") != types.end());
    EXPECT_TRUE(std::find(types.begin(), types.end(), "ORM") != types.end());
}

TEST_F(RegistryTest, UnregisterHandler) {
    registry_.register_handler<adt_handler>(cache_);
    EXPECT_TRUE(registry_.has_handler("ADT"));

    EXPECT_TRUE(registry_.unregister_handler("ADT"));
    EXPECT_FALSE(registry_.has_handler("ADT"));
    EXPECT_EQ(registry_.handler_count(), 0);
}

TEST_F(RegistryTest, UnregisterNonexistentHandler) {
    EXPECT_FALSE(registry_.unregister_handler("NONEXISTENT"));
}

TEST_F(RegistryTest, Clear) {
    registry_.register_handler<adt_handler>(cache_);
    registry_.register_handler<orm_handler>(mwl_client_);

    registry_.clear();
    EXPECT_EQ(registry_.handler_count(), 0);
}

TEST_F(RegistryTest, FindHandler) {
    registry_.register_handler<adt_handler>(cache_);
    registry_.register_handler<orm_handler>(mwl_client_);

    auto adt_msg = hl7_message::parse(SAMPLE_ADT_A01);
    auto orm_msg = hl7_message::parse(SAMPLE_ORM_O01);
    auto siu_msg = hl7_message::parse(SAMPLE_SIU_S12);

    ASSERT_TRUE(adt_msg.has_value());
    ASSERT_TRUE(orm_msg.has_value());
    ASSERT_TRUE(siu_msg.has_value());

    auto* adt_handler_ptr = registry_.find_handler(*adt_msg);
    auto* orm_handler_ptr = registry_.find_handler(*orm_msg);
    auto* siu_handler_ptr = registry_.find_handler(*siu_msg);

    EXPECT_NE(adt_handler_ptr, nullptr);
    EXPECT_NE(orm_handler_ptr, nullptr);
    EXPECT_EQ(siu_handler_ptr, nullptr);  // Not registered

    EXPECT_EQ(adt_handler_ptr->handler_type(), "ADT");
    EXPECT_EQ(orm_handler_ptr->handler_type(), "ORM");
}

TEST_F(RegistryTest, CanProcess) {
    registry_.register_handler<adt_handler>(cache_);

    auto adt_msg = hl7_message::parse(SAMPLE_ADT_A01);
    auto orm_msg = hl7_message::parse(SAMPLE_ORM_O01);

    ASSERT_TRUE(adt_msg.has_value());
    ASSERT_TRUE(orm_msg.has_value());

    EXPECT_TRUE(registry_.can_process(*adt_msg));
    EXPECT_FALSE(registry_.can_process(*orm_msg));
}

TEST_F(RegistryTest, Process) {
    registry_.register_handler<adt_handler>(cache_);

    auto adt_msg = hl7_message::parse(SAMPLE_ADT_A01);
    ASSERT_TRUE(adt_msg.has_value());

    auto result = registry_.process(*adt_msg);
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().success);
    EXPECT_EQ(result.value().handler_type, "ADT");
}

TEST_F(RegistryTest, ProcessNoHandler) {
    // Empty registry
    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    ASSERT_TRUE(msg.has_value());

    auto result = registry_.process(*msg);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, to_error_code(registry_error::no_handler));
}

TEST_F(RegistryTest, Statistics) {
    registry_.register_handler<adt_handler>(cache_);

    auto initial_stats = registry_.get_statistics();
    EXPECT_EQ(initial_stats.total_processed, 0);

    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    registry_.process(*msg);
    registry_.process(*msg);

    auto stats = registry_.get_statistics();
    EXPECT_EQ(stats.total_processed, 2);
    EXPECT_EQ(stats.success_count, 2);
}

TEST_F(RegistryTest, ResetStatistics) {
    registry_.register_handler<adt_handler>(cache_);

    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    registry_.process(*msg);

    registry_.reset_statistics();
    auto stats = registry_.get_statistics();
    EXPECT_EQ(stats.total_processed, 0);
}

// =============================================================================
// Handler Error Code Tests
// =============================================================================

class HandlerErrorCodeTest : public ::testing::Test {};

TEST_F(HandlerErrorCodeTest, HandlerErrorRange) {
    EXPECT_EQ(to_error_code(handler_error::unsupported_message_type), -880);
    EXPECT_EQ(to_error_code(handler_error::processing_failed), -881);
    EXPECT_EQ(to_error_code(handler_error::not_initialized), -882);
    EXPECT_EQ(to_error_code(handler_error::busy), -883);
    EXPECT_EQ(to_error_code(handler_error::invalid_state), -884);
}

TEST_F(HandlerErrorCodeTest, RegistryErrorRange) {
    EXPECT_EQ(to_error_code(registry_error::handler_exists), -890);
    EXPECT_EQ(to_error_code(registry_error::no_handler), -891);
    EXPECT_EQ(to_error_code(registry_error::registration_failed), -892);
    EXPECT_EQ(to_error_code(registry_error::ambiguous_handler), -893);
    EXPECT_EQ(to_error_code(registry_error::empty_registry), -894);
}

TEST_F(HandlerErrorCodeTest, HandlerErrorStrings) {
    EXPECT_NE(to_string(handler_error::unsupported_message_type), nullptr);
    EXPECT_NE(to_string(handler_error::processing_failed), nullptr);
    EXPECT_NE(to_string(handler_error::not_initialized), nullptr);
    EXPECT_NE(to_string(handler_error::busy), nullptr);
    EXPECT_NE(to_string(handler_error::invalid_state), nullptr);
}

TEST_F(HandlerErrorCodeTest, RegistryErrorStrings) {
    EXPECT_NE(to_string(registry_error::handler_exists), nullptr);
    EXPECT_NE(to_string(registry_error::no_handler), nullptr);
    EXPECT_NE(to_string(registry_error::registration_failed), nullptr);
    EXPECT_NE(to_string(registry_error::ambiguous_handler), nullptr);
    EXPECT_NE(to_string(registry_error::empty_registry), nullptr);
}

TEST_F(HandlerErrorCodeTest, ToErrorInfo) {
    auto info = to_error_info(handler_error::processing_failed, "test details");
    EXPECT_EQ(info.code, -881);
    EXPECT_EQ(info.module, "hl7::handler");
    EXPECT_TRUE(info.details.has_value());
    EXPECT_EQ(info.details.value(), "test details");
}

// =============================================================================
// Performance Tests
// =============================================================================

class CrtpPerformanceTest : public ::testing::Test {
protected:
    std::shared_ptr<cache::patient_cache> cache_;
    static constexpr int ITERATIONS = 10000;

    void SetUp() override {
        cache_ = std::make_shared<cache::patient_cache>();
    }
};

TEST_F(CrtpPerformanceTest, DirectCrtpCanHandlePerformance) {
    adt_handler handler(cache_);

    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    ASSERT_TRUE(msg.has_value());

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        [[maybe_unused]] bool result = handler.can_handle(*msg);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_ns = (duration.count() * 1000.0) / ITERATIONS;
    std::cout << "Direct CRTP can_handle: " << avg_ns << " ns/call" << std::endl;

    // Performance assertion: should be fast (< 1000 ns per call)
    EXPECT_LT(avg_ns, 1000.0);
}

TEST_F(CrtpPerformanceTest, WrapperCanHandlePerformance) {
    auto wrapper = make_handler_wrapper<adt_handler>(cache_);

    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    ASSERT_TRUE(msg.has_value());

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        [[maybe_unused]] bool result = wrapper->can_handle(*msg);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_ns = (duration.count() * 1000.0) / ITERATIONS;
    std::cout << "Wrapper can_handle: " << avg_ns << " ns/call" << std::endl;

    // Wrapper overhead should be minimal
    EXPECT_LT(avg_ns, 2000.0);
}

TEST_F(CrtpPerformanceTest, RegistryProcessPerformance) {
    hl7_handler_registry registry;
    registry.register_handler<adt_handler>(cache_);

    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    ASSERT_TRUE(msg.has_value());

    // Warm up
    for (int i = 0; i < 100; ++i) {
        cache_->clear();
        registry.process(*msg);
    }
    cache_->clear();

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; ++i) {
        cache_->clear();  // Clear cache to allow new patient creation each time
        registry.process(*msg);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double avg_us = (duration.count() * 1000.0) / 1000;
    std::cout << "Registry process: " << avg_us << " us/call" << std::endl;

    // Full processing should complete within reasonable time
    EXPECT_LT(duration.count(), 10000);  // < 10 seconds for 1000 iterations
}

// =============================================================================
// Handler Result Conversion Tests
// =============================================================================

class HandlerResultTest : public ::testing::Test {};

TEST_F(HandlerResultTest, DefaultValues) {
    handler_result result;
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message_type.empty());
    EXPECT_TRUE(result.handler_type.empty());
    EXPECT_TRUE(result.description.empty());
    EXPECT_TRUE(result.warnings.empty());
}

TEST_F(HandlerResultTest, WrapperConvertsResult) {
    auto cache = std::make_shared<cache::patient_cache>();
    auto wrapper = make_handler_wrapper<adt_handler>(cache);

    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    ASSERT_TRUE(msg.has_value());

    auto result = wrapper->process(*msg);
    ASSERT_TRUE(result.is_ok());

    auto& value = result.value();
    EXPECT_TRUE(value.success);
    EXPECT_EQ(value.handler_type, "ADT");
    EXPECT_FALSE(value.description.empty());
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

class RegistryThreadSafetyTest : public ::testing::Test {
protected:
    std::shared_ptr<cache::patient_cache> cache_;
    std::shared_ptr<pacs_adapter::mwl_client> mwl_client_;

    void SetUp() override {
        cache_ = std::make_shared<cache::patient_cache>();
        mwl_client_ = create_test_mwl_client();
    }
};

TEST_F(RegistryThreadSafetyTest, ConcurrentProcessing) {
    hl7_handler_registry registry;
    registry.register_handler<adt_handler>(cache_);

    auto msg = hl7_message::parse(SAMPLE_ADT_A01);
    ASSERT_TRUE(msg.has_value());

    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    auto process_messages = [&](int thread_id) {
        for (int i = 0; i < 100; ++i) {
            std::string patient_id = std::to_string(thread_id * 1000 + i);
            std::string msg_str =
                "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG" +
                patient_id +
                "|P|2.4|||AL|NE\r"
                "EVN|A01|20240115103000|||OPERATOR^JOHN\r"
                "PID|1||" +
                patient_id +
                "^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
                "PV1|1|I|WARD^101^A^HOSPITAL\r";

            auto parsed = hl7_message::parse(msg_str);
            if (parsed) {
                auto result = registry.process(*parsed);
                if (result.is_ok() && result.value().success) {
                    ++success_count;
                } else {
                    ++failure_count;
                }
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(process_messages, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, 400);
    EXPECT_EQ(failure_count, 0);

    auto stats = registry.get_statistics();
    EXPECT_EQ(stats.total_processed, 400);
}

}  // namespace pacs::bridge::hl7::test
