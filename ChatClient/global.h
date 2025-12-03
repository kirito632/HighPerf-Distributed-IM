#ifndef GLOBAL_H
#define GLOBAL_H

#include<QWidget>
#include<functional>
#include<QRegularExpression>
#include"QStyle"
#include<memory>
#include<iostream>
#include<mutex>
#include<QByteArray>
#include<QNetworkReply>
#include<QJsonObject>
#include<QDir>
#include<QSettings>
#include <QString>

#define UI_DEBUG 1

enum ReqId{
    ID_GET_VERTIFY_CODE = 1001, // 获取验证码
    ID_REG_USER = 1002, // 注册用户
    ID_FORGET_PASSWORD  = 1003,  // 找回密码
    ID_LOGIN_USER = 1004,  // 用户登录
    ID_CHAT_LOGIN = 1005,  // 登录聊天服务器
    ID_CHAT_LOGIN_RSP = 1006,  // 登录聊天服务器回包

    // ====== 新增联系人相关 ======
    ID_GET_CONTACTS = 1010,     // 请求或返回联系人列表（Body 为 JSON 数组）
    ID_CONTACT_UPDATE = 1011,   // 单条联系人更新（服务器主动推送，Body 为单个 JSON 对象）
    ID_CHAT_NEW_MSG = 1012,
    ID_ADD_FRIEND = 1013,
    ID_FRIEND_REQUESTS = 1014,  // 新好友申请
    ID_SEARCH_USER = 1015,      // 搜索用户
    // ID_ACCEPT_FRIEND = 1016,    // 接受好友
    // ID_REJECT_FRIEND = 1017,    // 拒绝好友

    // ====== 文本聊天（与服务器 const.h 对齐） ======
    ID_TEXT_CHAT_MSG_REQ = 1017,
    ID_TEXT_CHAT_MSG_RSP = 1018,
    ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019,

    ID_SEARCH_USER_RSP = 1020,
    ID_NOTIFY_ADD_FRIEND_REQ = 1021,
    // 新增：好友回复结果通知（TCP 下发给发起方）
    ID_NOTIFY_FRIEND_REPLY = 1022,
    ID_GET_OFFLINE_MSG_REQ = 1023,
    ID_NOTIFY_TEXT_CHAT_MSG_RSP = 1024 // 客户端确认收到通知 (ACK)
};

enum Modules{
    REGISTERMOD = 0,
    FORGETMOD   = 1,
    LOGINMOD = 2
};

enum ErrorCodes{
    SUCCESS = 0,
    ERR_JSON = 1, // json解析失败
    ERR_NETWORK = 2 // 网a2668348774@qq.com络错误
};

struct ServerInfo{
    QString Host;
    QString Port;
    QString Token;
    int Uid;
};

extern QString gate_url_prefix;

class global
{
public:
    global();
};

Q_DECLARE_METATYPE(ReqId)
Q_DECLARE_METATYPE(ErrorCodes)
Q_DECLARE_METATYPE(ServerInfo)


#endif // GLOBAL_H
