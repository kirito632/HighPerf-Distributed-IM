#pragma once
#include "const.h"
#include "MysqlDao.h"

// MysqlMgr类：MySQL管理器，作为业务逻辑层和DAO层之间的中间层
// 
// 作用：
//   1. 封装MysqlDao，提供统一的数据库访问接口
//   2. 作为业务逻辑层和数据库访问层之间的桥梁
//   3. 提供单例模式，确保全局唯一的数据库管理器
// 
// 设计模式：
//   1. 单例模式（Singleton）- 确保全局唯一实例
//   2. 门面模式（Facade）- 简化数据库操作接口
//   3. 委托模式（Delegation）- 将实际操作委托给MysqlDao
// 
// 功能分类：
//   - 用户管理：注册、登录、密码管理
//   - 好友管理：搜索、申请、回复、列表查询
class MysqlMgr : public Singleton<MysqlMgr>
{
    friend class Singleton<MysqlMgr>;  // 允许Singleton访问私有构造函数
public:
    // 析构函数
    ~MysqlMgr();

    // --------- 用户管理功能 ---------

    // 注册用户
    // 参数：
    //   - name: 用户名
    //   - email: 邮箱
    //   - pwd: 密码（已哈希）
    // 返回值：
    //   成功返回用户ID，失败返回0或-1
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);

    // 检查邮箱是否与用户名匹配
    // 参数：
    //   - name: 用户名
    //   - email: 邮箱
    // 返回值：
    //   匹配返回true，否则返回false
    bool CheckEmail(const std::string& name, const std::string& email);

    // 根据邮箱更新密码
    // 参数：
    //   - email: 邮箱
    //   - pwd: 新密码（明文，内部会哈希）
    // 返回值：
    //   成功返回true，否则返回false
    bool UpdatePwdByEmail(const std::string& email, const std::string& pwd);

    // 检查密码是否正确
    // 参数：
    //   - email: 用户名或邮箱
    //   - pwd: 密码（明文）
    //   - userInfo: 输出参数，用户信息
    // 返回值：
    //   正确返回true，否则返回false
    bool CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo);

    // --------- 好友管理功能 ---------

    // 搜索用户
    // 参数：
    //   - keyword: 搜索关键词（用户名或邮箱）
    // 返回值：
    //   匹配的用户列表
    std::vector<UserInfo> SearchUsers(const std::string& keyword);

    // 添加好友申请
    // 参数：
    //   - fromUid: 申请者用户ID
    //   - toUid: 被申请者用户ID
    //   - desc: 申请描述
    // 返回值：
    //   成功返回true，否则返回false
    bool AddFriendRequest(int fromUid, int toUid, const std::string& desc);

    // 获取好友申请列表
    // 参数：
    //   - uid: 用户ID
    // 返回值：
    //   该用户收到的好友申请列表
    std::vector<ApplyInfo> GetFriendRequests(int uid);

    // 回复好友申请
    // 参数：
    //   - fromUid: 申请者用户ID
    //   - toUid: 被申请者用户ID
    //   - agree: 是否同意（true=同意，false=拒绝）
    // 返回值：
    //   成功返回true，否则返回false
    bool ReplyFriendRequest(int fromUid, int toUid, bool agree);

    // 获取我的好友列表
    // 参数：
    //   - uid: 用户ID
    // 返回值：
    //   该用户的好友列表
    std::vector<UserInfo> GetMyFriends(int uid);

    // 检查两个用户是否为好友
    // 参数：
    //   - uid1: 用户1的ID
    //   - uid2: 用户2的ID
    // 返回值：
    //   是好友返回true，否则返回false
    bool IsFriend(int uid1, int uid2);

private:
    // 私有构造函数：单例模式
    MysqlMgr();

    // MySQL数据访问对象（实际执行数据库操作）
    MysqlDao  _dao;
};

