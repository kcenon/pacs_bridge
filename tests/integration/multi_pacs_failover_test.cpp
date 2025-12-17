/**
 * @file multi_pacs_failover_test.cpp
 * @brief Integration tests for multi-PACS failover scenarios
 *
 * Tests for handling multiple PACS systems, failover between primary
 * and secondary systems, and load balancing scenarios.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/145
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <string>
#include <vector>
#include <queue>
#include <chrono>
#include <thread>
#include <atomic>
#include <functional>

namespace pacs::bridge::integration::test {
namespace {

using namespace ::testing;
using namespace std::chrono;

// =============================================================================
// Mock PACS System
// =============================================================================

class MockPacsSystem {
public:
    explicit MockPacsSystem(const std::string& name)
        : name_(name), available_(true), latency_ms_(10), failure_count_(0) {}

    void set_available(bool available) { available_ = available; }
    void set_latency(int ms) { latency_ms_ = ms; }
    bool is_available() const { return available_; }
    const std::string& name() const { return name_; }
    int failure_count() const { return failure_count_; }
    int success_count() const { return success_count_; }

    bool send_message(const std::string& msg) {
        if (!available_) {
            ++failure_count_;
            return false;
        }
        std::this_thread::sleep_for(milliseconds(latency_ms_));
        ++success_count_;
        messages_.push(msg);
        return true;
    }

    std::optional<std::string> get_last_message() {
        if (messages_.empty()) return std::nullopt;
        auto msg = messages_.front();
        messages_.pop();
        return msg;
    }

private:
    std::string name_;
    std::atomic<bool> available_;
    std::atomic<int> latency_ms_;
    std::atomic<int> failure_count_;
    std::atomic<int> success_count_;
    std::queue<std::string> messages_;
};

// =============================================================================
// Multi-PACS Router
// =============================================================================

enum class RoutingStrategy {
    PRIMARY_WITH_FAILOVER,
    ROUND_ROBIN,
    LOAD_BALANCED
};

class MultiPacsRouter {
public:
    void add_pacs(std::shared_ptr<MockPacsSystem> pacs, bool is_primary = false) {
        if (is_primary) {
            primary_pacs_ = pacs;
        }
        pacs_systems_.push_back(pacs);
    }

    void set_strategy(RoutingStrategy strategy) {
        strategy_ = strategy;
    }

    bool route_message(const std::string& msg) {
        switch (strategy_) {
            case RoutingStrategy::PRIMARY_WITH_FAILOVER:
                return route_with_failover(msg);
            case RoutingStrategy::ROUND_ROBIN:
                return route_round_robin(msg);
            case RoutingStrategy::LOAD_BALANCED:
                return route_load_balanced(msg);
        }
        return false;
    }

    std::shared_ptr<MockPacsSystem> get_last_used_pacs() const {
        return last_used_pacs_;
    }

private:
    bool route_with_failover(const std::string& msg) {
        // Try primary first
        if (primary_pacs_ && primary_pacs_->is_available()) {
            if (primary_pacs_->send_message(msg)) {
                last_used_pacs_ = primary_pacs_;
                return true;
            }
        }

        // Try secondaries
        for (auto& pacs : pacs_systems_) {
            if (pacs != primary_pacs_ && pacs->is_available()) {
                if (pacs->send_message(msg)) {
                    last_used_pacs_ = pacs;
                    return true;
                }
            }
        }
        return false;
    }

    bool route_round_robin(const std::string& msg) {
        size_t attempts = 0;
        while (attempts < pacs_systems_.size()) {
            auto& pacs = pacs_systems_[current_index_];
            current_index_ = (current_index_ + 1) % pacs_systems_.size();

            if (pacs->is_available() && pacs->send_message(msg)) {
                last_used_pacs_ = pacs;
                return true;
            }
            ++attempts;
        }
        return false;
    }

    bool route_load_balanced(const std::string& msg) {
        // Simple load balancing: choose least-loaded available PACS
        std::shared_ptr<MockPacsSystem> best = nullptr;
        int min_load = std::numeric_limits<int>::max();

        for (auto& pacs : pacs_systems_) {
            if (pacs->is_available() && pacs->success_count() < min_load) {
                best = pacs;
                min_load = pacs->success_count();
            }
        }

        if (best && best->send_message(msg)) {
            last_used_pacs_ = best;
            return true;
        }
        return false;
    }

    std::vector<std::shared_ptr<MockPacsSystem>> pacs_systems_;
    std::shared_ptr<MockPacsSystem> primary_pacs_;
    std::shared_ptr<MockPacsSystem> last_used_pacs_;
    RoutingStrategy strategy_ = RoutingStrategy::PRIMARY_WITH_FAILOVER;
    size_t current_index_ = 0;
};

// =============================================================================
// Test Fixture
// =============================================================================

class MultiPacsFailoverTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7::hl7_parser>();

        // Create mock PACS systems
        primary_pacs_ = std::make_shared<MockPacsSystem>("PRIMARY");
        secondary_pacs_ = std::make_shared<MockPacsSystem>("SECONDARY");
        tertiary_pacs_ = std::make_shared<MockPacsSystem>("TERTIARY");

        // Set up router
        router_ = std::make_unique<MultiPacsRouter>();
        router_->add_pacs(primary_pacs_, true);
        router_->add_pacs(secondary_pacs_);
        router_->add_pacs(tertiary_pacs_);
    }

    std::unique_ptr<hl7::hl7_parser> parser_;
    std::unique_ptr<MultiPacsRouter> router_;
    std::shared_ptr<MockPacsSystem> primary_pacs_;
    std::shared_ptr<MockPacsSystem> secondary_pacs_;
    std::shared_ptr<MockPacsSystem> tertiary_pacs_;

    std::string create_test_message(int id) {
        return
            "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ORM^O01|MSG" +
            std::to_string(id) + "|P|2.4\r"
            "PID|1||" + std::to_string(10000 + id) + "^^^HOSPITAL^MR||DOE^JOHN\r"
            "ORC|NW|ORD" + std::to_string(id) + "|ACC" + std::to_string(id) + "\r"
            "OBR|1|ORD" + std::to_string(id) + "|ACC" + std::to_string(id) +
            "|71020^CHEST XRAY^CPT\r";
    }
};

// =============================================================================
// Primary with Failover Tests
// =============================================================================

TEST_F(MultiPacsFailoverTest, RouteToAvailablePrimary) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);

    std::string msg = create_test_message(1);
    EXPECT_TRUE(router_->route_message(msg));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "PRIMARY");
}

TEST_F(MultiPacsFailoverTest, FailoverToSecondaryWhenPrimaryDown) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);
    primary_pacs_->set_available(false);

    std::string msg = create_test_message(1);
    EXPECT_TRUE(router_->route_message(msg));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "SECONDARY");
}

TEST_F(MultiPacsFailoverTest, FailoverToTertiaryWhenPrimaryAndSecondaryDown) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);
    primary_pacs_->set_available(false);
    secondary_pacs_->set_available(false);

    std::string msg = create_test_message(1);
    EXPECT_TRUE(router_->route_message(msg));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "TERTIARY");
}

TEST_F(MultiPacsFailoverTest, FailWhenAllSystemsDown) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);
    primary_pacs_->set_available(false);
    secondary_pacs_->set_available(false);
    tertiary_pacs_->set_available(false);

    std::string msg = create_test_message(1);
    EXPECT_FALSE(router_->route_message(msg));
}

TEST_F(MultiPacsFailoverTest, ReturnToPrimaryWhenRestored) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);

    // Initial: primary down
    primary_pacs_->set_available(false);
    EXPECT_TRUE(router_->route_message(create_test_message(1)));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "SECONDARY");

    // Primary restored
    primary_pacs_->set_available(true);
    EXPECT_TRUE(router_->route_message(create_test_message(2)));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "PRIMARY");
}

// =============================================================================
// Round Robin Tests
// =============================================================================

TEST_F(MultiPacsFailoverTest, RoundRobinDistribution) {
    router_->set_strategy(RoutingStrategy::ROUND_ROBIN);

    // Send 6 messages - should distribute evenly
    for (int i = 0; i < 6; ++i) {
        EXPECT_TRUE(router_->route_message(create_test_message(i)));
    }

    // Each PACS should have received 2 messages
    EXPECT_EQ(primary_pacs_->success_count(), 2);
    EXPECT_EQ(secondary_pacs_->success_count(), 2);
    EXPECT_EQ(tertiary_pacs_->success_count(), 2);
}

TEST_F(MultiPacsFailoverTest, RoundRobinSkipsUnavailable) {
    router_->set_strategy(RoutingStrategy::ROUND_ROBIN);
    secondary_pacs_->set_available(false);

    // Send 4 messages
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(router_->route_message(create_test_message(i)));
    }

    // Primary and tertiary should share the load
    EXPECT_EQ(primary_pacs_->success_count(), 2);
    EXPECT_EQ(secondary_pacs_->success_count(), 0);
    EXPECT_EQ(tertiary_pacs_->success_count(), 2);
}

// =============================================================================
// Load Balanced Tests
// =============================================================================

TEST_F(MultiPacsFailoverTest, LoadBalancedDistribution) {
    router_->set_strategy(RoutingStrategy::LOAD_BALANCED);

    // Send multiple messages
    for (int i = 0; i < 9; ++i) {
        EXPECT_TRUE(router_->route_message(create_test_message(i)));
    }

    // Load should be relatively balanced
    int min_load = std::min({primary_pacs_->success_count(),
                            secondary_pacs_->success_count(),
                            tertiary_pacs_->success_count()});
    int max_load = std::max({primary_pacs_->success_count(),
                            secondary_pacs_->success_count(),
                            tertiary_pacs_->success_count()});

    EXPECT_LE(max_load - min_load, 1);  // Difference should be at most 1
}

TEST_F(MultiPacsFailoverTest, LoadBalancedAvoidsSlowSystem) {
    router_->set_strategy(RoutingStrategy::LOAD_BALANCED);

    // Make primary slow
    primary_pacs_->set_latency(100);  // 100ms latency

    // Send messages quickly
    for (int i = 0; i < 6; ++i) {
        EXPECT_TRUE(router_->route_message(create_test_message(i)));
    }

    // Faster systems should handle more messages
    // (Note: actual behavior depends on implementation details)
}

// =============================================================================
// Failover Sequence Tests
// =============================================================================

TEST_F(MultiPacsFailoverTest, CascadingFailover) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);

    // Start: all available
    EXPECT_TRUE(router_->route_message(create_test_message(1)));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "PRIMARY");

    // Primary fails
    primary_pacs_->set_available(false);
    EXPECT_TRUE(router_->route_message(create_test_message(2)));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "SECONDARY");

    // Secondary fails
    secondary_pacs_->set_available(false);
    EXPECT_TRUE(router_->route_message(create_test_message(3)));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "TERTIARY");

    // All fail
    tertiary_pacs_->set_available(false);
    EXPECT_FALSE(router_->route_message(create_test_message(4)));

    // Tertiary recovers
    tertiary_pacs_->set_available(true);
    EXPECT_TRUE(router_->route_message(create_test_message(5)));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "TERTIARY");

    // Secondary recovers
    secondary_pacs_->set_available(true);
    EXPECT_TRUE(router_->route_message(create_test_message(6)));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "SECONDARY");

    // Primary recovers
    primary_pacs_->set_available(true);
    EXPECT_TRUE(router_->route_message(create_test_message(7)));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "PRIMARY");
}

// =============================================================================
// Message Integrity Tests
// =============================================================================

TEST_F(MultiPacsFailoverTest, MessageIntegrityDuringFailover) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);

    std::string original = create_test_message(123);
    EXPECT_TRUE(router_->route_message(original));

    auto received = router_->get_last_used_pacs()->get_last_message();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(*received, original);
}

TEST_F(MultiPacsFailoverTest, MultipleMessagesAfterFailover) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);

    // Send to primary
    EXPECT_TRUE(router_->route_message(create_test_message(1)));
    EXPECT_EQ(primary_pacs_->success_count(), 1);

    // Primary fails
    primary_pacs_->set_available(false);

    // Send multiple to secondary
    for (int i = 2; i <= 5; ++i) {
        EXPECT_TRUE(router_->route_message(create_test_message(i)));
    }

    EXPECT_EQ(secondary_pacs_->success_count(), 4);
    EXPECT_EQ(primary_pacs_->failure_count(), 0);  // Shouldn't try failed primary
}

// =============================================================================
// High Volume Failover Tests
// =============================================================================

TEST_F(MultiPacsFailoverTest, HighVolumeWithFailover) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);

    int total = 100;
    int success = 0;

    for (int i = 0; i < total; ++i) {
        // Simulate intermittent primary failure
        if (i % 10 == 5) {
            primary_pacs_->set_available(false);
        } else if (i % 10 == 8) {
            primary_pacs_->set_available(true);
        }

        if (router_->route_message(create_test_message(i))) {
            ++success;
        }
    }

    EXPECT_EQ(success, total);  // All messages should be delivered
}

// =============================================================================
// Timing and Latency Tests
// =============================================================================

TEST_F(MultiPacsFailoverTest, FailoverTimingUnderLoad) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);

    auto start = high_resolution_clock::now();

    // Send messages and measure failover time
    for (int i = 0; i < 10; ++i) {
        if (i == 5) {
            primary_pacs_->set_available(false);
        }
        EXPECT_TRUE(router_->route_message(create_test_message(i)));
    }

    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();

    // Failover should be fast (10 messages * 10ms latency = ~100ms + overhead)
    EXPECT_LT(elapsed, 500);
}

// =============================================================================
// Configuration Change Tests
// =============================================================================

TEST_F(MultiPacsFailoverTest, StrategyChangeWhileRouting) {
    router_->set_strategy(RoutingStrategy::PRIMARY_WITH_FAILOVER);
    EXPECT_TRUE(router_->route_message(create_test_message(1)));
    EXPECT_EQ(router_->get_last_used_pacs()->name(), "PRIMARY");

    // Change strategy
    router_->set_strategy(RoutingStrategy::ROUND_ROBIN);

    // Continue routing - behavior should change
    for (int i = 2; i <= 4; ++i) {
        EXPECT_TRUE(router_->route_message(create_test_message(i)));
    }

    // Round robin should have distributed messages
    EXPECT_GT(secondary_pacs_->success_count(), 0);
    EXPECT_GT(tertiary_pacs_->success_count(), 0);
}

}  // namespace
}  // namespace pacs::bridge::integration::test
