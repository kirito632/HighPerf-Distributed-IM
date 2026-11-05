#include "MysqlDao.h"
#include"ConfigMgr.h"
#include"crypto_utils.h"
#include <sstream>

using MySqlPoolSingleton = Singleton<MySqlPool>;

MysqlDao::MysqlDao() {
    pool_ = MySqlPoolSingleton::GetInstance();
}

MysqlDao::~MysqlDao() {
    //pool_->Close();
}

int MysqlDao::RegUser(const std::string& name,
    const std::string& email,
    const std::string& pwd)
{
    // 使用 RAII ConnectionGuard，自动归还连接
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return -1;
    }

    try {
        sql::Connection* con = guard.get();
        
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement("CALL reg_user(?,?,?,@result)"));

        pstmt->setString(1, name);
        pstmt->setString(2, email);
        pstmt->setString(3, pwd);
        pstmt->execute();

        std::unique_ptr<sql::Statement> stmtResult(con->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmtResult->executeQuery("SELECT @result AS result"));

        int result = -1;
        if (res->next()) {
            result = res->getInt("result");
            std::cout << "[MysqlDao] RegUser result: " << result << std::endl;
        }

        // 不需要手动 returnConnection，Guard 析构时自动执行
        return result;
    }
    catch (sql::SQLException& e) {
        // 异常时标记连接为坏的，Guard 析构时会销毁并补充新连接
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in RegUser: " << e.what()
            << " (MySQL error code: " << e.getErrorCode()
            << ", SQLState: " << e.getSQLState() << ")" << std::endl;
        return -1;
    }
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email) {
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool in CheckEmail" << std::endl;
        return false;
    }

    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement("SELECT email FROM user WHERE name = ?"));

        pstmt->setString(1, name);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            std::cout << "Check Email: " << res->getString("email") << std::endl;
            if (email != res->getString("email")) {
                return false;
            }
            return true;
        }
        
        return false;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "SQLException in CheckEmail: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlDao::UpdatePwdByEmail(const std::string& email, const std::string& newpwdPlain) {
    ConnectionGuard guard(pool_);
    if (!guard) {
        return false;
    }
    try {
        sql::Connection* con = guard.get();
        std::string hashedPwd = sha256_hex(newpwdPlain);

        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("UPDATE user SET pwd = ? WHERE email = ?")
        );
        pstmt->setString(1, hashedPwd);
        pstmt->setString(2, email);

        int updateCount = pstmt->executeUpdate();
        std::cout << "Updated rows: " << updateCount << std::endl;

        return updateCount > 0;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "SQLException in UpdatePwdByEmail: " << e.what()
            << " (MySQL error code: " << e.getErrorCode()
            << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlDao::CheckPwd(const std::string& identifier, const std::string& pwdPlain, UserInfo& userInfo) {
    ConnectionGuard guard(pool_);
    if (!guard) {
        return false;
    }
    
    try {
        sql::Connection* con = guard.get();
        bool isEmail = (identifier.find('@') != std::string::npos);
        std::unique_ptr<sql::PreparedStatement> pstmt;
        if (isEmail) {
            pstmt.reset(con->prepareStatement("SELECT * FROM user WHERE email = ?"));
            pstmt->setString(1, identifier);
        }
        else {
            pstmt.reset(con->prepareStatement("SELECT * FROM user WHERE name = ?"));
            pstmt->setString(1, identifier);
        }

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (!res->next()) {
            return false;
        }

        std::string origin_pwd = res->getString("pwd");
        std::string db_email = res->getString("email");
        std::string db_name = res->getString("name");
        int db_uid = res->getInt("uid");

        std::string hashedInput = sha256_hex(pwdPlain);
        if (hashedInput == origin_pwd) {
            userInfo.name = db_name;
            userInfo.email = db_email;
            userInfo.uid = db_uid;
            userInfo.pwd = origin_pwd;
            return true;
        }

        if (pwdPlain == origin_pwd) {
            bool migrated = UpdatePwdByEmail(db_email, pwdPlain);
            if (migrated) {
                std::cout << "Migrated password for email " << db_email << " to hashed format." << std::endl;
                origin_pwd = sha256_hex(pwdPlain);
            }
            else {
                std::cout << "Password migration failed for " << db_email << std::endl;
            }
            userInfo.name = db_name;
            userInfo.email = db_email;
            userInfo.uid = db_uid;
            userInfo.pwd = origin_pwd;
            return true;
        }

        return false;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "SQLException in CheckPwd: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlDao::GetUser(int uid, UserInfo& userInfo)
{
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("SELECT uid, name, email, pwd FROM user WHERE uid = ?")
        );
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (!res->next()) {
            std::cerr << "[MysqlDao] No user found for uid: " << uid << std::endl;
            return false;
        }

        userInfo.uid = res->getInt("uid");
        userInfo.name = res->getString("name");
        userInfo.email = res->getString("email");
        userInfo.pwd = res->getString("pwd");

        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in GetUser: " << e.what() << std::endl;
        return false;
    }
}

