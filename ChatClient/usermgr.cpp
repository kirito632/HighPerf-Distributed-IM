#include "usermgr.h"
#include <QDebug>

// 构造函数：初始化用户管理器
// 
// 初始化逻辑：
//   1. 初始化用户名为空
//   2. 初始化token为空
//   3. 初始化uid为0
UserMgr::UserMgr()
    : QObject(nullptr),
    _name(),
    _token(),
    _uid(0)
{
    // 可在此做必要初始化
}

// 析构函数：清理资源
UserMgr::~UserMgr()
{
    // 清理（如果需要）
}

// 设置用户名（线程安全）
// 
// 参数：
//   - name: 用户名
// 
// 实现：
//   使用写锁保护，确保并发安全
void UserMgr::SetName(const QString &name)
{
    QWriteLocker locker(&_lock);
    _name = name;
}

// 设置用户ID（线程安全）
// 
// 参数：
//   - uid: 用户ID
// 
// 实现：
//   使用写锁保护，确保并发安全
void UserMgr::SetUid(int uid)
{
    QWriteLocker locker(&_lock);
    _uid = uid;
}

// 设置认证令牌（线程安全）
// 
// 参数：
//   - token: 认证令牌
// 
// 实现：
//   使用写锁保护，确保并发安全
void UserMgr::SetToken(const QString &token)
{
    QWriteLocker locker(&_lock);
    _token = token;
}

// 获取用户名（线程安全）
// 
// 返回值：
//   用户名
// 
// 实现：
//   使用读锁保护，允许多个读操作并发
QString UserMgr::GetName() const
{
    QReadLocker locker(&_lock);
    return _name;
}

// 获取用户ID（线程安全）
// 
// 返回值：
//   用户ID
// 
// 实现：
//   使用读锁保护，允许多个读操作并发
int UserMgr::GetUid() const
{
    QReadLocker locker(&_lock);
    return _uid;
}

// 获取认证令牌（线程安全）
// 
// 返回值：
//   认证令牌
// 
// 实现：
//   使用读锁保护，允许多个读操作并发
QString UserMgr::GetToken() const
{
    QReadLocker locker(&_lock);
    return _token;
}

// 打印当前用户状态（线程安全）
// 
// 返回值：
//   格式化的用户信息字符串
// 
// 格式：
//   "UserMgr{ uid=xxx, name='xxx', token='xxx' }"
QString UserMgr::dump() const
{
    QReadLocker locker(&_lock);
    return QString("UserMgr{ uid=%1, name='%2', token='%3' }")
        .arg(_uid).arg(_name).arg(_token);
}
