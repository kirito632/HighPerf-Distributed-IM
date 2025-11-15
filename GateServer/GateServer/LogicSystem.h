#pragma once
#include"const.h"

// 前向声明
class HttpConnection;

// HTTP处理器类型定义
// 
// 作用：
//   定义处理HTTP请求的回调函数类型
// 
// 参数：
//   std::shared_ptr<HttpConnection>: HTTP连接对象的智能指针
// 
// 返回值：
//   void
typedef std::function<void(std::shared_ptr<HttpConnection>)> HttpHandler;

// LogicSystem类：HTTP路由和业务逻辑处理系统
// 
// 作用：
//   1. 管理HTTP请求的路由（GET/POST）
//   2. 注册各种API端点的处理函数
//   3. 处理业务逻辑（用户注册、登录、好友管理等）
// 
// 设计模式：
//   继承Singleton单例模式，确保全局只有一个实例
// 
// API端点：
//   POST /get_verifycode - 获取验证码
//   POST /user_register - 用户注册
//   POST /reset_password - 重置密码
//   POST /user_login - 用户登录
//   POST /search_friends - 搜索好友
//   POST /get_friend_requests - 获取好友申请列表
//   POST /get_my_friends - 获取我的好友列表
//   POST /send_friend_request - 发送好友申请
//   POST /reply_friend_request - 回复好友申请
class LogicSystem :public Singleton<LogicSystem>
{
    friend class Singleton<LogicSystem>;
public:
    // 析构函数
    ~LogicSystem() {}

    // 处理GET请求
    // 参数：
    //   - path: 请求路径
    //   - con: HTTP连接对象
    // 返回值：
    //   如果找到对应的处理器返回true，否则返回false
    bool HandleGet(std::string, std::shared_ptr<HttpConnection>);

    // 注册GET请求处理器
    // 参数：
    //   - url: 请求路径
    //   - handler: 处理函数
    void RegGet(std::string, HttpHandler handler);

    // 注册POST请求处理器
    // 参数：
    //   - url: 请求路径
    //   - handler: 处理函数
    void RegPost(std::string url, HttpHandler handler);

    // 处理POST请求
    // 参数：
    //   - path: 请求路径
    //   - con: HTTP连接对象
    // 返回值：
    //   如果找到对应的处理器返回true，否则返回false
    bool HandlePost(std::string, std::shared_ptr<HttpConnection>);

private:
    // 私有构造函数：在构造函数中注册所有路由
    LogicSystem();

    // POST请求处理器映射表（路径 -> 处理函数）
    std::map<std::string, HttpHandler> _post_handlers;

    // GET请求处理器映射表（路径 -> 处理函数）
    std::map<std::string, HttpHandler> _get_handlers;
};


