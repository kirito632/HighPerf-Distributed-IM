#include "MetricsServer.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include <boost/asio/write.hpp>

#include "Metrics.h"

MetricsServer::~MetricsServer() { Stop(); }

bool MetricsServer::Start(std::uint16_t port, std::string& error) {
    if (_running.load()) {
        return true;
    }
    try {
        _acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(
            boost::asio::system_executor());
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::tcp::v4(), port);
        _acceptor->open(endpoint.protocol());
        _acceptor->set_option(boost::asio::socket_base::reuse_address(true));
        _acceptor->bind(endpoint);
        _acceptor->listen(boost::asio::socket_base::max_listen_connections);
        // Non-blocking accept lets Stop() join the worker promptly instead of
        // waiting for the next scrape (blocking accept is not reliably woken
        // by close() from another thread on Linux).
        _acceptor->non_blocking(true);
    }
    catch (const std::exception& e) {
        error = e.what();
        _acceptor.reset();
        return false;
    }
    _running.store(true);
    _worker = std::thread(&MetricsServer::Run, this);
    std::cout << "[Metrics] listening on 0.0.0.0:" << port << std::endl;
    return true;
}

void MetricsServer::Stop() {
    if (!_running.exchange(false)) {
        return;
    }
    // The worker only polls a non-blocking acceptor/read loop, so it exits
    // within a few milliseconds. Join before closing to avoid touching the
    // acceptor concurrently.
    if (_worker.joinable()) {
        _worker.join();
    }
    if (_acceptor) {
        boost::system::error_code ec;
        _acceptor->close(ec);
        _acceptor.reset();
    }
}

void MetricsServer::Run() {
    while (_running.load()) {
        try {
            boost::asio::ip::tcp::socket socket{
                boost::asio::system_executor()};
            boost::system::error_code ec;
            _acceptor->accept(socket, ec);
            if (ec) {
                if (!_running.load()) {
                    break;
                }
                if (ec == boost::asio::error::would_block ||
                    ec == boost::asio::error::try_again) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    continue;
                }
                std::cerr << "[Metrics] accept failed: " << ec.message() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            socket.non_blocking(true);
            std::string request;
            char buffer[1024];
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (_running.load() &&
                   request.find("\r\n\r\n") == std::string::npos &&
                   std::chrono::steady_clock::now() < deadline) {
                const std::size_t n =
                    socket.read_some(boost::asio::buffer(buffer), ec);
                if (ec == boost::asio::error::would_block ||
                    ec == boost::asio::error::try_again) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                if (ec) {
                    break;
                }
                request.append(buffer, n);
                if (request.size() > 8192) {
                    break;
                }
            }
            if (!_running.load()) {
                break;
            }

            std::istringstream input(request);
            std::string method;
            std::string path;
            input >> method >> path;

            const bool ok = method == "GET" && path == "/metrics";
            const std::string body = ok ? im::metrics::RenderPrometheus()
                                        : "not found\n";
            const std::string response =
                "HTTP/1.1 " + std::string(ok ? "200 OK" : "404 Not Found") +
                "\r\nContent-Type: text/plain; version=0.0.4\r\nContent-Length: " +
                std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" +
                body;
            boost::asio::write(socket, boost::asio::buffer(response), ec);
        }
        catch (const std::exception& e) {
            if (_running.load()) {
                std::cerr << "[Metrics] handler error: " << e.what() << std::endl;
            }
        }
    }
}
