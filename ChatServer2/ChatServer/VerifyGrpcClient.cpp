#include "VerifyGrpcClient.h"
#include"ConfigMgr.h"

// 构造函数：初始化VerifyGrpcClient
// 
// 实现逻辑：
//   1. 从配置管理器获取VerifyServer的连接信息
//   2. 创建VerifyServer的连接池（默认5个连接）
VerifyGrpcClient::VerifyGrpcClient() {
    auto& gCfgMgr = ConfigMgr::Inst();
    std::string host = gCfgMgr["VerifyServer"]["Host"];
    std::string port = gCfgMgr["VerifyServer"]["Port"];
    pool_.reset(new RPConPool(5, host, port));
}

// 构造函数：初始化VerifyServer连接池
// 
// 参数：
//   - poolsize: 连接池大小
//   - host: VerifyServer主机地址
//   - port: VerifyServer端口
// 
// 实现逻辑：
//   1. 保存连接参数
//   2. 创建指定数量的gRPC通道和Stub
//   3. 将Stub加入连接池队列
RPConPool::RPConPool(size_t poolsize, std::string host, std::string port) :
    _poolsize(poolsize), _host(host), _port(port), b_stop_(false)
{
    for (size_t i = 0; i < poolsize; ++i) {
        std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port,
            grpc::InsecureChannelCredentials());
        _connections.emplace(VerifyService::NewStub(channel));
    }
}

// 析构函数：清理所有连接
RPConPool::~RPConPool()
{
    std::lock_guard<std::mutex> lock(_mutex);
    Close();
    while (!_connections.empty()) {
        _connections.pop();
    }
}

// 关闭连接池
void RPConPool::Close()
{
    b_stop_ = true;
    _cv.notify_all();
}


// 获取一个Stub连接
// 
// 返回值：
//   成功返回Stub指针，否则返回nullptr
std::unique_ptr<VerifyService::Stub> RPConPool::getConnection()
{
    std::unique_lock<std::mutex> lock(_mutex);
    // 等待直到有可用的Stub或已停止
    _cv.wait(lock, [this]() {
        if (b_stop_) {
            return true;
        }
        return !_connections.empty();
        });

    // 如果已停止，返回nullptr
    if (b_stop_) {
        std::cerr << "[RPConPool] getConnection: pool stopped!" << std::endl;
        return nullptr;
    }

    // 从队列中取出Stub
    auto context = std::move(_connections.front());
    _connections.pop();

    std::cout << "[RPConPool] Connection acquired, remaining pool size = "
        << _connections.size() << std::endl;

    return context;
}

// 归还Stub到连接池
void RPConPool::returnConnection(std::unique_ptr<VerifyService::Stub> context)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (b_stop_) {
        std::cerr << "[RPConPool] returnConnection: pool stopped, discard connection" << std::endl;
        return;
    }

    _connections.push(std::move(context));
    std::cout << "[RPConPool] Connection returned, pool size = "
        << _connections.size() << std::endl;

    _cv.notify_one();
}
