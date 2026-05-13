#include "RedisMgr.h"
#include"const.h"
#include"ConfigMgr.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>

// hiredis头文件需要在项目中包含include路径
#include <hiredis/hiredis.h>

namespace {
    // 工具函数：安全地将redisReply->str转换为std::string，防止reply==nullptr
    static std::string replyToString(redisReply* reply) {
        if (!reply || reply->type != REDIS_REPLY_STRING) return {};
        return std::string(reply->str, reply->len);
    }
} // namespace

// RedisConnectionGuard的RAII类确保获取的连接在作用域结束时归还
// 
// 说明：
//   Redis连接的生命周期管理
// 
// 使用方法：
//   auto guard = RedisConnectionGuard(pool, connection);
//   redisContext* ctx = guard.get();
class RedisConnectionGuard {
public:
    RedisConnectionGuard(RedisConPool* pool, redisContext* ctx) : pool_(pool), ctx_(ctx) {}

    // 析构函数自动归还连接到连接池
    ~RedisConnectionGuard() {
        if (pool_ && ctx_) {
            pool_->returnConnection(ctx_);
            // 不将ctx_重置为nullptr，因为连接已归还
        }
    }

    // 获取连接指针
    redisContext* get() const { return ctx_; }

    // 禁止复制和赋值
    RedisConnectionGuard(const RedisConnectionGuard&) = delete;
    RedisConnectionGuard& operator=(const RedisConnectionGuard&) = delete;
private:
    RedisConPool* pool_;      // 连接池指针
    redisContext* ctx_;       // Redis连接指针
};

// RedisMgr类的构造函数
// 
// 说明：
//   初始化Redis连接池
// 
// 实现逻辑：
//   1. 从配置文件中读取Redis服务器信息（地址、端口、密码）
//   2. 根据CPU核心数动态设置连接池大小
RedisMgr::RedisMgr()
{
    auto& gCfgMgr = ConfigMgr::Inst();
    auto host = gCfgMgr["Redis"]["Host"];
    auto port = gCfgMgr["Redis"]["Port"];
    auto pwd = gCfgMgr["Redis"]["Passwd"];
    // 根据CPU核心数动态设置连接池大小
    size_t pool_size = std::max(16u, std::thread::hardware_concurrency() * 2);
    std::cout << "[RedisMgr] CPU cores: " << std::thread::hardware_concurrency() 
              << ", Redis pool size: " << pool_size << std::endl;
    con_pool_.reset(new RedisConPool(pool_size, host.c_str(), atoi(port.c_str()), pwd.c_str()));
}

// RedisMgr类的析构函数
// 
// 说明：
//   关闭Redis连接池
// 
// 实现逻辑：
//   1. 调用连接池的Close方法
RedisMgr::~RedisMgr()
{
    Close();
}

// GET命令
// 
// 说明：
//   获取键值
// 
// 参数：
//   - key：键名
//   - value：键值
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 使用RAII类自动管理连接
//   3. 执行GET命令
//   4. 检查返回结果（NIL表示键不存在）
//   5. 获取键值
bool RedisMgr::Get(const std::string& key, std::string& value)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::Get] getConnection returned nullptr for key=" << key << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "GET %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "[RedisMgr::Get] redisCommand returned NULL for key=" << key << std::endl;
        return false;
    }

    // 检查返回结果
    if (reply->type == REDIS_REPLY_NIL) {
        // 键不存在
        freeReplyObject(reply);
        std::cout << "[RedisMgr::Get] GET " << key << " -> (nil)\n";
        return false;
    }

    if (reply->type != REDIS_REPLY_STRING) {
        std::cout << "[RedisMgr::Get] GET " << key << " unexpected reply type=" << reply->type << std::endl;
        freeReplyObject(reply);
        return false;
    }

    // 获取键值
    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Succeed to execute command [ GET " << key << " ]\n";
    return true;
}

// SET命令
// 
// 说明：
//   设置键值
// 
// 参数：
//   - key：键名
//   - value：键值
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行SET命令
//   3. 检查返回值是否为"OK"
bool RedisMgr::Set(const std::string& key, const std::string& value) {
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::Set] getConnection returned nullptr for key=" << key << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "SET %s %s", key.c_str(), value.c_str());
    if (reply == nullptr) {
        std::cout << "[RedisMgr::Set] Execut command [ SET " << key << "  " << value << " ] failure (reply==NULL)!\n";
        return false;
    }

    // 检查返回值
    bool ok = false;
    if (reply->type == REDIS_REPLY_STATUS) {
        // "OK" or "ok"
        std::string s(reply->str, reply->len);
        if (s == "OK" || s == "ok") ok = true;
    }

    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ SET " << key << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ SET " << key << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// PUBLISH命令
