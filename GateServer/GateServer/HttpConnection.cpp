#include "HttpConnection.h"
#include"LogicSystem.h"

// 构造函数：初始化HTTP连接对象
// 参数：
//   - ioc: IO上下文引用
// 
// 作用：
//   创建TCP套接字，准备接收HTTP请求
HttpConnection::HttpConnection(boost::asio::io_context& ioc) :_socket(ioc)
{
}

// 启动HTTP连接处理
// 
// 作用：
//   开始异步读取HTTP请求
// 
// 实现逻辑：
//   1. 异步读取HTTP请求数据
//   2. 读取完成后调用HandleReq处理请求
//   3. 设置连接超时检查
// 
// 工作流程：
//   Start() -> async_read() -> HandleReq() -> CheckDeadline()
void HttpConnection::Start()
{
    auto self = shared_from_this();

    // 异步读取HTTP请求
    http::async_read(_socket, _buffer, _request, [self](beast::error_code ec, ::std::size_t bytes_transferrd) {
        try {
            if (ec) {
                std::cout << "http read err is " << ec.what() << std::endl;
            }

            boost::ignore_unused(bytes_transferrd);

            // 处理HTTP请求
            self->HandleReq();
            // 启动连接超时检查
            self->CheckDeadline();
        }
        catch (std::exception& e) {
            std::cout << "exception is " << e.what() << std::endl;
        }
        });
}

// 将十六进制数转换为ASCII字符
// 参数：
//   - x: 十六进制值（0-15）
// 返回值：
//   ASCII字符（0-9对应'0'-'9'，10-15对应'A'-'F'）
unsigned char ToHex(unsigned char x)
{
    return  x > 9 ? x + 55 : x + 48;
}

// 将ASCII字符转换为十六进制数
// 参数：
//   - x: ASCII字符
// 返回值：
//   十六进制值（0-15）
unsigned char FromHex(unsigned char x)
{
    unsigned char y;
    if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
    else if (x >= '0' && x <= '9') y = x - '0';
    else assert(0);
    return y;
}

// URL编码：将特殊字符转换为%XX格式
// 参数：
//   - str: 原始字符串
// 返回值：
//   编码后的字符串
// 
// 编码规则：
//   1. 字母、数字和某些字符（-、_、.、~）保持不变
//   2. 空格转换为+
//   3. 其他特殊字符转换为%XX（XX为十六进制）
std::string UrlEncode(const std::string& str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        // 判断是否为字母、数字或特定符号（-、_、.、~）
        if (isalnum((unsigned char)str[i]) ||
            (str[i] == '-') ||
            (str[i] == '_') ||
            (str[i] == '.') ||
            (str[i] == '~'))
            strTemp += str[i];
        else if (str[i] == ' ') // 为空字符
            strTemp += "+";
        else
        {
            // 特殊字符需要在前面加%，并高低位分别转为十六进制
            strTemp += '%';
            strTemp += ToHex((unsigned char)str[i] >> 4);
            strTemp += ToHex((unsigned char)str[i] & 0x0F);
        }
    }
    return strTemp;
}

// URL解码：将%XX格式还原为原始字符
// 参数：
//   - str: 编码后的字符串
// 返回值：
//   解码后的字符串
// 
// 解码规则：
//   1. +转换为空格
//   2. %XX转换为对应字符（XX为十六进制）
std::string UrlDecode(const std::string& str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        // 将+还原为空格
        if (str[i] == '+') strTemp += ' ';
        // 遇到%，则按十六进制转为char并拼接
        else if (str[i] == '%')
        {
            assert(i + 2 < length);
            unsigned char high = FromHex((unsigned char)str[++i]);
            unsigned char low = FromHex((unsigned char)str[++i]);
            strTemp += high * 16 + low;
        }
        else strTemp += str[i];
    }
    return strTemp;
}

// 解析GET请求的URL参数
// 
// 作用：
//   从GET请求的URL中解析查询参数，并存储到_get_params中
// 
// 实现逻辑：
//   1. 从请求URI中获取URL
//   2. 查找查询字符串起始位置（'?'位置）
//   3. 解析key=value对（用'&'分隔）
//   4. 对参数进行URL解码
//   5. 存储到_get_params映射表
// 
// URL格式示例：
//   http://example.com/api?key1=value1&key2=value2
void HttpConnection::PreParseGetParam() {
    // 获取URI
    auto uri = _request.target();
    // 查找查询字符串的起始位置，即'?'的位置
    auto query_pos = uri.find('?');
    if (query_pos == std::string::npos) {
        _get_url = uri;
        return;
    }

    // 获取URL（不包含查询参数）
    _get_url = uri.substr(0, query_pos);
    // 获取查询字符串
    std::string query_string = uri.substr(query_pos + 1);
    std::string key;
    std::string value;
    size_t pos = 0;

    // 解析key=value对
    while ((pos = query_string.find('&')) != std::string::npos) {
        auto pair = query_string.substr(0, pos);
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            // 使用url_decode来解码URL编码
            key = UrlDecode(pair.substr(0, eq_pos));
            value = UrlDecode(pair.substr(eq_pos + 1));
            _get_params[key] = value;
        }
        query_string.erase(0, pos + 1);
    }
    // 处理最后一组参数，也就是没有&分隔的
    if (!query_string.empty()) {
        size_t eq_pos = query_string.find('=');
        if (eq_pos != std::string::npos) {
            key = UrlDecode(query_string.substr(0, eq_pos));
            value = UrlDecode(query_string.substr(eq_pos + 1));
            _get_params[key] = value;
        }
    }
}

