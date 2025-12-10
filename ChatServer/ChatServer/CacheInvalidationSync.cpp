#include "CacheInvalidationSync.h"
#include "RedisMgr.h"
#include <iostream>
#include <sstream>

CacheInvalidationSync::CacheInvalidationSync(const std::string& channel)
    : channel_(channel)
{
}

CacheInvalidationSync::~CacheInvalidationSync()
{
    Stop();
}

bool CacheInvalidationSync::Start()
{
    if (running_.load()) {
        std::cout << "[CacheInvalidationSync] Already running" << std::endl;
        return true;
    }

    stop_requested_.store(false);
    
    // 启动订阅者线程
    subscriber_thread_ = std::thread(&CacheInvalidationSync::SubscriberLoop, this);
    
    // 等待线程启动
    int wait_count = 0;
    while (!running_.load() && wait_count < 50) {  // 最多等待 5 秒
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++wait_count;
    }

    if (running_.load()) {
        std::cout << "[CacheInvalidationSync] Started, channel: " << channel_ << std::endl;
        return true;
    } else {
        std::cout << "[CacheInvalidationSync] Failed to start" << std::endl;
        return false;
    }
}

void CacheInvalidationSync::Stop()
{
    if (!running_.load()) {
        return;
    }

    std::cout << "[CacheInvalidationSync] Stopping..." << std::endl;
    stop_requested_.store(true);

    // 断开订阅连接，使 redisGetReply 返回
    if (sub_context_) {
        // 发送 UNSUBSCRIBE 命令
        redisCommand(sub_context_, "UNSUBSCRIBE %s", channel_.c_str());
    }

    // 等待线程结束
    if (subscriber_thread_.joinable()) {
        subscriber_thread_.join();
    }

    std::cout << "[CacheInvalidationSync] Stopped" << std::endl;
}

void CacheInvalidationSync::SubscriberLoop()
{
    // 获取 Redis 连接信息
    auto redisMgr = RedisMgr::GetInstance();
    const char* host = redisMgr->GetHost();
    int port = redisMgr->GetPort();
    const char* pwd = redisMgr->GetPassword();

    // 创建独立的订阅连接
    sub_context_ = redisConnect(host, port);
    if (sub_context_ == nullptr || sub_context_->err) {
        std::cout << "[CacheInvalidationSync] Failed to connect to Redis: " 
                  << (sub_context_ ? sub_context_->errstr : "null context") << std::endl;
        return;
    }

    // 认证
    if (pwd && strlen(pwd) > 0) {
        redisReply* reply = (redisReply*)redisCommand(sub_context_, "AUTH %s", pwd);
        if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
            std::cout << "[CacheInvalidationSync] AUTH failed" << std::endl;
            if (reply) freeReplyObject(reply);
            redisFree(sub_context_);
            sub_context_ = nullptr;
            return;
        }
        freeReplyObject(reply);
    }

    // 订阅频道
    redisReply* reply = (redisReply*)redisCommand(sub_context_, "SUBSCRIBE %s", channel_.c_str());
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
        std::cout << "[CacheInvalidationSync] SUBSCRIBE failed" << std::endl;
        if (reply) freeReplyObject(reply);
        redisFree(sub_context_);
        sub_context_ = nullptr;
        return;
    }
    freeReplyObject(reply);

    running_.store(true);
    std::cout << "[CacheInvalidationSync] Subscribed to channel: " << channel_ << std::endl;

    // 消息循环
    while (!stop_requested_.load()) {
        redisReply* msg = nullptr;
        int ret = redisGetReply(sub_context_, (void**)&msg);
        
        if (ret != REDIS_OK || msg == nullptr) {
            if (!stop_requested_.load()) {
                std::cout << "[CacheInvalidationSync] redisGetReply error, reconnecting..." << std::endl;
                // TODO: 实现重连逻辑
            }
            break;
        }

        // 处理消息
        // 格式: ["message", channel, message_content]
        if (msg->type == REDIS_REPLY_ARRAY && msg->elements >= 3) {
            if (strcmp(msg->element[0]->str, "message") == 0) {
                std::string message_content(msg->element[2]->str, msg->element[2]->len);
                HandleMessage(message_content);
            }
        }

        freeReplyObject(msg);
    }

    // 清理
    if (sub_context_) {
        redisFree(sub_context_);
        sub_context_ = nullptr;
    }
    running_.store(false);
}

void CacheInvalidationSync::HandleMessage(const std::string& message)
{
    std::cout << "[CacheInvalidationSync] Received: " << message << std::endl;

    if (message.rfind("INVALIDATE:", 0) == 0) {
        // 单个失效: "INVALIDATE:uid"
        std::string uid_str = message.substr(11);
        int uid = std::stoi(uid_str);
        if (invalidate_cb_) {
            invalidate_cb_(uid);
            std::cout << "[CacheInvalidationSync] Invalidated uid=" << uid << std::endl;
        }
    }
    else if (message.rfind("INVALIDATE_BATCH:", 0) == 0) {
        // 批量失效: "INVALIDATE_BATCH:uid1,uid2,uid3"
        std::string uid_list = message.substr(17);
        std::vector<int> uids = ParseUidList(uid_list);
        if (invalidate_batch_cb_) {
            invalidate_batch_cb_(uids);
            std::cout << "[CacheInvalidationSync] Invalidated " << uids.size() << " users" << std::endl;
        }
    }
    else if (message == "CLEAR_ALL") {
        // 全量清空
        if (clear_all_cb_) {
            clear_all_cb_();
            std::cout << "[CacheInvalidationSync] Cleared all cache" << std::endl;
        }
    }
    else {
        std::cout << "[CacheInvalidationSync] Unknown message format: " << message << std::endl;
    }
}

std::vector<int> CacheInvalidationSync::ParseUidList(const std::string& uid_list)
{
    std::vector<int> uids;
    std::stringstream ss(uid_list);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            uids.push_back(std::stoi(item));
        }
    }
    return uids;
}

bool CacheInvalidationSync::PublishInvalidation(int uid)
{
    std::string message = "INVALIDATE:" + std::to_string(uid);
    return RedisMgr::GetInstance()->Publish(channel_, message);
}

bool CacheInvalidationSync::PublishInvalidationBatch(const std::vector<int>& uids)
{
    if (uids.empty()) return true;
    
    std::stringstream ss;
    ss << "INVALIDATE_BATCH:";
    for (size_t i = 0; i < uids.size(); ++i) {
        if (i > 0) ss << ",";
        ss << uids[i];
    }
    return RedisMgr::GetInstance()->Publish(channel_, ss.str());
}

bool CacheInvalidationSync::PublishClearAll()
{
    return RedisMgr::GetInstance()->Publish(channel_, "CLEAR_ALL");
}
