#pragma once
#include "const.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>
#include <iostream>
#include <chrono>
#include <mysql_driver.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/exception.h>
#include <string>
#include "data.h"
/*数据库访问层（DAO  data access object）*/

// ------------------ MySqlPool ------------------
class MySqlPool {
public:
    MySqlPool() : poolSize_(0), b_stop_(false) {}

    // Init 接受 url，例如 "tcp://127.0.0.1:3306"
    void Init(const std::string& url,
        const std::string& user,
        const std::string& pass,
        const std::string& schema,
        int poolSize)
    {
        url_ = url;
        user_ = user;
        pass_ = pass;
        schema_ = schema;
        poolSize_ = poolSize;
        b_stop_ = false;

        std::cout << "[MySqlPool] Init called. url=" << url_
            << " user=" << user_ << " schema=" << schema_
            << " poolSize=" << poolSize_ << std::endl;

        sql::mysql::MySQL_Driver* driver = nullptr;
        try {
            driver = sql::mysql::get_mysql_driver_instance();
            if (!driver) {
                std::cerr << "[MySqlPool] get_mysql_driver_instance returned null!" << std::endl;
                throw std::runtime_error("driver null");
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[MySqlPool] get_mysql_driver_instance exception: " << e.what() << std::endl;
            throw;
        }
        catch (...) {
            std::cerr << "[MySqlPool] unknown exception getting driver" << std::endl;
            throw;
        }

        // 先单次尝试连接（便于定位）
        try {
            std::cout << "[MySqlPool] Trying single test connection..." << std::endl;
            std::unique_ptr<sql::Connection> testCon(driver->connect(url_, user_, pass_));
            if (!testCon) {
                std::cerr << "[MySqlPool] testCon is null after connect!" << std::endl;
                throw std::runtime_error("testCon null");
            }
            testCon->setSchema(schema_);
            std::cout << "[MySqlPool] single test connection ok" << std::endl;
        }
        catch (sql::SQLException& e) {
            std::cerr << "[MySqlPool] test connect failed (SQLException): " << e.what()
                << " (err:" << e.getErrorCode() << ", state:" << e.getSQLState() << ")" << std::endl;
            throw;
        }
        catch (const std::exception& e) {
            std::cerr << "[MySqlPool] test connect failed (std::exception): " << e.what() << std::endl;
            throw;
        }
        catch (...) {
            std::cerr << "[MySqlPool] test connect failed (unknown)" << std::endl;
            throw;
        }

        // 保存驱动指针，供后续 getConnection() 动态创建新连接
        driver_ = driver;
        
        // 如果 test 通过，逐个建池（每次都有 try/catch）
        for (int i = 0; i < poolSize_; ++i) {
            try {
                std::unique_ptr<sql::Connection> con(driver->connect(url_, user_, pass_));
                con->setSchema(schema_);
                pool_.push(std::move(con));
                std::cout << "[MySqlPool] push connection " << i << std::endl;
            }
            catch (sql::SQLException& e) {
                std::cerr << "[MySqlPool] connect #" << i << " failed: " << e.what()
                    << " (err:" << e.getErrorCode() << ", state:" << e.getSQLState() << ")" << std::endl;
                // 这里选择：继续尝试剩下的，或直接抛出。为了稳健，这里继续但打印错误。
            }
            catch (const std::exception& e) {
                std::cerr << "[MySqlPool] connect #" << i << " std::exception: " << e.what() << std::endl;
            }
            catch (...) {
                std::cerr << "[MySqlPool] connect #" << i << " unknown exception" << std::endl;
            }
        }

        std::cout << "[MySqlPool] Init done, actual pool size = " << pool_.size() << ", driver saved for lazy loading" << std::endl;
    }

    // 从池子里取一个连接，支持自动重建
    // 实现懒加载 + 自动重建机制：
    //   注意：不在这里检查 isClosed()，因为已经失效的连接调用 isClosed() 可能导致段错误
    //   连接的有效性在实际使用时通过异常处理来判断（在 MysqlDao 的各个方法中）
    //   如果连接在使用时失效，会抛出 SQLException，然后被 catch 块捕获并丢弃
    std::unique_ptr<sql::Connection> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        while (true) {
            cond_.wait(lock, [this] { return b_stop_ || !pool_.empty(); });
            if (b_stop_) return nullptr;
            if (pool_.empty()) continue;  // 继续等待

            auto con = std::move(pool_.front());
            pool_.pop();
            
            // 直接返回连接，有效性在使用时通过异常处理
            if (con) {
                return con;
            }
            
            // 如果连接为 nullptr，尝试创建新连接
            if (driver_) {
                try {
                    std::unique_ptr<sql::Connection> new_con(
                        driver_->connect(url_, user_, pass_)
                    );
                    new_con->setSchema(schema_);
                    std::cout << "[MySqlPool] Created replacement connection in getConnection" << std::endl;
                    return new_con;
                }
                catch (sql::SQLException& e) {
                    std::cerr << "[MySqlPool] Failed to create replacement connection: " << e.what() << std::endl;
                    // 新建失败，继续等待下一个可用连接
                    continue;
                }
            }
            
            // 驱动不可用，继续等待
            std::cerr << "[MySqlPool] Driver not available, waiting for next connection" << std::endl;
        }
    }

