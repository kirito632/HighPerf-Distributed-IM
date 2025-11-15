#include <iostream>
#include<json/json.h>
#include<json/reader.h>
#include<json/value.h>
#include"CServer.h"
#include"ConfigMgr.h"
#include"const.h"
#include"RedisMgr.h"
#include"MysqlDao.h"
#include"Singleton.h"
#include <jdbc/cppconn/exception.h>
#include<csignal>
class MysqlPool;

// 用于管理全局唯一的MySQL连接池
// 使用单例模式确保整个程序只有一个MySQL连接池实例
using MySqlPoolSingleton = Singleton<MySqlPool>;

// 测试Redis连接和基本操作
// 
// 作用：
//   测试Redis数据库的连接、认证、基本操作等功能
// 
// 实现逻辑：
//   1. 连接到本地Redis服务器（端口6380）
//   2. 进行身份认证（密码: 123456）
//   3. 执行基本操作：SET、GET、STRLEN命令
//   4. 释放连接资源
// 
// 注意：
//   需要启动Redis服务器才能进行连接
//   Redis默认端口为6379，这里使用的是6380
void TestRedis() {
    // 连接Redis服务器（默认端口可能不同，需要检查配置）
    // Redis默认监听端口为6387，可以在配置文件中修改
    redisContext* c = redisConnect("127.0.0.1", 6380);
    if (c->err)
    {
        printf("Connect to redisServer faile:%s\n", c->errstr);
        redisFree(c);
        return;
    }
    printf("Connect to redisServer Success\n");

    // 进行Redis身份认证
    std::string redis_password = "123456";
    redisReply* r = (redisReply*)redisCommand(c, "AUTH %s", redis_password.c_str());
    if (r->type == REDIS_REPLY_ERROR) {
        printf("Redis认证失败！\n");
    }
    else {
        printf("Redis认证成功！\n");
    }

    // 测试SET命令：为Redis设置key-value
    const char* command1 = "set stest1 value1";

    // 执行redis命令行
    r = (redisReply*)redisCommand(c, command1);

    // 如果返回NULL则说明执行失败
    if (NULL == r)
    {
        printf("Execut command1 failure\n");
        redisFree(c);
        return;
    }

    // 如果执行失败则释放连接
    if (!(r->type == REDIS_REPLY_STATUS && (strcmp(r->str, "OK") == 0 || strcmp(r->str, "ok") == 0)))
    {
        printf("Failed to execute command[%s]\n", command1);
        freeReplyObject(r);
        redisFree(c);
        return;
    }

    // 执行成功，释放redisCommand执行后返回的redisReply所占用的内存
    freeReplyObject(r);
    printf("Succeed to execute command[%s]\n", command1);

    // 测试STRLEN命令：获取字符串长度
    const char* command2 = "strlen stest1";
    r = (redisReply*)redisCommand(c, command2);

    // 如果返回类型不是整型则释放连接
    if (r->type != REDIS_REPLY_INTEGER)
    {
        printf("Failed to execute command[%s]\n", command2);
        freeReplyObject(r);
        redisFree(c);
        return;
    }

    // 获取字符串长度
    int length = r->integer;
    freeReplyObject(r);
    printf("The length of 'stest1' is %d.\n", length);
    printf("Succeed to execute command[%s]\n", command2);

    // 测试GET命令：获取redis键值对信息
    const char* command3 = "get stest1";
    r = (redisReply*)redisCommand(c, command3);
    if (r->type != REDIS_REPLY_STRING)
    {
        printf("Failed to execute command[%s]\n", command3);
        freeReplyObject(r);
        redisFree(c);
        return;
    }
    printf("The value of 'stest1' is %s\n", r->str);
    freeReplyObject(r);
    printf("Succeed to execute command[%s]\n", command3);

    // 测试获取不存在的key
    const char* command4 = "get stest2";
    r = (redisReply*)redisCommand(c, command4);
    if (r->type != REDIS_REPLY_NIL)
    {
        printf("Failed to execute command[%s]\n", command4);
        freeReplyObject(r);
        redisFree(c);
        return;
    }
    freeReplyObject(r);
    printf("Succeed to execute command[%s]\n", command4);

    // 释放连接资源
    redisFree(c);

}

