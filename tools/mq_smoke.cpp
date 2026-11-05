#include <cstdlib>
#include <iostream>
#include <string>

#include "EventEnvelope.h"
#include "KafkaConsumer.h"
#include "KafkaProducer.h"

namespace {

im::mq::KafkaOptions Options(const std::string& group = "im-mq-smoke") {
    im::mq::KafkaOptions options;
    const char* broker = std::getenv("KAFKA_BROKER");
    options.bootstrap_servers = broker ? broker : "127.0.0.1:19092";
    options.client_id = "im-mq-smoke";
    options.group_id = group;
    return options;
}

int Produce(const std::string& topic, const std::string& key,
            const std::string& value) {
    im::mq::KafkaProducer producer;
    std::string error;
    if (!producer.Init(Options(), error)) {
        std::cerr << error << std::endl;
        return 2;
    }
    const std::vector<std::pair<std::string, std::string>> headers = {
        {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"}};
    if (!producer.Produce(topic, key, value, error, headers) ||
        !producer.Flush(5000, error)) {
        std::cerr << error << std::endl;
        return 3;
    }
    std::cout << "produced topic=" << topic << " key=" << key << std::endl;
    return 0;
}

int Consume(const std::string& topic, const std::string& group,
            const std::string& expected) {
    im::mq::KafkaConsumer consumer;
    std::string error;
    if (!consumer.Init(Options(group), error) ||
        !consumer.Subscribe({topic}, error)) {
        std::cerr << error << std::endl;
        return 2;
    }

    for (int i = 0; i < 50; ++i) {
        auto message = consumer.Consume(200);
        if (!message) {
            continue;
        }
        if (message->err() != RdKafka::ERR_NO_ERROR) {
            continue;
        }
        const std::string payload(
            static_cast<const char*>(message->payload()), message->len());
        if (!expected.empty() && payload.find(expected) == std::string::npos) {
            continue;
        }
        const std::string key =
            message->key() ? *message->key() : std::string();
        std::string traceparent;
        if (auto* headers = message->headers()) {
            const auto header = headers->get_last("traceparent");
            if (header.err() == RdKafka::ERR_NO_ERROR && header.value()) {
                traceparent.assign(static_cast<const char*>(header.value()),
                                   header.value_size());
            }
        }
        std::cout << "consumed topic=" << message->topic_name()
                  << " key=" << key << " traceparent=" << traceparent
                  << " payload=" << payload << std::endl;
        consumer.Commit(*message, error);
        return 0;
    }
    std::cerr << "no matching message within timeout" << std::endl;
    return 4;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: mq_smoke produce <topic> <key> <value>\n"
                     "       mq_smoke consume <topic> <group> <expected>\n";
        return 1;
    }
    const std::string mode = argv[1];
    if (mode == "produce") {
        return Produce(argv[2], argv[3], argc > 4 ? argv[4] : "");
    }
    if (mode == "consume") {
        return Consume(argv[2], argv[3], argc > 4 ? argv[4] : "");
    }
    std::cerr << "unknown mode: " << mode << std::endl;
    return 1;
}