// 处理HTTP请求
// 
// 作用：
//   根据请求方法（GET/POST）处理HTTP请求，并调用逻辑系统处理
// 
// 实现逻辑：
//   1. 设置响应版本和Keep-Alive标志
//   2. 如果是GET请求：
//      - 解析GET参数
//      - 调用LogicSystem处理GET请求
//      - 如果处理失败，返回404
//      - 如果处理成功，返回200
//   3. 如果是POST请求：
//      - 调用LogicSystem处理POST请求
//      - 如果处理失败，返回404
//      - 如果处理成功，返回200
//   4. 发送响应
void HttpConnection::HandleReq()
{
    // 设置响应版本
    _response.version(_request.version());
    _response.keep_alive(false);

    // 处理GET请求
    if (_request.method() == http::verb::get) {
        // 解析GET参数
        PreParseGetParam();

        // 调用逻辑系统处理GET请求
        bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());

        if (!success) {
            // 处理失败，返回404
            _response.result(http::status::not_found);
            _response.set(http::field::content_type, "text/plain");
            beast::ostream(_response.body()) << "url not found\r\n";
            WriteResponse();
            return;
        }

        // 处理成功，返回200
        _response.result(http::status::ok);
        _response.set(http::field::server, "GateServer");
        WriteResponse();
        return;
    }

    // 处理POST请求
    if (_request.method() == http::verb::post) {
        // 调用逻辑系统处理POST请求
        bool success = LogicSystem::GetInstance()->HandlePost(_request.target(), shared_from_this());

        if (!success) {
            // 处理失败，返回404
            _response.result(http::status::not_found);
            _response.set(http::field::content_type, "text/plain");
            beast::ostream(_response.body()) << "url not found\r\n";
            WriteResponse();
            return;
        }

        // 处理成功，返回200
        _response.result(http::status::ok);
        _response.set(http::field::server, "GateServer");
        WriteResponse();
        return;
    }
}

// 异步写入HTTP响应
// 
// 作用：
//   将HTTP响应发送给客户端
// 
// 实现逻辑：
//   1. 设置响应内容长度
//   2. 异步写入响应数据
//   3. 写入完成后关闭套接字的发送端
//   4. 取消超时定时器
// 
// 注意：
//   使用async_write确保完整发送响应数据
void HttpConnection::WriteResponse()
{
    auto self = shared_from_this();
    // 设置响应内容长度
    _response.content_length(_response.body().size());

    // 异步写入HTTP响应
    http::async_write(_socket, _response,
        [self](beast::error_code ec, std::size_t bytes_transferred)
        {
            if (ec) {
                std::cerr << "[HttpConnection] async_write failed: "
                    << ec.message() << std::endl;
            }
            else {
                std::cout << "[HttpConnection] Response sent, "
                    << bytes_transferred << " bytes written." << std::endl;
            }

            // 关闭套接字的发送端（优雅关闭）
            beast::error_code shutdown_ec;
            self->_socket.shutdown(tcp::socket::shutdown_send, shutdown_ec);
            if (shutdown_ec) {
                std::cerr << "[HttpConnection] shutdown error: "
                    << shutdown_ec.message() << std::endl;
            }

            // 取消超时定时器
            self->_deadline.cancel();
        });
}


// 检查连接超时
// 
// 作用：
//   设置连接超时检查，如果60秒内没有完成操作则关闭连接
// 
// 实现逻辑：
//   1. 异步等待超时事件
//   2. 如果超时，关闭套接字
// 
// 超时时间：
//   60秒（在构造函数中设置）
void HttpConnection::CheckDeadline()
{
    auto self = shared_from_this();

    // 异步等待超时事件
    _deadline.async_wait([self](beast::error_code ec) {
        // 如果没有被取消（即超时），则关闭连接
        if (!ec) {
            self->_socket.close(ec);
        }
        });
}
