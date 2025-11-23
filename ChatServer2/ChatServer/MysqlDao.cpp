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
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return -1;
    }

    try {
        std::unique_ptr<sql::PreparedStatement> stmt(
            con->prepareStatement("CALL reg_user(?,?,?,@result)")  // ??洢????
        );
        stmt->setString(1, name);
        stmt->setString(2, email);
        stmt->setString(3, pwd);
        stmt->execute();

        std::unique_ptr<sql::Statement> stmtResult(con->createStatement());
        std::unique_ptr<sql::ResultSet> res(
            stmtResult->executeQuery("SELECT @result AS result")
        );

        int result = -1;
        if (res->next()) {
            result = res->getInt("result");
            std::cout << "[MysqlDao] RegUser result: " << result << std::endl;
        }

        pool_->returnConnection(std::move(con));
        return result;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in RegUser: " << e.what()
            << " (MySQL error code: " << e.getErrorCode()
            << ", SQLState: " << e.getSQLState() << ")" << std::endl;
        return -1;
    }
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email) {
    auto con = pool_->getConnection();
    try {
        if (con == nullptr) {
            pool_->returnConnection(std::move(con));
            return false;
        }

        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement("SELECT email FROM user WHERE name = ?"));

        pstmt->setString(1, name);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            std::cout << "Check Email: " << res->getString("email") << std::endl;
            if (email != res->getString("email")) {
                pool_->returnConnection(std::move(con));
                return false;
            }
            pool_->returnConnection(std::move(con));
            return true;
        }
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlDao::UpdatePwdByEmail(const std::string& email, const std::string& newpwdPlain) {
    auto con = pool_->getConnection();
    if (con == nullptr) {
        return false;
    }
    try {
        std::string hashedPwd = sha256_hex(newpwdPlain);

        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("UPDATE user SET pwd = ? WHERE email = ?")
        );
        pstmt->setString(1, hashedPwd);
        pstmt->setString(2, email);

        int updateCount = pstmt->executeUpdate();
        std::cout << "Updated rows: " << updateCount << std::endl;

        pool_->returnConnection(std::move(con));
        return updateCount > 0;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "SQLException: " << e.what()
            << " (MySQL error code: " << e.getErrorCode()
            << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlDao::CheckPwd(const std::string& identifier, const std::string& pwdPlain, UserInfo& userInfo) {
    auto con = pool_->getConnection();
    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        if (con == nullptr) {
            return false;
        }

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
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlDao::GetUser(int uid, UserInfo& userInfo)
{
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("SELECT uid, name, email, pwd FROM user WHERE uid = ?")
        );
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (!res->next()) {
            pool_->returnConnection(std::move(con));
            std::cerr << "[MysqlDao] No user found for uid: " << uid << std::endl;
            return false;
        }

        //  UserInfo
        userInfo.uid = res->getInt("uid");
        userInfo.name = res->getString("name");
        userInfo.email = res->getString("email");
        userInfo.pwd = res->getString("pwd");

        pool_->returnConnection(std::move(con));
        return true;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in GetUser: " << e.what() << std::endl;
        return false;
    }
}

std::shared_ptr<UserInfo> MysqlDao::GetUserByName(const std::string& name)
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return nullptr;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
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
        std::cerr << "[MysqlDao] SQLException in GetUserByName: " << e.what() << std::endl;
        return nullptr;
    }
}

// 搜索用户
std::vector<UserInfo> MysqlDao::SearchUsers(const std::string& keyword) {
    std::vector<UserInfo> users;
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return users;
    }

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("SELECT uid, name, email, nick, icon, sex, desc FROM user WHERE name LIKE ? OR email LIKE ? LIMIT 20")
        );
        std::string searchPattern = "%" + keyword + "%";
        pstmt->setString(1, searchPattern);
        pstmt->setString(2, searchPattern);

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
            users.push_back(user);
        }

        pool_->returnConnection(std::move(con));
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in SearchUsers: " << e.what() << std::endl;
    }

    return users;
}

