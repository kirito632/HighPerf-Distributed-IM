#include <gtest/gtest.h>

#include "EventEnvelope.h"

TEST(EventEnvelope, RoundTrip) {
    Json::Value payload(Json::objectValue);
    payload["server_msg_id"] = Json::Int64(42);
    payload["content"] = "hello";

    const auto envelope = im::mq::EventEnvelope::Create("MessageCreated", payload);
    const std::string encoded = envelope.ToJson();

    im::mq::EventEnvelope decoded;
    std::string error;
    ASSERT_TRUE(im::mq::EventEnvelope::FromJson(encoded, decoded, error)) << error;
    EXPECT_EQ(decoded.event_id, envelope.event_id);
    EXPECT_EQ(decoded.type, "MessageCreated");
    EXPECT_EQ(decoded.occurred_at_ms, envelope.occurred_at_ms);
    EXPECT_EQ(decoded.payload["server_msg_id"].asInt64(), 42);
    EXPECT_EQ(decoded.payload["content"].asString(), "hello");
}

TEST(EventEnvelope, RejectsInvalidJson) {
    im::mq::EventEnvelope decoded;
    std::string error;
    EXPECT_FALSE(im::mq::EventEnvelope::FromJson("{not-json", decoded, error));
    EXPECT_FALSE(error.empty());
}
