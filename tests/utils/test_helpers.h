/**
 * @file test_helpers.h
 * @brief Common test utilities and helpers for PACS Bridge tests
 *
 * Provides utility functions, fixtures, and macros for unit testing.
 * Uses Google Test (gtest) and Google Mock (gmock) frameworks.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/6
 */

#ifndef PACS_BRIDGE_TEST_HELPERS_H
#define PACS_BRIDGE_TEST_HELPERS_H

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace pacs::bridge::test {

// =============================================================================
// Test Data Path Utilities
// =============================================================================

/**
 * @brief Get the test data directory path
 * @return Path to the test data directory
 */
inline std::filesystem::path test_data_dir() {
#ifdef PACS_BRIDGE_TEST_DATA_DIR
    return std::filesystem::path(PACS_BRIDGE_TEST_DATA_DIR);
#else
    return std::filesystem::current_path() / "data";
#endif
}

/**
 * @brief Get path to a specific test data file
 * @param filename Name of the test data file
 * @return Full path to the test data file
 */
inline std::filesystem::path test_data_path(std::string_view filename) {
    return test_data_dir() / filename;
}

/**
 * @brief Read entire contents of a test data file
 * @param filename Name of the test data file
 * @return File contents as string
 * @throws std::runtime_error if file cannot be read
 */
inline std::string read_test_file(std::string_view filename) {
    auto path = test_data_path(filename);
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open test file: " + path.string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

/**
 * @brief Check if a test data file exists
 * @param filename Name of the test data file
 * @return true if file exists
 */
inline bool test_file_exists(std::string_view filename) {
    return std::filesystem::exists(test_data_path(filename));
}

// =============================================================================
// Sample HL7 Messages
// =============================================================================

namespace hl7_samples {

/**
 * @brief Sample ADT^A01 (Patient Admit) message
 */
constexpr std::string_view ADT_A01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4|||AL|NE\r"
    "EVN|A01|20240115103000|||OPERATOR^JOHN\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||123 MAIN ST^^SPRINGFIELD^IL^62701||555-123-4567\r"
    "PV1|1|I|WARD^101^A^HOSPITAL||||SMITH^ROBERT^MD\r";

/**
 * @brief Sample ADT^A08 (Patient Update) message
 */
constexpr std::string_view ADT_A08 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115120000||ADT^A08|MSG002|P|2.4|||AL|NE\r"
    "EVN|A08|20240115120000\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M|||456 OAK AVE^^SPRINGFIELD^IL^62702||555-987-6543\r"
    "PV1|1|I|WARD^102^B^HOSPITAL||||JONES^SARAH^MD\r";

/**
 * @brief Sample ORM^O01 (Order) message
 */
constexpr std::string_view ORM_O01 =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG003|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "PV1|1|I|WARD^101^A\r"
    "ORC|NW|ORD001^HIS|ACC001^PACS||SC\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT\r";

/**
 * @brief Sample ORU^R01 (Result) message
 */
constexpr std::string_view ORU_R01 =
    "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115140000||ORU^R01|MSG004|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT|||20240115130000|||||||||SMITH^ROBERT^MD\r"
    "OBX|1|TX|GDT^REPORT^L||No acute cardiopulmonary abnormality.||||||F\r";

/**
 * @brief Sample ACK (Acknowledgment) message
 */
constexpr std::string_view ACK_AA =
    "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115103001||ACK|ACK001|P|2.4\r"
    "MSA|AA|MSG001|Message accepted\r";

/**
 * @brief Minimal valid HL7 message (MSH only)
 */
constexpr std::string_view MINIMAL_MSG =
    "MSH|^~\\&|APP|FAC|DEST|DFAC|20240115103000||ADT^A01|MSG001|P|2.4\r";

/**
 * @brief HL7 message with custom delimiters
 */
constexpr std::string_view CUSTOM_DELIM_MSG =
    "MSH#*~!@#SENDER#FAC#RECV#RFAC#20240115||ADT*A01#MSG001#P#2.4\r"
    "PID#1##12345*HOSPITAL####DOE*JOHN##M\r";

/**
 * @brief HL7 message with Z-segment (custom segment)
 */
constexpr std::string_view MSG_WITH_ZDS =
    "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115110000||ORM^O01|MSG005|P|2.4|||AL|NE\r"
    "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^WILLIAM||19800515|M\r"
    "ORC|NW|ORD001^HIS|ACC001^PACS||SC\r"
    "OBR|1|ORD001^HIS|ACC001^PACS|71020^CHEST XRAY^CPT\r"
    "ZDS|1.2.840.10008.5.1.4.1.1.2.1.12345||Custom Z-segment data\r";

}  // namespace hl7_samples

// =============================================================================
// Performance Testing Utilities
// =============================================================================

/**
 * @brief Simple timer for performance measurements
 */
class scoped_timer {
public:
    explicit scoped_timer(std::string_view name = "")
        : name_(name), start_(std::chrono::steady_clock::now()) {}

    ~scoped_timer() {
        if (!stopped_) {
            stop();
        }
    }

    /**
     * @brief Stop the timer and return elapsed time in microseconds
     */
    int64_t stop() {
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            end - start_).count();
        stopped_ = true;
        return elapsed;
    }

    /**
     * @brief Get elapsed time without stopping
     */
    [[nodiscard]] int64_t elapsed_us() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now - start_).count();
    }

