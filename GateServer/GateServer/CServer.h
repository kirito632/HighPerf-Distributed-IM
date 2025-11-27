#pragma once
#include"const.h"

// HTTP服务器类：用于接受和管理HTTP客户端连接
// 
// 作用：
//   1. 监听指定端口
//   2. 异步接受客户端连接
//   3. 为每个连接创建HttpConnection对象
//   4. 实现负载均衡（使用IO服务池）
// 
// 设计模式：
//   继承enable_shared_from_this，用于在异步回调中保持对象生命周期
class CServer :public std::enable_shared_from_this<CServer>
{
public:
    // 构造函数：初始化HTTP服务器
    // 参数：
    //   - ioc: IO上下文引用
    //   - port: 服务器监听端口号
    CServer(boost::asio::io_context& ioc, unsigned short& port);

    // 启动服务器：开始接受客户端连接
    void Start();

private:
    // TCP acceptor：用于接受客户端连接
    tcp::acceptor _acceptor;

    // IO上下文引用：用于异步操作
    net::io_context& _ioc;
};
