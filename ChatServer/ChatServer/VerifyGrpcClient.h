#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "const.h"
#include "Singleton.h"
#include <atomic>
#include <queue>
#include <condition_variable>
#include <mutex>

// gRPC相关的类型别名
using grpc::Channel;      // gRPC通道
using grpc::Status;       // gRPC调用状态
using grpc::ClientContext; // 客户端上下文

// 消息类型别名
using message::GetVerifyReq;   // 获取验证码请求
using message::GetVerifyRsp;   // 获取验证码响应
using message::VerifyService; // VerifyServer的gRPC服务

// RPConPool类：VerifyServer的gRPC客户端连接池
// 
// 作用：
//   管理多个gRPC Stub连接，实现连接的复用
// 
// 工作原理：
//   1. 初始化时创建指定数量的gRPC通道和Stub
//   2. 使用队列存储Stub
//   3. 使用条件变量实现连接的等待机制
//   4. 线程安全的Stub获取和归还
class RPConPool {
public:
    // 构造函数：初始化VerifyServer连接池
    RPConPool(size_t poolsize, std::string host, std::string port);

    // 析构函数：清理所有连接
    ~RPConPool();

    // 关闭连接池
    void Close();

    // 获取一个Stub连接
    std::unique_ptr<VerifyService::Stub> getConnection();

    // 归还Stub到连接池
    void returnConnection(std::unique_ptr<VerifyService::Stub> context);

private:
    std::atomic<bool> b_stop_;                              // 停止标志
    size_t _poolsize;                                       // 连接池大小
    std::string _host;                                      // VerifyServer主机地址
    std::string _port;                                      // VerifyServer端口
    std::queue<std::unique_ptr<VerifyService::Stub>> _connections;  // Stub队列
    std::condition_variable _cv;                            // 条件变量（用于等待连接）
    std::mutex _mutex;                                      // 互斥锁（保证线程安全）
};


// VerifyGrpcClient类：VerifyServer的gRPC客户端
// 
// 作用：
//   提供调用VerifyServer的接口（当前未使用）
// 
// 设计模式：
//   单例模式（Singleton）
// 
// 注意：
//   ChatServer不需要调用VerifyServer，此客户端保留用于未来扩展
class VerifyGrpcClient : public Singleton<VerifyGrpcClient>
{
    friend class Singleton<VerifyGrpcClient>;  // 允许Singleton访问私有构造函数
public:
    // 获取验证码（当前未使用）
    GetVerifyRsp GetVerifyCode(const std::string& email) {
        std::cout << "[VerifyGrpcClient] Start GetVerifyCode, email = " << email << std::endl;

        GetVerifyRsp reply;
        ClientContext context;
        GetVerifyReq request;
        request.set_email(email);

        auto stub = pool_->getConnection();
        if (!stub) {
            std::cerr << "[VerifyGrpcClient] Failed to get stub from pool!" << std::endl;
            reply.set_error(ErrorCodes::RPCFailed);
            reply.set_email(email);
            return reply;
        }

        std::cout << "[VerifyGrpcClient] Sending gRPC request..." << std::endl;

        Status status = stub->GetVerifyCode(&context, request, &reply);

        // 确保连接归还到池
        pool_->returnConnection(std::move(stub));

        if (!status.ok()) {
            std::cerr << "[VerifyGrpcClient] gRPC调用失败: "
                << status.error_message()
                << " (code " << status.error_code() << ")" << std::endl;
            reply.set_error(ErrorCodes::RPCFailed);
            reply.set_email(email);
        }
        else {
            std::cout << "[VerifyGrpcClient] gRPC调用成功, reply.error = "
                << reply.error() << " , email = "
                << reply.email() << " , verifycode = "
                << std::endl;
        }

        return reply;
    }

private:
    // 私有构造函数：单例模式
    VerifyGrpcClient();

    // VerifyServer的连接池指针
    std::unique_ptr<RPConPool> pool_;
};


