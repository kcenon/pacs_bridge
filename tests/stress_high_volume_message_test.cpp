/**
 * @file stress_high_volume_message_test.cpp
 * @brief Stress tests for high-volume message processing
 *
 * Tests for handling large messages, many messages, and sustained
 * high-throughput scenarios.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/145
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <random>

namespace pacs::bridge::hl7::test {
namespace {

using namespace ::testing;
using namespace std::chrono;

// =============================================================================
// Test Fixture
// =============================================================================

class StressHighVolumeTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;

    // Create a standard ADT message
    std::string create_adt_message(int id) {
        return
            "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG" +
            std::to_string(id) + "|P|2.4\r"
            "EVN|A01|20240115103000\r"
            "PID|1||" + std::to_string(10000 + id) + "^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
            "PV1|1|I|WARD^101^A\r";
    }

    // Create an ORM message with order details
    std::string create_orm_message(int id) {
        return
            "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ORM^O01|MSG" +
            std::to_string(id) + "|P|2.4\r"
            "PID|1||" + std::to_string(10000 + id) + "^^^HOSPITAL^MR||PATIENT^TEST||19900101|M\r"
            "ORC|NW|ORD" + std::to_string(id) + "|ACC" + std::to_string(id) + "||SC\r"
            "OBR|1|ORD" + std::to_string(id) + "|ACC" + std::to_string(id) +
            "|71020^CHEST XRAY^CPT\r";
    }

    // Create an ORU message with results
    std::string create_oru_message(int id, int obx_count = 5) {
        std::string msg =
            "MSH|^~\\&|LAB|HOSPITAL|HIS|HOSPITAL|20240115103000||ORU^R01|MSG" +
            std::to_string(id) + "|P|2.4\r"
            "PID|1||" + std::to_string(10000 + id) + "^^^HOSPITAL^MR||DOE^JOHN\r"
            "OBR|1|ORD" + std::to_string(id) + "|ACC" + std::to_string(id) +
            "|CBC^Complete Blood Count\r";

        for (int i = 1; i <= obx_count; ++i) {
            msg += "OBX|" + std::to_string(i) + "|NM|TEST" + std::to_string(i) +
                   "||" + std::to_string(100 + i) + "|unit|0-200|N|||F\r";
        }
        return msg;
    }

    // Create a large message with many segments
    std::string create_large_message(int segment_count) {
        std::string msg =
            "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ORU^R01|LARGE001|P|2.4\r"
            "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
            "OBR|1|ORD001|ACC001|CBC^Complete Blood Count\r";

        for (int i = 1; i <= segment_count; ++i) {
            msg += "OBX|" + std::to_string(i) + "|TX|NOTE" + std::to_string(i) +
                   "||Test result number " + std::to_string(i) +
                   " with some additional text to increase size||||||F\r";
        }
        return msg;
    }

    // Create message with large field values
    std::string create_message_large_fields(int field_size) {
        std::string large_value(field_size, 'X');
        return
            "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
            "PID|1||12345^^^HOSPITAL^MR||" + large_value + "^JOHN||19800515|M\r";
    }

    // Measure parsing time
    template<typename F>
    double measure_time_ms(F&& func) {
        auto start = high_resolution_clock::now();
        func();
        auto end = high_resolution_clock::now();
        return duration_cast<microseconds>(end - start).count() / 1000.0;
    }
};

// =============================================================================
// Message Count Stress Tests
// =============================================================================

TEST_F(StressHighVolumeTest, Parse1000Messages) {
    const int count = 1000;
    int success = 0;

    auto elapsed = measure_time_ms([&]() {
        for (int i = 0; i < count; ++i) {
            auto msg = parser_->parse(create_adt_message(i));
            if (msg.is_ok()) ++success;
        }
    });

    EXPECT_EQ(success, count);
    double msgs_per_sec = count / (elapsed / 1000.0);

    // Should process at least 1000 messages per second
    EXPECT_GT(msgs_per_sec, 1000.0) << "Performance: " << msgs_per_sec << " msg/s";
}

TEST_F(StressHighVolumeTest, Parse5000Messages) {
    const int count = 5000;
    int success = 0;

    auto elapsed = measure_time_ms([&]() {
        for (int i = 0; i < count; ++i) {
            auto msg = parser_->parse(create_orm_message(i));
            if (msg.is_ok()) ++success;
        }
    });

    EXPECT_EQ(success, count);
    double msgs_per_sec = count / (elapsed / 1000.0);

    // Should maintain reasonable throughput
    EXPECT_GT(msgs_per_sec, 500.0) << "Performance: " << msgs_per_sec << " msg/s";
}

TEST_F(StressHighVolumeTest, Parse10000SimpleMessages) {
    const int count = 10000;
    int success = 0;

    std::string simple_msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345|||DOE^JOHN\r";

    auto elapsed = measure_time_ms([&]() {
        for (int i = 0; i < count; ++i) {
            auto msg = parser_->parse(simple_msg);
            if (msg.is_ok()) ++success;
        }
    });

    EXPECT_EQ(success, count);
}

// =============================================================================
// Message Size Stress Tests
// =============================================================================

TEST_F(StressHighVolumeTest, ParseMessageWith100Segments) {
    std::string large_msg = create_large_message(100);
    auto msg = parser_->parse(large_msg);

    ASSERT_TRUE(msg.is_ok());
    auto obx_segments = msg.value().segments("OBX");
    EXPECT_EQ(obx_segments.size(), 100);
}

TEST_F(StressHighVolumeTest, ParseMessageWith500Segments) {
    std::string large_msg = create_large_message(500);
    auto msg = parser_->parse(large_msg);

    ASSERT_TRUE(msg.is_ok());
    auto obx_segments = msg.value().segments("OBX");
    EXPECT_EQ(obx_segments.size(), 500);
}

TEST_F(StressHighVolumeTest, ParseMessageWith1000Segments) {
    std::string large_msg = create_large_message(1000);
    auto msg = parser_->parse(large_msg);

    ASSERT_TRUE(msg.is_ok());
    auto obx_segments = msg.value().segments("OBX");
    EXPECT_EQ(obx_segments.size(), 1000);
}

TEST_F(StressHighVolumeTest, ParseMessageWith1KBField) {
    std::string msg = create_message_large_fields(1024);
    auto parsed = parser_->parse(msg);
    ASSERT_TRUE(parsed.is_ok());
}

TEST_F(StressHighVolumeTest, ParseMessageWith10KBField) {
    std::string msg = create_message_large_fields(10 * 1024);
    auto parsed = parser_->parse(msg);
    ASSERT_TRUE(parsed.is_ok());
}

TEST_F(StressHighVolumeTest, ParseMessageWith100KBField) {
    std::string msg = create_message_large_fields(100 * 1024);
    auto parsed = parser_->parse(msg);
    ASSERT_TRUE(parsed.is_ok());
}

TEST_F(StressHighVolumeTest, ParseMessageWith1MBTotal) {
    // Create message approximately 1MB in size
    int segments_needed = 1024 * 1024 / 100;  // ~10KB per segment
    std::string large_msg = create_large_message(segments_needed);

    auto start = high_resolution_clock::now();
    auto msg = parser_->parse(large_msg);
    auto end = high_resolution_clock::now();

    ASSERT_TRUE(msg.is_ok());

    auto elapsed_ms = duration_cast<milliseconds>(end - start).count();
    // 1MB message should parse in reasonable time (< 5 seconds)
    EXPECT_LT(elapsed_ms, 5000) << "Parsing 1MB message took " << elapsed_ms << "ms";
}

// =============================================================================
// Mixed Message Type Tests
// =============================================================================

TEST_F(StressHighVolumeTest, ParseMixedMessageTypes) {
    const int count_per_type = 500;
    int adt_success = 0, orm_success = 0, oru_success = 0;

    auto elapsed = measure_time_ms([&]() {
        for (int i = 0; i < count_per_type; ++i) {
            if (parser_->parse(create_adt_message(i)).is_ok()) ++adt_success;
            if (parser_->parse(create_orm_message(i)).is_ok()) ++orm_success;
            if (parser_->parse(create_oru_message(i)).is_ok()) ++oru_success;
        }
    });

    EXPECT_EQ(adt_success, count_per_type);
    EXPECT_EQ(orm_success, count_per_type);
    EXPECT_EQ(oru_success, count_per_type);
}

TEST_F(StressHighVolumeTest, ParseRandomMessageTypes) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 2);

    const int total = 1000;
    int success = 0;

    for (int i = 0; i < total; ++i) {
        std::string msg;
        switch (dist(gen)) {
            case 0: msg = create_adt_message(i); break;
            case 1: msg = create_orm_message(i); break;
            case 2: msg = create_oru_message(i); break;
        }
        if (parser_->parse(msg).is_ok()) ++success;
    }

    EXPECT_EQ(success, total);
}

// =============================================================================
// Sustained Load Tests
// =============================================================================

TEST_F(StressHighVolumeTest, SustainedLoadFor1Second) {
    auto start = high_resolution_clock::now();
    int count = 0;

    while (duration_cast<seconds>(high_resolution_clock::now() - start).count() < 1) {
        auto msg = parser_->parse(create_adt_message(count));
        if (msg.is_ok()) ++count;
    }

    // Should process at least 500 messages in 1 second
    EXPECT_GT(count, 500) << "Only processed " << count << " messages in 1 second";
}

TEST_F(StressHighVolumeTest, SustainedLoadFor5Seconds) {
    auto start = high_resolution_clock::now();
    int count = 0;
    int errors = 0;

    while (duration_cast<seconds>(high_resolution_clock::now() - start).count() < 5) {
        auto msg = parser_->parse(create_orm_message(count));
        if (msg.is_ok()) {
            ++count;
        } else {
            ++errors;
        }
    }

    EXPECT_EQ(errors, 0) << "Had " << errors << " parse errors during sustained load";
    double msgs_per_sec = count / 5.0;

    // Should maintain consistent throughput
    EXPECT_GT(msgs_per_sec, 100.0) << "Average: " << msgs_per_sec << " msg/s";
}

// =============================================================================
// Memory Stability Tests
// =============================================================================

TEST_F(StressHighVolumeTest, ParseAndDiscardMany) {
    // Parse many messages without keeping references
    for (int i = 0; i < 10000; ++i) {
        auto msg = parser_->parse(create_adt_message(i));
        ASSERT_TRUE(msg.is_ok());
        // Message immediately goes out of scope
    }
    // No memory issues should occur
}

TEST_F(StressHighVolumeTest, ParseAndStoreSome) {
    std::vector<hl7_message> stored;
    stored.reserve(1000);

    for (int i = 0; i < 10000; ++i) {
        auto msg = parser_->parse(create_adt_message(i));
        ASSERT_TRUE(msg.is_ok());

        // Keep every 10th message
        if (i % 10 == 0) {
            stored.push_back(std::move(msg.value()));
        }
    }

    EXPECT_EQ(stored.size(), 1000);
}

// =============================================================================
// Build Performance Tests
// =============================================================================

TEST_F(StressHighVolumeTest, Build1000Messages) {
    const int count = 1000;
    int success = 0;

    auto elapsed = measure_time_ms([&]() {
        for (int i = 0; i < count; ++i) {
            auto msg = hl7_builder::create()
                .sending_app("HIS")
                .sending_facility("HOSPITAL")
                .receiving_app("PACS")
                .receiving_facility("RADIOLOGY")
                .message_type("ADT", "A01")
                .control_id("MSG" + std::to_string(i))
                .build();
            if (msg.is_ok()) ++success;
        }
    });

    EXPECT_EQ(success, count);
}

// =============================================================================
// Round-Trip Performance Tests
// =============================================================================

TEST_F(StressHighVolumeTest, RoundTrip1000Messages) {
    const int count = 1000;
    int success = 0;

    auto elapsed = measure_time_ms([&]() {
        for (int i = 0; i < count; ++i) {
            std::string original = create_adt_message(i);
            auto parsed = parser_->parse(original);
            if (!parsed.is_ok()) continue;

            std::string rebuilt = parsed.value().serialize();
            auto reparsed = parser_->parse(rebuilt);
            if (reparsed.is_ok()) ++success;
        }
    });

    EXPECT_EQ(success, count);
}

// =============================================================================
// Edge Case Under Load Tests
// =============================================================================

TEST_F(StressHighVolumeTest, ParseEmptyStringsMixed) {
    int valid = 0, invalid = 0;

    for (int i = 0; i < 1000; ++i) {
        if (i % 10 == 0) {
            // Every 10th is empty
            auto msg = parser_->parse("");
            if (!msg.is_ok()) ++invalid;
        } else {
            auto msg = parser_->parse(create_adt_message(i));
            if (msg.is_ok()) ++valid;
        }
    }

    EXPECT_EQ(valid, 900);
    EXPECT_EQ(invalid, 100);
}

TEST_F(StressHighVolumeTest, VaryingMessageSizes) {
    int success = 0;

    for (int i = 1; i <= 100; ++i) {
        // Vary OBX count from 1 to 100
        std::string msg = create_oru_message(i, i);
        auto parsed = parser_->parse(msg);
        if (parsed.is_ok()) {
            auto obx_segments = parsed.value().segments("OBX");
            if (obx_segments.size() == static_cast<size_t>(i)) {
                ++success;
            }
        }
    }

    EXPECT_EQ(success, 100);
}

// =============================================================================
// Concurrent Access Tests (Single Thread Stress)
// =============================================================================

TEST_F(StressHighVolumeTest, RapidParserReuse) {
    // Rapidly reuse the same parser instance
    for (int i = 0; i < 10000; ++i) {
        auto msg = parser_->parse(create_adt_message(i % 100));
        ASSERT_TRUE(msg.is_ok());
    }
}

TEST_F(StressHighVolumeTest, InterleavedParseAndBuild) {
    for (int i = 0; i < 1000; ++i) {
        // Parse
        auto parsed = parser_->parse(create_adt_message(i));
        ASSERT_TRUE(parsed.is_ok());

        // Build ACK
        auto ack = hl7_builder::create_ack(parsed.value(), ack_code::AA, "OK");
        // ack is hl7_message directly, no has_value check needed

        // Parse ACK
        auto ack_parsed = parser_->parse(ack.serialize());
        ASSERT_TRUE(ack_parsed.is_ok());
    }
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
