#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <hiredis/hiredis.h>

/**
 * @brief 缓存失效同步器
 * 
 * 通过 Redis Pub/Sub 实现多机缓存失效同步。
 * 
 * 工作原理：
 * 1. 当本机修改用户数据时，调用 PublishInvalidation() 发布失效消息
 * 2. 订阅者线程监听 Redis 频道，收到消息后调用回调函数失效本地缓存
 * 3. 所有机器都订阅同一频道，实现多机缓存一致性
 * 
 * 消息格式：
 * - 单个失效: "INVALIDATE:uid"
 * - 批量失效: "INVALIDATE_BATCH:uid1,uid2,uid3"
 * - 全量清空: "CLEAR_ALL"
 */
class CacheInvalidationSync {
public:
    // 失效回调类型
    using InvalidateCallback = std::function<void(int uid)>;
    using InvalidateBatchCallback = std::function<void(const std::vector<int>& uids)>;
    using ClearAllCallback = std::function<void()>;

    /**
     * @brief 构造函数
     * @param channel Redis 频道名称
     */
    explicit CacheInvalidationSync(const std::string& channel = "cache:invalidation");
    
    ~CacheInvalidationSync();

    // 禁止拷贝
    CacheInvalidationSync(const CacheInvalidationSync&) = delete;
    CacheInvalidationSync& operator=(const CacheInvalidationSync&) = delete;

    /**
     * @brief 设置回调函数
     */
    void SetInvalidateCallback(InvalidateCallback cb) { invalidate_cb_ = std::move(cb); }
    void SetInvalidateBatchCallback(InvalidateBatchCallback cb) { invalidate_batch_cb_ = std::move(cb); }
    void SetClearAllCallback(ClearAllCallback cb) { clear_all_cb_ = std::move(cb); }

    /**
     * @brief 启动订阅者线程
     * @return 成功返回 true
     */
    bool Start();

    /**
     * @brief 停止订阅者线程
     */
    void Stop();

    /**
     * @brief 发布单个失效消息
     * @param uid 用户 ID
     * @return 成功返回 true
     */
    bool PublishInvalidation(int uid);

    /**
     * @brief 发布批量失效消息
     * @param uids 用户 ID 列表
     * @return 成功返回 true
     */
    bool PublishInvalidationBatch(const std::vector<int>& uids);

    /**
     * @brief 发布全量清空消息
     * @return 成功返回 true
     */
    bool PublishClearAll();

    /**
     * @brief 检查是否正在运行
     */
    bool IsRunning() const { return running_.load(); }

    /**
     * @brief 获取频道名称
     */
    const std::string& GetChannel() const { return channel_; }

private:
    // 订阅者线程主函数
    void SubscriberLoop();

    // 处理收到的消息
    void HandleMessage(const std::string& message);

    // 解析批量 UID
    std::vector<int> ParseUidList(const std::string& uid_list);

    std::string channel_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread subscriber_thread_;

    // 回调函数
    InvalidateCallback invalidate_cb_;
    InvalidateBatchCallback invalidate_batch_cb_;
    ClearAllCallback clear_all_cb_;

    // 订阅者连接（独立于连接池）
    redisContext* sub_context_{nullptr};
};
