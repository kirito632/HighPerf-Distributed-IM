#pragma once
#include"const.h"

// HTTP连接类：处理单个HTTP客户端连接
// 
// 作用：
//   1. 接收和解析HTTP请求
//   2. 处理GET/POST请求
//   3. 发送HTTP响应
//   4. 管理连接超时
// 
// 设计模式：
//   继承enable_shared_from_this，用于在异步回调中保持对象生命周期
class HttpConnection :public std::enable_shared_from_this<HttpConnection>
{
public:
    friend class LogicSystem;  // 允许LogicSystem访问私有成员

    // 构造函数：初始化HTTP连接
    // 参数：
    //   - ioc: IO上下文引用
    HttpConnection(boost::asio::io_context& ioc);

    // 启动连接处理：开始接收HTTP请求
    void Start();

    // 获取TCP套接字引用
    tcp::socket& GetSocket() {
        return _socket;
    }

private:
    // 检查连接超时（60秒超时）
    void CheckDeadline();

    // 异步写入HTTP响应
    void WriteResponse();

    // 处理HTTP请求（GET/POST）
    void HandleReq();

    // 解析GET请求的URL参数
    void PreParseGetParam();

    // TCP套接字
    tcp::socket _socket;

    // HTTP请求缓冲区（8KB）
    beast::flat_buffer _buffer{ 8192 };

    // HTTP请求对象
    http::request<http::dynamic_body> _request;

    // HTTP响应对象
    http::response<http::dynamic_body> _response;

    // 连接超时定时器（60秒）
    boost::asio::steady_timer _deadline{
        _socket.get_executor(), std::chrono::seconds(60)
    };

    // GET请求的URL（不含查询参数）
    std::string _get_url;

    // GET请求的查询参数映射表
    std::unordered_map<std::string, std::string> _get_params;
};


