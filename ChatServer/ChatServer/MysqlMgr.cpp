#include "MysqlMgr.h"

MysqlMgr::~MysqlMgr() {

}

int MysqlMgr::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
    return _dao.RegUser(name, email, pwd);
}

bool MysqlMgr::CheckEmail(const std::string& name, const std::string& email)
{
    return _dao.CheckEmail(name, email);
}

bool MysqlMgr::UpdatePwdByEmail(const std::string& name, const std::string& pwd)
{
    return _dao.UpdatePwdByEmail(name, pwd);
}

bool MysqlMgr::CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo)
{
    return _dao.CheckPwd(email, pwd, userInfo);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(int uid)
{
    auto userInfo = std::make_shared<UserInfo>();
    if (_dao.GetUser(uid, *userInfo)) {
        return userInfo;
    }
    return nullptr;
}

std::shared_ptr<UserInfo> MysqlMgr::GetUserByName(const std::string& name)
{
    return _dao.GetUserByName(name);
}



MysqlMgr::MysqlMgr() {
}

// 好友相关功能实现

std::vector<ApplyInfo> MysqlMgr::GetFriendRequests(int uid)
{
    return _dao.GetFriendRequests(uid);
}

bool MysqlMgr::ReplyFriendRequest(int fromUid, int toUid, bool agree)
{
    return _dao.ReplyFriendRequest(fromUid, toUid, agree);
}

std::vector<UserInfo> MysqlMgr::GetMyFriends(int uid)
{
    return _dao.GetMyFriends(uid);
}

bool MysqlMgr::IsFriend(int uid1, int uid2)
{
    return _dao.IsFriend(uid1, uid2);
}

bool MysqlMgr::SaveChatMessage(int fromUid, int toUid,
                               const std::string& clientMsgId,
                               const std::string& conversationId,
                               const std::string& initialPayload,
                               bool writeOutbox,
                               const std::function<std::string(long long)>& finalPayload,
                               long long& serverMsgId)
{
    return _dao.SaveChatMessage(fromUid, toUid, clientMsgId, conversationId,
                                initialPayload, writeOutbox, finalPayload,
                                serverMsgId);
}

bool MysqlMgr::UpdateChatMessagePayload(long long serverMsgId, const std::string& payload)
{
    return _dao.UpdateChatMessagePayload(serverMsgId, payload);
}

bool MysqlMgr::GetPendingOutbox(int limit, std::vector<MysqlDao::OutboxRow>& rows)
{
    return _dao.GetPendingOutbox(limit, rows);
}

bool MysqlMgr::MarkOutboxPublished(const std::vector<long long>& ids)
{
    return _dao.MarkOutboxPublished(ids);
}

bool MysqlMgr::MarkOutboxFailed(long long id, const std::string& error)
{
    return _dao.MarkOutboxFailed(id, error);
}

bool MysqlMgr::IsEventProcessed(const std::string& event_id)
{
    return _dao.IsEventProcessed(event_id);
}

bool MysqlMgr::MarkEventProcessed(const std::string& event_id,
                                  const std::string& event_type,
                                  const std::string& consumer)
{
    return _dao.MarkEventProcessed(event_id, event_type, consumer);
}

bool MysqlMgr::IsMessageUnread(int to_uid, long long server_msg_id)
{
    return _dao.IsMessageUnread(to_uid, server_msg_id);
}

bool MysqlMgr::GetUnreadChatMessages(int uid, long long cursor, int limit,
                                     std::vector<long long>& ids,
                                     std::vector<std::string>& payloads)
{
    return _dao.GetUnreadChatMessagesWithIds(uid, cursor, limit, ids, payloads);
}

bool MysqlMgr::DeleteChatMessagesByIds(const std::vector<long long>& ids)
{
    return _dao.DeleteChatMessagesByIds(ids);
}

bool MysqlMgr::AckOfflineMessages(int uid, const std::vector<long long>& ids)
{
    return _dao.AckOfflineMessages(uid, ids);
}