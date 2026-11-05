#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"
#include "RedisMgr.h"

#include <algorithm>
#include <climits>
#include <sstream>
#include <cctype>
#include<grpc/grpc.h>

// 辅助函数：去除字符串首尾的空白字符
static inline std::string trim(const std::string& s) {
    size_t l = 0, r = s.size();
    while (l < r && std::isspace((unsigned char)s[l])) ++l;
    while (r > l && std::isspace((unsigned char)s[r - 1])) --r;
    return s.substr(l, r - l);
}

bool StatusServiceImpl::checkInternal(ServerContext* context) {
    auto& cfg = ConfigMgr::Inst();
    auto internal_cfg = cfg["Internal"];
    const std::string expected = internal_cfg["Token"];
    if (expected.empty()) {
        std::cerr << "[InternalAuth] Internal.Token is not configured\n";
        return false;
    }
    const auto& metadata = context->client_metadata();
    const auto it = metadata.find("x-internal-token");
    if (it == metadata.end()) {
        return false;
    }
    const std::string supplied(it->second.data(), it->second.size());
    return supplied == expected;
}

// 获取可用的ChatServer（负载均衡算法）
// 
// 返回值：
//   一个ChatServer结构体
// 
// 负载均衡策略：
//   最小连接数优先（Least Connections）
//   从Redis读取每个ChatServer的当前连接数，选择连接数最少的
// 
// 实现逻辑：
//   1. 遍历所有ChatServer
//   2. 从Redis读取每个ChatServer的当前连接数（HGET LOGIN_COUNT server_name）
//   3. 选择连接数最少的ChatServer
//   4. 如果没有记录，将连接数设为INT_MAX（优先选择有记录的服务器）
ChatServer StatusServiceImpl::getChatServer()
{
    std::lock_guard<std::mutex> guard(_server_mtx);

    ChatServer best;
    bool initialized = false;

    // 检查服务器列表是否为空
    if (_servers.empty()) {
        std::cerr << "[getChatServer] _servers is empty\n";
        return best;
    }

    // 遍历所有ChatServer，选择连接数最少的
    for (auto& kv : _servers) {
        ChatServer s = kv.second;

        // 从 Redis 读取当前服务器的登录记录数
        std::string count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, s.name);
        if (count_str.empty()) {
            s.con_count = INT_MAX; // 无记录 -> 视为最小值（优先选择有记录的）
        }
        else {
            try {
                s.con_count = std::stoi(count_str);
            }
            catch (...) {
                s.con_count = INT_MAX;
            }
        }

        // 更新最佳选择
        if (!initialized || s.con_count < best.con_count) {
            best = s;
            initialized = true;
        }
    }

    // 如果所有 server 的 con_count 都是 INT_MAX，表示 Redis 没有记录，也返回第一台服务器（验证不会返回空）
    if (!initialized && !_servers.empty()) {
        best = _servers.begin()->second;
    }

    std::cout << "[getChatServer] selected name=" << best.name << " host=" << best.host
        << " port=" << best.port << " con_count=" << best.con_count << std::endl;

    return best;
}

// 获取聊天服务器（gRPC方法实现）
// 
// 功能：
//   为请求的用户分配一个可用的ChatServer
// 
// 实现逻辑：
//   1. 调用getChatServer()选择一个负载最轻的ChatServer
//   2. 检查选中的ChatServer是否有效
//   3. 生成唯一的token
//   4. 将token存储到Redis
//   5. 返回ChatServer的host、port和token
Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply)
{
    if (!checkInternal(context)) {
        return Status(grpc::StatusCode::UNAUTHENTICATED, "missing internal token");
    }
    std::cout << "[GetChatServer] called with uid=" << request->uid() << std::endl;

    // 选择一个负载最轻的ChatServer
    ChatServer server = getChatServer();

    // 详细日志
    std::cout << "[GetChatServer] selected server: name=" << server.name
        << " host=" << server.host << " port=" << server.port
        << " con_count=" << server.con_count << std::endl;

    // 检查选中的服务器是否有效
    if (server.host.empty() || server.port.empty()) {
        std::cerr << "[GetChatServer] no valid chat server available\n";
        reply->set_error(ErrorCodes::ServerNotFound); // 1013
        return Status::OK;
    }

    // 设置返回信息；认证由 Gate 签发 JWT，Status 不再生成 token
    reply->set_host(server.host);
    reply->set_port(server.port);
    reply->set_error(ErrorCodes::Success);

    std::cout << "[GetChatServer] reply set host=" << server.host << " port=" << server.port << std::endl;
    return Status::OK;
}

// 用户登录验证（gRPC方法实现）
// 
// 功能：
//   旧 UUID token 校验接口；现在认证由 ChatServer 本地 JWT 完成。
//   该接口保留只为 proto 兼容，始终返回 TokenInvalid。
Status StatusServiceImpl::Login(ServerContext* context, const message::LoginReq* request, message::LoginRsp* reply)
{
    if (!checkInternal(context)) {
        return Status(grpc::StatusCode::UNAUTHENTICATED, "missing internal token");
    }
    reply->set_error(ErrorCodes::TokenInvalid);
    return Status::OK;
}

// 构造函数：初始化StatusServiceImpl
// 
// 作用：
//   从配置文件读取所有ChatServer的配置信息
// 
// 实现逻辑：
//   1. 从配置文件读取ChatServer列表（逗号分隔的section名称）
//   2. 解析section列表
//   3. 为每个section读取Name、Host、Port配置
//   4. 创建ChatServer结构体并添加到_servers
//   5. con_count初始化为INT_MAX（实际值在getChatServer时从Redis读取）
StatusServiceImpl::StatusServiceImpl() : _server_index(0)
{
    auto& cfg = ConfigMgr::Inst();
    auto server_list = cfg["chatservers"]["Name"];

    std::vector<std::string> words;
    std::stringstream ss(server_list);
    std::string word;

    // 解析逗号分隔的section列表
    while (std::getline(ss, word, ',')) {
        word = trim(word);
        if (!word.empty()) words.push_back(word);
    }

    // 为每个section读取配置
    for (auto& w : words) {
        std::string section = w;
        // 首先读取 [chatserverX].Name，如果没有配置则 section 本身作为 name
        std::string configured_name = cfg[section]["Name"];
        std::string server_name = trim(configured_name.empty() ? section : configured_name);

        ChatServer server;
        server.port = cfg[section]["Port"];
        server.host = cfg[section]["Host"];
        server.name = server_name;
        server.con_count = INT_MAX; // 初始化为最大值，实际值在 getChatServer 时从 Redis 读取

        // 检查配置是否完整
        if (server.name.empty() || server.host.empty() || server.port.empty()) {
            std::cerr << "[StatusServiceImpl::ctor] skip invalid config for section=" << section
                << " name=" << server.name << " host=" << server.host << " port=" << server.port << std::endl;
            continue;
        }

        _servers[server.name] = server;
    }

    // 打印加载的服务器列表
    std::cout << "[StatusServiceImpl::ctor] loaded servers:" << std::endl;
    for (auto& kv : _servers) {
        std::cout << "  " << kv.first << " -> " << kv.second.host << ":" << kv.second.port << std::endl;
    }
}
