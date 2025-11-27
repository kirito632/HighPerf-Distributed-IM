#include "LogicSystem.h"
#include"HttpConnection.h"
#include"VerifyGrpcClient.h"
#include"RedisMgr.h"
#include"MysqlMgr.h"
#include"StatusGrpcClient.h"
#include"crypto_utils.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include "const.h"

// 注册POST请求处理器,,,,应该写成ReqPost的，写错了，以后再改
// 参数：
//   - url: 请求路径
//   - handler: 处理函数
void LogicSystem::RegPost(std::string url, HttpHandler handler)
{
    _post_handlers.emplace(make_pair(url, handler));
}

// 注册GET请求处理器
// 参数：
//   - url: 请求路径
//   - handler: 处理函数
void LogicSystem::RegGet(std::string url, HttpHandler handler)
{
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
    // 注册获取验证码API
    // 功能：通过gRPC调用VerifyServer获取验证码，并存储到Redis
    // 请求参数：{"email": "xxx@example.com"}
    // 返回：{"error": 错误码, "email": "xxx@example.com"}
    RegPost("/get_verifycode", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive body is " << body_str << std::endl;

        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
        if (!parse_success) {
            std::cout << "Failed to parse Json data!" << std::endl;
            root["error"] = ErrorCodes::Error_Json;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;

            //      д ??   
            connection->_response.set(http::field::content_type, "application/json"); //  ?? ?    
            connection->WriteResponse();
            return true;
        }

        if (!src_root.isMember("email")) {
            std::cout << "Failed to parse Json data!" << std::endl;
            root["error"] = ErrorCodes::Error_Json;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;

            //      д ??   
            connection->_response.set(http::field::content_type, "application/json");
            connection->WriteResponse();
            return true;
        }

        auto email = src_root["email"].asString();
        GetVerifyRsp rsp = VerifyGrpcClient::GetInstance()->GetVerifyCode(email);
        std::cout << "email is " << email << " , rsp.error = " << rsp.error() << std::endl;

        root["error"] = rsp.error();
        root["email"] = src_root["email"];
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;

        //      д ??   
        connection->_response.set(http::field::content_type, "application/json");
        connection->WriteResponse();
        return true;
        });

    RegPost("/user_register", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive body is " << body_str << std::endl;

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

        //  ?  redis    email   ?    ?   ?    
        std::string verify_code;
        bool b_get_verify = RedisMgr::GetInstance()->Get(CODEPREFIX + email, verify_code);
        if (!b_get_verify) {
            std::cout << " get verify code expired" << std::endl;
            root["error"] = ErrorCodes::VerifyExpired;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;

            //      д ??   
            connection->_response.set(http::field::content_type, "application/json");
            connection->WriteResponse();
            return true;
        }

        if (verify_code != src_root["verifycode"].asString()) {
            std::cout << " verify code error" << std::endl;
            root["error"] = ErrorCodes::VerifyCodeErr;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;

            //      д ??   
            connection->_response.set(http::field::content_type, "application/json");
            connection->WriteResponse();
            return true;
        }

        // register的时候用哈希
        std::string hashed = sha256_hex(pwd);

        //      ?  ж  ?  ?    
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
        //root["passwd"] = pwd;
        root["confirm"] = confirm;
        root["verifycode"] = src_root["verifycode"].asString();
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;

        connection->_response.set(http::field::content_type, "application/json");
        connection->WriteResponse();
        return true;
        });

    RegPost("/reset_password", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive body is " << body_str << std::endl;
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
        // ?      ?   ?μ  ? passwd    new_password  
        std::string pwd_plain = src_root.isMember("passwd") ? src_root["passwd"].asString()
            : src_root.get("new_password", "").asString();
        std::string verifycode = src_root.get("verifycode", "").asString();

        if (email.empty() || pwd_plain.empty() || verifycode.empty()) {
            root["error"] = ErrorCodes::Error_Json;
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        // У   Redis  е   ? ?     ? ? 
        std::string verify_code;
        bool b_get_verify = RedisMgr::GetInstance()->Get(CODEPREFIX + email, verify_code);
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

        //        ?? ?  email    ?  DAO  DAO       ?  
        bool b_up = MysqlMgr::GetInstance()->UpdatePwdByEmail(email, pwd_plain); //       
        if (!b_up) {
            std::cout << " update pwd failed" << std::endl;
            root["error"] = ErrorCodes::PasswdUpFailed;
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        std::cout << "succeed to update password (by email) for " << email << std::endl;
        root["error"] = 0;
        root["email"] = email;
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;
        connection->WriteResponse();
        return true;
        });

    RegPost("/user_login", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive body is " << body_str << std::endl;
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

        // ? ?        user  ?ο      ?   ?         ?   ? ? ? 
        std::string identifier = src_root.get("user", "").asString();
        std::string pwd_plain = src_root.get("passwd", "").asString();

        // 临时调试日志：打印密码长度与十六进制（不打印明文）
        auto rtrim_copy = [](std::string s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t' || s.back() == '\0')) {
                s.pop_back();
            }
            return s;
            };
        auto is_hex_64 = [](const std::string& s) {
            if (s.size() != 64) return false;
            for (unsigned char c : s) {
                if (!std::isxdigit(c)) return false;
            }
            return true;
            };
        std::ostringstream pwd_hex;
        pwd_hex << std::hex << std::setfill('0');
        size_t dump_len = std::min<size_t>(pwd_plain.size(), 64);
        for (size_t i = 0; i < dump_len; ++i) {
            pwd_hex << std::setw(2) << static_cast<int>(static_cast<unsigned char>(pwd_plain[i]));
        }
        bool has_trailing_ws = pwd_plain.size() != rtrim_copy(pwd_plain).size();
        bool looks_hex64 = is_hex_64(pwd_plain);
        std::cout << "[login] passwd_len=" << pwd_plain.size()
            << ", first_hex=" << pwd_hex.str()
            << ", looks_hex64=" << (looks_hex64 ? 1 : 0)
            << ", has_trailing_ws=" << (has_trailing_ws ? 1 : 0) << std::endl;

        if (identifier.empty() || pwd_plain.empty()) {
            root["error"] = ErrorCodes::PasswdInvalid;
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        //      handler     ?           ?  DAO     DAO     ? ??     ?  
        UserInfo userInfo;
        bool pwd_valid = MysqlMgr::GetInstance()->CheckPwd(identifier, pwd_plain, userInfo);
        if (!pwd_valid) {
            std::cout << " user pwd not match" << std::endl;
            root["error"] = ErrorCodes::PasswdInvalid;
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        //   ? StatusServer   ?    ?  chat server
        auto reply = StatusGrpcClient::GetInstance()->GetChatServer(userInfo.uid);
        if (reply.error() || reply.host().empty() || reply.port().empty()) {
            std::cout << "No chat server for uid=" << userInfo.uid
                << ", reply.error=" << reply.error()
                << ", host='" << reply.host() << "' port='" << reply.port() << "'\n";
            root["error"] = ErrorCodes::RPCGetFailed; //    ?    NoChatServer
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        //   ? ?       ?
        std::cout << "succeed to load userinfo uid is " << userInfo.uid << std::endl;

        // 写入用户基础信息到 Redis，供 ChatServer 读取
        // key: USER_BASE_INFO + uid
        try {
            std::string base_key = std::string(USER_BASE_INFO) + std::to_string(userInfo.uid);
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
        }
        catch (...) {
            // 忽略缓存失败，不影响登录主流程
        }

        root["error"] = 0;
        root["user"] = userInfo.name.empty() ? identifier : userInfo.name;
        root["uid"] = userInfo.uid;
        root["token"] = reply.token();
        root["host"] = reply.host();
        root["port"] = reply.port();
        beast::ostream(connection->_response.body()) << root.toStyledString();
        connection->WriteResponse();
        return true;
        });

    // 搜索好友API
    RegPost("/search_friends", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive search_friends body is " << body_str << std::endl;
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

        int uid = src_root.get("uid", 0).asInt();
        std::string keyword = src_root.get("keyword", "").asString();

        if (uid <= 0 || keyword.empty()) {
            root["error"] = ErrorCodes::Error_Json;
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        // 调用数据库搜索用户
        std::cout << "[LogicSystem] /search_friends 请求 - uid: " << uid << " keyword: \"" << keyword << "\"" << std::endl;
        auto users = MysqlMgr::GetInstance()->SearchUsers(keyword);
        std::cout << "[LogicSystem] 数据库返回 " << users.size() << " 个用户" << std::endl;

        root["error"] = 0;
        Json::Value usersArray(Json::arrayValue);
        for (const auto& user : users) {
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
            std::cout << "[LogicSystem] 添加用户到响应: uid=" << user.uid << " name=" << user.name << std::endl;
        }
        root["users"] = usersArray;
        std::cout << "[LogicSystem] 返回 " << usersArray.size() << " 个用户的JSON响应" << std::endl;

        beast::ostream(connection->_response.body()) << root.toStyledString();
        connection->WriteResponse();
        return true;
        });

    // 获取好友申请列表API
    RegPost("/get_friend_requests", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive get_friend_requests body is " << body_str << std::endl;
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

        int uid = src_root.get("uid", 0).asInt();
        if (uid <= 0) {
            root["error"] = ErrorCodes::Error_Json;
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        // 获取好友申请列表
        auto requests = MysqlMgr::GetInstance()->GetFriendRequests(uid);

        root["error"] = 0;
        Json::Value requestsArray(Json::arrayValue);
        for (const auto& request : requests) {
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
        return true;
        });

    // 获取我的好友列表API
    RegPost("/get_my_friends", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive get_my_friends body is " << body_str << std::endl;
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

        int uid = src_root.get("uid", 0).asInt();
        if (uid <= 0) {
            root["error"] = ErrorCodes::Error_Json;
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        // 获取我的好友列表
        auto friends = MysqlMgr::GetInstance()->GetMyFriends(uid);

        root["error"] = 0;
        Json::Value friendsArray(Json::arrayValue);
        for (const auto& friendObject : friends) {
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
        return true;
        });

    // 发送好友申请API
    RegPost("/send_friend_request", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive send_friend_request body is " << body_str << std::endl;
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

        int fromUid = src_root.get("from_uid", 0).asInt();
        int toUid = src_root.get("to_uid", 0).asInt();
        std::string desc = src_root.get("desc", "").asString();
        // [FriendNotify]
        std::cout << "[FriendNotify][Gate] /send_friend_request parsed from_uid=" << fromUid
            << " to_uid=" << toUid << " desc=\"" << desc << "\"" << std::endl;

        if (fromUid <= 0 || toUid <= 0 || fromUid == toUid) {
            root["error"] = ErrorCodes::Error_Json;
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        // 检查是否已经是好友
        if (MysqlMgr::GetInstance()->IsFriend(fromUid, toUid)) {
            root["error"] = ErrorCodes::UserExist; // 已经是好友
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        // 发送好友申请
        bool success = MysqlMgr::GetInstance()->AddFriendRequest(fromUid, toUid, desc);
        // [FriendNotify]
        std::cout << "[FriendNotify][Gate] DB AddFriendRequest result success=" << std::boolalpha << success << std::endl;

        root["error"] = success ? 0 : ErrorCodes::UserExist; // 如果失败，可能是重复申请

        // 发布 friend.apply 事件，供 ChatServer 推送TCP通知
        Json::Value ev;
        ev["type"] = "apply";
        ev["from_uid"] = fromUid;
        ev["to_uid"] = toUid;
        ev["desc"] = desc;
        ev["error"] = root["error"].asInt();
        {
            auto payload = ev.toStyledString();
            std::cout << "[FriendNotify][Gate] publish channel=friend.apply payload=" << payload << std::endl;
            bool pubok = RedisMgr::GetInstance()->Publish("friend.apply", payload);
            std::cout << "[FriendNotify][Gate] publish friend.apply result=" << std::boolalpha << pubok << std::endl;
        }

        beast::ostream(connection->_response.body()) << root.toStyledString();
        connection->WriteResponse();
        return true;
        });

    // 回复好友申请API
    RegPost("/reply_friend_request", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive reply_friend_request body is " << body_str << std::endl;
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

        int fromUid = src_root.get("from_uid", 0).asInt();
        int toUid = src_root.get("to_uid", 0).asInt();
        bool agree = src_root.get("agree", false).asBool();
        // [FriendNotify]
        std::cout << "[FriendNotify][Gate] /reply_friend_request parsed from_uid=" << fromUid
            << " to_uid=" << toUid << " agree=" << std::boolalpha << agree << std::endl;

        if (fromUid <= 0 || toUid <= 0) {
            root["error"] = ErrorCodes::Error_Json;
            beast::ostream(connection->_response.body()) << root.toStyledString();
            connection->WriteResponse();
            return true;
        }

        // 回复好友申请
        bool success = MysqlMgr::GetInstance()->ReplyFriendRequest(fromUid, toUid, agree);
        // [FriendNotify]
        std::cout << "[FriendNotify][Gate] DB ReplyFriendRequest result success=" << std::boolalpha << success << std::endl;

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
            std::cout << "[FriendNotify][Gate] publish channel=friend.reply payload=" << payload << std::endl;
            bool pubok = RedisMgr::GetInstance()->Publish("friend.reply", payload);
            std::cout << "[FriendNotify][Gate] publish friend.reply result=" << std::boolalpha << pubok << std::endl;
        }

        beast::ostream(connection->_response.body()) << root.toStyledString();
        connection->WriteResponse();
        return true;
        });

}


bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> con)
{
    if (_get_handlers.find(path) == _get_handlers.end()) {
        return false;
    }

    std::cout << " ?  POST      path = " << path << std::endl;


    _get_handlers[path](con);
    return true;
}

bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> con)
{
    if (_post_handlers.find(path) == _post_handlers.end()) {
        return false;
    }

    _post_handlers[path](con);
    return true;
}
