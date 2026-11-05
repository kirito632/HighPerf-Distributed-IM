#include "LogicSystem.h"
#include "HttpConnection.h"
#include "MysqlMgr.h"
#include "MysqlDao.h"
#include "MySqlRefreshTokenStore.h"
#include "RedisMgr.h"
#include "StatusGrpcClient.h"
#include "VerifyGrpcClient.h"
#include "TokenService.h"
#include "JwtVerifier.h"
#include "Jwks.h"
#include "JwtKeys.h"
#include "ConfigMgr.h"
#include "Singleton.h"
#include "const.h"
#include "crypto_utils.h"
#include "Tracing.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <ctime>
#include <chrono>

using im::auth::JwtConfig;
using im::auth::MySqlRefreshTokenStore;
using im::auth::TokenResult;
using im::auth::TokenService;

namespace {

std::string UrlDecodeForm(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const int high = hex(value[i + 1]);
            const int low = hex(value[i + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>((high << 4) | low));
                i += 2;
            } else {
                out.push_back(value[i]);
            }
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

// OAuth2 token endpoint accepts application/x-www-form-urlencoded, while the
// existing desktop client sends JSON. Support both during the migration.
std::map<std::string, std::string> ParseTokenRequest(const std::string& body) {
    std::map<std::string, std::string> params;
    if (body.empty()) {
        return params;
    }

    const auto first = body.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && body[first] == '{') {
        Json::Reader reader;
        Json::Value root;
        if (!reader.parse(body, root) || !root.isObject()) {
            return params;
        }
        for (const auto& key : root.getMemberNames()) {
            params[key] = root[key].asString();
        }
        return params;
    }

    size_t pos = 0;
    while (pos <= body.size()) {
        const size_t amp = body.find('&', pos);
        const std::string pair = body.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            params[UrlDecodeForm(pair.substr(0, eq))] = UrlDecodeForm(pair.substr(eq + 1));
        }
        if (amp == std::string::npos) {
            break;
        }
        pos = amp + 1;
    }
    return params;
}

Json::Value OAuthError(const std::string& code, const std::string& description) {
    Json::Value root;
    root["error"] = code;
    root["error_description"] = description;
    return root;
}

}  // namespace

std::string LogicSystem::ReadBody(const std::shared_ptr<HttpConnection>& con) {
    if (!con) {
        return "";
    }
    return boost::beast::buffers_to_string(con->_request.body().data());
}

void LogicSystem::SendJson(const std::shared_ptr<HttpConnection>& con,
                           const Json::Value& root,
                           http::status status) {
    con->_response.result(status);
    con->_response.set(http::field::content_type, "application/json");
    beast::ostream(con->_response.body()) << root.toStyledString();
    con->WriteResponse();
}

LogicSystem::~LogicSystem() = default;

// 注册POST请求处理器
// 参数：
//   - url: 请求路径
//   - handler: 处理函数
void LogicSystem::RegPost(std::string url, HttpHandler handler) {
  _post_handlers.emplace(make_pair(url, handler));
}

void LogicSystem::RegProtectedPost(std::string url, HttpHandler handler,
                                   std::string required_scope) {
  auto wrapped = [this, handler = std::move(handler),
                  required_scope = std::move(required_scope)](
                     std::shared_ptr<HttpConnection> connection) {
    if (!RequireAuth(connection, required_scope)) {
      return;
    }
    handler(std::move(connection));
  };
  _post_handlers.emplace(std::move(url), std::move(wrapped));
}

// 注册GET请求处理器
// 参数：
//   - url: 请求路径
//   - handler: 处理函数
void LogicSystem::RegGet(std::string url, HttpHandler handler) {
  _get_handlers.emplace(make_pair(url, handler));
}

