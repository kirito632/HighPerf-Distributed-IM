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

// ------------------ PooledConnection ------------------
// 池化连接：包含连接对象 + 最后使用时间戳
struct PooledConnection {
    std::unique_ptr<sql::Connection> conn;
    std::chrono::steady_clock::time_point last_used;
    
    PooledConnection(std::unique_ptr<sql::Connection> c) 
        : conn(std::move(c)), 
          last_used(std::chrono::steady_clock::now()) {}
    
    // 允许移动
    PooledConnection(PooledConnection&& other) noexcept
        : conn(std::move(other.conn)), last_used(other.last_used) {}
    
    PooledConnection& operator=(PooledConnection&& other) noexcept {
        if (this != &other) {
            conn = std::move(other.conn);
            last_used = other.last_used;
        }
        return *this;
    }
    
    // 禁止拷贝
    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;
};

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
                pool_.push(PooledConnection(std::move(con)));
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

        // 检查池子是否为空（防止死锁）
        if (pool_.empty()) {
            std::cerr << "[MySqlPool] CRITICAL: Pool is empty after Init! All connection attempts failed." << std::endl;
            throw std::runtime_error("[MySqlPool] Failed to initialize any database connections");
        }

        std::cout << "[MySqlPool] Init done, actual pool size = " << pool_.size() 
                  << " (min required: " << MIN_POOL_SIZE << "), driver saved for lazy loading" << std::endl;
    }

    // 从池子里取一个连接，支持自动重建
    // 实现懒加载 + 自动重建机制：
    //   - 超时机制：最多等待 3 秒，超时返回 nullptr（防止死锁）
    //   - 惰性检查：闲置 > 60s 的连接才执行 Ping 检查
    //   - 自动补充：坏连接销毁后尝试同步创建新连接
    // 调用者必须检查返回值是否为 nullptr
    std::unique_ptr<sql::Connection> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 设置超时时间为 3 秒，防止死锁
        if (cond_.wait_for(lock, std::chrono::seconds(3), 
            [this] { return b_stop_ || !pool_.empty(); })) {
            
            if (b_stop_) return nullptr;
            
            // 虚假唤醒检查：即使 wait_for 返回 true，pool 也可能为空
            if (pool_.empty()) {
                std::cerr << "[MySqlPool] Spurious wakeup detected, pool is empty" << std::endl;
                return nullptr;
            }
            
            // 成功获取连接
            auto pooledItem = std::move(pool_.front());
            pool_.pop();
            
            // 第二层：惰性检查（防失效）
            auto now = std::chrono::steady_clock::now();
            auto idle_duration = std::chrono::duration_cast<std::chrono::seconds>(
                now - pooledItem.last_used
            );
            
            bool isValid = true;
            if (idle_duration.count() > IDLE_THRESHOLD_SECONDS) {
                // 闲置超过 60 秒，执行 Ping 检查
                isValid = isConnectionValid(pooledItem.conn.get());
            }
            
            // 第三层：自动补充（防失效）
            if (!isValid) {
                std::cout << "[MySqlPool] Connection stale (idle " << idle_duration.count() 
                          << "s), attempting to reconnect..." << std::endl;
                
                // 销毁坏连接
                pooledItem.conn.reset();
                
                // 尝试创建新连接替换
                if (driver_) {
                    try {
                        std::unique_ptr<sql::Connection> new_con(
                            driver_->connect(url_, user_, pass_)
                        );
                        new_con->setSchema(schema_);
                        std::cout << "[MySqlPool] Reconnected successfully" << std::endl;
                        return new_con;
                    }
                    catch (sql::SQLException& e) {
                        std::cerr << "[MySqlPool] Failed to reconnect: " << e.what() << std::endl;
                        // 重连失败，返回 nullptr，让上层处理
                        return nullptr;
                    }
                }
                
                std::cerr << "[MySqlPool] Driver not available for reconnection" << std::endl;
                return nullptr;
            }
            
            // 连接有效，返回
            return std::move(pooledItem.conn);
        } else {
            // 超时：池子在 3 秒内仍未有可用连接
            std::cerr << "[MySqlPool] getConnection timeout after 3s, pool is empty" << std::endl;
            return nullptr;
        }
    }

    // 用完把连接放回池子
    // isHealthy=true：连接正常，放回池子
    // isHealthy=false：连接坏了，销毁并尝试补充新连接
    void returnConnection(std::unique_ptr<sql::Connection> con, bool isHealthy = true) {
        if (!con) return;
        
        std::unique_lock<std::mutex> lock(mutex_);
        if (b_stop_) return;
        
        if (isHealthy) {
            // 好连接：放回池子，更新时间戳为当前时间
            pool_.push(PooledConnection(std::move(con)));
            cond_.notify_one();
        } else {
            // 坏连接：销毁并尝试补充新连接
            con.reset();
            
            if (driver_) {
                try {
                    std::unique_ptr<sql::Connection> newCon(
                        driver_->connect(url_, user_, pass_)
                    );
                    newCon->setSchema(schema_);
                    pool_.push(PooledConnection(std::move(newCon)));
                    std::cout << "[MySqlPool] Replaced bad connection with new one" << std::endl;
                    cond_.notify_one();
                }
                catch (sql::SQLException& e) {
                    std::cerr << "[MySqlPool] Failed to create replacement connection: " << e.what() << std::endl;
                    // 补充失败，池子暂时缩容，依靠 getConnection 的重连逻辑恢复
                }
            }
        }
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
    // 配置参数
    static constexpr int IDLE_THRESHOLD_SECONDS = 60;      // 闲置多久才需要 Ping
    static constexpr int MIN_POOL_SIZE = 2;                 // 最小池子大小
    
    std::string host_;
    std::string url_;
    std::string user_;
    std::string pass_;
    std::string schema_;
    int poolSize_ = 0;

    // 保存 MySQL 驱动指针，用于动态创建新连接（懒加载）
    sql::mysql::MySQL_Driver* driver_ = nullptr;

    std::queue<PooledConnection> pool_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> b_stop_;
    
    // 辅助方法：检查连接是否有效（不持有锁调用）
    bool isConnectionValid(sql::Connection* con) {
        if (!con) return false;
        try {
            std::unique_ptr<sql::Statement> stmt(con->createStatement());
            stmt->execute("SELECT 1");
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[MySqlPool] Connection validation failed: " << e.what() << std::endl;
            return false;
        }
        catch (...) {
            std::cerr << "[MySqlPool] Connection validation failed with unknown exception" << std::endl;
            return false;
        }
    }
};

