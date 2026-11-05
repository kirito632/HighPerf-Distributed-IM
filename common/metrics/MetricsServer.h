#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <boost/asio.hpp>

class MetricsServer {
public:
    MetricsServer() = default;
    ~MetricsServer();

    MetricsServer(const MetricsServer&) = delete;
    MetricsServer& operator=(const MetricsServer&) = delete;

    bool Start(std::uint16_t port, std::string& error);
    void Stop();

private:
    void Run();

    std::atomic<bool> _running{false};
    std::thread _worker;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> _acceptor;
};
