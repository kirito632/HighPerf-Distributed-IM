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
using grpc::ClientContext;    // 通用于上下文

// 消息类型别名
using message::GetChatServerReq;   // 获取聊天服务器请求
using message::GetChatServerRsp;   // 获取聊天服务器响应
using message::StatusService;     // StatusServer的gRPC服务
using message::LoginReq;          // 登录请求
using message::LoginRsp;          // 登录响应

// StatusConPool类：StatusServer的gRPC客户端连接池
// 
// 作用：
//   管理多个gRPC Stub连接，实现连接的复用和负载均衡
// 
// 设计模式：
//   对象池模式 - 预先创建Stub，按需分配
// 
// 特点：
//   - 连接获取有超时机制（1500ms）
//   - 线程安全
class StatusConPool {
public:
    // 构造函数：初始化StatusServer连接池
    StatusConPool(size_t poolSize, std::string host, std::string port)
        : poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        for (size_t i = 0; i < poolSize_; ++i) {

            std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port,
                grpc::InsecureChannelCredentials());

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

    // 获取一个Stub连接（带超时）
    // 返回值：
    //   成功返回Stub指针，超时或失败返回nullptr
    // 
    // 实现逻辑：
    //   1. 等待最多1500ms
    //   2. 如果有可用连接或已停止，返回Stub
    //   3. 如果超时，返回nullptr
    std::unique_ptr<StatusService::Stub> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        // 等待 1500ms，否则 RPC deadline 太短
        if (!cond_.wait_for(lock, std::chrono::milliseconds(1500), [this]() {
            return b_stop_ || !connections_.empty();
            })) {
            // timeout
            return nullptr;
        }

        if (b_stop_ || connections_.empty()) {
            return nullptr;
        }
        auto ctx = std::move(connections_.front());
        connections_.pop();
        return ctx;
    }


    // 归还Stub到连接池
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
    std::atomic<bool> b_stop_;                              // 停止标志
    size_t poolSize_;                                       // 连接池大小
    std::string host_;                                      // StatusServer主机地址
    std::string port_;                                      // StatusServer端口
    std::queue<std::unique_ptr<StatusService::Stub>> connections_;  // Stub队列
    std::mutex mutex_;                                      // 互斥锁（保证线程安全）
    std::condition_variable cond_;                          // 条件变量（用于等待连接）
};

// StatusGrpcClient类：StatusServer的gRPC客户端
// 
// 作用：
//   提供调用StatusServer的接口
// 
// 设计模式：
//   单例模式 - 确保全局唯一实例
// 
// 主要功能：
//   - GetChatServer: 根据用户ID获取ChatServer信息（当前未使用）
//   - Login: 验证用户token
class StatusGrpcClient :public Singleton<StatusGrpcClient>
{
    friend class Singleton<StatusGrpcClient>;  // 允许Singleton访问私有构造函数
public:
    // 析构函数：清理资源
    ~StatusGrpcClient() {

    }

    // 获取ChatServer信息（当前未使用）
    GetChatServerRsp GetChatServer(int uid);

    // 验证用户token
    // 参数：
    //   - uid: 用户ID
    //   - token: 认证令牌
    // 返回值：
    //   LoginRsp：包含验证结果
    LoginRsp Login(int uid, std::string token);

private:
    // 私有构造函数：单例模式
    // 从配置文件中读取StatusServer的连接信息
    StatusGrpcClient();

    // StatusServer的连接池指针
    std::unique_ptr<StatusConPool> pool_;

};