// 构造函数：注册所有API路由
//
// 作用：
//   在构造函数中注册所有的HTTP请求处理函数
//
// 注册的API端点：
//   - /get_verifycode: 获取验证码
//   - /user_register: 用户注册
//   - /reset_password: 重置密码
//   - /user_login: 用户登录
//   - /search_friends: 搜索好友
//   - /get_friend_requests: 获取好友申请列表
//   - /get_my_friends: 获取我的好友列表
//   - /send_friend_request: 发送好友申请
//   - /reply_friend_request: 回复好友申请
LogicSystem::LogicSystem() {
  // Optional JWT/OAuth2-style token service. Disabled by default so existing
  // deployments keep the legacy login flow until keys/config are provisioned.
  try {
    auto& cfg = ConfigMgr::Inst();
    auto feature_cfg = cfg["Feature"];
    auto jwt_cfg = cfg["Jwt"];
    if (feature_cfg["JwtEnabled"] == "true") {
      JwtConfig jwt;
      jwt.issuer = jwt_cfg["Issuer"];
      jwt.audience = jwt_cfg["Audience"];
      jwt.key_id = jwt_cfg["KeyId"];
      jwt.private_key_pem = im::auth::ReadPemFile(jwt_cfg["PrivateKeyPath"]);
      jwt.public_key_pem = im::auth::ReadPemFile(jwt_cfg["PublicKeyPath"]);

      const std::string access_ttl = jwt_cfg["AccessTtlSeconds"];
      if (!access_ttl.empty()) {
        jwt.access_ttl_seconds = std::stoi(access_ttl);
      }
      const std::string skew = jwt_cfg["ClockSkewSeconds"];
      if (!skew.empty()) {
        jwt.clock_skew_seconds = std::stoi(skew);
      }
      const std::string refresh_ttl = jwt_cfg["RefreshTtlSeconds"];
      const int refresh_ttl_seconds = refresh_ttl.empty() ? 604800 : std::stoi(refresh_ttl);

      auto store = std::make_shared<MySqlRefreshTokenStore>(
          Singleton<MySqlPool>::GetInstance());
      _token_service = std::make_unique<TokenService>(
          jwt, refresh_ttl_seconds, "im.friend im.message", store);
      _jwt_verifier = std::make_unique<im::auth::JwtVerifier>(jwt);
      _jwt_public_pem = jwt.public_key_pem;
      _jwt_key_id = jwt.key_id;
      std::cout << "[Auth] JWT enabled, issuer=" << jwt.issuer
                << " audience=" << jwt.audience
                << " kid=" << jwt.key_id << std::endl;
    }
  } catch (const std::exception& e) {
    std::cerr << "[Auth] JWT initialization failed, endpoints disabled: "
              << e.what() << std::endl;
    _token_service.reset();
  }

  // 注册获取验证码API
  // 功能：通过gRPC调用VerifyServer获取验证码，并存储到Redis
  // 请求参数：{"email": "xxx@example.com"}
  // 返回：{"error": 错误码, "email": "xxx@example.com"}
  RegPost("/get_verifycode", [](std::shared_ptr<HttpConnection> connection) {
    auto body_str =
        boost::beast::buffers_to_string(connection->_request.body().data());
    std::cout << "[request] body_len=" << body_str.size() << std::endl;

    Json::Value root;
    Json::Reader reader;
    Json::Value src_root;
    bool parse_success = reader.parse(body_str, src_root);
    if (!parse_success) {
      std::cout << "Failed to parse Json data!" << std::endl;
      root["error"] = ErrorCodes::Error_Json;
      std::string jsonstr = root.toStyledString();
      beast::ostream(connection->_response.body()) << jsonstr;

      // 设置响应内容类型
      connection->_response.set(http::field::content_type,
                                "application/json"); // 设置为JSON格式
      connection->WriteResponse();
      return true;
    }

    if (!src_root.isMember("email")) {
      std::cout << "Failed to parse Json data!" << std::endl;
      root["error"] = ErrorCodes::Error_Json;
      std::string jsonstr = root.toStyledString();
      beast::ostream(connection->_response.body()) << jsonstr;

      // 设置响应内容类型
      connection->_response.set(http::field::content_type, "application/json");
      connection->WriteResponse();
      return true;
    }

    auto email = src_root["email"].asString();
    const std::string client_ip = connection->GetClientIp();
    if (!CheckRateLimit("rl_code_send_email_" + email, 1, 60) ||
        !CheckRateLimit("rl_code_send_ip_" + client_ip, 10, 60)) {
      root["error"] = ErrorCodes::RateLimited;
      SendJson(connection, root, http::status::too_many_requests);
      return true;
    }

    GetVerifyRsp rsp = VerifyGrpcClient::GetInstance()->GetVerifyCode(email);
    std::cout << "email is " << email << " , rsp.error = " << rsp.error()
              << std::endl;

    root["error"] = rsp.error();
    root["email"] = src_root["email"];
    std::string jsonstr = root.toStyledString();
    beast::ostream(connection->_response.body()) << jsonstr;

    // 设置响应内容类型
    connection->_response.set(http::field::content_type, "application/json");
    connection->WriteResponse();
    return true;
  });

  RegPost("/user_register", [](std::shared_ptr<HttpConnection> connection) {
    auto body_str =
        boost::beast::buffers_to_string(connection->_request.body().data());
    std::cout << "[request] body_len=" << body_str.size() << std::endl;

    Json::Value root;
    Json::Reader reader;
    Json::Value src_root;
    bool parse_success = reader.parse(body_str, src_root);
    if (!parse_success) {
      std::cout << "Failed to parse JSON data!" << std::endl;
      root["error"] = ErrorCodes::Error_Json;
      std::string jsonstr = root.toStyledString();
      beast::ostream(connection->_response.body()) << jsonstr;

      connection->_response.set(http::field::content_type, "application/json");
      connection->WriteResponse();
      return true;
    }

    auto email = src_root["email"].asString();
    auto name = src_root["user"].asString();
    auto pwd = src_root["passwd"].asString();
    auto confirm = src_root["confirm"].asString();

    if (pwd != confirm) {
      std::cout << "password err " << std::endl;
      root["error"] = ErrorCodes::PasswdErr;
      std::string jsonstr = root.toStyledString();
      beast::ostream(connection->_response.body()) << jsonstr;

      connection->_response.set(http::field::content_type, "application/json");
      connection->WriteResponse();
      return true;
    }

    if (!CheckRateLimit("rl_code_try_" + email, 5, 600)) {
      root["error"] = ErrorCodes::RateLimited;
      SendJson(connection, root, http::status::too_many_requests);
      return true;
    }

    // 从Redis获取该email对应的验证码
    std::string verify_code;
    bool b_get_verify =
        RedisMgr::GetInstance()->Get(CODEPREFIX + email, verify_code);
    if (!b_get_verify) {
      std::cout << " get verify code expired" << std::endl;
      root["error"] = ErrorCodes::VerifyExpired;
      std::string jsonstr = root.toStyledString();
      beast::ostream(connection->_response.body()) << jsonstr;

      // 设置响应内容类型
      connection->_response.set(http::field::content_type, "application/json");
      connection->WriteResponse();
      return true;
    }

    if (verify_code != src_root["verifycode"].asString()) {
      std::cout << " verify code error" << std::endl;
      root["error"] = ErrorCodes::VerifyCodeErr;
      std::string jsonstr = root.toStyledString();
      beast::ostream(connection->_response.body()) << jsonstr;

      // 设置响应内容类型
      connection->_response.set(http::field::content_type, "application/json");
      connection->WriteResponse();
      return true;
    }

    // 原子消费验证码：并发请求只有一个能成功。
    std::string consumed_code;
    if (!RedisMgr::GetInstance()->GetDel(CODEPREFIX + email, consumed_code) ||
        consumed_code != verify_code) {
      root["error"] = ErrorCodes::VerifyExpired;
      SendJson(connection, root, http::status::bad_request);
      return true;
    }

    // register的时候用哈希
    std::string hashed = sha256_hex(pwd);

    // 调用DAO层注册新用户
    int uid = MysqlMgr::GetInstance()->RegUser(name, email, hashed);
    if (uid == 0 || uid == -1) {
      std::cout << " user or email exist" << std::endl;
      root["error"] = ErrorCodes::UserExist;
      std::string jsonstr = root.toStyledString();
      beast::ostream(connection->_response.body()) << jsonstr;

      connection->_response.set(http::field::content_type, "application/json");
      connection->WriteResponse();
      return true;
    }

    root["error"] = 0;
    root["uid"] = uid;
    root["email"] = email;
    root["user"] = name;
    // root["passwd"] = pwd;
    root["confirm"] = confirm;
    root["verifycode"] = src_root["verifycode"].asString();
    std::string jsonstr = root.toStyledString();
    beast::ostream(connection->_response.body()) << jsonstr;

    connection->_response.set(http::field::content_type, "application/json");
    connection->WriteResponse();
    return true;
  });

  RegPost("/reset_password", [](std::shared_ptr<HttpConnection> connection) {
    auto body_str =
        boost::beast::buffers_to_string(connection->_request.body().data());
    std::cout << "[request] body_len=" << body_str.size() << std::endl;
    connection->_response.set(http::field::content_type, "application/json");

    Json::Value root;
    Json::Reader reader;
    Json::Value src_root;
    if (!reader.parse(body_str, src_root)) {
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return true;
    }

    std::string email = src_root.get("email", "").asString();
    // 有些请求用passwd，有些用new_password
    std::string pwd_plain = src_root.isMember("passwd")
                                ? src_root["passwd"].asString()
                                : src_root.get("new_password", "").asString();
    std::string verifycode = src_root.get("verifycode", "").asString();

    if (email.empty() || pwd_plain.empty() || verifycode.empty()) {
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return true;
    }

    if (!CheckRateLimit("rl_code_try_" + email, 5, 600)) {
      root["error"] = ErrorCodes::RateLimited;
      SendJson(connection, root, http::status::too_many_requests);
      return true;
    }

    // 从Redis获取存储的验证码
    std::string verify_code;
    bool b_get_verify =
        RedisMgr::GetInstance()->Get(CODEPREFIX + email, verify_code);
    if (!b_get_verify) {
      root["error"] = ErrorCodes::VerifyExpired;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return true;
    }
    if (verify_code != verifycode) {
      root["error"] = ErrorCodes::VerifyCodeErr;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return true;
    }

    // 原子消费验证码，防止重放。
    std::string consumed_code;
    if (!RedisMgr::GetInstance()->GetDel(CODEPREFIX + email, consumed_code) ||
        consumed_code != verify_code) {
      root["error"] = ErrorCodes::VerifyExpired;
      SendJson(connection, root, http::status::bad_request);
      return true;
    }

    // 根据email更新密码，通过DAO层操作数据库
    bool b_up = MysqlMgr::GetInstance()->UpdatePwdByEmail(email, pwd_plain); //
    if (!b_up) {
      std::cout << " update pwd failed" << std::endl;
      root["error"] = ErrorCodes::PasswdUpFailed;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return true;
    }

    std::cout << "succeed to update password (by email) for " << email
              << std::endl;
    root["error"] = 0;
    root["email"] = email;
    std::string jsonstr = root.toStyledString();
    beast::ostream(connection->_response.body()) << jsonstr;
    connection->WriteResponse();
    return true;
  });

  RegPost("/user_login", [this](std::shared_ptr<HttpConnection> connection) {
    auto body_str =
        boost::beast::buffers_to_string(connection->_request.body().data());
    std::cout << "[request] body_len=" << body_str.size() << std::endl;
    connection->_response.set(http::field::content_type, "application/json");

    Json::Value root;
    Json::Reader reader;
    Json::Value src_root;
    if (!reader.parse(body_str, src_root)) {
      std::cout << "Failed to parse JSON data!" << std::endl;
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return true;
    }

    // 请求中的user字段可以是用户名或邮箱
    std::string identifier = src_root.get("user", "").asString();
    std::string pwd_plain = src_root.get("passwd", "").asString();

    if (identifier.empty() || pwd_plain.empty()) {
      root["error"] = ErrorCodes::PasswdInvalid;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return true;
    }

    if (!CheckRateLimit("rl_login_ip_" + connection->GetClientIp(), 60, 60)) {
      root["error"] = ErrorCodes::RateLimited;
      SendJson(connection, root, http::status::too_many_requests);
      return true;
    }

    // 调用DAO层验证用户密码
    UserInfo userInfo;
    bool pwd_valid =
        MysqlMgr::GetInstance()->CheckPwd(identifier, pwd_plain, userInfo);
    if (!pwd_valid) {
      std::cout << " user pwd not match" << std::endl;
      root["error"] = ErrorCodes::PasswdInvalid;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return true;
    }

    // 向StatusServer获取可用的chat server
    auto reply = StatusGrpcClient::GetInstance()->GetChatServer(userInfo.uid);
    if (reply.error() || reply.host().empty() || reply.port().empty()) {
      std::cout << "No chat server for uid=" << userInfo.uid
                << ", reply.error=" << reply.error() << ", host='"
                << reply.host() << "' port='" << reply.port() << "'\n";
      root["error"] = ErrorCodes::RPCGetFailed; // 如果没有可用的聊天服务器
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return true;
    }

    // 加载用户信息成功
    std::cout << "succeed to load userinfo uid is " << userInfo.uid
              << std::endl;

    // JWT is mandatory: never return a legacy UUID token.
    if (!_token_service) {
      root["error"] = ErrorCodes::RPCFailed;
      SendJson(connection, root, http::status::service_unavailable);
      return true;
    }
    auto jwt_span = im::tracing::StartSpan(
        "gate.jwt.issue",
        connection->_trace_span ? connection->_trace_span->Traceparent() : std::string{});
    TokenResult jwt_result =
        _token_service->Issue(userInfo.uid, "im.friend im.message");
    if (jwt_span) jwt_span->SetAttribute("enduser.id", static_cast<std::int64_t>(userInfo.uid));
    if (!jwt_result.ok) {
      std::cerr << "[Auth] user_login issue token failed: "
                << jwt_result.error << std::endl;
      root["error"] = ErrorCodes::RPCFailed;
      SendJson(connection, root, http::status::service_unavailable);
      return true;
    }

    // 写入用户基础信息到 Redis，供 ChatServer 读取
    // key: USER_BASE_INFO + uid
    try {
      std::string base_key =
          std::string(USER_BASE_INFO) + std::to_string(userInfo.uid);
      Json::Value redis_root;
      redis_root["uid"] = userInfo.uid;
      redis_root["name"] = userInfo.name.empty() ? identifier : userInfo.name;
      redis_root["email"] = userInfo.email;
      redis_root["pwd"] = userInfo.pwd;
      redis_root["nick"] = "";
      redis_root["desc"] = "";
      redis_root["sex"] = 0;
      redis_root["icon"] = "";
      RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
    } catch (...) {
      // 忽略缓存失败，不影响登录主流程
    }

    root["error"] = 0;
    root["user"] = userInfo.name.empty() ? identifier : userInfo.name;
    root["uid"] = userInfo.uid;
    // Backward-compatible field name; it is now the JWT access token.
    root["token"] = jwt_result.tokens.access_token;
    root["host"] = reply.host();
    root["port"] = reply.port();
    root["access_token"] = jwt_result.tokens.access_token;
    root["refresh_token"] = jwt_result.tokens.refresh_token;
    root["expires_in"] = jwt_result.tokens.expires_in;
    root["refresh_expires_in"] = jwt_result.tokens.refresh_expires_in;
    root["scope"] = jwt_result.tokens.scope;
    beast::ostream(connection->_response.body()) << root.toStyledString();
    connection->WriteResponse();
    return true;
  });

  // 搜索好友API
  RegPost("/search_friends", [this](std::shared_ptr<HttpConnection> connection) {
    if (!RequireAuth(connection, "im.friend")) {
      return;
    }
    auto body_str =
        boost::beast::buffers_to_string(connection->_request.body().data());
    std::cout << "receive search_friends body is " << body_str << std::endl;
    connection->_response.set(http::field::content_type, "application/json");

    Json::Value root;
    Json::Reader reader;
    Json::Value src_root;
    if (!reader.parse(body_str, src_root)) {
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return;
    }

    const int auth_uid = connection->GetAuthUid();
    int uid = auth_uid > 0 ? auth_uid : src_root.get("uid", 0).asInt();
    std::string keyword = src_root.get("keyword", "").asString();

    if (uid <= 0 || keyword.empty()) {
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return;
    }

    // 调用数据库搜索用户
    std::cout << "[LogicSystem] /search_friends 请求 - uid: " << uid
              << " keyword: \"" << keyword << "\"" << std::endl;
    auto users = MysqlMgr::GetInstance()->SearchUsers(keyword);
    std::cout << "[LogicSystem] 数据库返回 " << users.size() << " 个用户"
              << std::endl;

    root["error"] = 0;
    Json::Value usersArray(Json::arrayValue);
    for (const auto &user : users) {
      Json::Value userObj;
      userObj["uid"] = user.uid;
      userObj["name"] = user.name;
      userObj["email"] = user.email;
      userObj["nick"] = user.nick;
      userObj["icon"] = user.icon;
      userObj["sex"] = user.sex;
      userObj["desc"] = user.desc;
      // 检查是否已经是好友
      userObj["isFriend"] = MysqlMgr::GetInstance()->IsFriend(uid, user.uid);
      usersArray.append(userObj);
      std::cout << "[LogicSystem] 添加用户到响应: uid=" << user.uid
                << " name=" << user.name << std::endl;
    }
    root["users"] = usersArray;
    std::cout << "[LogicSystem] 返回 " << usersArray.size()
              << " 个用户的JSON响应" << std::endl;

    beast::ostream(connection->_response.body()) << root.toStyledString();
    connection->WriteResponse();
    return;
  });

  // 获取好友申请列表API
  RegPost(
      "/get_friend_requests", [this](std::shared_ptr<HttpConnection> connection) {
        if (!RequireAuth(connection, "im.friend")) {
          return;
        }
        auto body_str =
            boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive get_friend_requests body is " << body_str
                  << std::endl;
        connection->_response.set(http::field::content_type,
                                  "application/json");

        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        if (!reader.parse(body_str, src_root)) {
          root["error"] = ErrorCodes::Error_Json;
          beast::ostream(connection->_response.body()) << root.toStyledString();
          connection->WriteResponse();
          return;
        }

        const int auth_uid = connection->GetAuthUid();
        int uid = auth_uid > 0 ? auth_uid : src_root.get("uid", 0).asInt();
        if (uid <= 0) {
          root["error"] = ErrorCodes::Error_Json;
          beast::ostream(connection->_response.body()) << root.toStyledString();
          connection->WriteResponse();
          return;
        }

        // 获取好友申请列表
        auto requests = MysqlMgr::GetInstance()->GetFriendRequests(uid);

        root["error"] = 0;
        Json::Value requestsArray(Json::arrayValue);
        for (const auto &request : requests) {
          Json::Value requestObj;
          requestObj["uid"] = request._uid;
          requestObj["name"] = request._name;
          requestObj["desc"] = request._desc;
          requestObj["icon"] = request._icon;
          requestObj["nick"] = request._nick;
          requestObj["sex"] = request._sex;
          requestObj["status"] = request._status;
          requestsArray.append(requestObj);
        }
        root["requests"] = requestsArray;

        beast::ostream(connection->_response.body()) << root.toStyledString();
        connection->WriteResponse();
        return;
      });

  // 获取我的好友列表API
  RegPost("/get_my_friends", [this](std::shared_ptr<HttpConnection> connection) {
    if (!RequireAuth(connection, "im.friend")) {
      return;
    }
    auto body_str =
        boost::beast::buffers_to_string(connection->_request.body().data());
    std::cout << "receive get_my_friends body is " << body_str << std::endl;
    connection->_response.set(http::field::content_type, "application/json");

    Json::Value root;
    Json::Reader reader;
    Json::Value src_root;
    if (!reader.parse(body_str, src_root)) {
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return;
    }

    const int auth_uid = connection->GetAuthUid();
    int uid = auth_uid > 0 ? auth_uid : src_root.get("uid", 0).asInt();
    if (uid <= 0) {
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return;
    }

    // 获取我的好友列表
    auto friends = MysqlMgr::GetInstance()->GetMyFriends(uid);

    root["error"] = 0;
    Json::Value friendsArray(Json::arrayValue);
    for (const auto &friendObject : friends) {
      Json::Value friendObj;
      friendObj["uid"] = friendObject.uid;
      friendObj["name"] = friendObject.name;
      friendObj["email"] = friendObject.email;
      friendObj["nick"] = friendObject.nick;
      friendObj["icon"] = friendObject.icon;
      friendObj["sex"] = friendObject.sex;
      friendObj["desc"] = friendObject.desc;
      friendsArray.append(friendObj);
    }
    root["friends"] = friendsArray;

    beast::ostream(connection->_response.body()) << root.toStyledString();
    connection->WriteResponse();
    return;
  });

  // 发送好友申请API
  RegPost("/send_friend_request", [this](std::shared_ptr<HttpConnection>
                                         connection) {
    if (!RequireAuth(connection, "im.friend")) {
      return;
    }
    auto body_str =
        boost::beast::buffers_to_string(connection->_request.body().data());
    std::cout << "receive send_friend_request body is " << body_str
              << std::endl;
    connection->_response.set(http::field::content_type, "application/json");

    Json::Value root;
    Json::Reader reader;
    Json::Value src_root;
    if (!reader.parse(body_str, src_root)) {
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return;
    }

    const int auth_uid = connection->GetAuthUid();
    int fromUid = auth_uid > 0 ? auth_uid : src_root.get("from_uid", 0).asInt();
    int toUid = src_root.get("to_uid", 0).asInt();
    std::string desc = src_root.get("desc", "").asString();
    // [FriendNotify]
    std::cout << "[FriendNotify][Gate] /send_friend_request parsed from_uid="
              << fromUid << " to_uid=" << toUid << " desc=\"" << desc << "\""
              << std::endl;

    if (fromUid <= 0 || toUid <= 0 || fromUid == toUid) {
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return;
    }

    // 检查是否已经是好友
    if (MysqlMgr::GetInstance()->IsFriend(fromUid, toUid)) {
      root["error"] = ErrorCodes::UserExist; // 已经是好友
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return;
    }

    // 发送好友申请
    bool success =
        MysqlMgr::GetInstance()->AddFriendRequest(fromUid, toUid, desc);
    // [FriendNotify]
    std::cout << "[FriendNotify][Gate] DB AddFriendRequest result success="
              << std::boolalpha << success << std::endl;

    root["error"] =
        success ? 0 : ErrorCodes::UserExist; // 如果失败，可能是重复申请

    // 发布 friend.apply 事件，供 ChatServer 推送TCP通知
    Json::Value ev;
    ev["type"] = "apply";
    ev["from_uid"] = fromUid;
    ev["to_uid"] = toUid;
    ev["desc"] = desc;
    ev["error"] = root["error"].asInt();
    {
      auto payload = ev.toStyledString();
      std::cout << "[FriendNotify][Gate] publish channel=friend.apply payload="
                << payload << std::endl;
      bool pubok = RedisMgr::GetInstance()->Publish("friend.apply", payload);
      std::cout << "[FriendNotify][Gate] publish friend.apply result="
                << std::boolalpha << pubok << std::endl;
    }

    beast::ostream(connection->_response.body()) << root.toStyledString();
    connection->WriteResponse();
    return;
  });

  // 回复好友申请API
  RegPost("/reply_friend_request", [this](std::shared_ptr<HttpConnection>
                                          connection) {
    if (!RequireAuth(connection, "im.friend")) {
      return;
    }
    auto body_str =
        boost::beast::buffers_to_string(connection->_request.body().data());
    std::cout << "receive reply_friend_request body is " << body_str
              << std::endl;
    connection->_response.set(http::field::content_type, "application/json");

    Json::Value root;
    Json::Reader reader;
    Json::Value src_root;
    if (!reader.parse(body_str, src_root)) {
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return;
    }

    const int auth_uid = connection->GetAuthUid();
    int fromUid = src_root.get("from_uid", 0).asInt();
    const int body_to_uid = src_root.get("to_uid", 0).asInt();
    if (auth_uid > 0 && body_to_uid > 0 && body_to_uid != auth_uid) {
      Json::Value denied;
      denied["error"] = ErrorCodes::TokenInvalid;
      SendJson(connection, denied, http::status::forbidden);
      return;
    }
    int toUid = auth_uid > 0 ? auth_uid : body_to_uid;
    bool agree = src_root.get("agree", false).asBool();
    // [FriendNotify]
    std::cout << "[FriendNotify][Gate] /reply_friend_request parsed from_uid="
              << fromUid << " to_uid=" << toUid << " agree=" << std::boolalpha
              << agree << std::endl;

    if (fromUid <= 0 || toUid <= 0) {
      root["error"] = ErrorCodes::Error_Json;
      beast::ostream(connection->_response.body()) << root.toStyledString();
      connection->WriteResponse();
      return;
    }

    // 回复好友申请
    bool success =
        MysqlMgr::GetInstance()->ReplyFriendRequest(fromUid, toUid, agree);
    // [FriendNotify]
    std::cout << "[FriendNotify][Gate] DB ReplyFriendRequest result success="
              << std::boolalpha << success << std::endl;

    root["error"] = success ? 0 : ErrorCodes::PasswdUpFailed;

    // 发布 friend.reply 事件，供 ChatServer 推送TCP通知
    Json::Value ev;
    ev["type"] = "reply";
    ev["from_uid"] = fromUid;
    ev["to_uid"] = toUid;
    ev["agree"] = agree;
    ev["error"] = root["error"].asInt();
    {
      auto payload = ev.toStyledString();
      std::cout << "[FriendNotify][Gate] publish channel=friend.reply payload="
                << payload << std::endl;
      bool pubok = RedisMgr::GetInstance()->Publish("friend.reply", payload);
      std::cout << "[FriendNotify][Gate] publish friend.reply result="
                << std::boolalpha << pubok << std::endl;
    }

    beast::ostream(connection->_response.body()) << root.toStyledString();
    connection->WriteResponse();
    return;
  });

  // OAuth2-style token service (JWT access + rotating refresh token).
  RegPost("/oauth/token", [this](std::shared_ptr<HttpConnection> connection) {
    HandleOAuthToken(connection);
  });
  RegPost("/oauth/revoke", [this](std::shared_ptr<HttpConnection> connection) {
    HandleOAuthRevoke(connection);
  });
  RegGet("/.well-known/jwks.json", [this](std::shared_ptr<HttpConnection> connection) {
    HandleJwks(connection);
  });
}

