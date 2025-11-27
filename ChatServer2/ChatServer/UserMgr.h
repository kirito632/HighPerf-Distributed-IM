#pragma once
#include"Singleton.h"
#include<unordered_map>
#include<memory>
#include<mutex>

class CSession;

// UserMgr类：用户管理器
// 
// 作用：
//   管理用户ID到CSession的映射，提供用户会话的增删改查
// 
// 设计模式：
//   单例模式，确保全局唯一的用户管理器实例
// 
// 主要功能：
//   - 建立和维护用户ID与会话的映射关系
//   - 提供根据用户ID获取会话的方法
//   - 支持会话的添加和删除
class UserMgr : public Singleton<UserMgr>
{
    friend class Singleton<UserMgr>;  // 允许Singleton访问私有构造函数
public:
    ~UserMgr();

    // 根据用户ID获取会话
    // 参数：
    //   - uid: 用户ID
    // 返回值：
    //   如果找到返回会话指针，否则返回nullptr
    std::shared_ptr<CSession> GetSession(int uid);

    // 设置用户会话
    // 参数：
    //   - uid: 用户ID
    //   - session: 会话指针
    // 作用：
    //   建立用户ID与会话的映射关系
    void SetUserSession(int uid, std::shared_ptr<CSession> session);

    // 移除用户会话
    // 参数：
    //   - uid: 用户ID
    // 作用：
    //   清除用户ID与会话的映射关系
    void RmvUserSession(int uid);

private:
    UserMgr();

    std::mutex _session_mtx;

    // 用户ID到会话的映射表
    std::unordered_map<int, std::shared_ptr<CSession>> _uid_to_session;
};

