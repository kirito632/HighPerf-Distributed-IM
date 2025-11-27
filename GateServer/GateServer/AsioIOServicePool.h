#include <vector>
#include <boost/asio.hpp>
#include "Singleton.h"

// AsioIOServicePool类：IO服务池，实现负载均衡
// 
// 作用：
//   管理多个boost::asio::io_context，实现IO服务的负载均衡
//   通过多线程和多个io_context提高并发处理能力
// 
// 设计模式：
//   继承Singleton单例模式，确保全局只有一个IO服务池实例
// 
// 实现原理：
//   1. 创建多个io_context
//   2. 为每个io_context创建work_guard（保持运行状态）
//   3. 为每个io_context启动独立的工作线程
//   4. 使用Round-Robin轮询方式分配任务
class AsioIOServicePool :public Singleton<AsioIOServicePool>
{
    friend Singleton<AsioIOServicePool>;
public:
    using IOService = boost::asio::io_context;  // IO服务类型别名
    using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;  // Work类型别名
    using WorkPtr = std::unique_ptr<Work>;  // Work指针类型别名

    // 析构函数：停止所有IO服务
    ~AsioIOServicePool();

    // 禁止拷贝构造
    AsioIOServicePool(const AsioIOServicePool&) = delete;

    // 禁止赋值
    AsioIOServicePool& operator=(const AsioIOServicePool&) = delete;

    // 使用 round-robin 轮询的方式获取一个 io_service
    // 
    // 返回值：
    //   一个io_context的引用
    // 
    // 说明：
    //   每次调用都会返回下一个io_context，实现负载均衡
    boost::asio::io_context& GetIOService();

    // 停止所有IO服务并等待线程结束
    void Stop();

private:
    // 私有构造函数：单例模式
    // 参数：
    //   - size: IO服务池的大小
    AsioIOServicePool(std::size_t size = std::thread::hardware_concurrency());

    // IO服务列表
    std::vector<IOService> _ioServices;

    // Work指针列表：保持io_context运行
    std::vector<WorkPtr> _works;

    // 工作线程列表
    std::vector<std::thread> _threads;

    // 下一个要使用的IO服务索引（用于Round-Robin轮询）
    std::size_t _nextIOService;
};

