#include "OutboxPublisher.h"

#include <chrono>
#include <iostream>
#include <vector>

#include "MysqlDao.h"
#include "MysqlMgr.h"
#include "Metrics.h"
#include "TopicNames.h"

OutboxPublisher::~OutboxPublisher() { Stop(); }

bool OutboxPublisher::Start(const im::mq::KafkaOptions& options,
                            std::string& error) {
    if (_running.load()) {
        return true;
    }
    if (!_producer.Init(options, error)) {
        return false;
    }
    _running.store(true);
    _worker = std::thread(&OutboxPublisher::Run, this);
    std::cout << "[Outbox] publisher started topic="
              << im::mq::kMessageCreated << std::endl;
    return true;
}

void OutboxPublisher::Stop() {
    if (!_running.exchange(false)) {
        return;
    }
    if (_worker.joinable()) {
        _worker.join();
    }
    _producer.Close();
    std::cout << "[Outbox] publisher stopped" << std::endl;
}

void OutboxPublisher::Run() {
    while (_running.load()) {
        std::vector<MysqlDao::OutboxRow> rows;
        std::string error;
        if (!MysqlMgr::GetInstance()->GetPendingOutbox(50, rows)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        if (rows.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            continue;
        }

        std::vector<long long> ids;
        bool ok = true;
        for (const auto& row : rows) {
            // aggregate_id is already the durable server message id.
            if (!_producer.Produce(im::mq::kMessageCreated, row.event_id,
                                   row.payload, error)) {
                ok = false;
                break;
            }
            ids.push_back(row.id);
        }

        if (ok) {
            ok = _producer.Flush(5000, error);
        }
        if (ok) {
            MysqlMgr::GetInstance()->MarkOutboxPublished(ids);
            im::metrics::IncCounter("im_outbox_published_total", ids.size(),
                                    "Outbox events published to Kafka");
        } else {
            std::cerr << "[Outbox] publish failed: " << error << std::endl;
            im::metrics::IncCounter("im_outbox_failed_total", rows.size(),
                                    "Outbox publish failures");
            for (const auto& row : rows) {
                MysqlMgr::GetInstance()->MarkOutboxFailed(row.id, error);
            }
        }
    }
}
