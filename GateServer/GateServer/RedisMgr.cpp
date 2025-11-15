#include "RedisMgr.h"
#include"const.h"
#include"ConfigMgr.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>

// hiredis头文件（根据实际项目的include路径）
#include <hiredis/hiredis.h>

namespace {
    // 辅助函数：安全地将redisReply->str转换为std::string，防止reply==nullptr
    static std::string replyToString(redisReply* reply) {
        if (!reply || reply->type != REDIS_REPLY_STRING) return {};
        return std::string(reply->str, reply->len);
    }
} // namespace

// RedisConnectionGuard：RAII机制确保获取到的连接会在作用域结束时归还
// 
// 作用：
//   自动管理Redis连接的生命周期，确保连接在使用完后归还到连接池
// 
// 使用方式：
//   auto guard = RedisConnectionGuard(pool, connection);
//   redisContext* ctx = guard.get();
class RedisConnectionGuard {
public:
    RedisConnectionGuard(RedisConPool* pool, redisContext* ctx) : pool_(pool), ctx_(ctx) {}

    // 析构函数：自动归还连接到连接池
    ~RedisConnectionGuard() {
        if (pool_ && ctx_) {
            pool_->returnConnection(ctx_);
            // 将ctx_设置为nullptr，因为连接已归还
        }
    }

    // 获取连接的原始指针
    redisContext* get() const { return ctx_; }

    // 禁止拷贝（移动语义）
    RedisConnectionGuard(const RedisConnectionGuard&) = delete;
    RedisConnectionGuard& operator=(const RedisConnectionGuard&) = delete;
private:
    RedisConPool* pool_;      // 连接池指针
    redisContext* ctx_;       // Redis连接指针
};

// 构造函数：初始化Redis管理器
// 
// 作用：
//   从配置文件中读取Redis连接信息，创建连接池
// 
// 实现逻辑：
//   1. 从配置管理器获取Redis连接信息（主机、端口、密码）
//   2. 创建Redis连接池（默认5个连接）
RedisMgr::RedisMgr()
{
    auto& gCfgMgr = ConfigMgr::Inst();
    auto host = gCfgMgr["Redis"]["Host"];
    auto port = gCfgMgr["Redis"]["Port"];
    auto pwd = gCfgMgr["Redis"]["Passwd"];
    // 默认pool size 5，可根据需要调整
    con_pool_.reset(new RedisConPool(5, host.c_str(), atoi(port.c_str()), pwd.c_str()));
}

// 析构函数：清理资源
RedisMgr::~RedisMgr()
{
    Close();
}

// 获取键值（GET命令）
// 
// 参数：
//   - key: 键名
//   - value: 输出参数，键值
// 
// 返回值：
//   成功返回true，否则返回false
// 
// 实现逻辑：
//   1. 从连接池获取连接
//   2. 使用RAII机制自动管理连接
//   3. 执行GET命令
//   4. 处理返回结果（NIL表示键不存在）
//   5. 将结果写入value
bool RedisMgr::Get(const std::string& key, std::string& value)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::Get] getConnection returned nullptr for key=" << key << std::endl;
        return false;
    }
    // 使用RAII自动归还连接
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "GET %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "[RedisMgr::Get] redisCommand returned NULL for key=" << key << std::endl;
        return false;
    }

    // 检查返回类型
    if (reply->type == REDIS_REPLY_NIL) {
        // key不存在
        freeReplyObject(reply);
        std::cout << "[RedisMgr::Get] GET " << key << " -> (nil)\n";
        return false;
    }

    if (reply->type != REDIS_REPLY_STRING) {
        std::cout << "[RedisMgr::Get] GET " << key << " unexpected reply type=" << reply->type << std::endl;
        freeReplyObject(reply);
        return false;
    }

    // 复制字符串值
    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Succeed to execute command [ GET " << key << " ]\n";
    return true;
}

// 设置键值（SET命令）
// 
// 参数：
//   - key: 键名
//   - value: 键值
// 
// 返回值：
//   成功返回true，否则返回false
// 
// 实现逻辑：
//   1. 从连接池获取连接
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

    // 检查返回状态
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

// 发布消息到频道（PUBLISH channel message）
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

// 身份认证（AUTH命令）
// 
// 参数：
//   - password: Redis密码
// 
// 返回值：
//   认证成功返回true，否则返回false
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

// 从左端推入元素（LPUSH命令）
// 
// 参数：
//   - key: 列表键名
//   - value: 要推入的值
// 
// 返回值：
//   成功返回true，否则返回false
// 
// 说明：
//   LPUSH用于队列场景，从列表左端（头部）添加元素
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

    // LPUSH返回整数，表示操作后的列表长度
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// 从左端弹出元素（LPOP命令）
// 
// 参数：
//   - key: 列表键名
//   - value: 输出参数，弹出的值
// 
// 返回值：
//   成功返回true，否则返回false
// 
// 说明：
//   LPOP用于队列场景，从列表左端（头部）弹出元素
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

    // 检查返回类型
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

    // 复制字符串值
    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Execut command [ LPOP " << key << " ] success ! " << std::endl;
    return true;
}

