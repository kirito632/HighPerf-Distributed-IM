#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include<memory>
#include<iostream>
#include"Singleton.h"
#include<functional>
#include<map>
#include<unordered_map>
#include<json/json.h>
#include<json/value.h>
#include<json/reader.h>
#include<boost/filesystem.hpp>
#include<boost/property_tree/ptree.hpp>
#include<boost/property_tree/ini_parser.hpp>
#include <hiredis/hiredis.h> // �޸�Ϊ��ȷ��·�� 
#include<cassert>
#include<atomic>
#include<queue>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

enum ErrorCodes {
    Success = 0,
    Error_Json = 1001,           // json��������
    RPCFailed = 1002,           // RPC�������
    VerifyExpired = 1003,       // ��֤���ѹ���
    VerifyCodeErr = 1004,         // ��֤�����
    UserExist = 1005,       // �û��Ѵ���
    PasswdErr = 1006,
    EmailNotMatch = 1007,
    PasswdUpFailed = 1008,
    PasswdInvalid = 1009,
    RPCGetFailed = 1010,
    UidInvalid = 1011,
    TokenInvalid = 1012,
    RecipientOffline = 1020       // ��Ϣ���շ�����
};

enum MSG_IDS {
    MSG_CHAT_LOGIN = 1005,
    MSG_CHAT_LOGIN_RSP = 1006,
    ID_SEARCH_USER_REQ = 1007,
    ID_SEARCH_USER_RSP = 1008,
    ID_ADD_FRIEND_REQ = 1009,
    ID_ADD_FRIEND_RSP = 1010,
    ID_NOTIFY_ADD_FRIEND = 1011,
    ID_AUTH_FRIEND_REQ = 1013,
    ID_AUTH_FRIEND_RSP = 1014,
    ID_NOTIFY_AUTH_FRIEND = 1015,
    ID_TEXT_CHAT_MSG_REQ = 1017,
    ID_TEXT_CHAT_MSG_RSP = 1018,
    ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019,
    ID_NOTIFY_TEXT_CHAT_MSG_RSP = 1024,
    ID_GET_OFFLINE_MSG_REQ = 1023,
    // Align with Qt client
    ID_NOTIFY_ADD_FRIEND_REQ = 1021,
    ID_NOTIFY_FRIEND_REPLY = 1022
};

class Defer {
public:
    // ����һ��lambda����ʽ����ָ��
    Defer(std::function<void()> func) :func_(func) {}

    ~Defer() {
        func_();
    }

private:
    std::function<void()> func_;
};

#define CODEPREFIX "code_"



#define USERIPPREFIX "uip_"
#define USERTOKENPREFIX "utoken_"
#define IPCOUNTPREFIX "ipcount_"
#define USER_BASE_INFO "ubaseinfo_"
#define LOGIN_COUNT "logincount"
#define NAME_INFO "nameinfo_"
#define OFFLINE_MSG_PREFIX "offline_msg_"