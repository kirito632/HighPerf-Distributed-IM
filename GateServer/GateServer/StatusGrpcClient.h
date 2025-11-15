#pragma once
#include "const.h"
#include "Singleton.h"
#include "ConfigMgr.h"
#include"message.grpc.pb.h"
#include"message.pb.h"
#include <grpcpp/security/credentials.h>
#include <grpcpp/create_channel.h>

// gRPC相关的类型别名
using grpc::Channel;          // 连接的通道
using grpc::Status;           // grpc调用的返回状态
using grpc::ClientContext;   // 客户端上下文

// 消息类型别名
using message::GetChatServerReq;   // 获取聊天服务器请求
using message::GetChatServerRsp;   // 获取聊天服务器响应
using message::StatusService;      // StatusServer的gRPC服务
using message::LoginReq;           // 登录请求
using message::LoginRsp;           // 登录响应

// StatusConPool类：StatusServer的gRPC客户端连接池
// 
// 作用：
//   管理多个gRPC Stub连接，实现连接的复用和负载均衡
// 
// 设计模式：
//   对象池模式 - 预先创建Stub，按需分配
// 
// 工作原理：
//   1. 初始化时创建指定数量的gRPC通道和Stub
//   2. 使用队列存储Stub
//   3. 使用条件变量实现连接的等待机制
//   4. 线程安全的Stub获取和归还
class StatusConPool {
public:
    // 构造函数：初始化StatusServer连接池
    // 参数：
    //   - poolSize: 连接池大小
    //   - host: StatusServer主机地址
    //   - port: StatusServer端口
    StatusConPool(size_t poolSize, std::string host, std::string port)
        : poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        // 创建指定数量的gRPC通道和Stub
        for (size_t i = 0; i < poolSize_; ++i) {
            std::string addr = host + ":" + port;
            std::cout << "[StatusConPool] Creating channel to " << addr << std::endl;
            // 创建gRPC通道（使用不安全凭证）
            std::shared_ptr<Channel> channel = grpc::CreateChannel(addr,
                grpc::InsecureChannelCredentials());

            // 创建Stub并加入连接池
            connections_.push(StatusService::NewStub(channel));
        }
    }

    // 析构函数：清理所有连接
    ~StatusConPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        Close();
        while (!connections_.empty()) {
            connections_.pop();
        }
    }

    // 获取一个Stub连接
    // 返回值：
    //   成功返回Stub指针，否则返回nullptr
    // 实现逻辑：
    //   1. 等待直到有可用的Stub
    //   2. 从队列中取出一个Stub
    //   3. 返回Stub
    std::unique_ptr<StatusService::Stub> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        // 等待直到有可用的Stub或已经停止
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !connections_.empty();
            });

        // 如果已停止，直接返回空指针
        if (b_stop_) {
            return  nullptr;
        }

        // 从队列中取出Stub
        auto context = std::move(connections_.front());
        connections_.pop();
        return context;
    }

    // 归还Stub到连接池
    // 参数：
    //   - context: Stub指针
    // 实现逻辑：
    //   1. 将Stub放回队列
    //   2. 通知等待连接的线程
    void returnConnection(std::unique_ptr<StatusService::Stub> context) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;
        }
        connections_.push(std::move(context));
        cond_.notify_one();
    }

    // 关闭连接池
    void Close() {
        b_stop_ = true;
        cond_.notify_all();
    }

private:
    std::atomic<bool> b_stop_;                         // 停止标志
    size_t poolSize_;                                   // 连接池大小
    std::string host_;                                  // StatusServer主机地址
    std::string port_;                                  // StatusServer端口
    std::queue<std::unique_ptr<StatusService::Stub>> connections_;  // Stub队列
    std::mutex mutex_;                                  // 互斥锁（保证线程安全）
    std::condition_variable cond_;                     // 条件变量（用于等待连接）
};

// StatusGrpcClient类：StatusServer的gRPC客户端
// 
// 作用：
//   提供调用StatusServer的接口，用于获取聊天服务器信息
// 
// 设计模式：
//   单例模式（Singleton）- 确保全局唯一实例
// 
// 主要功能：
//   GetChatServer - 根据用户ID获取可用的聊天服务器信息（主机、端口、token）
// 
// 使用场景：
//   在用户登录时，调用此接口获取ChatServer的地址，然后让客户端连接到ChatServer
class StatusGrpcClient :public Singleton<StatusGrpcClient>
{
    friend class Singleton<StatusGrpcClient>;  // 允许Singleton访问私有构造函数
public:
    // 析构函数：清理资源
    ~StatusGrpcClient() {

    }

    // 获取聊天服务器信息
    // 参数：
    //   - uid: 用户ID
    // 返回值：
    //   GetChatServerRsp：包含主机地址、端口、token等信息
    GetChatServerRsp GetChatServer(int uid);

private:
    // 私有构造函数：单例模式
    // 从配置文件中读取StatusServer的连接信息
    StatusGrpcClient();

    // StatusServer的连接池指针
    std::unique_ptr<StatusConPool> pool_;

};


