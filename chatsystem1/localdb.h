#ifndef LOCALDB_H
#define LOCALDB_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <vector>
#include <memory>
#include "singleton.h"

struct MessageInfo {
    long long msg_id;    // 消息ID
    int from_uid;        // 发送者ID
    int to_uid;          // 接收者ID
    QString content;     // 消息内容
    QString create_time; // 消息创建时间
    int status;          // 消息状态
    int type;            // 消息类型
};

class LocalDb : public QObject, public Singleton<LocalDb>
{
    Q_OBJECT
    friend class Singleton<LocalDb>;
public:
    ~LocalDb();

    bool Init(int uid);
    bool SaveMessage(const MessageInfo& msg);
    long long GetMaxMsgId();
    std::vector<MessageInfo> GetHistory(int friendUid, int offset = 0, int limit = 50);
private:
    LocalDb();
    QSqlDatabase _db;
    int _uid;
};

#endif // LOCALDB_H
