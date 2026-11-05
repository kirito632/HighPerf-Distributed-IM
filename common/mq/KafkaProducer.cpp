#include "KafkaProducer.h"

#include <iostream>

namespace im::mq {

KafkaProducer::~KafkaProducer() { Close(); }

bool KafkaProducer::Init(const KafkaOptions& options, std::string& error) {
    Close();
    std::string errstr;
    conf_.reset(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    if (!conf_) {
        error = "failed to create librdkafka producer config";
        return false;
    }

    auto set = [&](const std::string& key, const std::string& value) {
        if (conf_->set(key, value, errstr) != RdKafka::Conf::CONF_OK) {
            error = "kafka producer config " + key + ": " + errstr;
            return false;
        }
        return true;
    };

    if (!set("bootstrap.servers", options.bootstrap_servers) ||
        !set("client.id", options.client_id) ||
        !set("acks", "all") ||
        !set("enable.idempotence", "true") ||
        !set("message.timeout.ms", "5000") ||
        !set("retries", "5")) {
        return false;
    }

    producer_.reset(RdKafka::Producer::create(conf_.get(), errstr));
    if (!producer_) {
        error = "failed to create producer: " + errstr;
        return false;
    }
    return true;
}

bool KafkaProducer::Produce(const std::string& topic, const std::string& key,
                            const std::string& value, std::string& error,
                            const std::vector<std::pair<std::string, std::string>>& headers) {
    if (!producer_) {
        error = "producer is not initialized";
        return false;
    }

    RdKafka::Headers* kafka_headers = nullptr;
    if (!headers.empty()) {
        kafka_headers = RdKafka::Headers::create();
        if (!kafka_headers) {
            error = "failed to create Kafka headers";
            return false;
        }
        for (const auto& header : headers) {
            if (kafka_headers->add(header.first, header.second) != RdKafka::ERR_NO_ERROR) {
                delete kafka_headers;
                error = "failed to add Kafka header: " + header.first;
                return false;
            }
        }
    }

    RdKafka::ErrorCode code = producer_->produce(
        topic, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
        const_cast<char*>(value.data()), value.size(), key.data(), key.size(),
        0, kafka_headers, nullptr);
    if (code != RdKafka::ERR_NO_ERROR) delete kafka_headers;
    producer_->poll(0);

    if (code != RdKafka::ERR_NO_ERROR) {
        error = "produce failed: " + RdKafka::err2str(code);
        return false;
    }
    return true;
}

bool KafkaProducer::Flush(int timeout_ms, std::string& error) {
    if (!producer_) {
        error = "producer is not initialized";
        return false;
    }
    const RdKafka::ErrorCode code = producer_->flush(timeout_ms);
    if (code != RdKafka::ERR_NO_ERROR) {
        error = "flush failed: " + RdKafka::err2str(code);
        return false;
    }
    return true;
}

bool KafkaProducer::Healthy(int timeout_ms) const {
    if (!producer_) {
        return false;
    }
    RdKafka::Metadata* metadata = nullptr;
    const RdKafka::ErrorCode code =
        producer_->metadata(true, nullptr, &metadata, timeout_ms);
    delete metadata;
    return code == RdKafka::ERR_NO_ERROR;
}

void KafkaProducer::Close() {
    if (producer_) {
        producer_->flush(2000);
        producer_.reset();
    }
    conf_.reset();
}

}  // namespace im::mq
