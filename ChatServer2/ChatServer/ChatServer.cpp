#include "LogicSystem.h"
#include <csignal>
#include <thread>
#include <mutex>
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "UserMgr.h"
#include "CSession.h"
#include<iostream>
#include<memory>
#include<algorithm>
#include<chrono>
#include<boost/asio.hpp>
#include<atomic>
#include"RedisMgr.h"
#include "ChatServiceImpl.h"
#include "const.h"
#include <filesystem>
#include "MysqlDao.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

std::atomic<bool> bstop = false;
std::condition_variable cond_quit;
std::mutex mutex_quit;

#include <string>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// 获取可执行文件所在目录
// 
// 作用：
//   用于定位配置文件config.ini的路径
// 
// 返回值：
//   可执行文件所在目录的路径
std::string getExecutableDir() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0) return {};
    fs::path p(buf);
    return p.parent_path().string();
#else
    char buf[1024];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return {};
    buf[len] = '\0';
    fs::path p(buf);
    return p.parent_path().string();
#endif
}


// 主函数：程序入口
// 
// 功能：
//   1. 初始化IO服务池
//   2. 启动gRPC服务器
//   3. 启动TCP服务器
//   4. 处理优雅关闭
// 
// 实现逻辑：
//   1. 初始化Redis中该ChatServer的登录计数为0
//   2. 启动gRPC服务器（监听StatusServer的调用）
//   3. 启动TCP服务器（接受客户端连接）
//   4. 监听SIGINT/SIGTERM信号，优雅关闭
//   5. 退出时清理Redis计数
int main()
{
    std::cout << "cwd: " << std::filesystem::current_path() << std::endl;
    std::cout << "exe dir: " << getExecutableDir() << std::endl;
    std::cout << "trying to load config at: " << (std::filesystem::current_path() / "config_chat2.ini") << std::endl;

    auto& cfg = ConfigMgr::Inst();
    auto server_name = cfg["SelfServer"]["Name"];
    std::transform(server_name.begin(), server_name.end(), server_name.begin(), ::tolower);

    try {
        // 初始化 MySQL 连接池
        std::string mysql_host = cfg["Mysql"]["Host"];
        std::string mysql_port_str = cfg["Mysql"]["Port"];
        std::string mysql_user = cfg["Mysql"]["User"];
        std::string mysql_passwd = cfg["Mysql"]["Passwd"];
        std::string mysql_schema = cfg["Mysql"]["Schema"];
        std::string mysql_url = "tcp://" + mysql_host + ":" + mysql_port_str;
        using MySqlPoolSingleton = Singleton<MySqlPool>;
        auto mysqlPool = MySqlPoolSingleton::GetInstance();
        // 根据CPU核心数动态设置MySQL连接池大小
        size_t mysql_pool_size = std::max(16u, std::thread::hardware_concurrency() * 2);
        std::cout << "[ChatServer] MySQL pool size: " << mysql_pool_size << std::endl;
        mysqlPool->Init(mysql_url, mysql_user, mysql_passwd, mysql_schema, mysql_pool_size);

        // 获取IO服务池单例
        auto pool = AsioIOServicePool::GetInstance();

        // 初始化登录计数为0（在Redis中存储该ChatServer的连接数）
        RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, "0");

        // 创建一个gRPC Server
        std::string server_address(cfg["SelfServer"]["Host"] + ":" + cfg["SelfServer"]["RPCPort"]);
        ChatServiceImpl service;
        grpc::ServerBuilder builder;

        // 绑定端口和添加服务
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);

        // 构建并启动 gRPC 服务器
        std::unique_ptr<grpc::Server> grpc_server(builder.BuildAndStart());
        std::cout << "gRPC Server listening on " << server_address << std::endl;

        // 启用 grpc 在一个线程中监听
        std::thread grpc_server_thread([&grpc_server]() {
            grpc_server->Wait(); // 阻塞等待 gRPC 服务器关闭
            });

        // 在独立线程中订阅 Redis 事件，并下发 TCP 通知
        std::thread redis_sub_thread([]() {
            auto& cfg = ConfigMgr::Inst();
            auto host = cfg["Redis"]["Host"];
            auto port_str = cfg["Redis"]["Port"];
            auto passwd = cfg["Redis"]["Passwd"];
            int port = 6379;
            try { port = std::stoi(port_str); }
            catch (...) {}

            while (!bstop.load()) {
                redisContext* ctx = redisConnect(host.c_str(), port);
                if (ctx == nullptr || ctx->err) {
                    std::cout << "[FriendNotify][Chat][Redis] connect failed host=" << host << ":" << port << std::endl;
                    if (ctx) { redisFree(ctx); }
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }

                if (!passwd.empty()) {
                    redisReply* auth = (redisReply*)redisCommand(ctx, "AUTH %s", passwd.c_str());
                    if (!auth || auth->type == REDIS_REPLY_ERROR) {
                        std::cout << "[FriendNotify][Chat][Redis] AUTH failed" << std::endl;
                        if (auth) freeReplyObject(auth);
                        redisFree(ctx);
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                        continue;
                    }
                    freeReplyObject(auth);
                }

                // 设置客户端名称，便于在 CLIENT LIST 中识别订阅连接
                try {
                    auto myname = cfg["SelfServer"]["Name"];
                    std::transform(myname.begin(), myname.end(), myname.begin(), ::tolower);
                    std::string cname = myname + "-sub";
                    redisReply* setname = (redisReply*)redisCommand(ctx, "CLIENT SETNAME %s", cname.c_str());
                    if (setname) freeReplyObject(setname);
                    std::cout << "[FriendNotify][Chat][Redis] SETNAME " << cname << std::endl;
                }
                catch (...) {}

                // 设置读超时，避免阻塞无法退出
                timeval tv{ 1, 0 };
                redisSetTimeout(ctx, tv);

                redisReply* sub = (redisReply*)redisCommand(ctx, "SUBSCRIBE friend.apply friend.reply");
                if (!sub) {
                    std::cout << "[FriendNotify][Chat][Redis] SUBSCRIBE send failed" << std::endl;
                    redisFree(ctx);
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }
                freeReplyObject(sub);
                std::cout << "[FriendNotify][Chat][Redis] SUBSCRIBED channels: friend.apply, friend.reply" << std::endl;
                // 订阅后进入读取循环
                size_t idle_ticks = 0; // 软超时的空转计数，用于周期性打点
                while (!bstop.load()) {
                    void* r = nullptr;
                    int rc = redisGetReply(ctx, &r);
                    if (rc != REDIS_OK) {
                        // 可能是超时或暂时无数据：hiredis 在超时/无数据时也会设置 err/errstr
                        std::string es = ctx->errstr ? ctx->errstr : "";
                        bool is_soft_timeout = (ctx->err == REDIS_ERR_TIMEOUT)
                            || (es.find("timed out") != std::string::npos)
                            || (es.find("Resource temporarily unavailable") != std::string::npos)
                            || (es.find("EAGAIN") != std::string::npos)
                            || (es.find("would block") != std::string::npos);

                        if (is_soft_timeout || es.empty()) {
                            // 视为正常轮询，无需重连
                            // 某些平台下 err 会被置位，不影响后续读取，这里不强制清零以避免未定义行为
                            // 修复：部分平台一旦置错将持续返回错误，影响后续读取，这里显式清空错误状态
                            ctx->err = 0;
                            if (ctx->errstr) { ctx->errstr[0] = '\0'; }
                            ++idle_ticks;
                            if ((idle_ticks % 100) == 0) {
                                std::cout << "[FriendNotify][Chat][Redis] idle tick (no message)" << std::endl;
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                            continue;
                        }

                        // 确认为真实错误，记录并重连
                        std::cout << "[FriendNotify][Chat][Redis] redisGetReply error, err=" << ctx->err
                            << " errstr=" << es << " reconnecting..." << std::endl;
                        break;
                    }
                    std::unique_ptr<redisReply, void(*)(void*)> reply((redisReply*)r, freeReplyObject);
                    if (!reply) continue;
                    // 诊断：更全面地记录 Redis 回复形态
                    if (reply->type == REDIS_REPLY_ARRAY) {
                        size_t elems = reply->elements;
                        std::string kind;
                        if (elems >= 1 && reply->element[0]->type == REDIS_REPLY_STRING && reply->element[0]->str) {
                            kind.assign(reply->element[0]->str, reply->element[0]->len);
                        }

                        // 非 message 的数组回复（如 subscribe 确认）也打印，便于诊断
                        if (kind != "message") {
                            std::cout << "[FriendNotify][Chat][Redis] kind=" << (kind.empty() ? std::string("<nil>") : kind)
                                << " elems=" << elems;
                            if (elems >= 2 && reply->element[1]->type == REDIS_REPLY_STRING && reply->element[1]->str) {
                                std::string ch(reply->element[1]->str, reply->element[1]->len);
                                std::cout << " channel=" << ch;
                            }
                            if (elems >= 3 && reply->element[2]->type == REDIS_REPLY_INTEGER) {
                                std::cout << " count=" << reply->element[2]->integer;
                            }
                            std::cout << std::endl;
                        }

                        // 正常的 message 事件
                        if (kind == "message" && elems >= 3
                            && reply->element[1]->type == REDIS_REPLY_STRING && reply->element[1]->str
                            && reply->element[2]->type == REDIS_REPLY_STRING && reply->element[2]->str) {
                            std::string channel(reply->element[1]->str, reply->element[1]->len);
                            std::string payload(reply->element[2]->str, reply->element[2]->len);
                            std::cout << "[FriendNotify][Chat][Redis] recv channel=" << channel << " payload=" << payload << std::endl;
                            Json::Value obj; Json::Reader rd; bool ok = rd.parse(payload, obj);
                            if (!ok || !obj.isObject()) {
                                std::cout << "[FriendNotify][Chat][Redis] invalid json payload" << std::endl;
                                continue;
                            }
                            int error = obj.get("error", -1).asInt();
                            if (channel == "friend.apply") {
                                int to_uid = obj.get("to_uid", 0).asInt();
                                auto sess = UserMgr::GetInstance()->GetSession(to_uid);
                                if (sess) {
                                    std::cout << "[FriendNotify][Chat][Redis] send TCP 1021 to_uid=" << to_uid << std::endl;
                                    sess->Send(payload, ID_NOTIFY_ADD_FRIEND_REQ);
                                }
                                else {
                                    std::cout << "[FriendNotify][Chat][Redis] to_uid offline, skip" << std::endl;
                                }
                            }
                            else if (channel == "friend.reply") {
                                int from_uid = obj.get("from_uid", 0).asInt();
                                auto sess = UserMgr::GetInstance()->GetSession(from_uid);
                                if (sess) {
                                    std::cout << "[FriendNotify][Chat][Redis] send TCP 1022 from_uid=" << from_uid << std::endl;
                                    sess->Send(payload, ID_NOTIFY_FRIEND_REPLY);
                                }
                                else {
                                    std::cout << "[FriendNotify][Chat][Redis] from_uid offline, skip" << std::endl;
                                }
                            }
                            continue;
                        }

                        // 可选：如收到 pmessage（模式订阅），做个提示
                        if (kind == "pmessage" && elems >= 4
                            && reply->element[2]->type == REDIS_REPLY_STRING && reply->element[2]->str
                            && reply->element[3]->type == REDIS_REPLY_STRING && reply->element[3]->str) {
                            std::string channel(reply->element[2]->str, reply->element[2]->len);
                            std::string payload(reply->element[3]->str, reply->element[3]->len);
                            std::cout << "[FriendNotify][Chat][Redis] recv pmessage channel=" << channel << " payload=" << payload << std::endl;
                        }
                    }
                    else {
                        // 非数组类型的回复，记录类型码以便诊断
                        std::cout << "[FriendNotify][Chat][Redis] unexpected reply type=" << reply->type << std::endl;
                    }
                }

                redisFree(ctx);
                if (!bstop.load()) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
            }
            });

        // 从 pool 获取 io_context（注意 pool 初始化时已经创建 io_contexts 和线程）
        boost::asio::io_context& io_context = pool->GetIOService();

        // 创建 SIGINT/SIGTERM 信号绑定到 pool 的 io_context
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

        // 设置异步信号处理
        signals.async_wait([&](const boost::system::error_code& ec, int signo) {
            if (!ec) {
                std::cout << "signal " << signo << " received, stopping..." << std::endl;
                // 停止 pool，同时 stop io_context 并 join 线程
                pool->Stop();

                // 通知主线程退出
                bstop.store(true);
                grpc_server->Shutdown(); // 关闭 gRPC 服务器
                cond_quit.notify_one();
            }
            else {
                std::cerr << "signal handler error: " << ec.message() << std::endl;
            }
            });

        // 获取监听端口
        std::string port_str = cfg["SelfServer"]["Port"];
        std::cout << "SelfServer.Port='" << port_str << "'" << std::endl;
        if (port_str.empty()) {
            std::cerr << "Config SelfServer.Port is empty!" << std::endl;
            return -1;
        }

        int port = 0;
        try {
            port = std::stoi(port_str);
        }
        catch (const std::exception& e) {
            std::cerr << "Invalid port string: " << port_str << " err=" << e.what() << std::endl;
            return -1;
        }
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port value: " << port << std::endl;
            return -1;
        }

        // 从 pool 的 io_context 创建 CServer，确保 CServer 使用该 io_context
        CServer s(io_context, static_cast<unsigned short>(port));

        // 主线程阻塞等待 signal 回通知退出
        std::unique_lock<std::mutex> lk(mutex_quit);
        std::cout << "About to wait on cond_quit\n";
        cond_quit.wait(lk, [&] { return bstop.load(); });

        // 清理资源
        RedisMgr::GetInstance()->HDel(LOGIN_COUNT, server_name);
        RedisMgr::GetInstance()->Close();
        grpc_server_thread.join(); // 等待 gRPC 线程退出
        // [FriendNotify]
        if (redis_sub_thread.joinable()) redis_sub_thread.join();

        std::cout << "Woke from cond_quit wait, bstop=" << bstop.load() << "\n";
        std::cout << "Server exiting normally." << std::endl;
        return 0;
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }
}



