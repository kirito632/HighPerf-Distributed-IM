#pragma once
#include "const.h"
#include "Singleton.h"
#include "ConfigMgr.h"
#include"message.grpc.pb.h"
#include"message.pb.h"
#include <grpcpp/security/credentials.h>
#include <grpcpp/create_channel.h>
#include<unordered_map>
#include<queue>

// gRPC相关的类型别名
using grpc::Channel;          // 连接的通道
using grpc::Status;           // grpc调用的返回状态
using grpc::ClientContext;   // 客户端上下文

// 消息类型别名
using message::AddFriendReq;      // 添加好友请求
using message::AddFriendRsp;      // 添加好友响应
using message::AuthFriendReq;     // 认证好友请求
using message::AuthFriendRsp;     // 认证好友响应
using message::TextChatMsgReq;    // 文本聊天消息请求
using message::TextChatMsgRsp;    // 文本聊天消息响应
using message::GetChatServerReq;  // 获取聊天服务器请求
using message::GetChatServerRsp;  // 获取聊天服务器响应
using message::ChatService;       // ChatServer的gRPC服务
using message::LoginReq;          // 登录请求
using message::LoginRsp;          // 登录响应

// ChatConPool类：ChatServer的gRPC客户端连接池
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
class ChatConPool {
public:
    // 构造函数：初始化ChatServer连接池
    // 参数：
    //   - poolSize: 连接池大小
    //   - host: ChatServer主机地址
    //   - port: ChatServer端口
    ChatConPool(size_t poolSize, std::string host, std::string port) :
        poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        // 创建指定数量的gRPC通道和Stub
        for (size_t i = 0; i < poolSize_; ++i) {
            std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port, grpc::InsecureChannelCredentials());
            connections_.push(ChatService::NewStub(channel));
        }
    }

    // 析构函数：清理所有连接
    ~ChatConPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        Close();
        while (!connections_.empty()) {
            connections_.pop();
        }
    }

    // 获取一个Stub连接
    std::unique_ptr<ChatService::Stub> getConnection() {
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
    void returnConnection(std::unique_ptr<ChatService::Stub> context) {
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
    std::string host_;                                      // ChatServer主机地址
    std::string port_;                                      // ChatServer端口
    std::queue<std::unique_ptr<ChatService::Stub> > connections_;  // Stub队列
    std::mutex mutex_;                                      // 互斥锁（保证线程安全）
    std::condition_variable cond_;                         // 条件变量（用于等待连接）
};

// ChatGrpcClient类：ChatServer的gRPC客户端
// 
// 作用：
//   提供调用ChatServer的接口，用于StatusServer向ChatServer发送消息
// 
// 设计模式：
//   单例模式（Singleton）- 确保全局唯一实例
// 
// 主要功能：
//   - NotifyAddFriend: 通知添加好友
//   - NotifyAuthFriend: 通知认证好友
//   - NotifyTextChatMsg: 通知文本聊天消息
//   - GetBaseInfo: 获取用户基础信息
// 
// 使用场景：
//   当GateServer需要向不同的ChatServer发送消息时，使用此客户端
class ChatGrpcClient : public Singleton<ChatGrpcClient>
{
    friend class Singleton<ChatGrpcClient>;  // 允许Singleton访问私有构造函数

public:
    // 析构函数：清理资源
    ~ChatGrpcClient() {

    }

    // 通知添加好友
    // 参数：
    //   - server_ip: ChatServer的地址
    //   - req: 添加好友请求
    // 返回值：
    //   添加好友响应（当前未实现）
    AddFriendRsp NotifyAddFriend(std::string server_ip, const AddFriendReq& req);

    // 通知认证好友
    // 参数：
    //   - server_ip: ChatServer的地址
    //   - req: 认证好友请求
    // 返回值：
    //   认证好友响应（当前未实现）
    AuthFriendRsp NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req);

    // 获取用户基础信息
    // 参数：
    //   - base_key: 基础键
    //   - uid: 用户ID
    //   - userinfo: 输出参数，用户信息
    // 返回值：
    //   成功返回true，否则返回false（当前未实现）
    bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);

    // 通知文本聊天消息
    // 参数：
    //   - server_ip: ChatServer的地址
    //   - req: 文本聊天消息请求
    //   - rtvalue: JSON值
    // 返回值：
    //   文本聊天消息响应（当前未实现）
    TextChatMsgRsp NotifyTextChatMsg(std::string server_ip, const TextChatMsgReq& req, const Json::Value& rtvalue);

private:
    // 私有构造函数：单例模式
    // 从配置文件中读取多个ChatServer的连接信息，为每个ChatServer创建连接池
    ChatGrpcClient();

    // 存储多个ChatServer的连接池（server_name -> connection_pool）
    std::unordered_map<std::string, std::unique_ptr<ChatConPool>> _pools;
};


