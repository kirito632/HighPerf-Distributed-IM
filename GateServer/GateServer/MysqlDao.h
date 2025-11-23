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

// ����ԭ������Ŀͷ
#include "const.h"
#include "data.h"

// ------------------ MySqlPool ------------------
class MySqlPool {
public:
    MySqlPool();
    // Init ���� url������ "tcp://127.0.0.1:3306"
    void Init(const std::string& url,
        const std::string& user,
        const std::string& pass,
        const std::string& schema,
        int poolSize);

    // �ӳ�����ȡһ�����ӣ����û�У��������ȴ���
    std::unique_ptr<sql::Connection> getConnection();

    // ��������ӷŻس���
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
    // ��������������������루���ϲ�ӿ�ƥ�䣩
    bool UpdatePwdByEmail(const std::string& email, const std::string& newpwdPlain);

    // ������������/������ؽӿ��������� ChatServer ���룩
    // �����˵����Ϊ֧�� /search_friends �Ƚӿڣ����� DAO �㷽��
    std::vector<UserInfo> SearchUsers(const std::string& keyword);
    bool AddFriendRequest(int fromUid, int toUid, const std::string& desc);
    std::vector<ApplyInfo> GetFriendRequests(int uid);
    bool ReplyFriendRequest(int fromUid, int toUid, bool agree);
    std::vector<UserInfo> GetMyFriends(int uid);
    bool IsFriend(int uid1, int uid2);

private:
    std::shared_ptr<MySqlPool> pool_;
};