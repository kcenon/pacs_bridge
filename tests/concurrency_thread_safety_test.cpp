/**
 * @file concurrency_thread_safety_test.cpp
 * @brief Unit tests for concurrency and thread safety
 *
 * Tests for race condition detection, deadlock prevention,
 * and thread-safe operation of HL7 message handling.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/145
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::hl7::test {
namespace {

using namespace ::testing;
using namespace std::chrono;

// =============================================================================
// Test Fixture
// =============================================================================

class ConcurrencyThreadSafetyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Parsers are created per-test for isolation
    }

    std::string create_test_message(int id) {
        return
            "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG" +
            std::to_string(id) + "|P|2.4\r"
            "EVN|A01|20240115103000\r"
            "PID|1||" + std::to_string(10000 + id) + "^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r"
            "PV1|1|I|WARD^101^A\r";
    }

    std::string create_large_message(int id, int obx_count = 10) {
        std::string msg =
            "MSH|^~\\&|LAB|HOSPITAL|HIS|HOSPITAL|20240115103000||ORU^R01|MSG" +
            std::to_string(id) + "|P|2.4\r"
            "PID|1||" + std::to_string(10000 + id) + "^^^HOSPITAL^MR||DOE^JOHN\r"
            "OBR|1|ORD" + std::to_string(id) + "|ACC" + std::to_string(id) + "|CBC\r";

        for (int i = 1; i <= obx_count; ++i) {
            msg += "OBX|" + std::to_string(i) + "|NM|TEST" + std::to_string(i) +
                   "||" + std::to_string(100 + i) + "|unit|0-200|N|||F\r";
        }
        return msg;
    }
};

// =============================================================================
// Multiple Parser Instance Tests
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, IndependentParsersInThreads) {
    const int thread_count = 10;
    const int messages_per_thread = 100;
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    auto thread_func = [&](int thread_id) {
        hl7_parser parser;  // Each thread has its own parser
        for (int i = 0; i < messages_per_thread; ++i) {
            int msg_id = thread_id * 1000 + i;
            std::string msg = create_test_message(msg_id);
            auto result = parser.parse(msg);
            if (result.has_value()) {
                ++success_count;
            } else {
                ++failure_count;
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(thread_func, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), thread_count * messages_per_thread);
    EXPECT_EQ(failure_count.load(), 0);
}

// =============================================================================
// Shared Parser Tests (Thread Safety of Parser)
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, SharedParserConcurrentAccess) {
    hl7_parser shared_parser;
    std::mutex parser_mutex;

    const int thread_count = 5;
    const int messages_per_thread = 50;
    std::atomic<int> success_count{0};

    auto thread_func = [&](int thread_id) {
        for (int i = 0; i < messages_per_thread; ++i) {
            int msg_id = thread_id * 1000 + i;
            std::string msg = create_test_message(msg_id);

            std::expected<hl7_message, hl7_error> result;
            {
                std::lock_guard<std::mutex> lock(parser_mutex);
                result = shared_parser.parse(msg);
            }

            if (result.has_value()) {
                ++success_count;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(thread_func, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), thread_count * messages_per_thread);
}

// =============================================================================
// Builder Thread Safety Tests
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, ConcurrentMessageBuilding) {
    const int thread_count = 10;
    const int messages_per_thread = 100;
    std::atomic<int> success_count{0};

    auto thread_func = [&](int thread_id) {
        for (int i = 0; i < messages_per_thread; ++i) {
            auto msg = hl7_builder::create()
                .sending_app("HIS_" + std::to_string(thread_id))
                .sending_facility("HOSPITAL")
                .receiving_app("PACS")
                .receiving_facility("RADIOLOGY")
                .message_type("ADT", "A01")
                .control_id("MSG" + std::to_string(thread_id * 1000 + i))
                .build();

            if (msg.has_value()) {
                ++success_count;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(thread_func, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), thread_count * messages_per_thread);
}

// =============================================================================
// Parse and Build Mixed Workload Tests
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, MixedParseAndBuildWorkload) {
    const int thread_count = 8;
    const int operations_per_thread = 50;
    std::atomic<int> parse_success{0};
    std::atomic<int> build_success{0};

    auto thread_func = [&](int thread_id) {
        hl7_parser parser;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 1);

        for (int i = 0; i < operations_per_thread; ++i) {
            if (dist(gen) == 0) {
                // Parse operation
                std::string msg = create_test_message(thread_id * 1000 + i);
                auto result = parser.parse(msg);
                if (result.has_value()) {
                    ++parse_success;
                }
            } else {
                // Build operation
                auto msg = hl7_builder::create()
                    .sending_app("HIS")
                    .sending_facility("HOSPITAL")
                    .receiving_app("PACS")
                    .receiving_facility("RADIOLOGY")
                    .message_type("ADT", "A01")
                    .control_id("MSG" + std::to_string(thread_id * 1000 + i))
                    .build();

                if (msg.has_value()) {
                    ++build_success;
                }
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(thread_func, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    int total_success = parse_success.load() + build_success.load();
    EXPECT_EQ(total_success, thread_count * operations_per_thread);
}

// =============================================================================
// Data Race Detection Tests
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, MessageObjectCopySafety) {
    const int thread_count = 4;
    hl7_parser parser;
    auto original = parser.parse(create_test_message(1));
    ASSERT_TRUE(original.has_value());

    std::atomic<int> success_count{0};

    auto thread_func = [&]() {
        for (int i = 0; i < 100; ++i) {
            // Copy the message
            hl7_message copy = *original;

            // Read from the copy
            auto msh = copy.segment("MSH");
            if (msh && !msh->field_value(9).empty()) {
                ++success_count;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(thread_func);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), thread_count * 100);
}

// =============================================================================
// Async Parse Tests
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, AsyncParseOperations) {
    const int async_count = 20;
    std::vector<std::future<std::expected<hl7_message, hl7_error>>> futures;
    futures.reserve(async_count);

    for (int i = 0; i < async_count; ++i) {
        futures.emplace_back(std::async(std::launch::async, [this, i]() {
            hl7_parser parser;
            return parser.parse(create_test_message(i));
        }));
    }

    int success = 0;
    for (auto& f : futures) {
        auto result = f.get();
        if (result.has_value()) {
            ++success;
        }
    }

    EXPECT_EQ(success, async_count);
}

// =============================================================================
// High Contention Tests
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, HighContentionParsing) {
    const int thread_count = 20;  // High thread count
    const int messages_per_thread = 20;
    std::atomic<int> success_count{0};
    std::atomic<int> in_progress{0};
    int max_concurrent = 0;
    std::mutex max_mutex;

    auto thread_func = [&](int thread_id) {
        hl7_parser parser;
        for (int i = 0; i < messages_per_thread; ++i) {
            ++in_progress;

            {
                std::lock_guard<std::mutex> lock(max_mutex);
                if (in_progress > max_concurrent) {
                    max_concurrent = in_progress.load();
                }
            }

            std::string msg = create_test_message(thread_id * 1000 + i);
            auto result = parser.parse(msg);

            --in_progress;

            if (result.has_value()) {
                ++success_count;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(thread_func, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), thread_count * messages_per_thread);
    // At least some concurrency should have occurred
    EXPECT_GT(max_concurrent, 1);
}

// =============================================================================
// Stress Tests with Large Messages
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, ConcurrentLargeMessages) {
    const int thread_count = 4;
    const int messages_per_thread = 10;
    std::atomic<int> success_count{0};

    auto thread_func = [&](int thread_id) {
        hl7_parser parser;
        for (int i = 0; i < messages_per_thread; ++i) {
            std::string msg = create_large_message(thread_id * 1000 + i, 50);
            auto result = parser.parse(msg);
            if (result.has_value()) {
                auto obx_segments = result->segments("OBX");
                if (obx_segments.size() == 50) {
                    ++success_count;
                }
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(thread_func, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), thread_count * messages_per_thread);
}

// =============================================================================
// Producer-Consumer Pattern Tests
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, ProducerConsumerPattern) {
    std::queue<std::string> message_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::atomic<bool> producer_done{false};
    std::atomic<int> consumed_count{0};

    const int message_count = 100;

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < message_count; ++i) {
            std::string msg = create_test_message(i);
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                message_queue.push(msg);
            }
            cv.notify_one();
        }
        producer_done = true;
        cv.notify_all();
    });

    // Consumer threads
    auto consumer_func = [&]() {
        hl7_parser parser;
        while (true) {
            std::string msg;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                cv.wait(lock, [&]() {
                    return !message_queue.empty() || producer_done.load();
                });

                if (message_queue.empty() && producer_done.load()) {
                    break;
                }

                if (!message_queue.empty()) {
                    msg = message_queue.front();
                    message_queue.pop();
                } else {
                    continue;
                }
            }

            auto result = parser.parse(msg);
            if (result.has_value()) {
                ++consumed_count;
            }
        }
    };

    std::vector<std::thread> consumers;
    for (int i = 0; i < 4; ++i) {
        consumers.emplace_back(consumer_func);
    }

    producer.join();
    for (auto& c : consumers) {
        c.join();
    }

    EXPECT_EQ(consumed_count.load(), message_count);
}

// =============================================================================
// Round-Trip Concurrent Tests
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, ConcurrentRoundTrip) {
    const int thread_count = 8;
    const int operations_per_thread = 25;
    std::atomic<int> success_count{0};

    auto thread_func = [&](int thread_id) {
        hl7_parser parser;
        for (int i = 0; i < operations_per_thread; ++i) {
            // Parse original
            std::string original = create_test_message(thread_id * 1000 + i);
            auto parsed = parser.parse(original);
            if (!parsed.has_value()) continue;

            // Convert to string
            std::string rebuilt = parsed->serialize();

            // Parse rebuilt
            auto reparsed = parser.parse(rebuilt);
            if (!reparsed.has_value()) continue;

            // Verify
            if (parsed->type() == reparsed->type()) {
                ++success_count;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(thread_func, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), thread_count * operations_per_thread);
}

// =============================================================================
// Memory Safety Under Concurrency
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, MemorySafetyUnderLoad) {
    const int thread_count = 10;
    const int iterations = 100;
    std::atomic<bool> has_error{false};

    auto thread_func = [&](int thread_id) {
        try {
            hl7_parser parser;
            for (int i = 0; i < iterations; ++i) {
                auto msg = parser.parse(create_test_message(thread_id * 1000 + i));
                if (msg.has_value()) {
                    // Access various parts of the message
                    auto msh = msg->segment("MSH");
                    auto pid = msg->segment("PID");
                    auto pv1 = msg->segment("PV1");

                    if (msh) { volatile auto _ = msh->field_value(9); }
                    if (pid) { volatile auto _ = pid->field_value(3); }
                    if (pv1) { volatile auto _ = pv1->field_value(3); }

                    // Check segment count and iterate known segments
                    volatile auto seg_count = msg->segment_count();
                    // Access a few standard segments
                    for (const auto& seg_id : {"MSH", "PID", "PV1"}) {
                        if (auto seg = msg->segment(seg_id)) {
                            volatile auto _ = seg->segment_id();
                        }
                    }
                }
            }
        } catch (...) {
            has_error = true;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(thread_func, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_FALSE(has_error.load());
}

// =============================================================================
// Thread Timeout Tests
// =============================================================================

TEST_F(ConcurrencyThreadSafetyTest, ParsingDoesNotDeadlock) {
    const int thread_count = 4;
    std::atomic<int> completed{0};

    auto thread_func = [&](int thread_id) {
        hl7_parser parser;
        for (int i = 0; i < 100; ++i) {
            parser.parse(create_test_message(thread_id * 1000 + i));
        }
        ++completed;
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(thread_func, i);
    }

    // Wait with timeout
    auto start = high_resolution_clock::now();
    for (auto& t : threads) {
        t.join();
    }
    auto elapsed = duration_cast<seconds>(high_resolution_clock::now() - start).count();

    // Should complete within reasonable time (no deadlock)
    EXPECT_LT(elapsed, 30);
    EXPECT_EQ(completed.load(), thread_count);
}

}  // namespace
}  // namespace pacs::bridge::hl7::test
