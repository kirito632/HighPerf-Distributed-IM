#include"StatusGrpcClient.h"

// 获取聊天服务器信息
// 
// 功能：
//   通过gRPC调用StatusServer，根据用户ID获取可用的ChatServer信息
// 
// 参数：
//   - uid: 用户ID
// 
// 返回值：
//   GetChatServerRsp：包含以下信息
//     - error: 错误码（0表示成功）
//     - host: ChatServer主机地址
//     - port: ChatServer端口
//     - token: 认证令牌
// 
// 实现逻辑：
//   1. 从连接池获取Stub
//   2. 设置RPC超时时间（10秒）
//   3. 调用StatusServer的GetChatServer方法
//   4. 处理返回结果（超时、错误等）
//   5. 归还Stub到连接池
// 
// 使用场景：
//   在用户登录成功时调用，获取ChatServer地址并返回给客户端
GetChatServerRsp StatusGrpcClient::GetChatServer(int uid)
{
    GetChatServerRsp reply;
    GetChatServerReq request;
    request.set_uid(uid);

    // 先从连接池获取Stub（带超时等待）
    auto stub = pool_->getConnection();
    if (!stub) {
        std::cerr << "[StatusGrpcClient] no stub from pool (timeout or stopped)\n";
        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }

    // 确保调用后Stub会归还（使用RAII模式）
    Defer defer([&stub, this]() {
        pool_->returnConnection(std::move(stub));
        });

    // 设置gRPC超时时间（10秒）
    grpc::ClientContext context;
    auto rpc_deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
    context.set_deadline(rpc_deadline);

    std::cout << "[StatusGrpcClient] calling GetChatServer uid=" << uid << "\n";
    // 调用StatusServer的GetChatServer方法
    grpc::Status status = stub->GetChatServer(&context, request, &reply);
    if (!status.ok()) {
        std::cerr << "[StatusGrpcClient] GetChatServer RPC failed: "
            << status.error_message() << " (code " << status.error_code() << ")\n";

        // 如果是超时，打印具体的超时信息
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cerr << "[StatusGrpcClient] RPC deadline exceeded for uid=" << uid << "\n";
        }

        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }

    // 打印成功的信息
    std::cout << "[StatusGrpcClient] GetChatServer reply: error=" << reply.error()
        << " host='" << reply.host() << "' port='" << reply.port()
        << "' token='" << reply.token() << "'\n";

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
    std::cout << "[StatusGrpcClient] Initializing with host=" << host << ", port=" << port << std::endl;
    pool_.reset(new StatusConPool(5, host, port));
}

