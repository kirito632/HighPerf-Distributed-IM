#include "RedisMgr.h"
#include"const.h"
#include"ConfigMgr.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>

// hiredisͷ�ļ�������ʵ����Ŀ��include·����
#include <hiredis/hiredis.h>

namespace {
    // ������������ȫ�ؽ�redisReply->strת��Ϊstd::string����ֹreply==nullptr
    static std::string replyToString(redisReply* reply) {
        if (!reply || reply->type != REDIS_REPLY_STRING) return {};
        return std::string(reply->str, reply->len);
    }
} // namespace

// RedisConnectionGuard��RAII����ȷ����ȡ�������ӻ������������ʱ�黹
// 
// ���ã�
//   �Զ�����Redis���ӵ��������ڣ�ȷ��������ʹ�����黹�����ӳ�
// 
// ʹ�÷�ʽ��
//   auto guard = RedisConnectionGuard(pool, connection);
//   redisContext* ctx = guard.get();
class RedisConnectionGuard {
public:
    RedisConnectionGuard(RedisConPool* pool, redisContext* ctx) : pool_(pool), ctx_(ctx) {}

    // �����������Զ��黹���ӵ����ӳ�
    ~RedisConnectionGuard() {
        if (pool_ && ctx_) {
            pool_->returnConnection(ctx_);
            // ��ctx_����Ϊnullptr����Ϊ�����ѹ黹
        }
    }

    // ��ȡ���ӵ�ԭʼָ��
    redisContext* get() const { return ctx_; }

    // ��ֹ�������ƶ����壩
    RedisConnectionGuard(const RedisConnectionGuard&) = delete;
    RedisConnectionGuard& operator=(const RedisConnectionGuard&) = delete;
private:
    RedisConPool* pool_;      // ���ӳ�ָ��
    redisContext* ctx_;       // Redis����ָ��
};

// ���캯������ʼ��Redis������
// 
// ���ã�
//   �������ļ��ж�ȡRedis������Ϣ���������ӳ�
// 
// ʵ���߼���
//   1. �����ù�������ȡRedis������Ϣ���������˿ڡ����룩
//   2. ����Redis���ӳأ�Ĭ��5�����ӣ�
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

// ����������������Դ
RedisMgr::~RedisMgr()
{
    Close();
}

// ��ȡ��ֵ��GET���
// 
// ������
//   - key: ����
//   - value: �����������ֵ
// 
// ����ֵ��
//   �ɹ�����true�����򷵻�false
// 
// ʵ���߼���
//   1. �����ӳػ�ȡ����
//   2. ʹ��RAII�����Զ���������
//   3. ִ��GET����
//   4. �������ؽ����NIL��ʾ�������ڣ�
//   5. �����д��value
bool RedisMgr::Get(const std::string& key, std::string& value)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::Get] getConnection returned nullptr for key=" << key << std::endl;
        return false;
    }
    // ʹ��RAII�Զ��黹����
    RedisConnectionGuard guard(con_pool_.get(), connect);

    redisReply* reply = (redisReply*)redisCommand(connect, "GET %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "[RedisMgr::Get] redisCommand returned NULL for key=" << key << std::endl;
        return false;
    }

    // ��鷵������
    if (reply->type == REDIS_REPLY_NIL) {
        // key������
        freeReplyObject(reply);
        std::cout << "[RedisMgr::Get] GET " << key << " -> (nil)\n";
        return false;
    }

    if (reply->type != REDIS_REPLY_STRING) {
        std::cout << "[RedisMgr::Get] GET " << key << " unexpected reply type=" << reply->type << std::endl;
        freeReplyObject(reply);
        return false;
    }

    // �����ַ���ֵ
    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Succeed to execute command [ GET " << key << " ]\n";
    return true;
}

