#pragma once
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <iostream>
#include "Singleton.h"

// 异步数据库线程池
// 
// 作用：
//   将耗时的数据库操作从主逻辑线程分离，避免阻塞网络IO和业务处理。
//   采用生产者-消费者模型，主线程生产任务，后台线程池消费任务。
//
// 使用方式：
//   AsyncDBPool::GetInstance()->PostTask([=](){
//       // 执行数据库操作
//       MysqlMgr::GetInstance()->Query(...);
//   });
class AsyncDBPool : public Singleton<AsyncDBPool> {
    friend class Singleton<AsyncDBPool>;
public:
    // 定义任务类型：无参无返回值的函数对象（通常使用Lambda表达式封装）
    using Task = std::function<void()>;

    // 初始化线程池
    // 参数：
    //   threadNum: 线程池中的工作线程数量，默认为 hardware_concurrency()
    // 注意：
    //   必须在系统启动时调用一次
    void Init(int threadNum = -1) {
        // 如果threadNum为-1，则使用CPU核心数
        if (threadNum <= 0) {
            threadNum = std::max(4, (int)std::thread::hardware_concurrency());
        }
        if (b_stop_) return; // 避免重复初始化
        
        for (int i = 0; i < threadNum; ++i) {
            threads_.emplace_back([this] {
                while (true) {
                    Task task;
                    {
                        // 获取互斥锁，访问任务队列
                        std::unique_lock<std::mutex> lock(mutex_);
                        
                        // 等待条件满足：停止运行 或 队列不为空
                        cond_.wait(lock, [this] { return b_stop_ || !tasks_.empty(); });
                        
                        // 如果收到停止信号且队列已空，则退出线程
                        if (b_stop_ && tasks_.empty()) return;
                        
                        // 取出任务
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    
                    // 执行任务（捕获异常防止线程崩溃）
                    try {
                        task();
                    } catch (const std::exception& e) {
                        std::cerr << "[AsyncDBPool] Task exception: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "[AsyncDBPool] Task unknown exception" << std::endl;
                    }
                }
            });
        }
    }

    // 停止线程池
    // 作用：
    //   1. 设置停止标志
    //   2. 唤醒所有等待中的线程
    //   3. 等待所有线程安全退出
    // 注意：
    //   通常在程序退出或析构时调用
    void Stop() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            b_stop_ = true;
        }
        cond_.notify_all();
        for (auto& t : threads_) {
            if (t.joinable()) t.join();
        }
    }

    // 投递任务
    // 参数：
    //   task: 要执行的函数对象或Lambda
    // 线程安全：
    //   该函数是线程安全的，可以从任意线程调用
    void PostTask(Task task) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.push(std::move(task));
        }
        cond_.notify_one(); // 唤醒一个工作线程来处理
    }

private:
    AsyncDBPool() : b_stop_(false) {}
    ~AsyncDBPool() { Stop(); }

    AsyncDBPool(const AsyncDBPool&) = delete;
    AsyncDBPool& operator=(const AsyncDBPool&) = delete;

    std::vector<std::thread> threads_;  // 线程容器
    std::queue<Task> tasks_;            // 任务队列
    std::mutex mutex_;                  // 互斥锁，保护任务队列
    std::condition_variable cond_;      // 条件变量，用于线程同步
    std::atomic<bool> b_stop_;          // 停止标志
};
