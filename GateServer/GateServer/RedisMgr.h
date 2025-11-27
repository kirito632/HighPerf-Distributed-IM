#pragma once
#include"const.h"

// RedisConPool类：Redis连接池
// 
// 作用：
//   管理多个Redis连接，实现连接的复用和负载均衡
// 
// 设计模式：
//   对象池模式 - 预先创建连接，按需分配
// 
// 工作原理：
//   1. 初始化时创建指定数量的Redis连接
//   2. 对每个连接进行身份认证
//   3. 使用队列存储可用连接
//   4. 使用条件变量实现连接的等待机制
class RedisConPool {
public:
    // 构造函数：初始化Redis连接池
    // 参数：
    //   - poolSize: 连接池大小
    //   - host: Redis服务器地址
    //   - port: Redis服务器端口
    //   - pwd: Redis密码
    // 
    // 实现逻辑：
    //   1. 创建指定数量的Redis连接
    //   2. 对每个连接进行身份认证
    //   3. 将认证成功的连接加入队列
    RedisConPool(size_t poolSize, const char* host, int port, const char* pwd)
        : poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        // 创建指定数量的Redis连接
        for (size_t i = 0; i < poolSize_; ++i) {
            auto* context = redisConnect(host, port);
            if (context == nullptr || context->err != 0) {
                if (context != nullptr) {
                    redisFree(context);
                }
                continue;
            }

            // 进行身份认证
            auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd);
            if (reply->type == REDIS_REPLY_ERROR) {
                std::cout << "认证失败" << std::endl;
                // 认证失败，释放连接
                redisFree(context);
                freeReplyObject(reply);
                continue;
            }

            // 认证成功，释放reply对象
            freeReplyObject(reply);
            std::cout << "认证成功" << std::endl;
            // 将连接加入连接池
            connections_.push(context);
        }

    }

    // 析构函数：清理所有连接
    ~RedisConPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!connections_.empty()) {
            connections_.pop();
        }
    }

    // 获取Redis连接
    // 
    // 返回值：
    //   连接成功返回redisContext指针，否则返回nullptr
    // 
    // 实现逻辑：
    //   1. 等待直到有可用连接
    //   2. 从队列中取出一个连接
    //   3. 返回连接
    redisContext* getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        // 等待直到有可用连接或已经停止
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !connections_.empty();
            });

        // 如果已停止，直接返回空指针
        if (b_stop_) {
            return  nullptr;
        }

        // 从队列中取出连接
        auto* context = connections_.front();
        connections_.pop();
        return context;
    }

    // 归还Redis连接到连接池
    // 
    // 参数：
    //   - context: Redis连接指针
    // 
    // 实现逻辑：
    //   1. 将连接放回队列
    //   2. 通知等待连接的线程
    void returnConnection(redisContext* context) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;
        }
        connections_.push(context);
        cond_.notify_one();
    }

    // 关闭连接池
    void Close() {
        b_stop_ = true;
        cond_.notify_all();
    }

private:
    std::atomic<bool> b_stop_;           // 停止标志
    size_t poolSize_;                    // 连接池大小
    const char* host_;                   // Redis主机地址
    int port_;                           // Redis端口
    std::queue<redisContext*> connections_;  // 连接队列
    std::mutex mutex_;                   // 互斥锁（保证线程安全）
    std::condition_variable cond_;      // 条件变量（用于等待连接）
};