// 从右端推入元素（RPUSH命令）
// 
// 参数：
//   - key: 列表键名
//   - value: 要推入的值
// 
// 返回值：
//   成功返回true，否则返回false
// 
// 说明：
//   RPUSH用于栈场景，从列表右端（尾部）添加元素
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

    // RPUSH返回整数，表示操作后的列表长度
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// 从右端弹出元素（RPOP命令）
// 
// 参数：
//   - key: 列表键名
//   - value: 输出参数，弹出的值
// 
// 返回值：
//   成功返回true，否则返回false
// 
// 说明：
//   RPOP用于栈场景，从列表右端（尾部）弹出元素
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

    // 检查返回类型
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

    // 复制字符串值
    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Execut command [ RPOP " << key << " ] success ! " << std::endl;
    return true;
}

// 设置哈希字段值（HSET命令 - 字符串版本）
// 
// 参数：
//   - key: 哈希键名
//   - hkey: 字段名
//   - value: 字段值
// 
// 返回值：
//   成功返回true，否则返回false
// 
// 说明：
//   HSET用于设置哈希中的字段值
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

    // HSET返回整数（新增的字段数）
    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// 设置哈希字段值（HSET命令 - 二进制版本）
// 
// 参数：
//   - key: 哈希键名
//   - hkey: 字段名
//   - hvalue: 字段值（二进制数据）
//   - hvaluelen: 数据长度
// 
// 返回值：
//   成功返回true，否则返回false
// 
// 说明：
//   使用redisCommandArgv支持二进制数据
//   适用于存储非文本数据（如图片、视频等）
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

// 删除哈希字段（HDEL命令）
// 
// 参数：
//   - key: 哈希键名
//   - field: 字段名
// 
// 返回值：
//   成功删除返回true，否则返回false
// 
// 实现逻辑：
//   1. 执行HDEL命令
//   2. 检查返回值（返回整数，表示删除的字段数）
//   3. 如果返回值>0，表示字段存在且被删除
bool RedisMgr::HDel(const std::string& key, const std::string& field)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HDel] getConnection returned nullptr for key=" << key << " field=" << field << std::endl;
        return false;
    }
    // RAII: 确保连接会被归还给连接池
    RedisConnectionGuard guard(con_pool_.get(), connect);

    // 执行 HDEL 命令
    redisReply* reply = (redisReply*)redisCommand(connect, "HDEL %s %s", key.c_str(), field.c_str());
    if (reply == nullptr) {
        std::cout << "Execut command [ HDEL " << key << " " << field << " ] failure (reply==NULL)!\n";
        return false;
    }

    // HDEL 返回整数（返回值表示已删除的字段数）
    bool ok = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        if (reply->integer > 0) {
            ok = true; // 字段被删除
        }
        else {
            ok = false; // 字段不存在或未删除
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


// 获取哈希字段值（HGET命令）
// 
// 参数：
//   - key: 哈希键名
//   - hkey: 字段名
// 
// 返回值：
//   字段值，不存在返回空字符串
// 
// 实现逻辑：
//   1. 使用redisCommandArgv执行HGET命令
//   2. 检查返回类型
//   3. 返回字段值
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

    // 检查返回类型
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

    std::string value(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Execut command [ HGet " << key << " " << hkey << " ] success ! " << std::endl;
    return value;
}

// 删除键（DEL命令）
// 
// 参数：
//   - key: 键名
// 
// 返回值：
//   成功返回true，否则返回false
// 
// 说明：
//   删除指定的键及其关联的值
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

    // DEL返回整数，表示删除的键数
    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ Del " << key << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ Del " << key << " ] failure ! " << std::endl;
    return false;
}

// 检查键是否存在（EXISTS命令）
// 
// 参数：
//   - key: 键名
// 
// 返回值：
//   存在返回true，否则返回false
// 
// 实现逻辑：
//   1. 执行EXISTS命令
//   2. 检查返回值（返回整数，>0表示存在）
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

    // EXISTS返回整数，>0表示键存在
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << " Found [ Key " << key << " ] exists ! " << std::endl;
        return true;
    }
    std::cout << " Not Found [ Key " << key << " ] ! " << std::endl;
    return false;
}

// 关闭连接池
// 
// 作用：
//   关闭Redis连接池，释放所有连接
// 
// 实现逻辑：
//   1. 调用连接池的Close方法
//   2. 重置连接池指针
// 
// 注意：
//   RedisConPool::Close() 应该释放所有 redisContext（redisFree），
//   如果还没有 RedisConPool 的实现，应该让 RedisConPool::Close/析构函数释放 ctx
void RedisMgr::Close()
{
    if (con_pool_) {
        con_pool_->Close();
        // 注意：RedisConPool::Close() 应该释放所有 redisContext（redisFree），
        // 如果还没有 RedisConPool 的实现，应该让 RedisConPool::Close/析构函数释放 ctx
        con_pool_.reset();
    }
}
