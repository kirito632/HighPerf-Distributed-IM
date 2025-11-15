#include <iostream>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "const.h"
#include "ConfigMgr.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "AsioIOServicePool.h"
#include <memory>
#include <string>
#include <thread>
#include <boost/asio.hpp>
#include "StatusServiceImpl.h"
#include<grpcpp/grpcpp.h>
#include <hiredis/hiredis.h>

// 运行gRPC服务器
// 
// 作用：
//   启动gRPC服务器，监听来自GateServer的请求
// 
// 实现逻辑：
//   1. 从配置文件读取StatusServer的地址和端口
//   2. 创建StatusServiceImpl服务实例
//   3. 构建gRPC服务器并注册服务
//   4. 启动gRPC服务器
//   5. 使用Boost.Asio捕获SIGINT信号，优雅关闭服务器
void RunServer() {
    auto& cfg = ConfigMgr::Inst();

    // 从配置文件获取StatusServer的地址
    std::string server_address(cfg["StatusServer"]["Host"] + ":" + cfg["StatusServer"]["Port"]);
    StatusServiceImpl service;

    grpc::ServerBuilder builder;
    // 监听端口和添加服务
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    // 构建并启动gRPC服务器
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    // 创建Boost.Asio的io_context
    boost::asio::io_context io_context;
    // 创建signal_set用于捕获SIGINT信号
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

    // 设置异步等待SIGINT信号（Ctrl+C）
    signals.async_wait([&server](const boost::system::error_code& error, int signal_number) {
        if (!error) {
            std::cout << "Shutting down server..." << std::endl;
            server->Shutdown(); // 优雅地关闭服务器
        }
        });

    // 在单独的线程中运行io_context
    std::thread([&io_context]() { io_context.run(); }).detach();

    // 等待服务器关闭
    server->Wait();
    io_context.stop(); // 停止io_context
}

// 主函数：程序入口
// 
// 作用：
//   启动StatusServer程序
// 
// 实现逻辑：
//   1. 调用RunServer()启动gRPC服务器
//   2. 处理异常并返回适当的退出码
int main(int argc, char** argv) {
    try {
        RunServer();
    }
    catch (std::exception const& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return 0;
}



