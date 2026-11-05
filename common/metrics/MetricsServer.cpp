#include "MetricsServer.h"

#include <iostream>

#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
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
    if (_acceptor) {
        boost::system::error_code ec;
        _acceptor->close(ec);
    }
    if (_worker.joinable()) {
        _worker.join();
    }
    _acceptor.reset();
}

void MetricsServer::Run() {
    while (_running.load()) {
        try {
            boost::asio::ip::tcp::socket socket{
                boost::asio::system_executor()};
            boost::system::error_code ec;
            _acceptor->accept(socket, ec);
            if (ec) {
                if (_running.load()) {
                    std::cerr << "[Metrics] accept failed: " << ec.message() << std::endl;
                }
                continue;
            }

            boost::asio::streambuf request;
            boost::asio::read_until(socket, request, "\r\n\r\n", ec);
            if (ec) {
                continue;
            }
            std::istream input(&request);
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