bool LogicSystem::HasScope(const std::string& scopes, const std::string& required) {
  if (required.empty()) {
    return true;
  }
  size_t pos = 0;
  while (pos < scopes.size()) {
    const size_t space = scopes.find(' ', pos);
    const std::string token = scopes.substr(
        pos, space == std::string::npos ? std::string::npos : space - pos);
    if (token == required) {
      return true;
    }
    if (space == std::string::npos) {
      break;
    }
    pos = space + 1;
  }
  return false;
}

bool LogicSystem::CheckRateLimit(const std::string& key, int limit, int window_seconds) {
  long long count = 0;
  if (!RedisMgr::GetInstance()->Incr(key, count)) {
    return false;
  }
  if (count == 1) {
    RedisMgr::GetInstance()->Expire(key, window_seconds);
  }
  return count <= limit;
}

bool LogicSystem::RequireAuth(const std::shared_ptr<HttpConnection>& con,
                              const std::string& required_scope) {
  if (!_jwt_verifier) {
    // JWT not enabled: keep the legacy body-uid flow for compatibility.
    con->SetAuthUid(0);
    return true;
  }

  const auto authorization = con->_request[http::field::authorization];
  const std::string header(authorization.data(), authorization.size());
  constexpr const char* kPrefix = "Bearer ";
  if (header.rfind(kPrefix, 0) != 0) {
    SendJson(con, OAuthError("invalid_token", "missing Bearer token"),
             http::status::unauthorized);
    return false;
  }

  const std::string token = header.substr(std::strlen(kPrefix));
  im::auth::VerifiedJwt verified;
  std::string error;
  if (!_jwt_verifier->Verify(token, verified, error)) {
    SendJson(con, OAuthError("invalid_token", error),
             http::status::unauthorized);
    return false;
  }
  if (!HasScope(verified.claims.scope, required_scope)) {
    SendJson(con, OAuthError("insufficient_scope", "missing scope: " + required_scope),
             http::status::forbidden);
    return false;
  }
  if (!verified.claims.jti.empty() &&
      RedisMgr::GetInstance()->ExistsKey("jwt:blacklist:" + verified.claims.jti)) {
    SendJson(con, OAuthError("invalid_token", "token revoked"),
             http::status::unauthorized);
    return false;
  }

  con->SetAuthUid(verified.claims.uid);
  return true;
}

