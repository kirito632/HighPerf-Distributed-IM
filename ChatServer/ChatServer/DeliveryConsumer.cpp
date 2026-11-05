#include "DeliveryConsumer.h"

#include <chrono>
#include <iostream>

#include <json/json.h>

#include "MysqlMgr.h"
#include "UserMgr.h"
#include "CSession.h"
#include "Metrics.h"
#include "TopicNames.h"
#include "const.h"
#include "Tracing.h"

DeliveryConsumer::~DeliveryConsumer() { Stop(); }

bool DeliveryConsumer::Start(const im::mq::KafkaOptions& options,
                             const std::string& consumer_name,
                             std::string& error) {
    if (_running.load()) {
        return true;
    }
    _consumer_name = consumer_name;

    im::mq::KafkaOptions consumer_options = options;
    consumer_options.group_id = options.group_id + "-" + consumer_name;
    if (!_consumer.Init(consumer_options, error) ||
        !_consumer.Subscribe({im::mq::kMessageCreated}, error)) {
        return false;
    }

    im::mq::KafkaOptions dlq_options = options;
    dlq_options.client_id = options.client_id + "-dlq";
    if (!_dlq.Init(dlq_options, error)) {
        std::cerr << "[Delivery] DLQ producer unavailable: " << error << std::endl;
    }

    _running.store(true);
    _worker = std::thread(&DeliveryConsumer::Run, this);
    std::cout << "[Delivery] consumer started group="
              << consumer_options.group_id << std::endl;
    return true;
}

void DeliveryConsumer::Stop() {
    if (!_running.exchange(false)) {
        return;
    }
    if (_worker.joinable()) {
        _worker.join();
    }
    _consumer.Close();
    _dlq.Close();
    std::cout << "[Delivery] consumer stopped" << std::endl;
}

void DeliveryConsumer::Run() {
    while (_running.load()) {
        auto message = _consumer.Consume(500);
        if (!message) {
            continue;
        }
        if (message->err() != RdKafka::ERR_NO_ERROR) {
            continue;
        }
        HandleMessage(*message);
    }
}

void DeliveryConsumer::HandleMessage(RdKafka::Message& message) {
    std::string parent_traceparent;
    if (auto* headers = message.headers()) {
        const auto header = headers->get_last("traceparent");
        if (header.err() == RdKafka::ERR_NO_ERROR && header.value()) {
            parent_traceparent.assign(static_cast<const char*>(header.value()),
                                      header.value_size());
        }
    }
    auto span = im::tracing::StartSpan("kafka.delivery", parent_traceparent);
    if (span) {
        span->SetAttribute("messaging.system", "kafka");
        span->SetAttribute("messaging.destination.name", message.topic_name());
    }
    std::string error;
    std::string event_id;
    if (message.key()) {
        event_id = *message.key();
    }
    const std::string payload(
        static_cast<const char*>(message.payload()), message.len());

    if (event_id.empty()) {
        // Defensive fallback for producers that did not set a Kafka key.
        event_id = "payload:" + std::to_string(message.offset());
    }
    if (span) span->SetAttribute("im.event_id", event_id);

    if (MysqlMgr::GetInstance()->IsEventProcessed(event_id)) {
        im::metrics::IncCounter("im_delivery_duplicate_total", 1,
                                "Duplicate delivery events skipped");
        _consumer.Commit(message, error);
        return;
    }

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(payload, root) || !root.isObject()) {
        std::cerr << "[Delivery] malformed event id=" << event_id << std::endl;
        im::metrics::IncCounter("im_delivery_dlq_total", 1,
                                "Events written to the DLQ");
        std::vector<std::pair<std::string, std::string>> dlq_headers;
        if (span) dlq_headers.emplace_back("traceparent", span->Traceparent());
        _dlq.Produce(im::mq::kMessageDlq, event_id, payload, error, dlq_headers);
        _dlq.Flush(2000, error);
        MysqlMgr::GetInstance()->MarkEventProcessed(event_id, "Malformed", _consumer_name);
        _consumer.Commit(message, error);
        return;
    }

    const int to_uid = root.get("touid", 0).asInt();
    const long long server_msg_id =
        root.get("server_msg_id", Json::Int64(0)).asInt64();
    if (span) {
        span->SetAttribute("im.recipient.id", static_cast<std::int64_t>(to_uid));
        span->SetAttribute("im.server_msg_id", static_cast<std::int64_t>(server_msg_id));
    }
    const std::string event_type = root.get("event_type", "MessageCreated").asString();
    if (to_uid > 0 && server_msg_id > 0 &&
        !MysqlMgr::GetInstance()->IsMessageUnread(to_uid, server_msg_id)) {
        // Event is stale: message was acknowledged or removed while the
        // consumer was catching up.
        MysqlMgr::GetInstance()->MarkEventProcessed(event_id, event_type,
                                                     _consumer_name);
        _consumer.Commit(message, error);
        return;
    }
    if (to_uid > 0) {
        auto session = UserMgr::GetInstance()->GetSession(to_uid);
        if (session) {
            if (span) root["traceparent"] = span->Traceparent();
            session->Send(root.toStyledString(), ID_NOTIFY_TEXT_CHAT_MSG_REQ);
        }
    }

    if (!MysqlMgr::GetInstance()->MarkEventProcessed(event_id, event_type,
                                                     _consumer_name)) {
        std::cerr << "[Delivery] failed to persist processed event id="
                  << event_id << ", will retry" << std::endl;
        return;
    }

    im::metrics::IncCounter("im_delivery_processed_total", 1,
                            "Events processed by the delivery consumer");

    if (!_consumer.Commit(message, error)) {
        std::cerr << "[Delivery] commit failed id=" << event_id
                  << " err=" << error << std::endl;
    }
}