// 测试Redis管理器功能
// 
// 作用：
//   测试RedisMgr封装的高层Redis操作接口
// 
// 实现逻辑：
//   测试以下功能：
//   1. Set/Get - 简单的key-value操作
//   2. HSet/HGet - Hash表操作
//   3. ExistsKey - 检查key是否存在
//   4. Del - 删除key
//   5. LPush/RPop/LPop - 列表操作（队列）
// 
// 注意：
//   使用断言确保每个操作都成功执行
void TestRedisMgr() {

    assert(RedisMgr::GetInstance()->Set("blogwebsite", "llfc.club"));
    std::string value = "";
    assert(RedisMgr::GetInstance()->Get("blogwebsite", value));
    assert(RedisMgr::GetInstance()->Get("nonekey", value) == false);
    assert(RedisMgr::GetInstance()->HSet("bloginfo", "blogwebsite", "llfc.club"));
    assert(RedisMgr::GetInstance()->HGet("bloginfo", "blogwebsite") != "");
    assert(RedisMgr::GetInstance()->ExistsKey("bloginfo"));
    assert(RedisMgr::GetInstance()->Del("bloginfo"));
    assert(RedisMgr::GetInstance()->Del("bloginfo"));
    assert(RedisMgr::GetInstance()->ExistsKey("bloginfo") == false);
    assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue1"));
    assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue2"));
    assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue3"));
    assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
    assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
    assert(RedisMgr::GetInstance()->LPop("lpushkey1", value));
    assert(RedisMgr::GetInstance()->LPop("lpushkey2", value) == false);

}

// 主函数：程序入口
// 
// 作用：
//   1. 初始化MySQL连接池
//   2. 启动HTTP服务器（GateServer）
// 
// 实现逻辑：
//   1. 从配置文件读取MySQL连接信息
//   2. 初始化MySQL连接池（10个连接）
//   3. 从配置文件读取GateServer端口
//   4. 创建io_context和信号处理器（处理SIGINT、SIGTERM）
//   5. 创建HTTP服务器并启动
//   6. 运行事件循环
// 
// 执行流程：
//   main() -> 读取配置 -> 初始化MySQL池 -> 创建HTTP服务器 -> 运行事件循环
int main()
{
    // 测试函数（已注释，如需要可以取消注释进行测试）
    //TestRedis();
    //TestRedisMgr();

    // 获取配置管理器单例
    auto& gCfgMgr = ConfigMgr::Inst();

    // 从配置文件读取MySQL连接信息
    std::string host = gCfgMgr["Mysql"]["Host"];
    std::string port = gCfgMgr["Mysql"]["Port"];
    std::string user = gCfgMgr["Mysql"]["User"];
    std::string passwd = gCfgMgr["Mysql"]["Passwd"];
    std::string schema = gCfgMgr["Mysql"]["Schema"];

    std::cout << "[main] MySQL config host=" << host << " port=" << port
        << " user=" << user << " schema=" << schema << std::endl;

    // 构建MySQL连接URL
    std::string url = "tcp://" + host + ":" + port;

    // 获取MySQL连接池单例并初始化
    auto pool = MySqlPoolSingleton::GetInstance();
    try {
        // 初始化连接池，参数：URL, 用户名, 密码, 数据库名, 连接数
        pool->Init(url, user, passwd, schema, 10);
    }
    catch (const sql::SQLException& e) {
        std::cerr << "[main] MySQL init failed: " << e.what()
            << " (MySQL error code: " << e.getErrorCode()
            << ", SQLState: " << e.getSQLState() << ")" << std::endl;
        return -1;
    }
    catch (const std::exception& e) {
        std::cerr << "[main] MySQL init std::exception: " << e.what() << std::endl;
        return -1;
    }
    catch (...) {
        std::cerr << "[main] MySQL init unknown exception" << std::endl;
        return -1;
    }

    // 从配置文件读取GateServer端口
    std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
    unsigned short gate_port = atoi(gate_port_str.c_str());

    try {
        // 设置HTTP服务器端口（8080）
        unsigned short port = static_cast<unsigned short>(gate_port);

        // 创建IO上下文（1个线程）
        net::io_context ioc{ 1 };

        // 创建信号集处理器，处理SIGINT（Ctrl+C）和SIGTERM信号
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

        // 异步等待信号，当收到信号时停止IO上下文
        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number) {
            if (error) {
                return;
            }
            // 停止事件循环，优雅地关闭服务器
            ioc.stop();
            });

        // 创建HTTP服务器并启动
        // CServer会自动开始接受客户端连接
        std::make_shared<CServer>(ioc, port)->Start();

        // 运行事件循环（阻塞直到调用ioc.stop()）
        ioc.run();
    }
    catch (std::exception& e) {
        std::cerr << "exception is " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}