// RAII 连接守卫：自动归还连接，彻底杜绝泄漏
// 使用方式：
//   ConnectionGuard guard(pool_);
//   if (!guard) return false;
//   sql::Connection* con = guard.get();
//   try {
//       // 使用 con 执行 SQL
//   } catch (...) {
//       guard.markBad();  // 标记连接为坏的
//       throw;
//   }
//   // 函数结束时自动归还（好连接或坏连接）
class ConnectionGuard {
public:
    ConnectionGuard(std::shared_ptr<MySqlPool> pool) : pool_(pool), is_healthy_(true) {
        if (pool_) {
            con_ = pool_->getConnection();
        }
    }

    ~ConnectionGuard() {
        if (pool_ && con_) {
            pool_->returnConnection(std::move(con_), is_healthy_);
        }
    }
    
    // 禁止拷贝
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
    
    // 允许移动
    ConnectionGuard(ConnectionGuard&& other) noexcept 
        : pool_(std::move(other.pool_)), con_(std::move(other.con_)), is_healthy_(other.is_healthy_) {}
    
    ConnectionGuard& operator=(ConnectionGuard&& other) noexcept {
        if (this != &other) {
            pool_ = std::move(other.pool_);
            con_ = std::move(other.con_);
            is_healthy_ = other.is_healthy_;
        }
        return *this;
    }
    
    // 获取原始指针用于操作
    sql::Connection* get() { return con_.get(); }
    
    // 判断是否获取成功
    operator bool() const { return con_ != nullptr; }
    
    // 标记连接为坏的（发生异常时调用）
    void markBad() { is_healthy_ = false; }

private:
    std::shared_ptr<MySqlPool> pool_;
    std::unique_ptr<sql::Connection> con_;
    bool is_healthy_;  // 连接是否健康
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