void LogicSystem::HandleOAuthToken(const std::shared_ptr<HttpConnection>& con) {
  auto jwt_span = im::tracing::StartSpan(
      "gate.jwt.issue",
      con->_trace_span ? con->_trace_span->Traceparent() : std::string{});
  if (!_token_service) {
    SendJson(con, OAuthError("temporarily_unavailable", "JWT service is disabled"),
             http::status::service_unavailable);
    return;
  }

  const auto params = ParseTokenRequest(ReadBody(con));
  const auto grant_it = params.find("grant_type");
  const std::string grant_type = grant_it == params.end() ? "" : grant_it->second;

  if (grant_type == "password") {
    const std::string username = params.count("username") ? params.at("username") : "";
    const std::string password = params.count("password") ? params.at("password") : "";
    if (username.empty() || password.empty()) {
      SendJson(con, OAuthError("invalid_request", "username and password are required"),
               http::status::bad_request);
      return;
    }

    UserInfo user;
    if (!MysqlMgr::GetInstance()->CheckPwd(username, password, user)) {
      SendJson(con, OAuthError("invalid_grant", "invalid username or password"),
               http::status::bad_request);
      return;
    }

    const std::string scope = params.count("scope") ? params.at("scope") : "";
    const TokenResult result = _token_service->Issue(user.uid, scope);
    if (!result.ok) {
      std::cerr << "[Auth] issue token failed: " << result.error << std::endl;
      SendJson(con, OAuthError("server_error", "failed to issue token"),
               http::status::internal_server_error);
      return;
    }
    if (jwt_span) jwt_span->SetAttribute("enduser.id", static_cast<std::int64_t>(user.uid));

    Json::Value out;
    out["access_token"] = result.tokens.access_token;
    out["token_type"] = "Bearer";
    out["expires_in"] = result.tokens.expires_in;
    out["refresh_token"] = result.tokens.refresh_token;
    out["refresh_expires_in"] = result.tokens.refresh_expires_in;
    out["scope"] = result.tokens.scope;
    out["uid"] = result.tokens.uid;
    SendJson(con, out);
    return;
  }

  if (grant_type == "refresh_token") {
    const std::string refresh_token =
        params.count("refresh_token") ? params.at("refresh_token") : "";
    if (refresh_token.empty()) {
      SendJson(con, OAuthError("invalid_request", "refresh_token is required"),
               http::status::bad_request);
      return;
    }

    const TokenResult result = _token_service->Refresh(refresh_token);
    if (!result.ok) {
      SendJson(con, OAuthError("invalid_grant", result.error),
               http::status::bad_request);
      return;
    }

    Json::Value out;
    out["access_token"] = result.tokens.access_token;
    out["token_type"] = "Bearer";
    out["expires_in"] = result.tokens.expires_in;
    out["refresh_token"] = result.tokens.refresh_token;
    out["refresh_expires_in"] = result.tokens.refresh_expires_in;
    out["scope"] = result.tokens.scope;
    out["uid"] = result.tokens.uid;
    SendJson(con, out);
    return;
  }

  SendJson(con, OAuthError("unsupported_grant_type", "supported: password, refresh_token"),
           http::status::bad_request);
}