// ���ü�ֵ��SET���
// 
// ������
//   - key: ����
//   - value: ��ֵ
// 
// ����ֵ��
//   �ɹ�����true�����򷵻�false
// 
// ʵ���߼���
//   1. �����ӳػ�ȡ����
//   2. ִ��SET����
//   3. ��鷵��ֵ�Ƿ�Ϊ"OK"
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

    // ��鷵��״̬
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

// ������Ϣ��Ƶ����PUBLISH channel message��
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

// ������֤��AUTH���
// 
// ������
//   - password: Redis����
// 
// ����ֵ��
//   ��֤�ɹ�����true�����򷵻�false
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

    if (ok) std::cout << "��֤�ɹ�" << std::endl;
    else std::cout << "��֤ʧ��" << std::endl;
    return ok;
}

// ���������Ԫ�أ�LPUSH���
// 
// ������
//   - key: �б�����
//   - value: Ҫ�����ֵ
// 
// ����ֵ��
//   �ɹ�����true�����򷵻�false
// 
// ˵����
//   LPUSH���ڶ��г��������б���ˣ�ͷ��������Ԫ��
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

    // LPUSH������������ʾ��������б�����
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// ����˵���Ԫ�أ�LPOP���
// 
// ������
//   - key: �б�����
//   - value: ���������������ֵ
// 
// ����ֵ��
//   �ɹ�����true�����򷵻�false
// 
// ˵����
//   LPOP���ڶ��г��������б���ˣ�ͷ��������Ԫ��
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

    // ��鷵������
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

    // �����ַ���ֵ
    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Execut command [ LPOP " << key << " ] success ! " << std::endl;
    return true;
}

// ���Ҷ�����Ԫ�أ�RPUSH���
// 
// ������
//   - key: �б�����
//   - value: Ҫ�����ֵ
// 
// ����ֵ��
//   �ɹ�����true�����򷵻�false
// 
// ˵����
//   RPUSH����ջ���������б��Ҷˣ�β��������Ԫ��
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

    // RPUSH������������ʾ��������б�����
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// ���Ҷ˵���Ԫ�أ�RPOP���
// 
// ������
//   - key: �б�����
//   - value: ���������������ֵ
// 
// ����ֵ��
//   �ɹ�����true�����򷵻�false
// 
// ˵����
//   RPOP����ջ���������б��Ҷˣ�β��������Ԫ��
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

    // ��鷵������
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

    // �����ַ���ֵ
    value.assign(reply->str, reply->len);
    freeReplyObject(reply);
    std::cout << "Execut command [ RPOP " << key << " ] success ! " << std::endl;
    return true;
}

// ���ù�ϣ�ֶ�ֵ��HSET���� - �ַ����汾��
// 
// ������
//   - key: ��ϣ����
//   - hkey: �ֶ���
//   - value: �ֶ�ֵ
// 
// ����ֵ��
//   �ɹ�����true�����򷵻�false
// 
// ˵����
//   HSET�������ù�ϣ�е��ֶ�ֵ
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

    // HSET�����������������ֶ�����
    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] failure ! " << std::endl;
    return false;
}

// ���ù�ϣ�ֶ�ֵ��HSET���� - �����ư汾��
// 
// ������
//   - key: ��ϣ����
//   - hkey: �ֶ���
//   - hvalue: �ֶ�ֵ�����������ݣ�
//   - hvaluelen: ���ݳ���
// 
// ����ֵ��
//   �ɹ�����true�����򷵻�false
// 
// ˵����
//   ʹ��redisCommandArgv֧�ֶ���������
//   �����ڴ洢���ı����ݣ���ͼƬ����Ƶ�ȣ�
bool RedisMgr::HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HSet(binary)] getConnection nullptr\n";
        return false;
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    // ׼��redisCommandArgv�Ĳ���
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