// 添加好友申请
bool MysqlDao::AddFriendRequest(int fromUid, int toUid, const std::string& desc) {
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        // 检查是否已经发送过申请
        std::unique_ptr<sql::PreparedStatement> checkStmt(
            con->prepareStatement("SELECT id FROM friend_requests WHERE from_uid = ? AND to_uid = ? AND status = 0")
        );
        checkStmt->setInt(1, fromUid);
        checkStmt->setInt(2, toUid);
        std::unique_ptr<sql::ResultSet> checkRes(checkStmt->executeQuery());

        if (checkRes->next()) {
            pool_->returnConnection(std::move(con));
            return false; // 已经发送过申请
        }

        // 插入新的好友申请
        std::unique_ptr<sql::PreparedStatement> insertStmt(
            con->prepareStatement("INSERT INTO friend_requests (from_uid, to_uid, desc, status, create_time) VALUES (?, ?, ?, 0, NOW())")
        );
        insertStmt->setInt(1, fromUid);
        insertStmt->setInt(2, toUid);
        insertStmt->setString(3, desc);
        insertStmt->execute();

        pool_->returnConnection(std::move(con));
        return true;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in AddFriendRequest: " << e.what() << std::endl;
        return false;
    }
}

// 获取好友申请列表
std::vector<ApplyInfo> MysqlDao::GetFriendRequests(int uid) {
    std::vector<ApplyInfo> requests;
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return requests;
    }

    try {
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

        pool_->returnConnection(std::move(con));
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in GetFriendRequests: " << e.what() << std::endl;
    }

    return requests;
}

// 回复好友申请
bool MysqlDao::ReplyFriendRequest(int fromUid, int toUid, bool agree) {
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
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

        pool_->returnConnection(std::move(con));
        return updateCount > 0;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in ReplyFriendRequest: " << e.what() << std::endl;
        return false;
    }
}

// 获取我的好友列表
std::vector<UserInfo> MysqlDao::GetMyFriends(int uid) {
    std::vector<UserInfo> friends;
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return friends;
    }

    try {
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

        pool_->returnConnection(std::move(con));
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in GetMyFriends: " << e.what() << std::endl;
    }

    return friends;
}

// 检查是否为好友
bool MysqlDao::IsFriend(int uid1, int uid2) {
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
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

        pool_->returnConnection(std::move(con));
        return isFriend;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in IsFriend: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::SaveChatMessage(int fromUid, int toUid, const std::string& payload)
{
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("INSERT INTO messages (from_uid, to_uid, payload, status, create_time) VALUES (?, ?, ?, 0, NOW())")
        );
        pstmt->setInt(1, fromUid);
        pstmt->setInt(2, toUid);
        pstmt->setString(3, payload);
        pstmt->execute();

        pool_->returnConnection(std::move(con));
        return true;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in SaveChatMessage: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::GetUnreadChatMessagesWithIds(int uid, std::vector<long long>& ids, std::vector<std::string>& payloads)
{
    ids.clear();
    payloads.clear();
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("SELECT id, payload FROM messages WHERE to_uid = ? AND status = 0 ORDER BY id ASC")
        );
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            ids.push_back(res->getInt64("id"));
            payloads.push_back(res->getString("payload"));
        }

        pool_->returnConnection(std::move(con));
        return true;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in GetUnreadChatMessagesWithIds: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::DeleteChatMessagesByIds(const std::vector<long long>& ids)
{
    // 历史保留：将消息标记为已读（status=1），而不是物理删除
    if (ids.empty()) return true;
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        std::ostringstream oss;
        oss << "UPDATE messages SET status=1 WHERE id IN (";
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

        pool_->returnConnection(std::move(con));
        return true;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in DeleteChatMessagesByIds: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::AckOfflineMessages(int uid, long long max_msg_id)
{
    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("UPDATE messages SET status=1 WHERE to_uid = ? AND id <= ?")
        );
        pstmt->setInt(1, uid);
        pstmt->setInt64(2, max_msg_id);
        pstmt->executeUpdate();

        pool_->returnConnection(std::move(con));
        return true;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "[MysqlDao] SQLException in AckOfflineMessages: " << e.what() << std::endl;
        return false;
    }
}