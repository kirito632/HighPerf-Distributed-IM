#include "ChatGrpcClient.h"
#include"RedisMgr.h"
#include"ConfigMgr.h"
#include"UserMgr.h"
#include"CSession.h"
#include"MysqlMgr.h"
#include <algorithm>
#include <cctype>
#include <iostream>

// 构造函数：初始化ChatGrpcClient
// 
// 作用：
//   从配置文件中读取所有ChatServer的列表，为每个ChatServer创建连接池
// 
// 实现逻辑：
//   1. 从配置文件读取ChatServer列表（逗号分隔的server名称）
//   2. 解析server列表
//   3. 为每个server创建连接池
//   4. 使用server的Name作为key，存储在unordered_map中
ChatGrpcClient::ChatGrpcClient() {
    auto& cfg = ConfigMgr::Inst();
    auto server_list = cfg["PeerServer"]["Servers"];

    std::vector<std::string> words;

    // 解析逗号分隔的server列表
    std::stringstream ss(server_list);
    std::string word;

    while (std::getline(ss, word, ',')) {
        words.push_back(word);
    }

    // 为每个server创建连接池
    for (auto& word : words) {
        // 去除首尾空白
        std::string section = word;
        while (!section.empty() && std::isspace(static_cast<unsigned char>(section.front()))) section.erase(section.begin());
        while (!section.empty() && std::isspace(static_cast<unsigned char>(section.back()))) section.pop_back();
        // 1) 优先按 PeerServer 列表中给出的节名查找
        // 2) 若未命中，尝试把 chatserverX -> ChatServerX（与 config.ini 节名一致）
        if (cfg[section]["Name"].empty()) {
            if (section.rfind("chatserver", 0) == 0) {
                std::string suffix = section.substr(std::string("chatserver").size());
                std::string alt = std::string("ChatServer") + suffix;
                if (!cfg[alt]["Name"].empty()) {
                    section = alt;
                }
            }
        }
        // 3) 仍未命中则跳过
        if (cfg[section]["Name"].empty()) {
            continue;
        }

        // 使用小写 Name 作为连接池键（与 Redis 存储的小写 server_name 对齐）
        std::string name_key = cfg[section]["Name"];
        std::transform(name_key.begin(), name_key.end(), name_key.begin(), ::tolower);

        // gRPC 连接应使用 RPCPort，而非 TCP 服务的 Port
        std::string host = cfg[section]["Host"];
        std::string rpc_port = cfg[section]["RPCPort"];
        if (rpc_port.empty()) {
            // 兜底：如果没有配置 RPCPort，则退回 Port（但不推荐）
            rpc_port = cfg[section]["Port"];
        }
        _pools[name_key] = std::make_unique<ChatConPool>(5, host, rpc_port);
        std::cout << "[gRPC][Pool] add section=" << section
                  << " name_key=" << name_key
                  << " host=" << host
                  << " rpc_port=" << rpc_port << std::endl;
    }
}


// 通知添加好友（当前未实现）
AddFriendRsp ChatGrpcClient::NotifyAddFriend(std::string server_ip, const AddFriendReq& req)
{
    AddFriendRsp rsp;
    Defer defer([&rsp, &req]() {
        rsp.set_error(ErrorCodes::Success);
        rsp.set_applyuid(req.applyuid());
        rsp.set_touid(req.touid());
        });

    auto find_iter = _pools.find(server_ip);
    if (find_iter == _pools.end())
    {
        rsp.set_error(ErrorCodes::RPCFailed);
        return rsp;
    }

    auto& pool = find_iter->second;
    ClientContext context;
    auto stub = pool->getConnection();
    Status status = stub->NotifyAddFriend(&context, req, &rsp);
    Defer defercon([&stub, this, &pool]() {
        pool->returnConnection(std::move(stub));
        });

    if (!status.ok()) {
        rsp.set_error(ErrorCodes::RPCFailed);
        return rsp;
    }

    return rsp;
}

// 通知认证好友（当前未实现）
AuthFriendRsp ChatGrpcClient::NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req)
{
    return AuthFriendRsp();
}

// 获取用户基础信息（当前未实现）
bool ChatGrpcClient::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
    return false;
}

// 通知文本聊天消息（当前未实现）
TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(std::string server_ip, const TextChatMsgReq& req)
{
    TextChatMsgRsp rsp;
    // 预置返回值，默认 Success，并把请求内容回填
    Defer defer([&rsp, &req]() {
        rsp.set_error(ErrorCodes::Success);
        rsp.set_fromuid(req.fromuid());
        rsp.set_touid(req.touid());
        for (const auto& text_data : req.textmsgs()) {
            TextChatData* new_msg = rsp.add_textmsgs();
            new_msg->set_msgid(text_data.msgid());
            new_msg->set_msgcontent(text_data.msgcontent());
        }
        });

    // 查找连接池时统一使用小写键
    std::string key = server_ip;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    std::cout << "[TextChat][gRPC][Client] call target=" << server_ip
              << " normalized=" << key << std::endl;
    auto find_iter = _pools.find(key);
    if (find_iter == _pools.end()) {
        std::cout << "[TextChat][gRPC][Client] pool not found for key=" << key << " pools=";
        for (auto &kv : _pools) std::cout << kv.first << ' ';
        std::cout << std::endl;
        rsp.set_error(ErrorCodes::RPCFailed);
        return rsp;
    }

    auto& pool = find_iter->second;
    ClientContext context;
    auto stub = pool->getConnection();
    if (!stub) {
        rsp.set_error(ErrorCodes::RPCFailed);
        return rsp;
    }
    Status status = stub->NotifyTextChatMsg(&context, req, &rsp);
    Defer defercon([&stub, this, &pool]() {
        pool->returnConnection(std::move(stub));
        });

    if (!status.ok()) {
        std::cout << "[TextChat][gRPC][Client] rpc failed ok=false error_code=" << status.error_code()
                  << " error_message=" << status.error_message() << std::endl;
        rsp.set_error(ErrorCodes::RPCFailed);
        return rsp;
    }
    std::cout << "[TextChat][gRPC][Client] rpc ok" << std::endl;

    return rsp;
}
