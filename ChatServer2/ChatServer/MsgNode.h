#pragma once
#include <string>
#include "const.h"
#include <iostream>
#include <boost/asio.hpp>

// 定义最大消息长度
#define MAX_LENGTH 1024 * 2

// 定义消息头部总长度（4字节）
#define HEAD_TOTAL_LEN 4

// 定义消息ID长度（2字节）
#define HEAD_ID_LEN 2

// 定义消息数据长度字段长度（2字节）
#define HEAD_DATA_LEN 2

// 定义最大接收队列大小
#define MAX_RECVQUE 10000
// 定义最大发送队列大小
#define MAX_SENDQUE 1000

using boost::asio::ip::tcp;

// 前向声明
class LogicSystem;
class CSession;

// 消息节点类型定义：用于处理消息的回调函数
// 参数：
//   - session: CSession对象
//   - msg_id: 消息ID
//   - msg_data: 消息数据
typedef std::function<void(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data)> FunCallBack;

// MsgNode类：消息节点的基类
// 
// 作用：
//   提供消息存储的基本结构，包含消息数据和数据长度
// 
// 实现逻辑：
//   1. 使用动态分配的字符数组存储消息数据
//   2. 跟踪当前已使用的长度和总长度
//   3. 提供清空消息的方法
class MsgNode
{
public:
    // 构造函数：创建消息节点
    // 参数：
    //   - max_len: 消息最大长度
    MsgNode(short max_len) :_total_len(max_len), _cur_len(0) {
        // 分配内存并初始化为0（末尾添加\0确保安全性）
        _data = new char[_total_len + 1]();
        _data[_total_len] = '\0';
    }

    // 析构函数：释放内存
    ~MsgNode() {
        //std::cout << "destruct MsgNode" << std::endl;
        delete[] _data;
    }

    // 清空消息内容
    void Clear() {
        ::memset(_data, 0, _total_len);
        _cur_len = 0;
    }

    short _cur_len;      // 当前使用的长度
    short _total_len;    // 总长度
    char* _data;         // 消息数据指针
};

// RecvNode类：接收消息节点
// 
// 作用：
//   表示从网络接收到的消息
// 
// 特点：
//   继承自MsgNode，添加了消息ID字段
//   用于CSession接收消息时使用
class RecvNode :public MsgNode {
    friend class LogicSystem;  // 允许LogicSystem访问私有成员
public:
    // 构造函数：创建接收消息节点
    // 参数：
    //   - max_len: 消息最大长度
    //   - msg_id: 消息ID
    RecvNode(short max_len, short msg_id);
private:
    short _msg_id;       // 消息ID
};

// SendNode类：发送消息节点
// 
// 作用：
//   表示要发送到网络的消息
// 
// 特点：
//   继承自MsgNode，添加了消息ID字段
//   消息格式：[消息ID(2字节)][数据长度(2字节)][数据内容]
class SendNode :public MsgNode {
public:
    // 构造函数：创建发送消息节点
    // 参数：
    //   - msg: 消息数据指针
    //   - max_len: 消息数据长度
    //   - msg_id: 消息ID
    SendNode(const char* msg, short max_len, short msg_id);
private:
    short _msg_id;       // 消息ID
};


