#include"StatusGrpcClient.h"

// 获取ChatServer信息（当前未使用）
GetChatServerRsp StatusGrpcClient::GetChatServer(int uid)
{
    ClientContext context;
    GetChatServerRsp reply;
    GetChatServerReq request;
    request.set_uid(uid);

    // 从连接池获取Stub
    auto stub = pool_->getConnection();
    if (!stub) {
        std::cerr << "StatusGrpcClient::GetChatServer - no stub from pool\n";
        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }

    // 使用RAII确保连接归还
    Defer defer([&stub, this]() {
        pool_->returnConnection(std::move(stub));
        });

    // 调用gRPC
    Status status = stub->GetChatServer(&context, request, &reply);

    if (!status.ok()) {
        std::cerr << "GetChatServer RPC failed: " << status.error_message()
            << " (code " << status.error_code() << ")\n";
        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }

    // 输出关键日志，打印 server 返回的 host/port/token
    std::cout << "StatusGrpcClient::GetChatServer reply: error=" << reply.error()
        << " host='" << reply.host() << "' port='" << reply.port()
        << "' token='" << reply.token() << "'\n";

    return reply;
}

// 验证用户token
// 
// 功能：
//   通过gRPC调用StatusServer验证用户token
// 
// 实现逻辑：
//   1. 构造请求（uid和token）
//   2. 从连接池获取Stub
//   3. 使用RAII确保连接归还
//   4. 调用gRPC
//   5. 返回验证结果
LoginRsp StatusGrpcClient::Login(int uid, std::string token)
{
    // 1. 构造请求消息
    LoginReq request;
    request.set_uid(uid);
    request.set_token(token);

    // 2. 构造响应消息
    LoginRsp reply;

    // 3. 获取连接池中的 stub
    auto stub = pool_->getConnection();
    if (!stub) {
        std::cerr << "StatusGrpcClient::Login - no stub from pool\n";
        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }

    // 4. 使用 Defer 确保退出时释放 stub
    Defer defer([&stub, this]() {
        pool_->returnConnection(std::move(stub));
        });

    // 5. 调用 gRPC
    ClientContext context;
    Status status = stub->Login(&context, request, &reply);

    // 6. 返回响应
    if (!status.ok()) {
        std::cerr << "Login RPC failed: " << status.error_message()
            << " (code " << status.error_code() << ")\n";
        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }

    // 7. 输出响应
    std::cout << "StatusGrpcClient::Login reply: error=" << reply.error()
        << " uid='" << reply.uid() << "' token='" << reply.token() << "'\n";

    return reply;
}


// 构造函数：初始化StatusGrpcClient
// 
// 实现逻辑：
//   1. 从配置管理器获取StatusServer的连接信息
//   2. 创建StatusServer的连接池（默认5个连接）
StatusGrpcClient::StatusGrpcClient()
{
    auto& gCfgMgr = ConfigMgr::Inst();
    std::string host = gCfgMgr["StatusServer"]["Host"];
    std::string port = gCfgMgr["StatusServer"]["Port"];
    pool_.reset(new StatusConPool(5, host, port));
}
