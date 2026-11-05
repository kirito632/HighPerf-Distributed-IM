#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "KafkaProducer.h"

class OutboxPublisher {
public:
    OutboxPublisher() = default;
    ~OutboxPublisher();

    OutboxPublisher(const OutboxPublisher&) = delete;
    OutboxPublisher& operator=(const OutboxPublisher&) = delete;

    bool Start(const im::mq::KafkaOptions& options, std::string& error);
    void Stop();

private:
    void Run();

    std::atomic<bool> _running{false};
    std::thread _worker;
    im::mq::KafkaProducer _producer;
};