    // 用完把连接放回池子
    // 注意：不在这里检查连接有效性，因为已经失效的连接调用 isClosed() 可能导致段错误
    // 连接有效性检查在 getConnection() 中进行，这样更安全
    void returnConnection(std::unique_ptr<sql::Connection> con) {
        if (!con) return;
        
        std::unique_lock<std::mutex> lock(mutex_);
        if (b_stop_) return;
        
        // 直接放回池子，有效性检查在 getConnection 中进行
        pool_.push(std::move(con));
        cond_.notify_one();
    }

    void Close() {
        std::unique_lock<std::mutex> lock(mutex_);
        b_stop_ = true;
        while (!pool_.empty()) pool_.pop();
        cond_.notify_all();
        std::cout << "[MySqlPool] Closed pool" << std::endl;
    }

    ~MySqlPool() {
        Close();
    }

private:
    std::string host_;
    std::string url_;
    std::string user_;
    std::string pass_;
    std::string schema_;
    int poolSize_ = 0;

    // 保存 MySQL 驱动指针，用于动态创建新连接（懒加载）
    sql::mysql::MySQL_Driver* driver_ = nullptr;

    std::queue<std::unique_ptr<sql::Connection>> pool_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> b_stop_;
};

// RAII 连接守卫：自动归还连接，彻底杜绝泄漏
// 使用方式：
//   ConnectionGuard guard(pool_);
//   if (!guard) return false;
//   sql::Connection* con = guard.get();
//   // 使用 con，函数结束时自动归还
class ConnectionGuard {
public:
    ConnectionGuard(std::shared_ptr<MySqlPool> pool) : pool_(pool) {
        if (pool_) {
            con_ = pool_->getConnection();
        }
    }

    ~ConnectionGuard() {
        if (pool_ && con_) {
            pool_->returnConnection(std::move(con_));
        }
    }
    
    // 禁止拷贝
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
    
    // 允许移动
    ConnectionGuard(ConnectionGuard&& other) noexcept 
        : pool_(std::move(other.pool_)), con_(std::move(other.con_)) {}
    
    ConnectionGuard& operator=(ConnectionGuard&& other) noexcept {
        if (this != &other) {
            pool_ = std::move(other.pool_);
            con_ = std::move(other.con_);
        }
        return *this;
    }
    
    // 获取原始指针用于操作
    sql::Connection* get() { return con_.get(); }
    
    // 判断是否获取成功
    operator bool() const { return con_ != nullptr; }

private:
    std::shared_ptr<MySqlPool> pool_;
    std::unique_ptr<sql::Connection> con_;
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
    bool UpdatePwdByEmail(const std::string& email, const std::string& newpwd);
    bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
    bool GetUser(int uid, UserInfo& userInfo);
    std::shared_ptr<UserInfo> GetUserByName(const std::string& name);

    std::vector<ApplyInfo> GetFriendRequests(int uid);
    bool ReplyFriendRequest(int fromUid, int toUid, bool agree);
    std::vector<UserInfo> GetMyFriends(int uid);
    bool IsFriend(int uid1, int uid2);
    bool SaveChatMessage(int fromUid, int toUid, const std::string& payload);
    bool GetUnreadChatMessagesWithIds(int uid, std::vector<long long>& ids, std::vector<std::string>& payloads);
    bool DeleteChatMessagesByIds(const std::vector<long long>& ids);
    bool AckOfflineMessages(int uid, long long max_msg_id);
private:
    std::shared_ptr<MySqlPool> pool_;
};