void LogicSystem::HandleOAuthRevoke(const std::shared_ptr<HttpConnection>& con) {
  if (!_token_service) {
    SendJson(con, OAuthError("temporarily_unavailable", "JWT service is disabled"),
             http::status::service_unavailable);
    return;
  }

  const auto params = ParseTokenRequest(ReadBody(con));
  std::string token;
  if (params.count("token")) {
    token = params.at("token");
  } else if (params.count("refresh_token")) {
    token = params.at("refresh_token");
  }
  if (token.empty()) {
    SendJson(con, OAuthError("invalid_request", "token is required"),
             http::status::bad_request);
    return;
  }

  // If this is a valid access token, blacklist its jti until expiry.
  if (_jwt_verifier) {
    im::auth::VerifiedJwt verified;
    std::string verify_error;
    if (_jwt_verifier->Verify(token, verified, verify_error) &&
        !verified.claims.jti.empty()) {
      const auto now = std::chrono::system_clock::now();
      auto ttl = std::chrono::duration_cast<std::chrono::seconds>(
                     verified.claims.expires_at - now)
                     .count();
      if (ttl < 1) {
        ttl = 1;
      }
      RedisMgr::GetInstance()->SetEx(
          "jwt:blacklist:" + verified.claims.jti, "1", static_cast<int>(ttl));
      SendJson(con, Json::Value(Json::objectValue));
      return;
    }
  }

  std::string error;
  if (!_token_service->Revoke(token, error)) {
    std::cerr << "[Auth] revoke failed: " << error << std::endl;
    SendJson(con, OAuthError("server_error", "failed to revoke token"),
             http::status::internal_server_error);
    return;
  }

  SendJson(con, Json::Value(Json::objectValue));
}

