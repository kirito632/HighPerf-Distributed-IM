#include "RedisMgr.h"
#include"const.h"
#include"ConfigMgr.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <hiredis/hiredis.h>

namespace {
    // 小的辅助函数：安全地把 reply->str 拷贝到 std::string（防止 reply==nullptr）
    static std::string replyToString(redisReply* reply) {
        if (!reply || reply->type != REDIS_REPLY_STRING) return {};
        return std::string(reply->str, reply->len);
    }
} // namespace

// RAII guard：确保取到的连接会在析构时归还到池里
class RedisConnectionGuard {
public:
    RedisConnectionGuard(RedisConPool* pool, redisContext* ctx) : pool_(pool), ctx_(ctx) {}
    ~RedisConnectionGuard() {
        if (pool_ && ctx_) {
            pool_->returnConnection(ctx_);
            // 不置 ctx_ 为 nullptr，因为析构后不会再用
        }
    }
    redisContext* get() const { return ctx_; }
    // 禁止复制
    RedisConnectionGuard(const RedisConnectionGuard&) = delete;
    RedisConnectionGuard& operator=(const RedisConnectionGuard&) = delete;
private:
    RedisConPool* pool_;
    redisContext* ctx_;
};

// RedisMgr 构造与析构
RedisMgr::RedisMgr()
{
    auto& gCfgMgr = ConfigMgr::Inst();
    auto host = gCfgMgr["Redis"]["Host"];
    auto port = gCfgMgr["Redis"]["Port"];
    auto pwd = gCfgMgr["Redis"]["Passwd"];
    // 默认 pool size 5（可按需调整）
    con_pool_.reset(new RedisConPool(5, host.c_str(), atoi(port.c_str()), pwd.c_str()));
}

RedisMgr::~RedisMgr()
{
    Close();
}

// Get
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

    if (reply->type == REDIS_REPLY_NIL) {
        // key not found
        freeReplyObject(reply);
        std::cout << "[RedisMgr::Get] GET " << key << " -> (nil)\n";
        return false;
    }

    if (reply->type != REDIS_REPLY_STRING) {
        std::cout << "[RedisMgr::Get] GET " << key << " unexpected reply type=" << reply->type << std::endl;
        freeReplyObject(reply);
        return false;
    }

    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Succeed to execute command [ GET " << key << " ]\n";
    return true;
}

// 原子地获取并清空list
bool RedisMgr::GetAllList(const std::string& key, std::vector<std::string>& values)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::GetAllList] getConnection nullptr for key=" << key << std::endl;
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    // 使用 MULTI/EXEC 保证原子性
    redisReply* multi_reply = (redisReply*)redisCommand(connect, "MULTI");
    if (multi_reply == nullptr || multi_reply->type == REDIS_REPLY_ERROR) {
        std::cout << "[RedisMgr::GetAllList] MULTI failed" << std::endl;
        if (multi_reply) freeReplyObject(multi_reply);
        return false;
    }
    freeReplyObject(multi_reply);

    redisReply* lrange_reply = (redisReply*)redisCommand(connect, "LRANGE %s 0 -1", key.c_str());
    redisReply* del_reply = (redisReply*)redisCommand(connect, "DEL %s", key.c_str());

    redisReply* exec_reply = (redisReply*)redisCommand(connect, "EXEC");
    if (exec_reply == nullptr || exec_reply->type != REDIS_REPLY_ARRAY || exec_reply->elements < 2) {
        std::cout << "[RedisMgr::GetAllList] EXEC failed" << std::endl;
        if (lrange_reply) freeReplyObject(lrange_reply);
        if (del_reply) freeReplyObject(del_reply);
        if (exec_reply) freeReplyObject(exec_reply);
        return false;
    }

    // 解析LRANGE的结果
    redisReply* lrange_result = exec_reply->element[0];
    if (lrange_result->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < lrange_result->elements; ++i) {
            values.push_back(std::string(lrange_result->element[i]->str, lrange_result->element[i]->len));
        }
    }

    freeReplyObject(exec_reply); // lrange_reply 和 del_reply 会被 exec_reply 一起释放
    return true;
}

// Set
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

// Auth
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

// LPush
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

    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// LPop
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

    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Execut command [ LPOP " << key << " ] success ! " << std::endl;
    return true;
}

// RPush
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

    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// RPop
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

    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Execut command [ RPOP " << key << " ] success ! " << std::endl;
    return true;
}

// HSet (string overload)
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

    bool ok = (reply->type == REDIS_REPLY_INTEGER); // HSET returns integer (number of fields added)
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// HSet (binary overload using redisCommandArgv)
bool RedisMgr::HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HSet(binary)] getConnection nullptr\n";
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

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

bool RedisMgr::HDel(const std::string& key, const std::string& field)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HDel] getConnection returned nullptr for key=" << key << " field=" << field << std::endl;
        return false;
    }
    // RAII: 确保连接会被归还到连接池
    RedisConnectionGuard guard(con_pool_.get(), connect);

    // 执行 HDEL 命令
    redisReply* reply = (redisReply*)redisCommand(connect, "HDEL %s %s", key.c_str(), field.c_str());
    if (reply == nullptr) {
        std::cout << "Execut command [ HDEL " << key << " " << field << " ] failure (reply==NULL)!\n";
        return false;
    }

    // HDEL 返回整数，表示被删除的字段数量
    bool ok = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        if (reply->integer > 0) {
            ok = true; // 有字段被删除
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


// HGet
std::string RedisMgr::HGet(const std::string& key, const std::string& hkey)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HGet] getConnection nullptr for key=" << key << std::endl;
        return "";
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

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

// Del
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

    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ Del " << key << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ Del " << key << " ] failure ! " << std::endl;
    return false;
}

// ExistsKey
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

    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << " Found [ Key " << key << " ] exists ! " << std::endl;
        return true;
    }
    std::cout << " Not Found [ Key " << key << " ] ! " << std::endl;
    return false;
}

void RedisMgr::Close()
{
    if (con_pool_) {
        con_pool_->Close();
        // 注意：RedisConPool::Close() 应该释放所有 redisContext（redisFree）
        // 如果你还没在 RedisConPool 中实现释放，请在 RedisConPool::Close/析构里释放 ctx
        con_pool_.reset();
    }
}