private:
    std::string name_;
    std::chrono::steady_clock::time_point start_;
    bool stopped_ = false;
};

/**
 * @brief Run a function multiple times and return average execution time
 * @tparam Func Callable type
 * @param iterations Number of iterations
 * @param func Function to benchmark
 * @return Average execution time in microseconds
 */
template <typename Func>
int64_t benchmark(size_t iterations, Func&& func) {
    int64_t total = 0;
    for (size_t i = 0; i < iterations; ++i) {
        scoped_timer timer;
        func();
        total += timer.stop();
    }
    return total / static_cast<int64_t>(iterations);
}

// =============================================================================
// Test Fixture Base Class
// =============================================================================

/**
 * @brief Base fixture class for PACS Bridge tests
 *
 * Provides common setup/teardown and utility methods for tests.
 */
class pacs_bridge_test : public ::testing::Test {
protected:
    void SetUp() override {
        // Common setup for all tests
    }

    void TearDown() override {
        // Common teardown for all tests
    }

    /**
     * @brief Get test data directory
     */
    [[nodiscard]] std::filesystem::path data_dir() const {
        return test_data_dir();
    }

    /**
     * @brief Read test data file
     */
    [[nodiscard]] std::string read_file(std::string_view filename) const {
        return read_test_file(filename);
    }
};

// =============================================================================
// Custom Matchers
// =============================================================================

/**
 * @brief Matcher for checking if a string contains a substring
 */
MATCHER_P(ContainsSubstring, substring, "") {
    return arg.find(substring) != std::string::npos;
}

/**
 * @brief Matcher for checking if a string starts with a prefix
 */
MATCHER_P(StartsWithPrefix, prefix, "") {
    return arg.substr(0, std::string_view(prefix).length()) == prefix;
}

/**
 * @brief Matcher for checking if a value is within range
 */
MATCHER_P2(InRange, min_val, max_val, "") {
    return arg >= min_val && arg <= max_val;
}

// =============================================================================
// Synchronization Utilities
// =============================================================================

/**
 * @brief Wait for a condition using yield-based polling with timeout
 *
 * Provides responsive waiting that checks the condition frequently
 * while being cooperative with other threads. Replaces sleep_for-based
 * polling for more deterministic test behavior.
 *
 * @tparam Predicate Callable returning bool
 * @param pred Predicate to wait for
 * @param timeout Maximum wait time (default 2 seconds)
 * @return true if condition met, false on timeout
 */
template <typename Predicate>
bool wait_for(Predicate pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

// =============================================================================
// Helper Macros
// =============================================================================

/**
 * @brief Expect that a statement throws a specific exception
 */
#define EXPECT_THROWS_WITH_MESSAGE(statement, exception_type, message_substring) \
    EXPECT_THROW({                                                                \
        try {                                                                     \
            statement;                                                            \
        } catch (const exception_type& e) {                                       \
            EXPECT_THAT(std::string(e.what()), ContainsSubstring(message_substring)); \
            throw;                                                                \
        }                                                                         \
    }, exception_type)

/**
 * @brief Skip test if a condition is not met
 */
#define SKIP_IF(condition, message) \
    if (condition) { \
        GTEST_SKIP() << message; \
    }

/**
 * @brief Assert that an expected value has a value (for std::expected)
 */
#define ASSERT_EXPECTED_OK(expected) \
    ASSERT_TRUE((expected).has_value()) << "Expected value but got error"

/**
 * @brief Expect that an expected value has a value (for std::expected)
 */
#define EXPECT_EXPECTED_OK(expected) \
    EXPECT_TRUE((expected).has_value()) << "Expected value but got error"

/**
 * @brief Assert that an expected value has an error (for std::expected)
 */
#define ASSERT_EXPECTED_ERROR(expected) \
    ASSERT_FALSE((expected).has_value()) << "Expected error but got value"

/**
 * @brief Expect that an expected value has an error (for std::expected)
 */
#define EXPECT_EXPECTED_ERROR(expected) \
    EXPECT_FALSE((expected).has_value()) << "Expected error but got value"

/**
 * @brief Assert that a Result<T> has a value (for kcenon::common::Result<T>)
 */
#define ASSERT_RESULT_OK(result) \
    ASSERT_TRUE((result).is_ok()) << "Expected value but got error"

/**
 * @brief Expect that a Result<T> has a value (for kcenon::common::Result<T>)
 */
#define EXPECT_RESULT_OK(result) \
    EXPECT_TRUE((result).is_ok()) << "Expected value but got error"

/**
 * @brief Assert that a Result<T> has an error (for kcenon::common::Result<T>)
 */
#define ASSERT_RESULT_ERROR(result) \
    ASSERT_FALSE((result).is_ok()) << "Expected error but got value"

/**
 * @brief Expect that a Result<T> has an error (for kcenon::common::Result<T>)
 */
#define EXPECT_RESULT_ERROR(result) \
    EXPECT_FALSE((result).is_ok()) << "Expected error but got value"

}  // namespace pacs::bridge::test

#endif  // PACS_BRIDGE_TEST_HELPERS_H