// ɾ����ϣ�ֶΣ�HDEL���
// 
// ������
//   - key: ��ϣ����
//   - field: �ֶ���
// 
// ����ֵ��
//   �ɹ�ɾ������true�����򷵻�false
// 
// ʵ���߼���
//   1. ִ��HDEL����
//   2. ��鷵��ֵ��������������ʾɾ�����ֶ�����
//   3. �������ֵ>0����ʾ�ֶδ����ұ�ɾ��
bool RedisMgr::HDel(const std::string& key, const std::string& field)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HDel] getConnection returned nullptr for key=" << key << " field=" << field << std::endl;
        return false;
    }
    // RAII: ȷ�����ӻᱻ�黹�����ӳ�
    RedisConnectionGuard guard(con_pool_.get(), connect);

    // ִ�� HDEL ����
    redisReply* reply = (redisReply*)redisCommand(connect, "HDEL %s %s", key.c_str(), field.c_str());
    if (reply == nullptr) {
        std::cout << "Execut command [ HDEL " << key << " " << field << " ] failure (reply==NULL)!\n";
        return false;
    }

    // HDEL ��������������ֵ��ʾ��ɾ�����ֶ�����
    bool ok = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        if (reply->integer > 0) {
            ok = true; // �ֶα�ɾ��
        }
        else {
            ok = false; // �ֶβ����ڻ�δɾ��
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


// ��ȡ��ϣ�ֶ�ֵ��HGET���
// 
// ������
//   - key: ��ϣ����
//   - hkey: �ֶ���
// 
// ����ֵ��
//   �ֶ�ֵ�������ڷ��ؿ��ַ���
// 
// ʵ���߼���
//   1. ʹ��redisCommandArgvִ��HGET����
//   2. ��鷵������
//   3. �����ֶ�ֵ
std::string RedisMgr::HGet(const std::string& key, const std::string& hkey)
{
    auto connect = con_pool_->getConnection();
    if (connect == nullptr) {
        std::cout << "[RedisMgr::HGet] getConnection nullptr for key=" << key << std::endl;
        return "";
    }
    RedisConnectionGuard guard(con_pool_.get(), connect);

    // ׼��redisCommandArgv�Ĳ���
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

    // ��鷵������
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

// ɾ������DEL���
// 
// ������
//   - key: ����
// 
// ����ֵ��
//   �ɹ�����true�����򷵻�false
// 
// ˵����
//   ɾ��ָ���ļ����������ֵ
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

    // DEL������������ʾɾ���ļ���
    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);

    if (ok) {
        std::cout << "Execut command [ Del " << key << " ] success ! " << std::endl;
        return true;
    }
    std::cout << "Execut command [ Del " << key << " ] failure ! " << std::endl;
    return false;
}

// �����Ƿ���ڣ�EXISTS���
// 
// ������
//   - key: ����
// 
// ����ֵ��
//   ���ڷ���true�����򷵻�false
// 
// ʵ���߼���
//   1. ִ��EXISTS����
//   2. ��鷵��ֵ������������>0��ʾ���ڣ�
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

    // EXISTS����������>0��ʾ������
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);

    if (ok) {
        std::cout << " Found [ Key " << key << " ] exists ! " << std::endl;
        return true;
    }
    std::cout << " Not Found [ Key " << key << " ] ! " << std::endl;
    return false;
}

// �ر����ӳ�
// 
// ���ã�
//   �ر�Redis���ӳأ��ͷ���������
// 
// ʵ���߼���
//   1. �������ӳص�Close����
//   2. �������ӳ�ָ��
// 
// ע�⣺
//   RedisConPool::Close() Ӧ���ͷ����� redisContext��redisFree����
//   �����û�� RedisConPool ��ʵ�֣�Ӧ���� RedisConPool::Close/���������ͷ� ctx
void RedisMgr::Close()
{
    if (con_pool_) {
        con_pool_->Close();
        // ע�⣺RedisConPool::Close() Ӧ���ͷ����� redisContext��redisFree����
        // �����û�� RedisConPool ��ʵ�֣�Ӧ���� RedisConPool::Close/���������ͷ� ctx
        con_pool_.reset();
    }
}