// 
// 说明：
//   发布消息到频道
// 
// 参数：
//   - channel：频道名
//   - message：消息内容
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行PUBLISH命令
bool RedisMgr::Publish(const std::string& channel, const std::string& message)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::Publish] getConnection returned nullptr for channel=" << channel << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "PUBLISH %s %s", channel.c_str(), message.c_str());
    if (reply == nullptr) {
        std::cout << "[RedisMgr::Publish] PUBLISH failed (reply==NULL) channel=" << channel << std::endl;
        return false;
    }

    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    long long receivers = ok ? reply->integer : -1;
    freeReplyObject(reply);

    std::cout << "[RedisMgr::Publish] channel=" << channel << " receivers=" << receivers
        << " payload_len=" << message.size() << std::endl;
    return ok;
}

// AUTH命令
// 
// 说明：
//   认证
// 
// 参数：
//   - password：密码
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行AUTH命令
bool RedisMgr::Auth(const std::string& password)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::Auth] getConnection returned nullptr\n";
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "AUTH %s", password.c_str());
    if (reply == nullptr) {
        std::cout << "[RedisMgr::Auth] AUTH returned NULL\n";
        return false;
    }

    bool ok = true;
    if (reply->type == REDIS_REPLY_ERROR) {
        ok = false;
    }
    freeReplyObject(reply);

    if (ok) std::cout << "认证成功" << std::endl;
    else std::cout << "认证失败" << std::endl;
    return ok;
}

// LPUSH命令
// 
// 说明：
//   列表左端添加元素
// 
// 参数：
//   - key：列表键名
//   - value：要添加的值
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行LPUSH命令
bool RedisMgr::LPush(const std::string& key, const std::string& value)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::LPush] getConnection nullptr for key=" << key << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "LPUSH %s %s", key.c_str(), value.c_str());
    if (reply == nullptr) {
        std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] failure (reply==NULL)!\n";
        return false;
    }

    // LPUSH返回整数表示更新后的列表长度
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// LPOP命令
// 
// 说明：
//   弹出列表左端元素
// 
// 参数：
//   - key：列表键名
//   - value：弹出的值
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行LPOP命令
bool RedisMgr::LPop(const std::string& key, std::string& value) {
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::LPop] getConnection nullptr for key=" << key << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "LPOP %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "Execut command [ LPOP " << key << " ] failure (reply==NULL)!\n";
        return false;
    }

    // 检查返回结果
    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        std::cout << "Execut command [ LPOP " << key << " ] -> (nil)\n";
        return false;
    }
    if (reply->type != REDIS_REPLY_STRING) {
        freeReplyObject(reply);
        std::cout << "Execut command [ LPOP " << key << " ] unexpected type=" << reply->type << "\n";
        return false;
    }

    // 获取弹出的值
    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Execut command [ LPOP " << key << " ] success ! " << std::endl;
    return true;
}

// RPUSH命令
// 
// 说明：
//   列表右端添加元素
// 
// 参数：
//   - key：列表键名
//   - value：要添加的值
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行RPUSH命令
bool RedisMgr::RPush(const std::string& key, const std::string& value) {
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::RPush] getConnection nullptr for key=" << key << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "RPUSH %s %s", key.c_str(), value.c_str());
    if (reply == nullptr) {
        std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] failure (reply==NULL)!\n";
        return false;
    }

    // RPUSH返回整数表示更新后的列表长度
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// RPOP命令
// 
// 说明：
//   弹出列表右端元素
// 
// 参数：
//   - key：列表键名
//   - value：弹出的值
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行RPOP命令
bool RedisMgr::RPop(const std::string& key, std::string& value) {
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::RPop] getConnection nullptr for key=" << key << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "RPOP %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "Execut command [ RPOP " << key << " ] failure (reply==NULL)!\n";
        return false;
    }

    // 检查返回结果
    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        std::cout << "Execut command [ RPOP " << key << " ] -> (nil)\n";
        return false;
    }
    if (reply->type != REDIS_REPLY_STRING) {
        freeReplyObject(reply);
        std::cout << "Execut command [ RPOP " << key << " ] unexpected type=" << reply->type << "\n";
        return false;
    }

    // 获取弹出的值
    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Execut command [ RPOP " << key << " ] success ! " << std::endl;
    return true;
}

// HSET命令
// 
// 说明：
//   设置哈希字段值
// 
// 参数：
//   - key：哈希键名
//   - hkey：字段名
//   - value：字段值
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行HSET命令
bool RedisMgr::HSet(const std::string& key, const std::string& hkey, const std::string& value) {
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HSet] getConnection nullptr for key=" << key << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "HSET %s %s %s", key.c_str(), hkey.c_str(), value.c_str());
    if (reply == nullptr) {
        std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] failure (reply==NULL)!\n";
        return false;
    }

    // 获取键值
    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// HSET命令（二进制数据）
