#ifndef USERMGR_H
#define USERMGR_H
#include <QObject>
#include <memory>
#include <singleton.h>
#include <QReadWriteLock>

// UserMgr类：用户管理器（单例模式）
// 
// 作用：
//   存储和管理当前登录用户的信息
//   提供线程安全的用户信息访问接口
// 
// 设计模式：
//   1. 单例模式（Singleton）- 确保全局唯一的用户管理器
//   2. enable_shared_from_this - 用于异步操作中的对象生命周期管理
// 
// 用户信息：
//   - uid: 用户ID
//   - name: 用户名
//   - token: 认证令牌（用于ChatServer登录）
// 
// 线程安全：
//   使用QReadWriteLock保证多线程访问安全
class UserMgr : public QObject,
                public Singleton<UserMgr>,
                public std::enable_shared_from_this<UserMgr>
{
    Q_OBJECT
public:
    friend class Singleton<UserMgr>;  // 允许Singleton访问私有构造函数

    // 析构函数
    ~UserMgr() override;

    // ========== 设置用户信息（线程安全）==========
    
    // 设置用户名
    void SetName(const QString &name);
    
    // 设置用户ID
    void SetUid(int uid);
    
    // 设置认证令牌
    void SetToken(const QString &token);

    // ========== 获取用户信息（线程安全）==========
    
    // 获取用户名
    QString GetName() const;
    
    // 获取用户ID
    int GetUid() const;
    
    // 获取认证令牌
    QString GetToken() const;

    // 调试：打印当前用户状态
    // 返回格式化的用户信息字符串
    QString dump() const;

private:
    // 私有构造函数：单例模式
    UserMgr();

    // 成员变量（受读写锁保护）
    mutable QReadWriteLock _lock;  // 读写锁（保证线程安全）
    QString _name;                 // 用户名
    QString _token;                // 认证令牌
    int _uid = 0;                  // 用户ID
};

#endif // USERMGR_H
