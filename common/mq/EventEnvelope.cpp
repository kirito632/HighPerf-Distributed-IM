#include "EventEnvelope.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <sstream>

namespace im::mq {
namespace {

std::string MakeEventId() {
    static std::atomic<std::uint64_t> seq{0};
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    std::ostringstream oss;
    oss << "evt-" << micros << "-" << ++seq;
    return oss.str();
}

std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

EventEnvelope EventEnvelope::Create(const std::string& type,
                                    const Json::Value& payload) {
    EventEnvelope envelope;
    envelope.event_id = MakeEventId();
    envelope.type = type;
    envelope.occurred_at_ms = NowMs();
    envelope.payload = payload;
    return envelope;
}

std::string EventEnvelope::ToJson() const {
    Json::Value root(Json::objectValue);
    root["event_id"] = event_id;
    root["type"] = type;
    root["occurred_at_ms"] = Json::Int64(occurred_at_ms);
    root["payload"] = payload;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, root);
}

bool EventEnvelope::FromJson(const std::string& text, EventEnvelope& out,
                             std::string& error) {
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string parse_error;
    if (!reader->parse(text.data(), text.data() + text.size(), &root,
                       &parse_error)) {
        error = "invalid event envelope: " + parse_error;
        return false;
    }
    if (!root.isObject() || !root["event_id"].isString() ||
        !root["type"].isString() || !root["occurred_at_ms"].isInt64()) {
        error = "event envelope is missing required fields";
        return false;
    }
    out.event_id = root["event_id"].asString();
    out.type = root["type"].asString();
    out.occurred_at_ms = root["occurred_at_ms"].asInt64();
    out.payload = root["payload"];
    return true;
}

}  // namespace im::mq
