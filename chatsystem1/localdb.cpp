#include "localdb.h"
#include <QDir>
#include <QStandardPaths>

LocalDb::LocalDb() : _uid(0) {

}

LocalDb::~LocalDb() {
    if (_db.isOpen()) {
        _db.close();
    }
}

bool LocalDb::Init(int uid) {
    _uid = uid;
    
    QString dbPath = QString("./chat_%1.db").arg(uid);

    if (QSqlDatabase::contains("qt_sql_default_connection")) {
        _db = QSqlDatabase::database("qt_sql_default_connection");
    } else {
        _db = QSqlDatabase::addDatabase("QSQLITE");
    }
    _db.setDatabaseName(dbPath);

    if (!_db.open()) {
        qDebug() << "[LocalDb] Failed to open database:" << _db.lastError();
        return false;
    }

    qDebug() << "[LocalDb] Database opened:" << dbPath;

    QSqlQuery query;
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS message (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            msg_id INTEGER UNIQUE,
            from_uid INTEGER,
            to_uid INTEGER,
            content TEXT,
            create_time TEXT,
            status INTEGER,
            type INTEGER
        )
    )";
    
    if (!query.exec(sql)) {
        qDebug() << "[LocalDb] Create table failed:" << query.lastError();
        return false;
    }
    
    query.exec("CREATE INDEX IF NOT EXISTS idx_msg_id ON message(msg_id)");

    return true;
}

bool LocalDb::SaveMessage(const MessageInfo& msg) {
    if (!_db.isOpen()) return false;

    QSqlQuery query;
    query.prepare("SELECT id FROM message WHERE msg_id = ?");
    query.addBindValue(msg.msg_id);
    if (query.exec() && query.next()) {
        return false; 
    }

    query.prepare("INSERT INTO message (msg_id, from_uid, to_uid, content, create_time, status, type) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(msg.msg_id);
    query.addBindValue(msg.from_uid);
    query.addBindValue(msg.to_uid);
    query.addBindValue(msg.content);
    query.addBindValue(msg.create_time);
    query.addBindValue(msg.status);
    query.addBindValue(msg.type);

    if (!query.exec()) {
        qDebug() << "[LocalDb] Insert failed:" << query.lastError();
        return false;
    }
    return true;
}

long long LocalDb::GetMaxMsgId() {
    if (!_db.isOpen()) return 0;
    
    QSqlQuery query("SELECT MAX(msg_id) FROM message");
    if (query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

std::vector<MessageInfo> LocalDb::GetHistory(int friendUid, int offset, int limit) {
    std::vector<MessageInfo> result;
    if (!_db.isOpen()) return result;

    QSqlQuery query;
    query.prepare("SELECT msg_id, from_uid, to_uid, content, create_time, status, type "
                  "FROM message "
                  "WHERE (from_uid = ? AND to_uid = ?) OR (from_uid = ? AND to_uid = ?) "
                  "ORDER BY msg_id DESC LIMIT ? OFFSET ?");
    
    query.addBindValue(_uid);
    query.addBindValue(friendUid);
    query.addBindValue(friendUid);
    query.addBindValue(_uid);
    query.addBindValue(limit);
    query.addBindValue(offset);

    if (query.exec()) {
        while (query.next()) {
            MessageInfo msg;
            msg.msg_id = query.value("msg_id").toLongLong();
            msg.from_uid = query.value("from_uid").toInt();
            msg.to_uid = query.value("to_uid").toInt();
            msg.content = query.value("content").toString();
            msg.create_time = query.value("create_time").toString();
            msg.status = query.value("status").toInt();
            msg.type = query.value("type").toInt();
            result.push_back(msg);
        }
    } else {
        qDebug() << "[LocalDb] GetHistory failed:" << query.lastError();
    }
    
    std::reverse(result.begin(), result.end());
    
    return result;
}
