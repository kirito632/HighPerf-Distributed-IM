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
#include <cppconn/exception.h>
#include<csignal>
class MysqlPool;

// ���ڹ���ȫ��Ψһ��MySQL���ӳ�
// ʹ�õ���ģʽȷ����������ֻ��һ��MySQL���ӳ�ʵ��
using MySqlPoolSingleton = Singleton<MySqlPool>;

// ����Redis���Ӻͻ�������
// 
// ���ã�
//   ����Redis���ݿ�����ӡ���֤�����������ȹ���
// 
// ʵ���߼���
//   1. ���ӵ�����Redis���������˿�6380��
//   2. ����������֤������: 123456��
//   3. ִ�л���������SET��GET��STRLEN����
//   4. �ͷ�������Դ
// 
// ע�⣺
//   ��Ҫ����Redis���������ܽ�������
//   RedisĬ�϶˿�Ϊ6379������ʹ�õ���6380
void TestRedis() {
    // ����Redis��������Ĭ�϶˿ڿ��ܲ�ͬ����Ҫ������ã�
    // RedisĬ�ϼ����˿�Ϊ6387�������������ļ����޸�
    redisContext* c = redisConnect("127.0.0.1", 6380);
    if (c->err)
    {
        printf("Connect to redisServer faile:%s\n", c->errstr);
        redisFree(c);
        return;
    }
    printf("Connect to redisServer Success\n");

    // ����Redis������֤
    std::string redis_password = "123456";
    redisReply* r = (redisReply*)redisCommand(c, "AUTH %s", redis_password.c_str());
    if (r->type == REDIS_REPLY_ERROR) {
        printf("Redis��֤ʧ�ܣ�\n");
    }
    else {
        printf("Redis��֤�ɹ���\n");
    }

    // ����SET���ΪRedis����key-value
    const char* command1 = "set stest1 value1";

    // ִ��redis������
    r = (redisReply*)redisCommand(c, command1);

    // �������NULL��˵��ִ��ʧ��
    if (NULL == r)
    {
        printf("Execut command1 failure\n");
        redisFree(c);
        return;
    }

    // ���ִ��ʧ�����ͷ�����
    if (!(r->type == REDIS_REPLY_STATUS && (strcmp(r->str, "OK") == 0 || strcmp(r->str, "ok") == 0)))
    {
        printf("Failed to execute command[%s]\n", command1);
        freeReplyObject(r);
        redisFree(c);
        return;
    }

    // ִ�гɹ����ͷ�redisCommandִ�к󷵻ص�redisReply��ռ�õ��ڴ�
    freeReplyObject(r);
    printf("Succeed to execute command[%s]\n", command1);

    // ����STRLEN�����ȡ�ַ�������
    const char* command2 = "strlen stest1";
    r = (redisReply*)redisCommand(c, command2);

    // ����������Ͳ����������ͷ�����
    if (r->type != REDIS_REPLY_INTEGER)
    {
        printf("Failed to execute command[%s]\n", command2);
        freeReplyObject(r);
        redisFree(c);
        return;
    }

    // ��ȡ�ַ�������
    int length = r->integer;
    freeReplyObject(r);
    printf("The length of 'stest1' is %d.\n", length);
    printf("Succeed to execute command[%s]\n", command2);

    // ����GET�����ȡredis��ֵ����Ϣ
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

    // ���Ի�ȡ�����ڵ�key
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

    // �ͷ�������Դ
    redisFree(c);

}

// ����Redis����������
// 
// ���ã�
//   ����RedisMgr��װ�ĸ߲�Redis�����ӿ�
// 
// ʵ���߼���
//   �������¹��ܣ�
//   1. Set/Get - �򵥵�key-value����
//   2. HSet/HGet - Hash������
//   3. ExistsKey - ���key�Ƿ����
//   4. Del - ɾ��key
//   5. LPush/RPop/LPop - �б����������У�
// 
// ע�⣺
//   ʹ�ö���ȷ��ÿ���������ɹ�ִ��
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

// ���������������
// 
// ���ã�
//   1. ��ʼ��MySQL���ӳ�
//   2. ����HTTP��������GateServer��
// 
// ʵ���߼���
//   1. �������ļ���ȡMySQL������Ϣ
//   2. ��ʼ��MySQL���ӳأ�10�����ӣ�
//   3. �������ļ���ȡGateServer�˿�
//   4. ����io_context���źŴ�����������SIGINT��SIGTERM��
//   5. ����HTTP������������
//   6. �����¼�ѭ��
// 
// ִ�����̣�
//   main() -> ��ȡ���� -> ��ʼ��MySQL�� -> ����HTTP������ -> �����¼�ѭ��
int main()
{
    // ���Ժ�������ע�ͣ�����Ҫ����ȡ��ע�ͽ��в��ԣ�
    //TestRedis();
    //TestRedisMgr();

    // ��ȡ���ù���������
    auto& gCfgMgr = ConfigMgr::Inst();

    // �������ļ���ȡMySQL������Ϣ
    std::string host = gCfgMgr["Mysql"]["Host"];
    std::string port = gCfgMgr["Mysql"]["Port"];
    std::string user = gCfgMgr["Mysql"]["User"];
    std::string passwd = gCfgMgr["Mysql"]["Passwd"];
    std::string schema = gCfgMgr["Mysql"]["Schema"];

    std::cout << "[main] MySQL config host=" << host << " port=" << port
        << " user=" << user << " schema=" << schema << std::endl;

    // ����MySQL����URL
    std::string url = "tcp://" + host + ":" + port;

    // ��ȡMySQL���ӳص�������ʼ��
    auto pool = MySqlPoolSingleton::GetInstance();
    try {
        // ��ʼ�����ӳأ�������URL, �û���, ����, ���ݿ���, ������
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

    // �������ļ���ȡGateServer�˿�
    std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
    unsigned short gate_port = atoi(gate_port_str.c_str());

    try {
        // ����HTTP�������˿ڣ�8080��
        unsigned short port = static_cast<unsigned short>(gate_port);

        // ����IO�����ģ�1���̣߳�
        net::io_context ioc{ 1 };

        // �����źż�������������SIGINT��Ctrl+C����SIGTERM�ź�
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

        // �첽�ȴ��źţ����յ��ź�ʱֹͣIO������
        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number) {
            if (error) {
                return;
            }
            // ֹͣ�¼�ѭ�������ŵعرշ�����
            ioc.stop();
            });

        // ����HTTP������������
        // CServer���Զ���ʼ���ܿͻ�������
        std::make_shared<CServer>(ioc, port)->Start();

        // �����¼�ѭ��������ֱ������ioc.stop()��
        ioc.run();
    }
    catch (std::exception& e) {
        std::cerr << "exception is " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}


