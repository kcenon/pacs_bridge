/**
 * @file monitoring_trace_validation_test.cpp
 * @brief Unit tests for distributed tracing validation
 *
 * Tests to ensure trace context propagation, span creation,
 * and trace correlation are working correctly.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/145
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <chrono>
#include <random>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::tracing::test {
namespace {

using namespace ::testing;
using namespace std::chrono;

// =============================================================================
// Mock Trace Context
// =============================================================================

struct TraceContext {
    std::string trace_id;      // 32 hex characters (128-bit)
    std::string span_id;       // 16 hex characters (64-bit)
    std::string parent_span_id;
    uint8_t trace_flags = 0;   // 0x01 = sampled

    bool is_valid() const {
        return !trace_id.empty() && !span_id.empty() &&
               trace_id.length() == 32 && span_id.length() == 16;
    }

    // W3C traceparent format: version-trace_id-parent_id-flags
    std::string to_traceparent() const {
        char flags_hex[3];
        snprintf(flags_hex, sizeof(flags_hex), "%02x", trace_flags);
        return "00-" + trace_id + "-" + span_id + "-" + std::string(flags_hex);
    }

    static std::optional<TraceContext> from_traceparent(const std::string& traceparent) {
        std::regex pattern("^([0-9a-f]{2})-([0-9a-f]{32})-([0-9a-f]{16})-([0-9a-f]{2})$");
        std::smatch matches;

        if (!std::regex_match(traceparent, matches, pattern)) {
            return std::nullopt;
        }

        TraceContext ctx;
        ctx.trace_id = matches[2].str();
        ctx.span_id = matches[3].str();
        ctx.trace_flags = static_cast<uint8_t>(std::stoul(matches[4].str(), nullptr, 16));
        return ctx;
    }
};

// =============================================================================
// Mock Span
// =============================================================================

class MockSpan {
public:
    MockSpan(const std::string& name, const TraceContext& context)
        : name_(name), context_(context), start_time_(high_resolution_clock::now()) {}

    void set_attribute(const std::string& key, const std::string& value) {
        attributes_[key] = value;
    }

    void set_status(bool ok, const std::string& message = "") {
        status_ok_ = ok;
        status_message_ = message;
    }

    void add_event(const std::string& name) {
        events_.push_back({name, high_resolution_clock::now()});
    }

    void end() {
        end_time_ = high_resolution_clock::now();
        ended_ = true;
    }

    const std::string& name() const { return name_; }
    const TraceContext& context() const { return context_; }
    bool is_ended() const { return ended_; }
    bool status_ok() const { return status_ok_; }
    const std::string& status_message() const { return status_message_; }

    double duration_ms() const {
        if (!ended_) return 0;
        return duration_cast<microseconds>(end_time_ - start_time_).count() / 1000.0;
    }

    const std::map<std::string, std::string>& attributes() const { return attributes_; }
    size_t event_count() const { return events_.size(); }

private:
    std::string name_;
    TraceContext context_;
    high_resolution_clock::time_point start_time_;
    high_resolution_clock::time_point end_time_;
    bool ended_ = false;
    bool status_ok_ = true;
    std::string status_message_;
    std::map<std::string, std::string> attributes_;
    std::vector<std::pair<std::string, high_resolution_clock::time_point>> events_;
};

// =============================================================================
// Mock Tracer
// =============================================================================

class MockTracer {
public:
    TraceContext create_context() {
        TraceContext ctx;
        ctx.trace_id = generate_hex_id(32);
        ctx.span_id = generate_hex_id(16);
        ctx.trace_flags = 0x01;  // sampled
        return ctx;
    }

    std::shared_ptr<MockSpan> start_span(const std::string& name,
                                          const TraceContext& parent = {}) {
        TraceContext ctx;
        if (parent.is_valid()) {
            ctx.trace_id = parent.trace_id;
            ctx.parent_span_id = parent.span_id;
            ctx.trace_flags = parent.trace_flags;
        } else {
            ctx.trace_id = generate_hex_id(32);
            ctx.trace_flags = 0x01;
        }
        ctx.span_id = generate_hex_id(16);

        auto span = std::make_shared<MockSpan>(name, ctx);
        spans_.push_back(span);
        return span;
    }

    const std::vector<std::shared_ptr<MockSpan>>& spans() const { return spans_; }

    void clear() { spans_.clear(); }

private:
    std::string generate_hex_id(size_t length) {
        static const char hex_chars[] = "0123456789abcdef";
        std::string result;
        result.reserve(length);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 15);

        for (size_t i = 0; i < length; ++i) {
            result += hex_chars[dist(gen)];
        }
        return result;
    }

    std::vector<std::shared_ptr<MockSpan>> spans_;
};

// =============================================================================
// Traced Message Processor
// =============================================================================

class TracedMessageProcessor {
public:
    explicit TracedMessageProcessor(MockTracer& tracer)
        : tracer_(tracer), parser_() {}

    bool process_message(const std::string& raw_message,
                        const std::string& incoming_traceparent = "") {
        // Parse incoming trace context
        TraceContext parent_ctx;
        if (!incoming_traceparent.empty()) {
            auto parsed = TraceContext::from_traceparent(incoming_traceparent);
            if (parsed.has_value()) {
                parent_ctx = *parsed;
            }
        }

        // Start span for message processing
        auto span = tracer_.start_span("process_hl7_message", parent_ctx);
        span->add_event("message_received");

        // Parse message
        auto msg = parser_.parse(raw_message);

        if (msg.is_ok()) {
            span->set_attribute("message_type", hl7::to_string(msg.value().type()));
            span->set_attribute("trigger_event", std::string(msg.value().trigger_event()));
            span->set_attribute("message_control_id", get_message_control_id(msg.value()));
            span->add_event("message_parsed");
            span->set_status(true);
        } else {
            span->set_attribute("error", "parse_failed");
            span->add_event("parse_error");
            span->set_status(false, "Failed to parse HL7 message");
        }

        span->end();
        return msg.is_ok();
    }

    std::string get_current_traceparent() const {
        if (tracer_.spans().empty()) return "";
        return tracer_.spans().back()->context().to_traceparent();
    }

private:
    std::string get_message_control_id(const hl7::hl7_message& msg) {
        auto msh = msg.segment("MSH");
        if (!msh) return "";
        return std::string(msh->field_value(10));
    }

    MockTracer& tracer_;
    hl7::hl7_parser parser_;
};

// =============================================================================
// Test Fixture
// =============================================================================

class MonitoringTraceValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        tracer_ = std::make_unique<MockTracer>();
        processor_ = std::make_unique<TracedMessageProcessor>(*tracer_);
    }

    std::unique_ptr<MockTracer> tracer_;
    std::unique_ptr<TracedMessageProcessor> processor_;

    std::string create_test_message() {
        return
            "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
            "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r";
    }
};

// =============================================================================
// Trace Context Generation Tests
// =============================================================================

TEST_F(MonitoringTraceValidationTest, GenerateValidTraceContext) {
    TraceContext ctx = tracer_->create_context();

    EXPECT_TRUE(ctx.is_valid());
    EXPECT_EQ(ctx.trace_id.length(), 32);
    EXPECT_EQ(ctx.span_id.length(), 16);
    EXPECT_TRUE(ctx.parent_span_id.empty());
}

TEST_F(MonitoringTraceValidationTest, TraceIdIsHexadecimal) {
    TraceContext ctx = tracer_->create_context();

    std::regex hex_pattern("^[0-9a-f]+$");
    EXPECT_TRUE(std::regex_match(ctx.trace_id, hex_pattern));
    EXPECT_TRUE(std::regex_match(ctx.span_id, hex_pattern));
}

TEST_F(MonitoringTraceValidationTest, UniqueTraceIds) {
    std::set<std::string> trace_ids;
    std::set<std::string> span_ids;

    for (int i = 0; i < 100; ++i) {
        TraceContext ctx = tracer_->create_context();
        trace_ids.insert(ctx.trace_id);
        span_ids.insert(ctx.span_id);
    }

    // All IDs should be unique
    EXPECT_EQ(trace_ids.size(), 100);
    EXPECT_EQ(span_ids.size(), 100);
}

// =============================================================================
// W3C Traceparent Format Tests
// =============================================================================

TEST_F(MonitoringTraceValidationTest, ValidTraceparentFormat) {
    TraceContext ctx = tracer_->create_context();
    std::string traceparent = ctx.to_traceparent();

    // Format: version-trace_id-parent_id-flags
    std::regex pattern("^00-[0-9a-f]{32}-[0-9a-f]{16}-[0-9a-f]{2}$");
    EXPECT_TRUE(std::regex_match(traceparent, pattern));
}

TEST_F(MonitoringTraceValidationTest, ParseTraceparent) {
    std::string traceparent = "00-0123456789abcdef0123456789abcdef-0123456789abcdef-01";
    auto ctx = TraceContext::from_traceparent(traceparent);

    ASSERT_TRUE(ctx.has_value());
    EXPECT_EQ(ctx->trace_id, "0123456789abcdef0123456789abcdef");
    EXPECT_EQ(ctx->span_id, "0123456789abcdef");
    EXPECT_EQ(ctx->trace_flags, 0x01);
}

TEST_F(MonitoringTraceValidationTest, RejectInvalidTraceparent) {
    std::vector<std::string> invalid_traceparents = {
        "",
        "invalid",
        "00-tooshort-0123456789abcdef-01",
        "00-0123456789abcdef0123456789abcdef-short-01",
        "00-ZZZZ456789abcdef0123456789abcdef-0123456789abcdef-01",  // Non-hex
    };

    for (const auto& tp : invalid_traceparents) {
        auto ctx = TraceContext::from_traceparent(tp);
        EXPECT_FALSE(ctx.has_value()) << "Should reject: " << tp;
    }
}

TEST_F(MonitoringTraceValidationTest, AcceptFutureVersionTraceparent) {
    // W3C Trace Context spec recommends accepting unknown versions for forward compatibility
    // This includes both future versions like 99 and the reserved ff version
    auto ctx99 = TraceContext::from_traceparent("99-0123456789abcdef0123456789abcdef-0123456789abcdef-01");
    EXPECT_TRUE(ctx99.has_value()) << "Should accept version 99 for forward compatibility";

    auto ctxff = TraceContext::from_traceparent("ff-0123456789abcdef0123456789abcdef-0123456789abcdef-01");
    EXPECT_TRUE(ctxff.has_value()) << "Should accept version ff for forward compatibility";
}

TEST_F(MonitoringTraceValidationTest, RoundTripTraceparent) {
    TraceContext original = tracer_->create_context();
    std::string traceparent = original.to_traceparent();
    auto parsed = TraceContext::from_traceparent(traceparent);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->trace_id, original.trace_id);
    EXPECT_EQ(parsed->span_id, original.span_id);
    EXPECT_EQ(parsed->trace_flags, original.trace_flags);
}

// =============================================================================
// Span Creation Tests
// =============================================================================

TEST_F(MonitoringTraceValidationTest, CreateRootSpan) {
    auto span = tracer_->start_span("test_operation");

    EXPECT_TRUE(span->context().is_valid());
    EXPECT_FALSE(span->is_ended());
    EXPECT_EQ(span->name(), "test_operation");
}

TEST_F(MonitoringTraceValidationTest, CreateChildSpan) {
    auto parent = tracer_->start_span("parent_operation");
    auto child = tracer_->start_span("child_operation", parent->context());

    // Child should inherit trace_id from parent
    EXPECT_EQ(child->context().trace_id, parent->context().trace_id);
    // Child should have different span_id
    EXPECT_NE(child->context().span_id, parent->context().span_id);
    // Child's parent_span_id should be parent's span_id
    EXPECT_EQ(child->context().parent_span_id, parent->context().span_id);
}

TEST_F(MonitoringTraceValidationTest, SpanEndSetsEndTime) {
    auto span = tracer_->start_span("test_operation");

    EXPECT_FALSE(span->is_ended());
    EXPECT_EQ(span->duration_ms(), 0);

    std::this_thread::sleep_for(milliseconds(10));
    span->end();

    EXPECT_TRUE(span->is_ended());
    EXPECT_GT(span->duration_ms(), 0);
}

// =============================================================================
// Span Attributes Tests
// =============================================================================

TEST_F(MonitoringTraceValidationTest, SpanAttributes) {
    auto span = tracer_->start_span("test_operation");
    span->set_attribute("key1", "value1");
    span->set_attribute("key2", "value2");
    span->end();

    const auto& attrs = span->attributes();
    EXPECT_EQ(attrs.size(), 2);
    EXPECT_EQ(attrs.at("key1"), "value1");
    EXPECT_EQ(attrs.at("key2"), "value2");
}

TEST_F(MonitoringTraceValidationTest, SpanStatus) {
    auto success_span = tracer_->start_span("success_op");
    success_span->set_status(true);
    success_span->end();
    EXPECT_TRUE(success_span->status_ok());

    auto error_span = tracer_->start_span("error_op");
    error_span->set_status(false, "Something went wrong");
    error_span->end();
    EXPECT_FALSE(error_span->status_ok());
    EXPECT_EQ(error_span->status_message(), "Something went wrong");
}

TEST_F(MonitoringTraceValidationTest, SpanEvents) {
    auto span = tracer_->start_span("test_operation");
    span->add_event("event1");
    span->add_event("event2");
    span->add_event("event3");
    span->end();

    EXPECT_EQ(span->event_count(), 3);
}

// =============================================================================
// Message Processing Tracing Tests
// =============================================================================

TEST_F(MonitoringTraceValidationTest, TracedMessageProcessing) {
    processor_->process_message(create_test_message());

    EXPECT_EQ(tracer_->spans().size(), 1);

    auto span = tracer_->spans()[0];
    EXPECT_EQ(span->name(), "process_hl7_message");
    EXPECT_TRUE(span->is_ended());
    EXPECT_TRUE(span->status_ok());

    const auto& attrs = span->attributes();
    EXPECT_EQ(attrs.at("message_type"), "ADT");
    EXPECT_EQ(attrs.at("trigger_event"), "A01");
}

TEST_F(MonitoringTraceValidationTest, TracedMessageProcessingWithError) {
    processor_->process_message("INVALID MESSAGE");

    EXPECT_EQ(tracer_->spans().size(), 1);

    auto span = tracer_->spans()[0];
    EXPECT_FALSE(span->status_ok());
    EXPECT_FALSE(span->status_message().empty());

    const auto& attrs = span->attributes();
    EXPECT_EQ(attrs.at("error"), "parse_failed");
}

TEST_F(MonitoringTraceValidationTest, TraceContextPropagation) {
    std::string incoming_traceparent =
        "00-0123456789abcdef0123456789abcdef-fedcba9876543210-01";

    processor_->process_message(create_test_message(), incoming_traceparent);

    auto span = tracer_->spans()[0];
    // Should inherit trace_id from incoming context
    EXPECT_EQ(span->context().trace_id, "0123456789abcdef0123456789abcdef");
    // Parent span should be set
    EXPECT_EQ(span->context().parent_span_id, "fedcba9876543210");
}

// =============================================================================
// Trace Correlation Tests
// =============================================================================

TEST_F(MonitoringTraceValidationTest, MultipleMessagesHaveDifferentTraces) {
    processor_->process_message(create_test_message());
    processor_->process_message(create_test_message());
    processor_->process_message(create_test_message());

    EXPECT_EQ(tracer_->spans().size(), 3);

    std::set<std::string> trace_ids;
    for (const auto& span : tracer_->spans()) {
        trace_ids.insert(span->context().trace_id);
    }

    // Without incoming context, each message should have unique trace
    EXPECT_EQ(trace_ids.size(), 3);
}

TEST_F(MonitoringTraceValidationTest, SameTraceForCorrelatedMessages) {
    std::string traceparent =
        "00-11111111111111111111111111111111-aaaaaaaaaaaaaaaa-01";

    processor_->process_message(create_test_message(), traceparent);
    processor_->process_message(create_test_message(), traceparent);
    processor_->process_message(create_test_message(), traceparent);

    // All should have same trace_id
    for (const auto& span : tracer_->spans()) {
        EXPECT_EQ(span->context().trace_id, "11111111111111111111111111111111");
    }
}

// =============================================================================
// Trace Export Format Tests
// =============================================================================

TEST_F(MonitoringTraceValidationTest, ExportTraceparentAfterProcessing) {
    processor_->process_message(create_test_message());

    std::string traceparent = processor_->get_current_traceparent();
    EXPECT_FALSE(traceparent.empty());

    // Should be valid W3C format
    auto parsed = TraceContext::from_traceparent(traceparent);
    EXPECT_TRUE(parsed.has_value());
}

// =============================================================================
// High Volume Tracing Tests
// =============================================================================

TEST_F(MonitoringTraceValidationTest, HighVolumeTracing) {
    const int count = 100;

    for (int i = 0; i < count; ++i) {
        processor_->process_message(create_test_message());
    }

    EXPECT_EQ(tracer_->spans().size(), count);

    // All spans should be properly ended
    for (const auto& span : tracer_->spans()) {
        EXPECT_TRUE(span->is_ended());
        EXPECT_TRUE(span->context().is_valid());
    }
}

}  // namespace
}  // namespace pacs::bridge::tracing::test