std::shared_ptr<UserInfo> MysqlDao::GetUserByName(const std::string& name)
{
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return nullptr;
    }

    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement("SELECT * from user where name = ?"));
        pstmt->setString(1, name);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        std::shared_ptr<UserInfo> userInfo = nullptr;

        while (res->next()) {
            userInfo.reset(new UserInfo);
            userInfo->pwd = res->getString("pwd");
            userInfo->uid = res->getInt("uid");
            userInfo->name = res->getString("name");
            userInfo->email = res->getString("email");
            userInfo->nick = res->getString("nick");
            userInfo->icon = res->getString("icon");
            userInfo->sex = res->getInt("sex");
            userInfo->desc = res->getString("desc");
            break;
        }
        return userInfo;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in GetUserByName: " << e.what() << std::endl;
        return nullptr;
    }
}


// 获取好友申请列表
std::vector<ApplyInfo> MysqlDao::GetFriendRequests(int uid) {
    std::vector<ApplyInfo> requests;
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return requests;
    }

    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "SELECT fr.from_uid, u.name, fr.desc, u.icon, u.nick, u.sex, fr.status "
                "FROM friend_requests fr "
                "JOIN user u ON fr.from_uid = u.uid "
                "WHERE fr.to_uid = ? AND fr.status = 0 "
                "ORDER BY fr.create_time DESC"
            )
        );
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            ApplyInfo apply(
                res->getInt("from_uid"),
                res->getString("name"),
                res->getString("desc"),
                res->getString("icon"),
                res->getString("nick"),
                res->getInt("sex"),
                res->getInt("status")
            );
            requests.push_back(apply);
        }
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in GetFriendRequests: " << e.what() << std::endl;
    }

    return requests;
}

// 回复好友申请
bool MysqlDao::ReplyFriendRequest(int fromUid, int toUid, bool agree) {
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        sql::Connection* con = guard.get();
        // 更新申请状态
        std::unique_ptr<sql::PreparedStatement> updateStmt(
            con->prepareStatement("UPDATE friend_requests SET status = ? WHERE from_uid = ? AND to_uid = ? AND status = 0")
        );
        updateStmt->setInt(1, agree ? 1 : 2);
        updateStmt->setInt(2, fromUid);
        updateStmt->setInt(3, toUid);
        int updateCount = updateStmt->executeUpdate();

        if (updateCount > 0 && agree) {
            // 如果同意，添加好友关系
            std::unique_ptr<sql::PreparedStatement> insertFriendStmt(
                con->prepareStatement("INSERT INTO friends (uid1, uid2, create_time) VALUES (?, ?, NOW()), (?, ?, NOW())")
            );
            insertFriendStmt->setInt(1, fromUid);
            insertFriendStmt->setInt(2, toUid);
            insertFriendStmt->setInt(3, toUid);
            insertFriendStmt->setInt(4, fromUid);
            insertFriendStmt->execute();
        }

        return updateCount > 0;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in ReplyFriendRequest: " << e.what() << std::endl;
        return false;
    }
}

// 获取我的好友列表
std::vector<UserInfo> MysqlDao::GetMyFriends(int uid) {
    std::vector<UserInfo> friends;
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return friends;
    }

    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "SELECT u.uid, u.name, u.email, u.nick, u.icon, u.sex, u.desc "
                "FROM friends f "
                "JOIN user u ON (f.uid2 = u.uid) "
                "WHERE f.uid1 = ? "
                "ORDER BY u.nick ASC"
            )
        );
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            UserInfo user;
            user.uid = res->getInt("uid");
            user.name = res->getString("name");
            user.email = res->getString("email");
            user.nick = res->getString("nick");
            user.icon = res->getString("icon");
            user.sex = res->getInt("sex");
            user.desc = res->getString("desc");
            friends.push_back(user);
        }
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in GetMyFriends: " << e.what() << std::endl;
    }

    return friends;
}