void LogicSystem::HandleJwks(const std::shared_ptr<HttpConnection>& con) {
  if (_jwt_public_pem.empty() || _jwt_key_id.empty()) {
    SendJson(con, OAuthError("temporarily_unavailable", "JWKS is not configured"),
             http::status::service_unavailable);
    return;
  }

  try {
    const std::string jwks = im::auth::BuildJwksJson(_jwt_public_pem, _jwt_key_id);
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(jwks, root)) {
      SendJson(con, OAuthError("server_error", "failed to build JWKS"),
               http::status::internal_server_error);
      return;
    }
    SendJson(con, root);
  } catch (const std::exception& e) {
    std::cerr << "[Auth] JWKS generation failed: " << e.what() << std::endl;
    SendJson(con, OAuthError("server_error", "failed to build JWKS"),
             http::status::internal_server_error);
  }
}

bool LogicSystem::HandleGet(std::string path,
                            std::shared_ptr<HttpConnection> con) {
  if (_get_handlers.find(path) == _get_handlers.end()) {
    return false;
  }

  std::cout << "[HandleGet] path = " << path << std::endl;

  _get_handlers[path](con);
  return true;
}

bool LogicSystem::HandlePost(std::string path,
                             std::shared_ptr<HttpConnection> con) {
  if (_post_handlers.find(path) == _post_handlers.end()) {
    return false;
  }

  _post_handlers[path](con);
  return true;
}


