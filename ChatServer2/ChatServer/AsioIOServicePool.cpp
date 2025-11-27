#include "AsioIOServicePool.h"
#include <iostream>
using namespace std;
AsioIOServicePool::AsioIOServicePool(std::size_t size) :_ioServices(size),
_works(size), _nextIOService(0) {
    for (std::size_t i = 0; i < size; ++i) {
        _works[i] = std::unique_ptr<Work>(new Work(_ioServices[i].get_executor()));
        std::cout << "AsioIOServicePool: created work for io[" << i << "]\n";
    }

    //遍历多个ioservice，创建多个线程，每个线程内部启动ioservice
    for (std::size_t i = 0; i < _ioServices.size(); ++i) {
        _threads.emplace_back([this, i]() {
            std::cout << "thread " << i << " start run()\n";
            _ioServices[i].run();
            std::cout << "thread " << i << " exit run()\n";
            });
    }
    std::cout << "AsioIOServicePool started " << _threads.size() << " threads\n";
}

AsioIOServicePool::~AsioIOServicePool() {
    Stop();
    std::cout << "AsioIOServicePool destruct" << std::endl;
}

boost::asio::io_context& AsioIOServicePool::GetIOService() {
    auto idx = _nextIOService;
    auto& service = _ioServices[_nextIOService++];
    if (_nextIOService == _ioServices.size()) {
        _nextIOService = 0;
    }
    std::cout << "GetIOService -> idx=" << idx << "\n";
    return service;
}

void AsioIOServicePool::Stop() {
    //因为仅仅执行work.reset并不能让iocontext从run的状态中退出
    //当iocontext已经绑定了读或写的监听事件后，还需要手动stop该服务。
    std::cout << "AsioIOServicePool::Stop() called\n";
    for (auto& work : _works) {
        //把服务先停止
        // 通过executor获取io_context
        auto& io_context = boost::asio::query(
            work->get_executor(),
            boost::asio::execution::context
        );

        io_context.stop();
        work.reset();
    }

    for (auto& t : _threads) {
        t.join();
    }
    std::cout << "AsioIOServicePool::Stop() finished\n";
}