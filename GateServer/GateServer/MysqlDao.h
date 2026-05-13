#pragma once
#include <cppconn/connection.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>
#include <iostream>
#include <chrono>
#include <vector>
#include <string>

#include "const.h"
#include "data.h"

// ------------------ MySqlPool ------------------
class MySqlPool {
public:
    MySqlPool();
    // Init 连接 url，格式为 "tcp://127.0.0.1:3306"
    void Init(const std::string& url,
        const std::string& user,
        const std::string& pass,
        const std::string& schema,
        int poolSize);

    // 获取数据库连接
    std::unique_ptr<sql::Connection> getConnection();

    // 归还数据库连接
    void returnConnection(std::unique_ptr<sql::Connection> con);

    void Close();

    ~MySqlPool();

private:
    std::string host_;
    std::string url_;
    std::string user_;
    std::string pass_;
    std::string schema_;
    int poolSize_ = 0;

    std::queue<std::unique_ptr<sql::Connection>> pool_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> b_stop_{ false };
};

// ------------------ MysqlDao ------------------
class MysqlDao {
public:
    MysqlDao();
    ~MysqlDao();

    int RegUser(const std::string& name,
        const std::string& email,
        const std::string& pwd);

    bool CheckEmail(const std::string& name, const std::string& email);
    bool UpdatePwd(const std::string& name, const std::string& newpwd);
    bool CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo);
    // 根据邮箱更新密码
    bool UpdatePwdByEmail(const std::string& email, const std::string& newpwdPlain);

    // 好友相关接口/好友管理接口（主要供 ChatServer 调用）
    // 说明：为了支持 /search_friends 等接口，添加 DAO 层方法
    std::vector<UserInfo> SearchUsers(const std::string& keyword);
    bool AddFriendRequest(int fromUid, int toUid, const std::string& desc);
    std::vector<ApplyInfo> GetFriendRequests(int uid);
    bool ReplyFriendRequest(int fromUid, int toUid, bool agree);
    std::vector<UserInfo> GetMyFriends(int uid);
    bool IsFriend(int uid1, int uid2);

private:
    std::shared_ptr<MySqlPool> pool_;
};