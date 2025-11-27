#include "ChatGrpcClient.h"

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
ChatGrpcClient::ChatGrpcClient()
{
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
        // 检查server配置是否存在
        if (cfg[word]["Name"].empty()) {
            continue;
        }
        // 创建连接池（每个连接池5个连接）
        _pools[cfg[word]["Name"]] = std::make_unique<ChatConPool>(5, cfg[word]["Host"], cfg[word]["Port"]);
    }

}

// 通知添加好友（当前未实现）
AddFriendRsp ChatGrpcClient::NotifyAddFriend(std::string server_ip, const AddFriendReq& req) {
    AddFriendRsp rsp;
    return rsp;
}

// 通知认证好友（当前未实现）
AuthFriendRsp ChatGrpcClient::NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req) {
    AuthFriendRsp rsp;
    return rsp;
}

// 获取用户基础信息（当前未实现）
bool ChatGrpcClient::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo) {
    return true;
}

// 通知文本聊天消息（当前未实现）
TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(std::string server_ip,
    const TextChatMsgReq& req, const Json::Value& rtvalue) {

    TextChatMsgRsp rsp;
    return rsp;
}
