#include "CServer.h"
#include"HttpConnection.h"
#include"AsioIOServicePool.h"

// 构造函数：初始化HTTP服务器
// 参数：
//   - ioc: IO上下文引用（用于异步操作）
//   - port: 服务器监听端口号
// 
// 作用：
//   创建TCP acceptor，绑定到指定的IPv4地址和端口
// 
// 实现逻辑：
//   1. 使用io_context创建TCP acceptor
//   2. 绑定到IPv4地址和指定端口
//   3. 准备接受客户端连接
CServer::CServer(boost::asio::io_context& ioc, unsigned short& port) :_ioc(ioc),
_acceptor(ioc, tcp::endpoint(tcp::v4(), port))
{

}

// 启动HTTP服务器
// 
// 作用：
//   开始异步接受客户端连接，并为每个连接创建HttpConnection对象
// 
// 实现逻辑：
//   1. 从IO服务池获取一个io_context（实现负载均衡）
//   2. 创建一个新的HttpConnection对象处理客户端请求
//   3. 异步接受客户端连接
//   4. 当有客户端连接时：
//      - 如果有错误，继续接受下一个连接
//      - 如果成功，启动HttpConnection处理请求
//      - 然后继续接受下一个连接（实现并发处理）
// 
// 工作流程：
//   Start() -> async_accept() -> HttpConnection::Start() -> Start() (循环)
void CServer::Start()
{
    auto self = shared_from_this();
    // 从IO服务池获取一个io_context（使用Round-Robin轮询方式）
    auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
    // 创建一个新的HTTP连接对象
    std::shared_ptr<HttpConnection> new_con = std::make_shared<HttpConnection>(io_context);

    // 异步接受客户端连接
    _acceptor.async_accept(new_con->GetSocket(), [self, new_con](beast::error_code ec) {
        try {
            // 如果有错误（如客户端主动断开），继续接受下一个连接
            if (ec) {
                self->Start();
                return;
            }
            // 连接成功，启动HttpConnection对象处理该客户端请求
            new_con->Start();

            // 继续接受下一个连接（实现并发处理多个客户端）
            self->Start();
        }
        catch (std::exception& e) {
            std::cout << "exception is " << e.what() << std::endl;
        }
        });
}