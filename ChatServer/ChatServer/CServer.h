#pragma once
#include"const.h"

#include <boost/asio.hpp>
#include "CSession.h"
#include <memory.h>
#include <map>
#include <mutex>
using namespace std;
using boost::asio::ip::tcp;

// CServer类：TCP服务器，用于接受客户端连接
// 
// 作用：
//   1. 监听指定端口，接受客户端连接
//   2. 管理所有客户端会话
//   3. 为每个连接创建CSession对象
// 
// 实现逻辑：
//   1. 使用boost::asio的acceptor接受TCP连接
//   2. 使用异步方式接受连接，实现高并发
//   3. 使用map存储所有会话（session_id -> session）
//   4. 使用互斥锁保证线程安全
class CServer
{
public:
    // 构造函数：初始化服务器
    // 参数：
    //   - io_context: Boost.Asio的IO上下文
    //   - port: 监听端口号
    CServer(boost::asio::io_context& io_context, unsigned short port);

    // 析构函数：清理资源
    ~CServer();

    // 清除会话
    // 参数：
    //   - session_id: 会话ID
    // 作用：
    //   从服务器中移除指定会话
    void ClearSession(std::string);

private:
    // 处理接受连接的回调
    // 参数：
    //   - new_session: 新的会话对象
    //   - error: 错误码
    void HandleAccept(std::shared_ptr<CSession>, const boost::system::error_code& error);

    // 开始异步接受连接
    void StartAccept();

    boost::asio::io_context& _io_context;  // IO上下文引用
    short _port;                            // 监听端口号
    tcp::acceptor _acceptor;               // TCP接受器
    std::map<std::string, std::shared_ptr<CSession>> _sessions;  // 会话映射表
    std::mutex _mutex;                     // 互斥锁
};


