#pragma once
#include <vector>
#include <boost/asio.hpp>
#include "Singleton.h"

// AsioIOServicePool类：管理Boost.Asio的io_context池，实现负载均衡
// 
// 作用：
//   提供一个io_context的池，并通过Round-Robin策略分发给不同的任务，
//   从而将IO操作分散到多个线程中，提高并发处理能力。
// 
// 设计模式：
//   单例模式，确保全局只有一个IO服务池实例。
// 
// 工作原理：
//   1. 创建多个io_context实例
//   2. 为每个io_context创建work_guard（防止没有任务时退出）
//   3. 为每个io_context启动独立的线程运行event loop
//   4. 使用Round-Robin方式分配io_context
class AsioIOServicePool :public Singleton<AsioIOServicePool>
{
    friend Singleton<AsioIOServicePool>;  // 允许Singleton访问私有构造函数
public:
    // 类型别名，简化io_context的使用
    using IOService = boost::asio::io_context;
    // 类型别名，用于保持io_context的运行状态
    using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    // Work对象的智能指针
    using WorkPtr = std::unique_ptr<Work>;

    // 析构函数：停止所有IO服务并清理资源
    ~AsioIOServicePool();

    // 禁用拷贝构造函数和赋值运算符
    AsioIOServicePool(const AsioIOServicePool&) = delete;
    AsioIOServicePool& operator=(const AsioIOServicePool&) = delete;

    // 获取一个io_context实例（使用Round-Robin轮询方式）
    // 作用：
    //   实现负载均衡，将新的任务分配给下一个可用的io_context
    // 返回值：
    //   一个io_context的引用
    boost::asio::io_context& GetIOService();

    // 停止所有IO服务和工作线程
    // 作用：
    //   优雅地关闭所有io_context，并等待所有工作线程结束
    void Stop();

private:
    // 私有构造函数：初始化IO服务池
    // 参数：
    //   - size: IO服务的数量，默认为2
    // 
    // 实现逻辑：
    //   1. 创建指定数量的io_context
    //   2. 为每个io_context创建work_guard
    //   3. 为每个io_context启动独立的线程
    AsioIOServicePool(std::size_t size = 2/*std::thread::hardware_concurrency()*/);

    // 存储io_context对象的向量
    std::vector<IOService> _ioServices;
    // 存储work_guard对象的智能指针向量，用于保持io_context运行
    std::vector<WorkPtr> _works;
    // 存储工作线程的向量
    std::vector<std::thread> _threads;
    // 下一个要分配的io_context的索引（用于Round-Robin）
    std::size_t _nextIOService;
};
