#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "KafkaConsumer.h"
#include "KafkaProducer.h"

class DeliveryConsumer {
public:
    DeliveryConsumer() = default;
    ~DeliveryConsumer();

    DeliveryConsumer(const DeliveryConsumer&) = delete;
    DeliveryConsumer& operator=(const DeliveryConsumer&) = delete;

    bool Start(const im::mq::KafkaOptions& options,
               const std::string& consumer_name, std::string& error);
    void Stop();

private:
    void Run();
    void HandleMessage(RdKafka::Message& message);

    std::atomic<bool> _running{false};
    std::thread _worker;
    im::mq::KafkaConsumer _consumer;
    im::mq::KafkaProducer _dlq;
    std::string _consumer_name;
};
