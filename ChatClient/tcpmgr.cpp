#include "tcpmgr.h"
#include<QJsonDocument>
#include"usermgr.h"
#include "localdb.h"
#include <thread>
#include <QThread>
#include <QJsonArray>

TcpMgr::TcpMgr():_host(""),_port(0),_b_recv_pending(false),_message_id(0),_message_len(0)
{
    QObject::connect(&_socket, &QTcpSocket::connected, [&]() {
        qDebug() << "Connected to server!";
        // 连接建立后发送消息
        emit sig_con_success(true);
    });

    // 读是“底层事件驱动”，可以在构造时绑定
    // 替换掉原来 readyRead 的 lambda

    int topeek = std::min(6, _buffer.size());
    QString hexStr;
    for (int i = 0; i < topeek; ++i) {
        hexStr += QString("%1 ").arg((unsigned char)_buffer.at(i), 2, 16, QChar('0'));
    }
    qDebug() << "[TcpMgr] buffer head hex:" << hexStr << " total_buffer=" << _buffer.size();
    QObject::connect(&_socket, &QTcpSocket::readyRead, [&]() {
        // 1) 先把 socket 中现有数据一次性读到 buffer（不要在循环里重复读）
        QByteArray newly = _socket.readAll();
        if (!newly.isEmpty()) {
            _buffer.append(newly);
            qDebug() << "[TcpMgr] readyRead: appended" << newly.size() << "bytes"
                     << " total_buffer=" << _buffer.size();
        } else {
            qDebug() << "[TcpMgr] readyRead: socket.readAll returned 0 bytes";
        }

        const int headerSize = 4; // 两个 quint16 (msgId, msgLen) 大端格式
        // 保护性检查：避免无限循环
        while (true) {
            // 如果我们正在等待剩余的 body（已经解析了 header），直接检查是否够数据
            if (_b_recv_pending) {
                if (_buffer.size() < _message_len) {
                    // 还不够 body，等待下一次 readyRead
                    qDebug() << "[TcpMgr] waiting for body: need=" << _message_len
                             << " have=" << _buffer.size();
                    return;
                }
                // 有足够 body，继续下面逻辑（使用已保存的 _message_id/_message_len）
            } else {
                // 还没有 header 数据，需要判断是否能读取 header
                if (_buffer.size() < headerSize) {
                    // 不够 header，等待更多字节
                    qDebug() << "[TcpMgr] not enough bytes for header, have=" << _buffer.size();
                    return;
                }

                // 从 buffer 的前 4 字节解析 header（大端 —— high byte first）
                // 使用 unsigned char 防止符号扩展问题
                const unsigned char b0 = static_cast<unsigned char>(_buffer[0]);
                const unsigned char b1 = static_cast<unsigned char>(_buffer[1]);
                const unsigned char b2 = static_cast<unsigned char>(_buffer[2]);
                const unsigned char b3 = static_cast<unsigned char>(_buffer[3]);

                quint16 msgId = static_cast<quint16>((b0 << 8) | b1);
                quint16 msgLen = static_cast<quint16>((b2 << 8) | b3);

                _message_id = msgId;
                _message_len = msgLen;

                // 移除 header
                _buffer.remove(0, headerSize);

                qDebug() << "[TcpMgr] parsed header msg_id=" << _message_id << " msg_len=" << _message_len;
            }

            // 防御：检查 message_len 是否合理（避免恶意或错误数据）
            if (_message_len == 0) {
                qDebug() << "[TcpMgr] warning: message_len == 0, skipping (msg_id=" << _message_id << ")";
                // 继续循环，看看 buffer 是否还有更多完整消息（或立即 return）
                // 这里选择继续，让循环尝试读取下一个 header if present
                _b_recv_pending = false;
                continue;
            }
            if (_message_len > MAX_LENGTH) {
                qDebug() << "[TcpMgr] error: message_len too large =" << _message_len
                         << " (max=" << MAX_LENGTH << "). Closing socket.";
                _socket.abort(); // 或者 disconnectFromHost/close，根据你的需求
                return;
            }

            // 判断 body 是否到齐
            if (_buffer.size() < _message_len) {
                // body 数据尚未完整，标记等待并返回
                _b_recv_pending = true;
                qDebug() << "[TcpMgr] need more body bytes, need=" << _message_len << " have=" << _buffer.size();
                return;
            }

            // 读取消息体（拷贝一份）
            QByteArray messageBody = _buffer.left(_message_len);
            // 从 buffer 中移除已读 body
            _buffer.remove(0, _message_len);
            // 解析完一条完整消息后，重置 pending 标记（下一次循环重新读取 header）
            _b_recv_pending = false;

            qDebug() << "[TcpMgr] receive body len=" << messageBody.size()
                     << " preview=" << QString::fromUtf8(messageBody).left(200);

            // 分发：先调用注册的 handler（若有）
            ReqId rid = static_cast<ReqId>(_message_id);
            if (_handlers.contains(rid)) {
                // 注意：你原来 handler 类型接受 (ReqId, int, QByteArray)
                // 如果原 handler 接受字符串，这里可以传 QString::fromUtf8(messageBody)
                _handlers[rid](rid, static_cast<int>(_message_len), messageBody);
            }

            // 记录 buffer 状态并准备 emit
            qDebug() << "[TcpMgr] after consume bufferSize=" << _buffer.size()
                     << " parsed id=" << _message_id << " len=" << _message_len;

            // 在发信号前做一个安全 preview（把换行替换，避免控制台换行干扰）
            QString preview = QString::fromUtf8(messageBody).left(200);
            preview.replace("\n", "\\n").replace("\r", "\\r");
            qDebug() << "[TcpMgr] about to emit sig_recv_pkg id=" << (int)_message_id
                     << " body_len=" << messageBody.size()
                     << " preview=" << preview;

            // 发射信号（如果你的信号签名仍然是 (ReqId, QString)）
            emit sig_recv_pkg(rid, QString::fromUtf8(messageBody));
            qDebug() << "[TcpMgr] emitted sig_recv_pkg id=" << (int)_message_id;

            // 循环回到 while(true)，尝试解析下一条消息（如果 buffer 还有数据）
            // 注意：在下一次 loop 里会根据 _b_recv_pending 决定是否需要 parse header
        } 
    });

    //5.15 之后版本
    QObject::connect(&_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), [&](QAbstractSocket::SocketError socketError) {
        Q_UNUSED(socketError)
        qDebug() << "Error:" << _socket.errorString();
    });

    // 处理连接断开
    QObject::connect(&_socket, &QTcpSocket::disconnected, [&]() {
        qDebug() << "Disconnected from server.";
    });

    QObject::connect(this, &TcpMgr::sig_send_data, this, &TcpMgr::slot_send_data);

    // 注册各消息ID的处理器
    initHandlers();
}

