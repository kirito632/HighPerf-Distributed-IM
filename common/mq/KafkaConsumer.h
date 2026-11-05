#pragma once

#include <memory>
#include <string>
#include <vector>

#include <librdkafka/rdkafkacpp.h>

#include "KafkaProducer.h"

namespace im::mq {

class KafkaConsumer {
public:
    KafkaConsumer() = default;
    ~KafkaConsumer();
    KafkaConsumer(const KafkaConsumer&) = delete;
    KafkaConsumer& operator=(const KafkaConsumer&) = delete;

    bool Init(const KafkaOptions& options, std::string& error);
    bool Subscribe(const std::vector<std::string>& topics, std::string& error);

    // Returns nullptr on timeout; caller owns the returned message.
    std::unique_ptr<RdKafka::Message> Consume(int timeout_ms);

    bool Commit(const RdKafka::Message& message, std::string& error);
    void Close();

private:
    std::unique_ptr<RdKafka::Conf> conf_;
    std::unique_ptr<RdKafka::KafkaConsumer> consumer_;
};

}  // namespace im::mq
