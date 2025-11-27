#include "ChatServiceImpl.h"
#include"UserMgr.h"
#include"CSession.h"
#include<json/json.h>
#include<json/value.h>
#include<json/reader.h>
#include"RedisMgr.h"
#include"MysqlMgr.h"

// 构造函数：初始化ChatServiceImpl
ChatServiceImpl::ChatServiceImpl()
{
}

// 通知添加好友（当前用于内部RPC路径）
Status ChatServiceImpl::NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* reply)
{
    // 查找用户是否在本服务器
    auto touid = request->touid();
    auto session = UserMgr::GetInstance()->GetSession(touid);

    // [FriendNotify]
    std::cout << "[FriendNotify][Chat][gRPC] NotifyAddFriend applyuid=" << request->applyuid()
        << " to_uid=" << request->touid() << " name=\"" << request->name() << "\""
        << " has_session=" << std::boolalpha << (session != nullptr) << std::endl;

    Defer defer([reply, request]() {
        reply->set_error(ErrorCodes::Success);
        reply->set_applyuid(request->applyuid());
        reply->set_touid(request->touid());
        });

    // 用户不在内存中，直接返回
    if (session == nullptr) {
        // [FriendNotify]
        std::cout << "[FriendNotify][Chat][gRPC] target user offline, skip TCP notify" << std::endl;
        return Status::OK;
    }

    // 在内存中则直接发送通知对方
    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;
    rtvalue["applyuid"] = request->applyuid();
    rtvalue["touid"] = request->touid();
    rtvalue["name"] = request->name();
    rtvalue["desc"] = request->desc();

    std::string return_str = rtvalue.toStyledString();

    // [FriendNotify]
    std::cout << "[FriendNotify][Chat][gRPC] send TCP notify uid=" << touid
        << " msgid=" << ID_NOTIFY_ADD_FRIEND
        << " body=" << return_str << std::endl;

    session->Send(return_str, ID_NOTIFY_ADD_FRIEND);

    return Status::OK;
}

// 通知认证好友（当前用于内部RPC路径）
Status ChatServiceImpl::NotifyAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* response)
{
    // [FriendNotify]
    std::cout << "[FriendNotify][Chat][gRPC] NotifyAuthFriend from_uid=" << request->fromuid()
        << " to_uid=" << request->touid() << std::endl;
    return Status::OK;
}

// 通知文本聊天消息（跨服投递至目标服，再向目标会话下发 1019）
Status ChatServiceImpl::NotifyTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* response)
{
    // 诊断：打印收到的 gRPC 通知及目标在线情况（稍后再判断）
    std::cout << "[TextChat][gRPC] NotifyTextChatMsg recv fromuid=" << request->fromuid()
              << " touid=" << request->touid()
              << " msgs=" << request->textmsgs_size() << std::endl;
    // 统一填充回包（回显请求内容）
    response->set_error(ErrorCodes::Success);
    response->set_fromuid(request->fromuid());
    response->set_touid(request->touid());
    for (const auto& text_data : request->textmsgs()) {
        TextChatData* new_msg = response->add_textmsgs();
        new_msg->set_msgid(text_data.msgid());
        new_msg->set_msgcontent(text_data.msgcontent());
    }

    // 目标用户是否在本服在线
    auto touid = request->touid();
    auto session = UserMgr::GetInstance()->GetSession(touid);
    if (session == nullptr) {
        std::cout << "[TextChat][gRPC] target uid=" << touid << " offline on this server, setting error to RecipientOffline" << std::endl;
        response->set_error(ErrorCodes::RecipientOffline);
        return Status::OK;
    }

    // 组装下发给客户端的 JSON
    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;
    rtvalue["fromuid"] = request->fromuid();
    rtvalue["touid"] = request->touid();
    Json::Value text_array;
    for (const auto& msg : request->textmsgs()) {
        Json::Value element;
        element["content"] = msg.msgcontent();
        element["msgid"] = msg.msgid();
        text_array.append(element);
    }
    rtvalue["text_array"] = text_array;

    std::string return_str = rtvalue.toStyledString();
    std::cout << "[TextChat][gRPC] send TCP 1019 to uid=" << touid
              << " body_len=" << return_str.size() << std::endl;
    session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
    return Status::OK;
}

// 获取用户基础信息（当前未实现）
bool ChatServiceImpl::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
    return true;
}