TcpMgr::~TcpMgr(){}

// 用于注册消息 ID 对应的回调函数
void TcpMgr::initHandlers()
{
    _handlers.insert(ID_CHAT_LOGIN_RSP, [this](ReqId id, int len, QByteArray data){
        Q_UNUSED(id);
        Q_UNUSED(len);
        qDebug() << "handle id is " << static_cast<int>(id) << " data is " << data;

        QJsonParseError jerr;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &jerr);
        if (jerr.error != QJsonParseError::NoError) {
            qDebug() << "Failed to create QJsonDocument:" << jerr.errorString();
            emit sig_login_failed(ErrorCodes::ERR_JSON);
            return;
        }

        if (!jsonDoc.isObject()) {
            qDebug() << "Login response is not JSON object";
            emit sig_login_failed(ErrorCodes::ERR_JSON);
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        // 支持 "error" 或 "err" 字段名
        int err = -1;
        if (jsonObj.contains("error")) err = jsonObj["error"].toInt();
        else if (jsonObj.contains("err")) err = jsonObj["err"].toInt();
        else {
            qDebug() << "Login response missing error/err field";
            emit sig_login_failed(ErrorCodes::ERR_JSON);
            return;
        }

        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Login Failed, err is " << err;
            emit sig_login_failed(static_cast<ErrorCodes>(err));
            return;
        }

        // 成功：把用户信息设置到 UserMgr（如果有）
        if (jsonObj.contains("uid")) UserMgr::GetInstance()->SetUid(jsonObj["uid"].toInt());
        if (jsonObj.contains("name")) UserMgr::GetInstance()->SetName(jsonObj["name"].toString());
        if (jsonObj.contains("token")) UserMgr::GetInstance()->SetToken(jsonObj["token"].toString());

        // 登录成功后，初始化DB并获取Cursor
        int uid = UserMgr::GetInstance()->GetUid();
        LocalDb::GetInstance()->Init(uid);
        long long cursor = LocalDb::GetInstance()->GetMaxMsgId();

        // 你可以在这里额外 emit 一个专门的信号，或通过 sig_recv_pkg 被 LoginDialog 捕获
        qDebug() << "Chat login handler processed success.";

        // 登录聊天服成功后，主动拉取离线消息（1023），带上 cursor
        QJsonObject offReq;
        offReq["uid"] = uid;
        offReq["max_msg_id"] = cursor;
        QString offJson = QString::fromUtf8(QJsonDocument(offReq).toJson(QJsonDocument::Compact));
        qDebug() << "[OfflineMsg][UI->TCP] send 1023 json=" << offJson;
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_GET_OFFLINE_MSG_REQ, offJson);
    });

    // _handlers.insert(ID_SEARCH_USER_RSP, [this](ReqId id, int len, QByteArray data){
    //     Q_UNUSED(len);
    //     qDebug()<< "handle id is "<< id << " data is " << data;
    //     // 将QByteArray转换为QJsonDocument
    //     QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

    //     // 检查转换是否成功
    //     if(jsonDoc.isNull()){
    //         qDebug() << "Failed to create QJsonDocument.";
    //         return;
    //     }

    //     QJsonObject jsonObj = jsonDoc.object();

    //     if(!jsonObj.contains("error")){
    //         int err = ErrorCodes::ERR_JSON;
    //         qDebug() << "Login Failed, err is Json Parse Err" << err ;
    //         emit sig_login_failed(err);
    //         return;
    //     }

    //     int err = jsonObj["error"].toInt();
    //     if(err != ErrorCodes::SUCCESS){
    //         qDebug() << "Login Failed, err is " << err ;
    //         emit sig_login_failed(err);
    //         return;
    //     }

    //     auto search_info = std::make_shared<SearchInfo>(jsonObj["uid"].toInt(),
    //                                                     jsonObj["name"].toString(), jsonObj["nick"].toString(),
    //                                                     jsonObj["desc"].toString(), jsonObj["sex"].toInt(), jsonObj["icon"].toString());

    //     emit sig_user_search(search_info);
    // });

    _handlers.insert(ID_NOTIFY_ADD_FRIEND_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        // 收到“好友申请”通知的原始数据
        qDebug() << "[FriendNotify] recv ID_NOTIFY_ADD_FRIEND_REQ id=" << static_cast<int>(id)
                 << " raw=" << QString::fromUtf8(data);
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            // [FriendNotify]
            qDebug() << "[FriendNotify] parse json failed for friend apply notify";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            // [FriendNotify]
            qDebug() << "[FriendNotify] friend apply notify missing 'error' field, err=" << err;

            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            // [FriendNotify]
            qDebug() << "[FriendNotify] friend apply notify error code=" << err;
            // emit sig_user_search(nullptr);
            return;
        }

        // 解析成功，发出信号，交由 UI 触发 HTTP 刷新
        qDebug() << "[FriendNotify] emit sig_friend_apply() -> will HTTP getFriendRequests() in UI";
        emit sig_friend_apply();
    });

    // 新增：好友回复结果通知处理器
    _handlers.insert(ID_NOTIFY_FRIEND_REPLY, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(id);
        Q_UNUSED(len);
        // 收到“好友回复结果”通知的原始数据
        qDebug() << "[FriendNotify] recv ID_NOTIFY_FRIEND_REPLY id=" << static_cast<int>(id)
                 << " raw=" << QString::fromUtf8(data);

        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull() || !jsonDoc.isObject()) {
            // [FriendNotify]
            qDebug() << "[FriendNotify] parse json failed for friend reply notify";
            return;
        }
        QJsonObject obj = jsonDoc.object();
        if (!obj.contains("error") || obj["error"].toInt() != ErrorCodes::SUCCESS) {
            // [FriendNotify]
            qDebug() << "[FriendNotify] friend reply notify missing/failed 'error' field, code="
                     << obj.value("error").toInt(-1);
            return;
        }

        // 期望字段：from_uid（申请发起方）、agree（bool）
        int from_uid = obj.value("from_uid").toInt();
        bool agree = obj.value("agree").toBool();
        // [FriendNotify]
        qDebug() << "[FriendNotify] emit sig_friend_reply(from_uid=" << from_uid
                 << ", agree=" << agree << ") -> will HTTP refresh in UI";
        emit sig_friend_reply(from_uid, agree);
    });

    // 文本聊天 ACK（1018）
    _handlers.insert(ID_TEXT_CHAT_MSG_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(id);
        Q_UNUSED(len);
        qDebug() << "[TextChat] recv ID_TEXT_CHAT_MSG_RSP raw=" << QString::fromUtf8(data);

        QJsonParseError jerr;
        QJsonDocument doc = QJsonDocument::fromJson(data, &jerr);
        if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
            qDebug() << "[TextChat] parse ack failed:" << jerr.errorString();
            return;
        }
        auto obj = doc.object();
        int err = obj.value("error").toInt(-1);
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "[TextChat] ack error code=" << err;
            return;
        }
        qDebug() << "[TextChat] ack success fromuid=" << obj.value("fromuid").toInt()
                 << " touid=" << obj.value("touid").toInt();
    });

    // 文本聊天下行通知（1019）
    _handlers.insert(ID_NOTIFY_TEXT_CHAT_MSG_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(id);
        Q_UNUSED(len);
        qDebug() << "[TextChat] recv ID_NOTIFY_TEXT_CHAT_MSG_REQ raw=" << QString::fromUtf8(data);

        QJsonParseError jerr;
        QJsonDocument doc = QJsonDocument::fromJson(data, &jerr);
        if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
            qDebug() << "[TextChat] parse notify failed:" << jerr.errorString();
            return;
        }
        auto obj = doc.object();
        int err = obj.value("error").toInt(-1);
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "[TextChat] notify error code=" << err;
            return;
        }
        int fromuid = obj.value("fromuid").toInt();
        int touid = obj.value("touid").toInt();
        auto arr = obj.value("text_array").toArray();
        qDebug() << "[TextChat] notify msgs count=" << arr.size() << " from=" << fromuid << " to=" << touid;
        for (const auto& v : arr) {
            auto o = v.toObject();
            const QString msgId = o.value("msgid").toString();
            const QString content = o.value("content").toString();
            qDebug() << "[TextChat] msg id=" << msgId << " content=" << content;
            // 向上层发送专用信号，便于 UI 直接显示
            emit sig_text_notify(fromuid, touid, msgId, content);
        }
        // 仍然保留通过 sig_recv_pkg 的整包派发（已在上层监听）
    });

}

void TcpMgr::slot_tcp_connect(ServerInfo si)
{
    qDebug()<< "receive tcp connect signal";
    // 尝试连接到服务器
    qDebug() << "Connecting to server...";
    _host = si.Host;
    _port = static_cast<uint16_t>(si.Port.toUInt());
    _socket.connectToHost(si.Host, _port);
}

void TcpMgr::slot_send_data(ReqId reqId, QString data)
{
    std::cout << "[TcpMgr::slot_send_data] reqId=" << reqId << " data=" << data.toStdString() << " thread=" << std::this_thread::get_id() << std::endl;
    uint16_t id = reqId;

    // 将字符串转换为UTF-8编码的字节数组
    QByteArray dataBytes = data.toUtf8();

    // 计算长度（使用网络字节序转换）
    quint16 len = static_cast<quint16>(dataBytes.size());

    // 创建一个QByteArray用于存储要发送的所有数据
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);

    // 设置数据流使用网络字节序
    out.setByteOrder(QDataStream::BigEndian);

    // 写入ID和长度,,,
    out << id << len;

    // 添加字符串数据
    block.append(dataBytes);

    // 发送数据
    _socket.write(block);
}
