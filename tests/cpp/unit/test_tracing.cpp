#include "Tracing.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

TEST(TracingTest, DisabledModeDoesNotRequireExporter) {
    std::string error;
    EXPECT_TRUE(im::tracing::Init(false, "", "test", error));
    EXPECT_FALSE(im::tracing::StartSpan("test"));
}

#ifdef IM_WITH_OPENTELEMETRY
TEST(TracingTest, ValidTraceparentKeepsTraceIdAndCreatesChildContext) {
    std::string error;
    const char* endpoint = std::getenv("IM_OTEL_ENDPOINT");
    ASSERT_TRUE(im::tracing::Init(true,
                                  endpoint ? endpoint : "http://127.0.0.1:1/v1/traces",
                                  "tracing-test", error)) << error;
    auto span = im::tracing::StartSpan(
        "child", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
    ASSERT_TRUE(span);
    const auto child = span->Traceparent();
    EXPECT_EQ(child.substr(0, 36), "00-4bf92f3577b34da6a3ce929d0e0e4736-");
    EXPECT_NE(child, "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
    span->End();
    im::tracing::Shutdown();
}
#endif
