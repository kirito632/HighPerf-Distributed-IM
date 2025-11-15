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
std::vector<UserInfo> MysqlMgr::SearchUsers(const std::string& keyword)
{
    return _dao.SearchUsers(keyword);
}

bool MysqlMgr::AddFriendRequest(int fromUid, int toUid, const std::string& desc)
{
    return _dao.AddFriendRequest(fromUid, toUid, desc);
}

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