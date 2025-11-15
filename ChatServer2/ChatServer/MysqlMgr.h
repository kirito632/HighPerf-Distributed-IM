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

    // 好友相关功能
    std::vector<UserInfo> SearchUsers(const std::string& keyword);
    bool AddFriendRequest(int fromUid, int toUid, const std::string& desc);
    std::vector<ApplyInfo> GetFriendRequests(int uid);
    bool ReplyFriendRequest(int fromUid, int toUid, bool agree);
    std::vector<UserInfo> GetMyFriends(int uid);
    bool IsFriend(int uid1, int uid2);
    bool SaveChatMessage(int fromUid, int toUid, const std::string& payload) { return _dao.SaveChatMessage(fromUid, toUid, payload); }
    bool GetUnreadChatMessages(int uid, std::vector<long long>& ids, std::vector<std::string>& payloads) { return _dao.GetUnreadChatMessagesWithIds(uid, ids, payloads); }
    bool DeleteChatMessagesByIds(const std::vector<long long>& ids) { return _dao.DeleteChatMessagesByIds(ids); }
private:
    MysqlMgr();
    MysqlDao  _dao;
};

