#include "AsioIOServicePool.h"
#include <iostream>
using namespace std;

// 构造函数：初始化IO服务池
// 参数：
//   - size: IO服务的数量（默认为2）
// 
// 实现逻辑：
//   1. 创建指定数量的io_context
//   2. 为每个io_context创建work_guard（防止没有任务时退出）
//   3. 为每个io_context启动独立的线程运行event loop
// 
// 目的：
//   通过多个io_context和线程池实现负载均衡，提高并发性能
AsioIOServicePool::AsioIOServicePool(std::size_t size) :_ioServices(size),
_works(size), _nextIOService(0) {
    // 为每个io_context创建work_guard
    // work_guard用于保持io_context的运行状态，防止没有任务时自动退出
    for (std::size_t i = 0; i < size; ++i) {
        _works[i] = std::unique_ptr<Work>(new Work(_ioServices[i].get_executor()));
    }

    // 为每个io_service启动独立线程，每个线程内部运行io_service的事件循环
    for (std::size_t i = 0; i < _ioServices.size(); ++i) {
        _threads.emplace_back([this, i]() {
            _ioServices[i].run();
            });
    }
}

// 析构函数：停止IO服务池
AsioIOServicePool::~AsioIOServicePool() {
    Stop();
    std::cout << "AsioIOServicePool destruct" << endl;
}

// 获取一个io_context（使用Round-Robin轮询方式）
// 
// 返回值：
//   返回一个io_context的引用
// 
// 实现逻辑：
//   1. 选择当前的io_context
//   2. 更新索引（Round-Robin）
//   3. 返回选择的io_context
boost::asio::io_context& AsioIOServicePool::GetIOService() {
    auto& service = _ioServices[_nextIOService++];
    // Round-Robin：到达末尾后从头开始
    if (_nextIOService == _ioServices.size()) {
        _nextIOService = 0;
    }
    return service;
}

// 停止所有IO服务和工作线程
// 
// 实现逻辑：
//   1. 遍历所有work_guard，获取对应的io_context
//   2. 调用io_context.stop()停止事件循环
//   3. 释放work_guard对象
//   4. 等待所有工作线程结束
void AsioIOServicePool::Stop() {
    // 因为先调用executor停止，然后io_context的run方法才会退出
    // io_context已经排满了几个读写文件的事件后，还需要手动stop方法退出
    for (auto& work : _works) {
        // 释放work_guard停止
        // 通过executor获取io_context
        auto& io_context = boost::asio::query(
            work->get_executor(),
            boost::asio::execution::context
        );

        // 停止io_context的事件循环
        io_context.stop();
        // 释放work_guard
        work.reset();
    }

    // 等待所有工作线程结束
    for (auto& t : _threads) {
        t.join();
    }
}
