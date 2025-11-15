#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"

// gRPC相关的类型别名
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::GetChatServerReq;   // 获取聊天服务器请求
using message::GetChatServerRsp;   // 获取聊天服务器响应
using message::StatusService;      // StatusServer的gRPC服务

// ChatServer结构体：存储ChatServer的配置信息
// 
// 字段说明：
//   - host: ChatServer主机地址
//   - port: ChatServer端口
//   - name: ChatServer名称
//   - con_count: 当前连接数（用于负载均衡）
struct ChatServer {
    std::string host;        // ChatServer主机地址
    std::string port;        // ChatServer端口
    std::string name;        // ChatServer名称（用于标识不同的ChatServer实例）
    int con_count;          // 当前连接数（从Redis读取，用于负载均衡）
};

// StatusServiceImpl类：StatusServer的gRPC服务实现
// 
// 作用：
//   实现StatusService接口，处理GateServer的请求
// 
// 主要功能：
//   1. GetChatServer: 根据用户ID分配一个可用的ChatServer（负载均衡）
//   2. Login: 验证用户token
// 
// 设计模式：
//   继承gRPC自动生成的Service基类，实现具体的服务方法
// 
// 负载均衡策略：
//   从Redis读取每个ChatServer的当前连接数（LOGIN_COUNT），
//   选择连接数最少的ChatServer分配给新用户
class StatusServiceImpl final : public StatusService::Service
{
public:
    // 构造函数：初始化StatusServiceImpl
    // 
    // 实现逻辑：
    //   从配置文件读取所有ChatServer的配置信息，存储在_servers中
    StatusServiceImpl();

    // 获取聊天服务器（带负载均衡）
    // 
    // 功能：
    //   为请求的用户分配一个可用的ChatServer
    // 
    // 参数：
    //   - context: gRPC服务上下文
    //   - request: 请求（包含用户ID）
    //   - reply: 响应（包含ChatServer的host、port、token）
    // 
    // 返回值：
    //   gRPC状态码
    Status GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply) override;

    // 用户登录验证
    // 
    // 功能：
    //   验证用户的token是否有效
    // 
    // 参数：
    //   - context: gRPC服务上下文
    //   - request: 请求（包含用户ID和token）
    //   - reply: 响应（包含验证结果）
    // 
    // 返回值：
    //   gRPC状态码
    Status Login(ServerContext* context, const message::LoginReq* request, message::LoginRsp* reply) override;

private:
    // 插入用户token
    // 
    // 参数：
    //   - uid: 用户ID
    //   - token: 认证令牌
    // 
    // 实现逻辑：
    //   将token存储到Redis中（key = USERTOKENPREFIX + uid）
    void insertToken(int uid, std::string token);

    // 获取可用的ChatServer（负载均衡）
    // 
    // 返回值：
    //   一个ChatServer结构体
    // 
    // 实现逻辑：
    //   1. 从Redis读取每个ChatServer的当前连接数
    //   2. 选择连接数最少的ChatServer
    //   3. 返回选中的ChatServer
    // 
    // 负载均衡算法：
    //   最小连接数优先（Least Connections）
    ChatServer getChatServer();

    // ChatServer列表（name -> ChatServer）
    std::unordered_map<std::string, ChatServer> _servers;
    // ChatServer列表的互斥锁（保证线程安全）
    std::mutex _server_mtx;
    // 服务器索引（用于Round-Robin，当前未使用）
    int _server_index;
};


