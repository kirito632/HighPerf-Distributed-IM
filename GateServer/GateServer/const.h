#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include<memory>
#include<iostream>
#include"Singleton.h"
#include<functional>
#include<map>
#include<unordered_map>
#include<json/json.h>
#include<json/value.h>
#include<json/reader.h>
#include<boost/filesystem.hpp>
#include<boost/property_tree/ptree.hpp>
#include<boost/property_tree/ini_parser.hpp>
#include <hiredis/hiredis.h> // 修改为正确的路径 
#include<cassert>
#include<atomic>
#include<queue>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

enum ErrorCodes {
    Success = 0,
    Error_Json = 1001,           // json解析错误
    RPCFailed = 1002,           // RPC请求错误
    VerifyExpired = 1003,       // 验证码已过期
    VerifyCodeErr = 1004,         // 验证码错误
    UserExist = 1005,       // 用户已存在
    PasswdErr = 1006,
    EmailNotMatch = 1007,
    PasswdUpFailed = 1008,
    PasswdInvalid = 1009,
    RPCGetFailed = 1010
};

class Defer {
public:
    // 接受一个lambda表达式或函数指针
    Defer(std::function<void()> func) :func_(func) {}

    ~Defer() {
        func_();
    }

private:
    std::function<void()> func_;
};

#define CODEPREFIX "code_"
#define USERIPPREFIX "uip_"
#define USERTOKENPREFIX "utoken_"
#define IPCOUNTPREFIX "ipcount_"
#define USER_BASE_INFO "ubaseinfo_"
#define LOGIN_COUNT "logincount"
