#pragma once
#include<grpcpp/grpcpp.h>
#include"message.grpc.pb.h"
#include"message.pb.h"
#include<mutex>
#include"const.h"
#include"data.h"

// gRPC相关的类型别名
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;

// 消息类型别名
using message::AddFriendReq;      // 添加好友请求
using message::AddFriendRsp;      // 添加好友响应
using message::AuthFriendReq;     // 认证好友请求
using message::AuthFriendRsp;     // 认证好友响应
using message::ChatService;       // ChatServer的gRPC服务
using message::GetChatServerRsp;  // 获取聊天服务器响应
using message::LoginReq;          // 登录请求
using message::LoginRsp;         // 登录响应
using message::TextChatMsgReq;    // 文本聊天消息请求
using message::TextChatMsgRsp;    // 文本聊天消息响应
using message::TextChatData;     // 文本聊天数据

// ChatServiceImpl类：ChatServer的gRPC服务实现
// 
// 作用：
//   实现ChatService接口，处理来自其他ChatServer或StatusServer的gRPC调用
// 
// 主要功能：
//   - NotifyAddFriend: 通知添加好友（跨ChatServer）
//   - NotifyAuthFriend: 通知认证好友（跨ChatServer）
//   - NotifyTextChatMsg: 通知文本聊天消息（跨ChatServer）
//   - GetBaseInfo: 获取用户基础信息
// 
// 设计模式：
//   继承gRPC自动生成的Service基类，实现具体的服务方法
class ChatServiceImpl final : public ChatService::Service
{
public:
    // 构造函数：初始化服务实现
    ChatServiceImpl();

    // 通知添加好友（gRPC方法实现）
    // 参数：
    //   - context: gRPC服务上下文
    //   - request: 添加好友请求
    //   - reply: 添加好友响应
    // 返回值：
    //   gRPC状态码
    Status NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* reply) override;

    // 通知认证好友（gRPC方法实现）
    // 参数：
    //   - context: gRPC服务上下文
    //   - request: 认证好友请求
    //   - response: 认证好友响应
    // 返回值：
    //   gRPC状态码
    Status NotifyAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* response) override;

    // 通知文本聊天消息（gRPC方法实现）
    // 参数：
    //   - context: gRPC服务上下文
    //   - request: 文本聊天消息请求
    //   - response: 文本聊天消息响应
    // 返回值：
    //   gRPC状态码
    Status NotifyTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* response) override;

    // 获取用户基础信息
    // 参数：
    //   - base_key: 基础键名
    //   - uid: 用户ID
    //   - userinfo: 输出参数，用户信息
    // 返回值：
    //   成功返回true，否则返回false
    bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);

private:

};


