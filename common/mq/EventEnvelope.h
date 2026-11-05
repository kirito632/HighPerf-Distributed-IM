#pragma once

#include <cstdint>
#include <string>

#include <json/json.h>

namespace im::mq {

// Transport-neutral event envelope used by Outbox and consumers.
struct EventEnvelope {
    std::string event_id;
    std::string type;
    std::int64_t occurred_at_ms = 0;
    Json::Value payload;

    std::string ToJson() const;
    static bool FromJson(const std::string& text, EventEnvelope& out,
                         std::string& error);

    static EventEnvelope Create(const std::string& type,
                                const Json::Value& payload);
};

}  // namespace im::mq
