#pragma once
#include"const.h"

class RedisConPool {
public:
    RedisConPool(size_t poolSize, const char* host, int port, const char* pwd)
        : poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        for (size_t i = 0; i < poolSize_; ++i) {
            auto* context = redisConnect(host, port);
            if (context == nullptr || context->err != 0) {
                if (context != nullptr) {
                    redisFree(context);
                }
                continue;
            }

            auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd);
            if (reply->type == REDIS_REPLY_ERROR) {
                std::cout << "??????" << std::endl;
                //??г?? ???redisCommand??к????redisReply?????????
                redisFree(context);
                freeReplyObject(reply);
                continue;
            }

            //??г?? ???redisCommand??к????redisReply?????????
            freeReplyObject(reply);
            std::cout << "??????" << std::endl;
            connections_.push(context);
        }

    }

    ~RedisConPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!connections_.empty()) {
            connections_.pop();
        }
    }

    redisContext* getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !connections_.empty();
            });
        //?????????????????
        if (b_stop_) {
            return  nullptr;
        }
        auto* context = connections_.front();
        connections_.pop();
        return context;
    }

    void returnConnection(redisContext* context) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;
        }
        connections_.push(context);
        cond_.notify_one();
    }

    void Close() {
        b_stop_ = true;
        cond_.notify_all();
    }

private:
    std::atomic<bool> b_stop_;
    size_t poolSize_;
    const char* host_;
    int port_;
    std::queue<redisContext*> connections_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

class RedisMgr : public Singleton<RedisMgr>,
    public std::enable_shared_from_this<RedisMgr>
{
    friend class Singleton<RedisMgr>;
public:
    ~RedisMgr();

    bool Get(const std::string& key, std::string& value);
    bool GetAllList(const std::string& key, std::vector<std::string>& values);
    bool Set(const std::string& key, const std::string& value);
    bool Auth(const std::string& password);
    bool LPush(const std::string& key, const std::string& value);
    bool LPop(const std::string& key, std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    bool RPop(const std::string& key, std::string& value);
    bool HSet(const std::string& key, const std::string& hkey, const std::string& value);
    bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);
    bool HDel(const std::string& key, const std::string& field);
    std::string HGet(const std::string& key, const std::string& hkey);
    bool Del(const std::string& key);
    bool ExistsKey(const std::string& key);
    void Close();
    
    // ==================== Pub/Sub 支持 ====================
    
    /**
     * @brief 发布消息到指定频道
     * @param channel 频道名称
     * @param message 消息内容
     * @return 成功返回 true
     */
    bool Publish(const std::string& channel, const std::string& message);
    
    /**
     * @brief 获取 Redis 连接信息（用于订阅者创建独立连接）
     */
    const char* GetHost() const { return host_.c_str(); }
    int GetPort() const { return port_; }
    const char* GetPassword() const { return pwd_.c_str(); }
    
private:
    RedisMgr();

    //redisContext* _connect;
    //redisReply* _reply;

    std::unique_ptr<RedisConPool> con_pool_;
    
    // 连接信息（用于 Pub/Sub）
    std::string host_;
    int port_;
    std::string pwd_;
};



