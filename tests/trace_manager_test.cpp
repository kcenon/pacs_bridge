/**
 * @file trace_manager_test.cpp
 * @brief Unit tests for distributed tracing infrastructure
 */

#include <gtest/gtest.h>
#include "pacs/bridge/tracing/trace_manager.h"
#include "pacs/bridge/tracing/span_wrapper.h"
#include "pacs/bridge/tracing/tracing_types.h"

namespace pacs::bridge::tracing::test {

class TraceManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize with tracing enabled
        tracing_config config;
        config.enabled = true;
        config.service_name = "test_service";
        config.sampling_rate = 1.0;

        auto result = trace_manager::instance().initialize(config);
        ASSERT_TRUE(result.has_value());
    }

    void TearDown() override {
        trace_manager::instance().shutdown();
    }
};

TEST_F(TraceManagerTest, InitializationWorks) {
    EXPECT_TRUE(trace_manager::instance().is_enabled());
    EXPECT_EQ(trace_manager::instance().config().service_name, "test_service");
}

TEST_F(TraceManagerTest, CreateRootSpan) {
    auto span = trace_manager::instance().start_span("test_operation");

    EXPECT_TRUE(span.is_valid());
    EXPECT_TRUE(span.is_active());
    EXPECT_EQ(span.name(), "test_operation");

    const auto& ctx = span.context();
    EXPECT_FALSE(ctx.trace_id.empty());
    EXPECT_FALSE(ctx.span_id.empty());
    EXPECT_FALSE(ctx.parent_span_id.has_value());

    span.end();
    EXPECT_FALSE(span.is_active());
}

TEST_F(TraceManagerTest, CreateChildSpan) {
    auto parent = trace_manager::instance().start_span("parent");
    auto child = parent.start_child("child");

    EXPECT_TRUE(child.is_valid());
    EXPECT_EQ(child.name(), "child");

    // Child should have same trace_id as parent
    EXPECT_EQ(child.context().trace_id, parent.context().trace_id);

    // Child should have parent's span_id as parent_span_id
    EXPECT_TRUE(child.context().parent_span_id.has_value());
    EXPECT_EQ(child.context().parent_span_id.value(), parent.context().span_id);

    // But different span_id
    EXPECT_NE(child.context().span_id, parent.context().span_id);
}

TEST_F(TraceManagerTest, SpanAttributes) {
    auto span = trace_manager::instance().start_span("test");

    span.set_attribute("string_attr", "value")
        .set_attribute("int_attr", int64_t{42})
        .set_attribute("double_attr", 3.14)
        .set_attribute("bool_attr", true);

    EXPECT_TRUE(span.is_active());
}

TEST_F(TraceManagerTest, SpanStatus) {
    auto span = trace_manager::instance().start_span("test");

    span.set_status(span_status::error, "something went wrong");
    EXPECT_TRUE(span.is_active());
}

TEST_F(TraceManagerTest, SpanEvents) {
    auto span = trace_manager::instance().start_span("test");

    span.add_event("event1");
    span.add_event("event2", {{"key", "value"}});

    EXPECT_TRUE(span.is_active());
}

TEST_F(TraceManagerTest, RAIISpanManagement) {
    {
        auto span = trace_manager::instance().start_span("scoped");
        EXPECT_TRUE(span.is_active());
    }  // Span should end here
}

TEST_F(TraceManagerTest, TraceparentFormat) {
    auto span = trace_manager::instance().start_span("test");

    std::string traceparent = span.get_traceparent();

    // Format: 00-{trace-id}-{span-id}-{flags}
    EXPECT_FALSE(traceparent.empty());
    EXPECT_EQ(traceparent.substr(0, 3), "00-");

    // Should have 4 parts separated by '-'
    int dash_count = 0;
    for (char c : traceparent) {
        if (c == '-') dash_count++;
    }
    EXPECT_EQ(dash_count, 3);
}

TEST_F(TraceManagerTest, ParseTraceparent) {
    std::string traceparent = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01";

    auto ctx = trace_context::from_traceparent(traceparent);

    ASSERT_TRUE(ctx.has_value());
    EXPECT_EQ(ctx->trace_id, "0af7651916cd43dd8448eb211c80319c");
    EXPECT_TRUE(ctx->parent_span_id.has_value());
    EXPECT_EQ(ctx->parent_span_id.value(), "b7ad6b7169203331");
    EXPECT_EQ(ctx->trace_flags, 0x01);
}

TEST_F(TraceManagerTest, InvalidTraceparent) {
    EXPECT_FALSE(trace_context::from_traceparent("").has_value());
    EXPECT_FALSE(trace_context::from_traceparent("invalid").has_value());
    EXPECT_FALSE(trace_context::from_traceparent("00-abc-def-01").has_value());
}

TEST_F(TraceManagerTest, SpanFromTraceparent) {
    std::string traceparent = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01";

    auto span = trace_manager::instance().start_span_from_traceparent(
        "continued_operation", traceparent);

    EXPECT_TRUE(span.is_valid());
    EXPECT_EQ(span.context().trace_id, "0af7651916cd43dd8448eb211c80319c");
    EXPECT_TRUE(span.context().parent_span_id.has_value());
}

TEST_F(TraceManagerTest, Statistics) {
    auto stats_before = trace_manager::instance().get_statistics();

    {
        auto span1 = trace_manager::instance().start_span("op1");
        auto span2 = trace_manager::instance().start_span("op2");
    }

    auto stats_after = trace_manager::instance().get_statistics();
    EXPECT_GE(stats_after.spans_created, stats_before.spans_created + 2);
}

class TraceDisabledTest : public ::testing::Test {
protected:
    void SetUp() override {
        tracing_config config;
        config.enabled = false;

        auto result = trace_manager::instance().initialize(config);
        ASSERT_TRUE(result.has_value());
    }

    void TearDown() override {
        trace_manager::instance().shutdown();
    }
};

TEST_F(TraceDisabledTest, DisabledTracingReturnsNoOpSpans) {
    auto span = trace_manager::instance().start_span("test");

    // No-op span should not be valid
    EXPECT_FALSE(span.is_valid());

    // Operations should still work (no-op)
    span.set_attribute("key", "value");
    span.set_status(span_status::ok);
    span.end();
}

}  // namespace pacs::bridge::tracing::test