// 
// 说明：
//   设置哈希字段值（二进制数据）
// 
// 参数：
//   - key：哈希键名
//   - hkey：字段名
//   - hvalue：字段值（二进制数据）
//   - hvaluelen：数据长度
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行HSET命令（二进制数据）
bool RedisMgr::HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HSet(binary)] getConnection nullptr\n";
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    // 准备redisCommandArgv的参数
    const char* argv[4];
    size_t argvlen[4];
    argv[0] = "HSET";
    argvlen[0] = 4;
    argv[1] = key;
    argvlen[1] = strlen(key);
    argv[2] = hkey;
    argvlen[2] = strlen(hkey);
    argv[3] = hvalue;
    argvlen[3] = hvaluelen;

    redisReply* reply = (redisReply*)redisCommandArgv(connect, 4, argv, argvlen);
    if (reply == nullptr) {
        std::cout << "Execut command [ HSet(binary) ] failure (reply==NULL)!\n";
        return false;
    }

    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ HSet(binary) ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ HSet(binary) ] failure ! " << std::endl;
    return false;
}

// HDEL命令
// 
// 说明：
//   删除哈希字段
// 
// 参数：
//   - key：哈希键名
//   - field：字段名
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行HDEL命令
bool RedisMgr::HDel(const std::string& key, const std::string& field)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HDel] getConnection returned nullptr for key=" << key << " field=" << field << std::endl;
        return false;
    }
    // 使用RAII自动归还连接
    RedisConnectionGuard guard(con_pool_.get(), connect);

    // 删除哈希字段
    redisReply* reply = (redisReply*)redisCommand(connect, "HDEL %s %s", key.c_str(), field.c_str());
    if (reply == nullptr) {
        std::cout << "Execut command [ HDEL " << key << " " << field << " ] failure (reply==NULL)!\n";
        return false;
    }

    // HDEL返回整数，返回值的含义是删除的字段数量
    bool ok = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        if (reply->integer > 0) {
            ok = true; // 删除成功
        }
        else {
            ok = false; // 删除失败
        }
    }
    else {
        std::cout << "Execut command [ HDEL " << key << " " << field << " ] unexpected reply type=" << reply->type << std::endl;
    }

    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ HDEL " << key << " " << field << " ] success ! " << std::endl;
        return true;
    }
    else {
        std::cout << "Execut command [ HDEL " << key << " " << field << " ] no field removed.\n";
        return false;
    }
}

// HGET命令
// 
// 说明：
//   获取哈希字段值
// 
// 参数：
//   - key：哈希键名
//   - hkey：字段名
// 
// 返回值：
//   - 字段值
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行HGET命令
std::string RedisMgr::HGet(const std::string& key, const std::string& hkey)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HGet] getConnection nullptr for key=" << key << std::endl;
        return "";
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    // 准备redisCommandArgv的参数
    const char* argv[3];
    size_t argvlen[3];
    argv[0] = "HGET";
    argvlen[0] = 4;
    argv[1] = key.c_str();
    argvlen[1] = key.length();
    argv[2] = hkey.c_str();
    argvlen[2] = hkey.length();

    redisReply* reply = (redisReply*)redisCommandArgv(connect, 3, argv, argvlen);
    if (reply == nullptr) {
        std::cout << "Execut command [ HGet " << key << " " << hkey << " ] failure (reply==NULL)!\n";
        return "";
    }

    // 检查返回结果
    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        std::cout << "Execut command [ HGet " << key << " " << hkey << " ] -> (nil)\n";
        return "";
    }

    if (reply->type != REDIS_REPLY_STRING) {
        std::cout << "Execut command [ HGet " << key << " " << hkey << " ] unexpected type=" << reply->type << std::endl;
        freeReplyObject(reply);
        return "";
    }

    // 获取字段值
    std::string value(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Execut command [ HGet " << key << " " << hkey << " ] success ! " << std::endl;
    return value;
}

// DEL命令
// 
// 说明：
//   删除键
// 
// 参数：
//   - key：键名
// 
// 返回值：
//   - true：成功
//   - false：失败
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行DEL命令
bool RedisMgr::Del(const std::string& key)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::Del] getConnection nullptr for key=" << key << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "DEL %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "Execut command [ Del " << key << " ] failure (reply==NULL)!\n";
        return false;
    }

    // DEL返回整数表示删除的键数量
    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ Del " << key << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ Del " << key << " ] failure ! " << std::endl;
    return false;
}

// EXISTS命令
// 
// 说明：
//   检查键是否存在
// 
// 参数：
//   - key：键名
// 
// 返回值：
//   - true：存在
//   - false：不存在
// 
// 实现逻辑：
//   1. 获取连接
//   2. 执行EXISTS命令
bool RedisMgr::ExistsKey(const std::string& key)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::ExistsKey] getConnection nullptr for key=" << key << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "EXISTS %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "Not Found [ Key " << key << " ]  ! (reply==NULL)\n";
        return false;
    }

    // 检查键是否存在
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << " Found [ Key " << key << " ] exists ! " << std::endl;
        return true;
    }
    std::cout << " Not Found [ Key " << key << " ] ! " << std::endl;
    return false;
}

// 关闭Redis连接池
// 
// 说明：
//   关闭Redis连接池
// 
// 实现逻辑：
//   1. 调用连接池的Close方法
void RedisMgr::Close()
{
    if (con_pool_) {
        con_pool_->Close();
        con_pool_.reset();
    }
}
