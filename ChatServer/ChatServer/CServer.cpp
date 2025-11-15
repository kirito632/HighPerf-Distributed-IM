#include "CServer.h"
#include"AsioIOServicePool.h"
#include "UserMgr.h"

// 构造函数：初始化TCP服务器
// 
// 实现逻辑：
//   1. 保存IO上下文和端口
//   2. 验证端口号
//   3. 创建acceptor并绑定到指定端口
//   4. 开始监听
//   5. 开始异步接受连接
CServer::CServer(boost::asio::io_context& io_context, unsigned short port)
    : _io_context(io_context), _port(port), _acceptor(io_context)
{
    std::cout << "CServer ctor called with port: " << port << std::endl;

    // 验证端口号
    if (port == 0) {
        std::cerr << "ERROR: invalid port 0 passed to CServer. Aborting." << std::endl;
        throw std::runtime_error("Invalid port: 0");
    }

    try {
        // 创建endpoint并绑定
        boost::asio::ip::tcp::endpoint ep(boost::asio::ip::tcp::v4(), port);
        _acceptor.open(ep.protocol());
        _acceptor.set_option(boost::asio::socket_base::reuse_address(true));  // 允许地址重用
        _acceptor.bind(ep);
        _acceptor.listen(boost::asio::socket_base::max_listen_connections);  // 开始监听

        auto ep_local = _acceptor.local_endpoint();
        std::cout << "Acceptor bound, local endpoint port: " << ep_local.port() << std::endl;
    }
    catch (const boost::system::system_error& e) {
        std::cerr << "Acceptor init failed, port=" << port << ", err=" << e.what() << std::endl;
        throw;
    }

    std::cout << "Server start success, listen on port : " << _port << std::endl;
    std::cout << "CServer StartAccept using io_context at " << &(_io_context) << "\n";

    // 开始异步接受连接
    StartAccept();
}

// 析构函数：清理资源
CServer::~CServer()
{
}

// 清除会话
// 
// 参数：
//   - session_id: 会话ID
// 
// 实现逻辑：
//   1. 从UserMgr中移除用户会话
//   2. 加锁保证线程安全
//   3. 从_sessions中删除会话
void CServer::ClearSession(std::string session_id)
{
    // 如果会话存在，移除用户的session映射
    if (_sessions.find(session_id) != _sessions.end()) {
        // 移除用户的 session 映射
        UserMgr::GetInstance()->RmvUserSession(_sessions[session_id]->GetUserId());
    }

    // 从_sessions中删除会话
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _sessions.erase(session_id);
    }

}

// 开始异步接受连接
// 
// 实现逻辑：
//   1. 创建一个新的CSession对象
//   2. 异步接受客户端连接
//   3. 当有连接时，调用HandleAccept处理
void CServer::StartAccept() {
    // 使用传递过来的_io_context，例如AsioIOServicePool::GetInstance()
    std::cout << "CServer StartAccept using io_context at " << &(_io_context) << "\n";

    // 创建新的会话对象
    std::shared_ptr<CSession> new_session = std::make_shared<CSession>(_io_context, this);

    // 异步接受连接
    _acceptor.async_accept(new_session->GetSocket(),
        std::bind(&CServer::HandleAccept, this, new_session, std::placeholders::_1));
}

// 处理接受连接的回调
// 
// 参数：
//   - new_session: 新的会话对象
//   - error: 错误码
// 
// 实现逻辑：
//   1. 检查是否有错误
//   2. 如果没有错误，启动会话并将会话加入_sessions
//   3. 继续异步接受下一个连接
void CServer::HandleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error) {
    if (!error) {
        // 启动会话
        new_session->Start();

        // 加锁保证线程安全
        lock_guard<mutex> lock(_mutex);

        // 将会话加入_sessions
        _sessions.insert(std::make_pair(new_session->GetSessionId(), new_session));
    }
    else {
        cout << "session accept failed, error is " << error.what() << std::endl;
    }

    // 继续接受下一个连接
    StartAccept();
}
