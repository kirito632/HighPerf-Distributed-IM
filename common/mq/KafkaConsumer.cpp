#include "KafkaConsumer.h"

#include <iostream>

namespace im::mq {

KafkaConsumer::~KafkaConsumer() { Close(); }

bool KafkaConsumer::Init(const KafkaOptions& options, std::string& error) {
    Close();
    std::string errstr;
    conf_.reset(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    if (!conf_) {
        error = "failed to create librdkafka consumer config";
        return false;
    }

    auto set = [&](const std::string& key, const std::string& value) {
        if (conf_->set(key, value, errstr) != RdKafka::Conf::CONF_OK) {
            error = "kafka consumer config " + key + ": " + errstr;
            return false;
        }
        return true;
    };

    if (!set("bootstrap.servers", options.bootstrap_servers) ||
        !set("client.id", options.client_id) ||
        !set("group.id", options.group_id) ||
        !set("enable.auto.commit", "false") ||
        !set("auto.offset.reset", "earliest") ||
        !set("enable.partition.eof", "false") ||
        !set("session.timeout.ms", "6000")) {
        return false;
    }

    consumer_.reset(RdKafka::KafkaConsumer::create(conf_.get(), errstr));
    if (!consumer_) {
        error = "failed to create consumer: " + errstr;
        return false;
    }
    return true;
}

bool KafkaConsumer::Subscribe(const std::vector<std::string>& topics,
                              std::string& error) {
    if (!consumer_) {
        error = "consumer is not initialized";
        return false;
    }
    const RdKafka::ErrorCode code = consumer_->subscribe(topics);
    if (code != RdKafka::ERR_NO_ERROR) {
        error = "subscribe failed: " + RdKafka::err2str(code);
        return false;
    }
    return true;
}

std::unique_ptr<RdKafka::Message> KafkaConsumer::Consume(int timeout_ms) {
    if (!consumer_) {
        return nullptr;
    }
    return std::unique_ptr<RdKafka::Message>(consumer_->consume(timeout_ms));
}

bool KafkaConsumer::Commit(const RdKafka::Message& message,
                           std::string& error) {
    if (!consumer_) {
        error = "consumer is not initialized";
        return false;
    }
    auto* mutable_message = const_cast<RdKafka::Message*>(&message);
    const RdKafka::ErrorCode code = consumer_->commitSync(mutable_message);
    if (code != RdKafka::ERR_NO_ERROR) {
        error = "commit failed: " + RdKafka::err2str(code);
        return false;
    }
    return true;
}

void KafkaConsumer::Close() {
    if (consumer_) {
        consumer_->close();
        consumer_.reset();
    }
    conf_.reset();
}

}  // namespace im::mq