// 检查是否为好友
bool MysqlDao::IsFriend(int uid1, int uid2) {
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("SELECT COUNT(*) as count FROM friends WHERE (uid1 = ? AND uid2 = ?) OR (uid1 = ? AND uid2 = ?)")
        );
        pstmt->setInt(1, uid1);
        pstmt->setInt(2, uid2);
        pstmt->setInt(3, uid2);
        pstmt->setInt(4, uid1);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        bool isFriend = false;
        if (res->next()) {
            isFriend = res->getInt("count") > 0;
        }

        return isFriend;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in IsFriend: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::SaveChatMessage(int fromUid, int toUid,
                               const std::string& clientMsgId,
                               const std::string& conversationId,
                               const std::string& initialPayload,
                               bool writeOutbox,
                               const std::string& traceparent,
                               const std::function<std::string(long long)>& finalPayload,
                               long long& serverMsgId)
{
    serverMsgId = 0;
    if (clientMsgId.empty()) {
        return false;
    }
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    sql::Connection* con = guard.get();
    try {
        con->setAutoCommit(false);
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "INSERT INTO messages "
                "(from_uid, to_uid, payload, client_msg_id, conversation_id, status, create_time) "
                "VALUES (?, ?, ?, ?, ?, 0, NOW()) "
                "ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id)")
        );
        pstmt->setInt(1, fromUid);
        pstmt->setInt(2, toUid);
        pstmt->setString(3, initialPayload);
        pstmt->setString(4, clientMsgId);
        pstmt->setString(5, conversationId);
        pstmt->executeUpdate();

        {
            std::unique_ptr<sql::PreparedStatement> lookup(
                con->prepareStatement(
                    "SELECT id FROM messages WHERE from_uid=? AND client_msg_id=? LIMIT 1"));
            lookup->setInt(1, fromUid);
            lookup->setString(2, clientMsgId);
            std::unique_ptr<sql::ResultSet> res(lookup->executeQuery());
            if (res->next()) {
                serverMsgId = res->getInt64(1);
            }
        }
        if (serverMsgId <= 0) {
            con->rollback();
            con->setAutoCommit(true);
            return false;
        }

        const std::string final_payload =
            finalPayload ? finalPayload(serverMsgId) : initialPayload;
        {
            std::unique_ptr<sql::PreparedStatement> update(
                con->prepareStatement("UPDATE messages SET payload=? WHERE id=?"));
            update->setString(1, final_payload);
            update->setInt64(2, serverMsgId);
            update->executeUpdate();
        }

        if (writeOutbox) {
            const std::string event_id =
                "message:" + std::to_string(serverMsgId) + ":created";
            std::unique_ptr<sql::PreparedStatement> outbox(
                con->prepareStatement(
                    "INSERT INTO outbox "
                    "(event_id, aggregate_type, aggregate_id, event_type, payload, traceparent, status, available_at, created_at) "
                    "VALUES (?, 'message', ?, 'MessageCreated', ?, ?, 0, NOW(), NOW()) "
                    "ON DUPLICATE KEY UPDATE payload=VALUES(payload), available_at=NOW()"));
            outbox->setString(1, event_id);
            outbox->setString(2, std::to_string(serverMsgId));
            outbox->setString(3, final_payload);
            outbox->setString(4, traceparent);
            outbox->executeUpdate();
        }

        con->commit();
        con->setAutoCommit(true);

        std::cout << "[MysqlDao] SaveChatMessage from=" << fromUid
                  << " to=" << toUid << " client_msg_id=" << clientMsgId
                  << " server_msg_id=" << serverMsgId
                  << " outbox=" << (writeOutbox ? 1 : 0) << std::endl;
        return true;
    }
    catch (sql::SQLException& e) {
        try {
            con->rollback();
            con->setAutoCommit(true);
        }
        catch (...) {
        }
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in SaveChatMessage: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::UpdateChatMessagePayload(long long serverMsgId, const std::string& payload)
{
    ConnectionGuard guard(pool_);
    if (!guard) {
        return false;
    }
    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("UPDATE messages SET payload=? WHERE id=?"));
        pstmt->setString(1, payload);
        pstmt->setInt64(2, serverMsgId);
        pstmt->executeUpdate();
        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in UpdateChatMessagePayload: "
                  << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::GetPendingOutbox(int limit, std::vector<OutboxRow>& rows)
{
    rows.clear();
    if (limit <= 0) {
        limit = 100;
    }
    ConnectionGuard guard(pool_);
    if (!guard) {
        return false;
    }
    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "SELECT id, event_id, event_type, payload, traceparent FROM outbox "
                "WHERE status=0 AND available_at<=NOW() ORDER BY id LIMIT ?"));
        pstmt->setInt(1, limit);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            OutboxRow row;
            row.id = res->getInt64("id");
            row.event_id = res->getString("event_id");
            row.event_type = res->getString("event_type");
            row.payload = res->getString("payload");
            row.traceparent = res->isNull("traceparent") ? "" : res->getString("traceparent");
            rows.push_back(std::move(row));
        }
        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in GetPendingOutbox: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::MarkOutboxPublished(const std::vector<long long>& ids)
{
    if (ids.empty()) {
        return true;
    }
    ConnectionGuard guard(pool_);
    if (!guard) {
        return false;
    }
    try {
        sql::Connection* con = guard.get();
        std::ostringstream sql;
        sql << "UPDATE outbox SET status=1, published_at=NOW(), last_error=NULL WHERE id IN (";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i) sql << ",";
            sql << "?";
        }
        sql << ")";
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql.str()));
        for (size_t i = 0; i < ids.size(); ++i) {
            pstmt->setInt64(static_cast<unsigned int>(i + 1), ids[i]);
        }
        pstmt->executeUpdate();
        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in MarkOutboxPublished: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::MarkOutboxFailed(long long id, const std::string& error)
{
    ConnectionGuard guard(pool_);
    if (!guard) {
        return false;
    }
    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "UPDATE outbox SET attempts=attempts+1, last_error=?, "
                "status=IF(attempts+1>=5, 2, 0), "
                "available_at=DATE_ADD(NOW(), INTERVAL 5 SECOND) WHERE id=?"));
        pstmt->setString(1, error.substr(0, 500));
        pstmt->setInt64(2, id);
        pstmt->executeUpdate();
        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in MarkOutboxFailed: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::IsEventProcessed(const std::string& event_id)
{
    ConnectionGuard guard(pool_);
    if (!guard) {
        return false;
    }
    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("SELECT 1 FROM processed_events WHERE event_id=? LIMIT 1"));
        pstmt->setString(1, event_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        return res->next();
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in IsEventProcessed: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::MarkEventProcessed(const std::string& event_id,
                                  const std::string& event_type,
                                  const std::string& consumer)
{
    ConnectionGuard guard(pool_);
    if (!guard) {
        return false;
    }
    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "INSERT INTO processed_events(event_id, event_type, consumer) "
                "VALUES(?, ?, ?) ON DUPLICATE KEY UPDATE processed_at=NOW()"));
        pstmt->setString(1, event_id);
        pstmt->setString(2, event_type);
        pstmt->setString(3, consumer);
        pstmt->executeUpdate();
        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in MarkEventProcessed: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::IsMessageUnread(int to_uid, long long server_msg_id)
{
    ConnectionGuard guard(pool_);
    if (!guard) {
        return false;
    }
    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "SELECT 1 FROM messages WHERE id=? AND to_uid=? AND status=0 LIMIT 1"));
        pstmt->setInt64(1, server_msg_id);
        pstmt->setInt(2, to_uid);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        return res->next();
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in IsMessageUnread: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::GetUnreadChatMessagesWithIds(int uid, long long cursor, int limit,
                                            std::vector<long long>& ids,
                                            std::vector<std::string>& payloads)
{
    ids.clear();
    payloads.clear();
    if (limit <= 0) {
        limit = 100;
    }

    // 使用 RAII ConnectionGuard，自动归还连接
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        sql::Connection* con = guard.get();
        
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "SELECT id, payload FROM messages "
                "WHERE to_uid = ? AND status = 0 AND id > ? "
                "ORDER BY id ASC LIMIT ?")
        );
        pstmt->setInt(1, uid);
        pstmt->setInt64(2, cursor);
        pstmt->setInt(3, limit);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            ids.push_back(res->getInt64("id"));
            payloads.push_back(res->getString("payload"));
        }

        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in GetUnreadChatMessagesWithIds: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::DeleteChatMessagesByIds(const std::vector<long long>& ids)
{
    // 历史保留：将消息标记为已读（status=1），而不是物理删除
    if (ids.empty()) return true;
    
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        sql::Connection* con = guard.get();
        std::ostringstream oss;
        oss << "UPDATE messages SET status=1, delivered_at=NOW() WHERE id IN (";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i) oss << ",";
            oss << "?";
        }
        oss << ")";

        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(oss.str()));
        for (size_t i = 0; i < ids.size(); ++i) {
            pstmt->setInt64(static_cast<unsigned int>(i + 1), ids[i]);
        }
        pstmt->executeUpdate();

        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in DeleteChatMessagesByIds: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::AckOfflineMessages(int uid, const std::vector<long long>& ids)
{
    if (ids.empty()) {
        return true;
    }
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        sql::Connection* con = guard.get();
        std::ostringstream oss;
        oss << "UPDATE messages SET status=1, delivered_at=NOW() "
               "WHERE to_uid=? AND status=0 AND id IN (";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i) oss << ",";
            oss << "?";
        }
        oss << ")";

        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(oss.str()));
        pstmt->setInt(1, uid);
        for (size_t i = 0; i < ids.size(); ++i) {
            pstmt->setInt64(static_cast<unsigned int>(i + 2), ids[i]);
        }
        int affected_rows = pstmt->executeUpdate();
        
        std::cout << "[AckOfflineMessages] uid=" << uid
                  << " ids=" << ids.size()
                  << " affected_rows=" << affected_rows << std::endl;

        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in AckOfflineMessages: " << e.what() << std::endl;
        return false;
    }
}
