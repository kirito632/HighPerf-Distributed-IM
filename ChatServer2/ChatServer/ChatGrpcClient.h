#pragma once
#include<queue>
#include"Singleton.h"
#include"ConfigMgr.h"
#include<grpcpp/grpcpp.h>
#include<json/json.h>
#include<json/value.h>
#include<json/reader.h>
#include"message.grpc.pb.h"
#include"message.pb.h"
#include<atomic>
#include<unordered_map>
#include"data.h"

// gRPC相关的类型别名
using grpc::Channel;          // gRPC通道
using grpc::ClientContext;    // 客户端上下文
using grpc::Status;            // gRPC调用状态

// 消息类型别名
using message::AddFriendReq;      // 添加好友请求
using message::AddFriendRsp;      // 添加好友响应
using message::AuthFriendReq;     // 认证好友请求
using message::AuthFriendRsp;    // 认证好友响应
using message::ChatService;       // ChatServer的gRPC服务
using message::GetChatServerRsp;  // 获取聊天服务器响应
using message::LoginReq;          // 登录请求
using message::LoginRsp;         // 登录响应
using message::TextChatMsgReq;    // 文本聊天消息请求
using message::TextChatMsgRsp;    // 文本聊天消息响应
using message::TextChatData;     // 文本聊天数据

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
        for (size_t i = 0; i < poolSize_; ++i) {
            std::shared_ptr<Channel> channel = grpc::CreateChannel(host_ + ":" + port_, grpc::InsecureChannelCredentials());
            auto stub = ChatService::NewStub(channel);
            connections_.emplace(std::move(stub));
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
            if (!b_stop_) {
                return true;
            }
            return !connections_.empty();
            });
        // 如果已停止，直接返回空指针
        if (b_stop_) {
            return nullptr;
        }
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
    std::queue<std::unique_ptr<ChatService::Stub>> connections_;  // Stub队列
    std::mutex mutex_;                                      // 互斥锁（保证线程安全）
    std::condition_variable cond_;                         // 条件变量（用于等待连接）
};

// ChatGrpcClient类：ChatServer的gRPC客户端
// 
// 作用：
//   提供调用其他ChatServer的接口，用于跨ChatServer通信
// 
// 设计模式：
//   单例模式（Singleton）- 确保全局唯一实例
// 
// 主要功能：
//   - NotifyAddFriend: 通知添加好友（跨服务器）
//   - NotifyAuthFriend: 通知认证好友（跨服务器）
//   - NotifyTextChatMsg: 通知文本聊天消息（跨服务器）
//   - GetBaseInfo: 获取用户基础信息
// 
// 使用场景：
//   当需要向其他ChatServer发送消息时（如用户在不同ChatServer上时）
class ChatGrpcClient : public Singleton<ChatGrpcClient>
{
    friend class Singleton<ChatGrpcClient>;  // 允许Singleton访问私有构造函数

public:
    // 析构函数：清理资源
    ~ChatGrpcClient() {

    }

    // 通知添加好友（当前未实现）
    AddFriendRsp NotifyAddFriend(std::string server_ip, const AddFriendReq& req);

    // 通知认证好友（当前未实现）
    AuthFriendRsp NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req);

    // 获取用户基础信息（当前未实现）
    bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);

    // 通知文本聊天消息（当前未实现）
    TextChatMsgRsp NotifyTextChatMsg(std::string server_ip, const TextChatMsgReq& req);

private:
    // 私有构造函数：单例模式
    // 从配置文件中读取多个ChatServer的连接信息，为每个ChatServer创建连接池
    ChatGrpcClient();

    // 存储多个ChatServer的连接池（server_name -> connection_pool）
    std::unordered_map<std::string, std::unique_ptr<ChatConPool> > _pools;
};


