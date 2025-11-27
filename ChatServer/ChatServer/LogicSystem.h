#pragma once
#include<queue>
#include"Singleton.h"
#include<thread>
#include<map>
#include<functional>
#include"const.h"
#include"data.h"
#include<memory>
#include<string>
#include"StatusGrpcClient.h"
#include "CSession.h"

// 前向声明
class CSession;
class LogicNode;

// 消息节点类型定义：用于处理消息的回调函数
// 参数：
//   - session: CSession对象
//   - msg_id: 消息ID
//   - msg_data: 消息数据
typedef std::function<void(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data)> FunCallBack;

// LogicSystem类：逻辑系统，处理业务逻辑
// 
// 作用：
//   1. 接收网络层投递的消息
//   2. 在独立的工作线程中处理消息
//   3. 根据消息ID调用对应的处理函数
// 
// 设计模式：
//   单例模式 - 确保全局唯一的逻辑系统实例
//   生产者-消费者模式 - 网络层生产消息，工作线程消费消息
// 
// 主要功能：
//   - 注册和调用消息处理回调函数
//   - 异步处理消息（使用队列和条件变量）
//   - 用户登录验证
class LogicSystem : public Singleton<LogicSystem>
{
    friend class Singleton<LogicSystem>;  // 允许Singleton访问私有构造函数
public:
    // 析构函数：清理资源
    ~LogicSystem();

    // 投递消息到队列
    // 参数：
    //   - msg: 消息节点指针
    // 作用：
    //   将消息加入到处理队列，通知工作线程处理
    void PostMsgToQue(std::shared_ptr<LogicNode> msg);

private:
    // 私有构造函数：初始化逻辑系统
    LogicSystem();

    // 注册回调函数
    void RegisterCallBacks();

    // 处理消息（在工作线程中运行）
    void DealMsg();

    // 登录处理函数
    // 参数：
    //   - session: 会话对象
    //   - msg_id: 消息ID
    //   - msg_data: 消息数据（JSON格式）
    void LoginHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);


    void DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);

    void GetOfflineMsgHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
    void OfflineMsgAckHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);

    // 获取用户基础信息
    // 参数：
    //   - base_key: 基础键名
    //   - uid: 用户ID
    //   - userinfo: 输出参数，用户信息
    // 返回值：
    //   成功返回true，否则返回false
    bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);

    std::queue<std::shared_ptr<LogicNode>> _msg_que;  // 消息队列
    std::mutex _mutex;                                 // 互斥锁
    std::condition_variable _consume;                 // 条件变量（用于唤醒工作线程）
    std::thread _worker_thread;                       // 工作线程
    bool _b_stop;                                     // 停止标志
    std::map<short, FunCallBack> _fun_callbacks;     // 回调函数映射表（消息ID -> 处理函数）
};


