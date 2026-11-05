#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <librdkafka/rdkafkacpp.h>

namespace im::mq {

struct KafkaOptions {
    std::string bootstrap_servers = "127.0.0.1:19092";
    std::string client_id = "im-service";
    std::string group_id = "im-consumer";
};

class KafkaProducer {
public:
    KafkaProducer() = default;
    ~KafkaProducer();
    KafkaProducer(const KafkaProducer&) = delete;
    KafkaProducer& operator=(const KafkaProducer&) = delete;

    bool Init(const KafkaOptions& options, std::string& error);
    bool Produce(const std::string& topic, const std::string& key,
                 const std::string& value, std::string& error);
    bool Flush(int timeout_ms, std::string& error);
    bool Healthy(int timeout_ms) const;
    void Close();

private:
    std::unique_ptr<RdKafka::Conf> conf_;
    std::unique_ptr<RdKafka::Producer> producer_;
};

}  // namespace im::mq
