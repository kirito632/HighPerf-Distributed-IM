#pragma once
#include"const.h"

// 前向声明
class HttpConnection;
namespace im::auth {
class JwtVerifier;
class TokenService;
}

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
    // 析构函数（TokenService 是 unique_ptr，需要 out-of-line 析构）
    ~LogicSystem();

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

    // 注册需要 Bearer JWT 的 POST 处理器
    void RegProtectedPost(std::string url, HttpHandler handler,
                          std::string required_scope);

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

    // 读取请求体（LogicSystem 是 HttpConnection 的 friend）
    static std::string ReadBody(const std::shared_ptr<HttpConnection>& con);

    // 统一 JSON 响应
    static void SendJson(const std::shared_ptr<HttpConnection>& con,
                         const Json::Value& root,
                         http::status status = http::status::ok);

    void HandleOAuthToken(const std::shared_ptr<HttpConnection>& con);
    void HandleOAuthRevoke(const std::shared_ptr<HttpConnection>& con);
    void HandleJwks(const std::shared_ptr<HttpConnection>& con);

    // 校验 Bearer JWT 并写入 con->auth_uid。JWT enabled=false 时返回 true
    // 但 auth_uid=0，保持旧客户端兼容。
    bool RequireAuth(const std::shared_ptr<HttpConnection>& con,
                     const std::string& required_scope);

    static bool HasScope(const std::string& scopes, const std::string& required);

    // Redis-based fixed window limiter. Returns false when the limit is hit
    // or Redis is unavailable (fail closed for auth-sensitive endpoints).
    static bool CheckRateLimit(const std::string& key, int limit, int window_seconds);

    // POST请求处理器映射表（路径 -> 处理函数）
    std::map<std::string, HttpHandler> _post_handlers;

    // GET请求处理器映射表（路径 -> 处理函数）
    std::map<std::string, HttpHandler> _get_handlers;

    // JWT/OAuth 状态；JwtEnabled=false 时 _token_service 为空
    std::unique_ptr<im::auth::TokenService> _token_service;
    std::unique_ptr<im::auth::JwtVerifier> _jwt_verifier;
    std::string _jwt_public_pem;
    std::string _jwt_key_id;
};



