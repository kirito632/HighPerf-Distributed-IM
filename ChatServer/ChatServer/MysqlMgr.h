#pragma once
#include "const.h"
#include "MysqlDao.h"
#include "data.h"
#include <vector>
class MysqlMgr : public Singleton<MysqlMgr>
{
    friend class Singleton<MysqlMgr>;
public:
    ~MysqlMgr();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    bool CheckEmail(const std::string& name, const std::string& email);
    bool UpdatePwdByEmail(const std::string& email, const std::string& pwd);
    bool CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo);
    std::shared_ptr<UserInfo> GetUser(int uid);

    std::shared_ptr<UserInfo> GetUserByName(const std::string& name);

    std::vector<ApplyInfo> GetFriendRequests(int uid);
    bool ReplyFriendRequest(int fromUid, int toUid, bool agree);
    std::vector<UserInfo> GetMyFriends(int uid);
    bool IsFriend(int uid1, int uid2);
    bool SaveChatMessage(int fromUid, int toUid,
                         const std::string& clientMsgId,
                         const std::string& conversationId,
                         const std::string& initialPayload,
                         bool writeOutbox,
                         const std::function<std::string(long long)>& finalPayload,
                         long long& serverMsgId);
    bool UpdateChatMessagePayload(long long serverMsgId, const std::string& payload);
    bool GetPendingOutbox(int limit, std::vector<MysqlDao::OutboxRow>& rows);
    bool MarkOutboxPublished(const std::vector<long long>& ids);
    bool MarkOutboxFailed(long long id, const std::string& error);
    bool IsEventProcessed(const std::string& event_id);
    bool MarkEventProcessed(const std::string& event_id,
                            const std::string& event_type,
                            const std::string& consumer);
    bool IsMessageUnread(int to_uid, long long server_msg_id);
    bool GetUnreadChatMessages(int uid, long long cursor, int limit,
                               std::vector<long long>& ids,
                               std::vector<std::string>& payloads);
    bool DeleteChatMessagesByIds(const std::vector<long long>& ids);
    bool AckOfflineMessages(int uid, const std::vector<long long>& ids);
private:
    MysqlMgr();
    MysqlDao  _dao;
};

