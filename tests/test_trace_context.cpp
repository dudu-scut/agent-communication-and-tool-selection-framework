#include <gtest/gtest.h>
#include <thread>
#include "agent_rpc/common/trace_context.h"

using namespace agent_rpc::common;

TEST(TraceContextTest, GeneratesUniqueTraceIds) {
    TraceContext ctx1("user_a", "");
    TraceContext ctx2("user_b", "");

    EXPECT_NE(ctx1.traceId(), ctx2.traceId());
    EXPECT_FALSE(ctx1.traceId().empty());
}

TEST(TraceContextTest, SpanStackPushPop) {
    TraceContext ctx("user_a", "");

    ctx.startSpan("router", "router");
    ctx.startSpan("agent_call", "agent_call");

    EXPECT_EQ(ctx.currentDepth(), 2);

    ctx.endSpan();
    EXPECT_EQ(ctx.currentDepth(), 1);

    ctx.endSpan();
    EXPECT_EQ(ctx.currentDepth(), 0);
}

TEST(TraceContextTest, EndSpanRecordsDuration) {
    TraceContext ctx("user_a", "");
    ctx.startSpan("agent_call", "agent_call");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ctx.endSpan();

    auto spans = ctx.completedSpans();
    ASSERT_EQ(spans.size(), 1);
    EXPECT_GE(spans[0].duration_ms, 5); // at least 5ms
    EXPECT_EQ(spans[0].component, "agent_call");
}

TEST(TraceContextTest, ThreadLocalIsolation) {
    TraceContext::init("main_user", "");

    std::string child_trace_id;
    std::thread([&]() {
        TraceContext::init("child_user", "");
        child_trace_id = TraceContext::current()->traceId();
    }).join();

    EXPECT_NE(TraceContext::current()->traceId(), child_trace_id);
}

TEST(TraceContextTest, GenerateTraceSummary) {
    TraceContext::init("user_a", "");
    auto* ctx = TraceContext::current();

    ctx->startSpan("router", "router");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ctx->endSpan();

    ctx->startSpan("agent_call", "agent_call");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ctx->endSpan();

    std::string summary = ctx->traceSummary();
    EXPECT_NE(summary.find("router"), std::string::npos);
    EXPECT_NE(summary.find("agent_call"), std::string::npos);
    EXPECT_NE(summary.find("ms"), std::string::npos);
}
