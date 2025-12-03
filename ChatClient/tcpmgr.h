#ifndef TCPMGR_H
#define TCPMGR_H
#include<QTcpSocket>
#include"QObjectSingleton.h"
#include"global.h"
#include<functional>
#include<QObject>
#include<QMap>
// 为 std::shared_ptr 添加头文件
#include <memory>

// 调整：取消对 AddFriendApply 的依赖（不再需要前向声明）

constexpr int MAX_LENGTH = 4096; // 4KB：单条消息最大长度

class TcpMgr : public QObject, public QObjectSingleton<TcpMgr>, public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT

    friend class QObjectSingleton<TcpMgr>;
public:
    ~TcpMgr();

private:
    TcpMgr();
    // ------------------- 成员变量 -------------------
    QTcpSocket _socket;
    // TCP 套接字对象，用于和 ChatServer 建立长连接（收发消息）

    QString _host;
    // 服务器地址（IP 或域名）

    uint16_t _port;
    // 服务器端口号

    QByteArray _buffer;
    // 接收缓冲区，用来存储从服务器接收到的字节流（可能存在粘包/半包问题，需要缓存）

    bool _b_recv_pending;
    // 标志位：是否正在等待接收完整的一条消息（true 表示有未完整接收的数据）

    quint16 _message_id;
    // 当前正在处理的消息 ID（用来区分消息类型，比如登录回应/聊天消息）

    quint16 _message_len;
    // 当前消息的数据长度，用于判断一条完整消息是否已经接收完毕

    QMap<ReqId, std::function<void(ReqId id, int len, QByteArray data)>> _handlers;
    // 消息处理器表（映射表）：
    // key = 请求 ID（ReqId），value = 对应的回调函数
    // 当接收到某个请求 ID 的数据时，就调用对应的 handler 来处理逻辑

    void initHandlers();
    // 初始化函数，用于注册不同 ReqId 对应的处理回调函数
    // 相当于“消息路由表”的初始化

    // ------------------- Qt 槽函数 -------------------
public slots:
    void slot_tcp_connect(ServerInfo);
    // 槽函数：尝试连接到服务器（根据 ServerInfo 提供的 host/port）

    void slot_send_data(ReqId reqId, QString data);
    // 槽函数：向服务器发送数据
    // 参数 reqId 表示请求类型，data 是要发送的消息内容

    // ------------------- Qt 信号 -------------------
signals:
    void sig_con_success(bool bsuccess);
    // 信号：连接结果通知
    // bsuccess = true 表示连接成功，false 表示失败

    void sig_send_data(ReqId reqId, QString data);
    // 信号：通知有数据需要发送（可以和槽函数 slot_send_data 绑定）

    void sig_login_failed(int);
    // 信号：登录失败通知
    // 参数是错误码（比如账号不存在/密码错误等）

    // 通知上层：收到解析过的包（id + body）
    void sig_recv_pkg(ReqId id, const QString &body);

    // 调整：收到“好友申请”TCP通知（由 ChatServer 推送）→ 改为无参信号
    void sig_friend_apply();

    // 新增：收到“好友回复结果”TCP通知（由 ChatServer 推送）
    void sig_friend_reply(int fromUid, bool agree);

    // 文本聊天下行（1019）通知：fromuid -> touid 的一条文本（仅传首条内容，列表由上层自行遍历需要的话）
    // 增加 msgId 参数，用于去重和ACK
    void sig_text_notify(int fromUid, int toUid, const QString &msgId, const QString &content);
};

#endif // TCPMGR_H