// RedisMgr类：Redis管理器
// 
// 作用：
//   提供Redis操作的统一接口，封装底层hiredis库调用
//   实现常用Redis命令的封装
// 
// 设计模式：
//   1. 单例模式（Singleton）- 确保全局唯一实例
//   2. 门面模式（Facade）- 简化Redis操作接口
//   3. 连接池模式 - 使用RedisConPool管理连接
// 
// 功能分类：
//   - 字符串操作：Get、Set
//   - 列表操作：LPush、LPop、RPush、RPop
//   - 哈希操作：HSet、HGet、HDel
//   - 通用操作：Del、ExistsKey、Auth
class RedisMgr : public Singleton<RedisMgr>,
    public std::enable_shared_from_this<RedisMgr>
{
    friend class Singleton<RedisMgr>;  // 允许Singleton访问私有构造函数
public:
    // 析构函数：清理资源
    ~RedisMgr();

    // --------- 字符串操作 ---------

    // 获取键值（字符串）
    // 参数：
    //   - key: 键名
    //   - value: 输出参数，键值
    // 返回值：
    //   成功返回true，否则返回false
    bool Get(const std::string& key, std::string& value);

    // 设置键值（字符串）
    // 参数：
    //   - key: 键名
    //   - value: 键值
    // 返回值：
    //   成功返回true，否则返回false
    bool Set(const std::string& key, const std::string& value);

    // 发布消息到频道（PUBLISH）
    // 返回：true 表示命令执行成功（不代表有订阅者接收），false 表示执行失败
    bool Publish(const std::string& channel, const std::string& message);

    // 身份认证
    // 参数：
    //   - password: Redis密码
    // 返回值：
    //   认证成功返回true，否则返回false
    bool Auth(const std::string& password);

    // --------- 列表操作 ---------

    // 从列表左端推入元素
    // 参数：
    //   - key: 键名
    //   - value: 值
    // 返回值：
    //   成功返回true，否则返回false
    bool LPush(const std::string& key, const std::string& value);

    // 从列表左端弹出元素
    // 参数：
    //   - key: 键名
    //   - value: 输出参数，弹出的值
    // 返回值：
    //   成功返回true，否则返回false
    bool LPop(const std::string& key, std::string& value);

    // 从列表右端推入元素
    // 参数：
    //   - key: 键名
    //   - value: 值
    // 返回值：
    //   成功返回true，否则返回false
    bool RPush(const std::string& key, const std::string& value);

    // 从列表右端弹出元素
    // 参数：
    //   - key: 键名
    //   - value: 输出参数，弹出的值
    // 返回值：
    //   成功返回true，否则返回false
    bool RPop(const std::string& key, std::string& value);

    // --------- 哈希操作 ---------

    // 设置哈希字段值（字符串版本）
    // 参数：
    //   - key: 哈希键名
    //   - hkey: 字段名
    //   - value: 字段值
    // 返回值：
    //   成功返回true，否则返回false
    bool HSet(const std::string& key, const std::string& hkey, const std::string& value);

    // 设置哈希字段值（二进制版本）
    // 参数：
    //   - key: 哈希键名
    //   - hkey: 字段名
    //   - hvalue: 字段值（二进制数据）
    //   - hvaluelen: 数据长度
    // 返回值：
    //   成功返回true，否则返回false
    bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);

    // 删除哈希字段
    // 参数：
    //   - key: 哈希键名
    //   - field: 字段名
    // 返回值：
    //   成功删除返回true，否则返回false
    bool HDel(const std::string& key, const std::string& field);

    // 获取哈希字段值
    // 参数：
    //   - key: 哈希键名
    //   - hkey: 字段名
    // 返回值：
    //   字段值，不存在返回空字符串
    std::string HGet(const std::string& key, const std::string& hkey);

    // --------- 通用操作 ---------

    // 删除键
    // 参数：
    //   - key: 键名
    // 返回值：
    //   成功返回true，否则返回false
    bool Del(const std::string& key);

    // 检查键是否存在
    // 参数：
    //   - key: 键名
    // 返回值：
    //   存在返回true，否则返回false
    bool ExistsKey(const std::string& key);

    // 关闭连接池
    void Close();

private:
    // 私有构造函数：单例模式
    // 
    // 实现逻辑：
    //   从配置文件中读取Redis连接信息
    //   创建Redis连接池（默认5个连接）
    RedisMgr();

    // Redis连接池指针
    std::unique_ptr<RedisConPool> con_pool_;
};


