#include "MysqlMgr.h"

// MysqlMgr类：作为业务逻辑层和DAO层之间的中间层
// 
// 作用：
//   封装MysqlDao，提供更简单、统一的接口给业务逻辑层使用
//   负责将业务逻辑层的请求转发给DAO层处理
// 
// 设计模式：
//   门面模式（Facade）：简化复杂的数据访问接口
//   委托模式（Delegation）：所有操作委托给MysqlDao

// 析构函数：清理资源
MysqlMgr::~MysqlMgr() {

}

// 注册用户
// 
// 功能：将用户信息添加到数据库
// 
// 实现：
//   直接委托给MysqlDao处理
int MysqlMgr::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
    return _dao.RegUser(name, email, pwd);
}

// 检查邮箱是否与用户名匹配
// 
// 功能：验证用户名对应的邮箱是否正确
// 
// 实现：
//   直接委托给MysqlDao处理
bool MysqlMgr::CheckEmail(const std::string& name, const std::string& email)
{
    return _dao.CheckEmail(name, email);
}

// 根据邮箱更新密码
// 
// 功能：通过邮箱修改用户密码
// 
// 实现：
//   直接委托给MysqlDao处理
//   注意：参数是email和新密码（明文），DAO层会进行哈希处理
bool MysqlMgr::UpdatePwdByEmail(const std::string& email, const std::string& pwd)
{
    // 【修改】委托到 DAO：按邮箱更新密码
    return _dao.UpdatePwdByEmail(email, pwd);
}

// 检查密码是否正确
// 
// 功能：验证用户密码是否匹配
// 
// 实现：
//   直接委托给MysqlDao处理
//   支持用户名或邮箱登录
bool MysqlMgr::CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo)
{
    return _dao.CheckPwd(email, pwd, userInfo);
}

// 构造函数：初始化MysqlMgr
// 
// 实现逻辑：
//   创建MysqlDao实例，该实例会自动获取MySQL连接池单例
MysqlMgr::MysqlMgr() {
}

// --------- 好友管理功能实现 ---------

// 搜索用户
// 
// 功能：根据关键词搜索用户（用户名或邮箱）
// 
// 实现：
//   委托给MysqlDao处理
//   返回最多20个匹配结果
std::vector<UserInfo> MysqlMgr::SearchUsers(const std::string& keyword)
{
    // 【修改】委托到 DAO：搜索用户
    return _dao.SearchUsers(keyword);
}

// 添加好友申请
// 
// 功能：发送好友申请
// 
// 实现：
//   委托给MysqlDao处理
//   会自动检查是否已存在未处理的申请
bool MysqlMgr::AddFriendRequest(int fromUid, int toUid, const std::string& desc)
{
    // 【修改】委托到 DAO：添加好友申请
    return _dao.AddFriendRequest(fromUid, toUid, desc);
}

// 获取好友申请列表
// 
// 功能：获取用户收到的好友申请
// 
// 实现：
//   委托给MysqlDao处理
//   返回所有待处理（status=0）的申请
std::vector<ApplyInfo> MysqlMgr::GetFriendRequests(int uid)
{
    // 【修改】委托到 DAO：获取好友申请列表
    return _dao.GetFriendRequests(uid);
}

// 回复好友申请
// 
// 功能：处理好友申请（同意或拒绝）
// 
// 实现：
//   委托给MysqlDao处理
//   同意时会自动建立双向好友关系
bool MysqlMgr::ReplyFriendRequest(int fromUid, int toUid, bool agree)
{
    // 【修改】委托到 DAO：回复好友申请
    return _dao.ReplyFriendRequest(fromUid, toUid, agree);
}

// 获取我的好友列表
// 
// 功能：获取用户的所有好友
// 
// 实现：
//   委托给MysqlDao处理
//   按好友昵称升序排列
std::vector<UserInfo> MysqlMgr::GetMyFriends(int uid)
{
    // 【修改】委托到 DAO：获取我的好友列表
    return _dao.GetMyFriends(uid);
}

// 检查两个用户是否为好友
// 
// 功能：判断两个用户是否已建立好友关系
// 
// 实现：
//   委托给MysqlDao处理
//   检查双向的好友关系
bool MysqlMgr::IsFriend(int uid1, int uid2)
{
    // 【修改】委托到 DAO：检查是否为好友
    return _dao.IsFriend(uid1, uid2);
}
