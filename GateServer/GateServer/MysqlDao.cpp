#include "MysqlDao.h"
#include "ConfigMgr.h"
#include "crypto_utils.h"
#include <memory>
#include <iostream>
#include <functional>
#include <cctype>
#include <algorithm>
#include <stdexcept>
#include <jdbc/mysql_driver.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>

MySqlPool::MySqlPool()
    : poolSize_(0), b_stop_(false)
{

}

void MySqlPool::Init(const std::string& url,
    const std::string& user,
    const std::string& pass,
    const std::string& schema,
    int poolSize)
{
    url_ = url;
    user_ = user;
    pass_ = pass;
    schema_ = schema;
    poolSize_ = poolSize;
    b_stop_ = false;

    std::cout << "[MySqlPool] Init called. url=" << url_
        << " user=" << user_ << " schema=" << schema_
        << " poolSize=" << poolSize_ << std::endl;

    sql::mysql::MySQL_Driver* driver = nullptr;
    try {
        driver = sql::mysql::get_mysql_driver_instance();
        if (!driver) {
            std::cerr << "[MySqlPool] get_mysql_driver_instance returned null!" << std::endl;
            throw std::runtime_error("driver null");
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[MySqlPool] get_mysql_driver_instance exception: " << e.what() << std::endl;
        throw;
    }
    catch (...) {
        std::cerr << "[MySqlPool] unknown exception getting driver" << std::endl;
        throw;
    }

    try {
        std::cout << "[MySqlPool] Trying single test connection..." << std::endl;
        std::unique_ptr<sql::Connection> testCon(driver->connect(url_, user_, pass_));
        if (!testCon) {
            std::cerr << "[MySqlPool] testCon is null after connect!" << std::endl;
            throw std::runtime_error("testCon null");
        }
        testCon->setSchema(schema_);
        std::cout << "[MySqlPool] single test connection ok" << std::endl;
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MySqlPool] test connect failed (SQLException): " << e.what()
            << " (err:" << e.getErrorCode() << ", state:" << e.getSQLState() << ")" << std::endl;
        throw;
    }
    catch (const std::exception& e) {
        std::cerr << "[MySqlPool] test connect failed (std::exception): " << e.what() << std::endl;
        throw;
    }
    catch (...) {
        std::cerr << "[MySqlPool] test connect failed (unknown)" << std::endl;
        throw;
    }

    for (int i = 0; i < poolSize_; ++i) {
        try {
            std::unique_ptr<sql::Connection> con(driver->connect(url_, user_, pass_));
            con->setSchema(schema_);
            //             
            {
                std::unique_lock<std::mutex> lock(mutex_);
                pool_.push(std::move(con));
            }
            cond_.notify_one();
            std::cout << "[MySqlPool] push connection " << i << std::endl;
        }
        catch (sql::SQLException& e) {
            std::cerr << "[MySqlPool] connect #" << i << " failed: " << e.what()
                << " (err:" << e.getErrorCode() << ", state:" << e.getSQLState() << ")" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "[MySqlPool] connect #" << i << " std::exception: " << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "[MySqlPool] connect #" << i << " unknown exception" << std::endl;
        }
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::cout << "[MySqlPool] Init done, actual pool size = " << pool_.size() << std::endl;
    }
}

std::unique_ptr<sql::Connection> MySqlPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return b_stop_.load() || !pool_.empty(); });
    if (b_stop_.load() || pool_.empty()) return nullptr;

    auto con = std::move(pool_.front());
    pool_.pop();
    return con;
}

void MySqlPool::returnConnection(std::unique_ptr<sql::Connection> con) {
    if (!con) return;
    std::unique_lock<std::mutex> lock(mutex_);
    if (b_stop_.load()) return;
    pool_.push(std::move(con));
    cond_.notify_one();
}

void MySqlPool::Close() {
    std::unique_lock<std::mutex> lock(mutex_);
    b_stop_.store(true);
    while (!pool_.empty()) pool_.pop();
    cond_.notify_all();
    std::cout << "[MySqlPool] Closed pool" << std::endl;
}

MySqlPool::~MySqlPool() {
    Close();
}

using MySqlPoolSingleton = Singleton<MySqlPool>;

MysqlDao::MysqlDao() {
    try {
        pool_ = MySqlPoolSingleton::GetInstance();
    }
    catch (...) {
        std::cerr << "[MysqlDao] Warning: Failed to get MySqlPool singleton in ctor." << std::endl;
        pool_.reset();
    }
}

bool MysqlDao::UpdatePwdByEmail(const std::string& email, const std::string& newpwdPlain) {
    if (!pool_) return false;

    auto con = pool_->getConnection();
    if (!con) return false;
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::string hashedPwd = sha256_hex(newpwdPlain);
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("UPDATE user SET pwd = ? WHERE email = ?")
        );
        pstmt->setString(1, hashedPwd);
        pstmt->setString(2, email);
        int updateCount = pstmt->executeUpdate();
        std::cout << "[MysqlDao] UpdatePwdByEmail updated rows: " << updateCount << std::endl;
        return updateCount > 0;
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao] SQLException in UpdatePwdByEmail: " << e.what() << std::endl;
        return false;
    }
}

std::vector<UserInfo> MysqlDao::SearchUsers(const std::string& keyword) {
    std::vector<UserInfo> users;
    if (!pool_) return users;

    auto con = pool_->getConnection();
    if (!con) return users;
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        // 先尝试查询完整字段，如果失败则只查询基础字段
        // 这样可以兼容没有扩展字段的旧数据库表
        std::string sql;
        bool hasExtendedFields = false;

        // 检查表是否有扩展字段（通过尝试查询来判断）
        try {
            std::unique_ptr<sql::Statement> checkStmt(con->createStatement());
            std::unique_ptr<sql::ResultSet> checkRes(
                checkStmt->executeQuery("SHOW COLUMNS FROM user LIKE 'nick'")
            );
            hasExtendedFields = (checkRes && checkRes->next());
        }
        catch (...) {
            hasExtendedFields = false;
        }

        if (hasExtendedFields) {
            sql = "SELECT uid, name, email, nick, icon, sex, `desc` FROM user WHERE name LIKE ? OR email LIKE ? LIMIT 20";
        }
        else {
            // 如果表中没有扩展字段，只查询基础字段
            sql = "SELECT uid, name, email FROM user WHERE name LIKE ? OR email LIKE ? LIMIT 20";
            std::cout << "[MysqlDao::SearchUsers] 注意: 数据库表中缺少扩展字段(nick/icon/sex/desc)，将使用默认值" << std::endl;
        }

        std::cout << "[MysqlDao::SearchUsers] 搜索关键词: \"" << keyword << "\"" << std::endl;
        std::cout << "[MysqlDao::SearchUsers] 执行SQL: " << sql << std::endl;

        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
        std::string pattern = "%" + keyword + "%";
        std::cout << "[MysqlDao::SearchUsers] 搜索模式: \"" << pattern << "\"" << std::endl;

        pstmt->setString(1, pattern);
        pstmt->setString(2, pattern);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        int count = 0;
        while (res && res->next()) {
            UserInfo u;
            u.uid = res->getInt("uid");
            u.name = res->getString("name");
            u.email = res->getString("email");

            // 如果有扩展字段，读取它们；否则使用默认值
            if (hasExtendedFields) {
                u.nick = res->getString("nick");
                u.icon = res->getString("icon");
                u.sex = res->getInt("sex");
                u.desc = res->getString("desc");
            }
            else {
                // 使用默认值
                u.nick = "";
                u.icon = "";
                u.sex = 0;
                u.desc = "";
            }

            users.push_back(u);
            count++;
            std::cout << "[MysqlDao::SearchUsers] 找到用户: uid=" << u.uid
                << " name=\"" << u.name << "\" email=\"" << u.email << "\"" << std::endl;
        }
        std::cout << "[MysqlDao::SearchUsers] 总共找到 " << count << " 个用户" << std::endl;
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao::SearchUsers] SQLException: " << e.what() << std::endl;
        std::cerr << "[MysqlDao::SearchUsers] SQLState: " << e.getSQLState() << std::endl;
        std::cerr << "[MysqlDao::SearchUsers] ErrorCode: " << e.getErrorCode() << std::endl;
    }
    return users;
}

bool MysqlDao::AddFriendRequest(int fromUid, int toUid, const std::string& desc) {
    if (!pool_) return false;
    auto con = pool_->getConnection();
    if (!con) return false;
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::unique_ptr<sql::PreparedStatement> checkStmt(
            con->prepareStatement("SELECT id FROM friend_requests WHERE from_uid = ? AND to_uid = ? AND status = 0")
        );
        checkStmt->setInt(1, fromUid);
        checkStmt->setInt(2, toUid);
        std::unique_ptr<sql::ResultSet> checkRes(checkStmt->executeQuery());
        if (checkRes && checkRes->next()) {
            return false;
        }

        std::unique_ptr<sql::PreparedStatement> insertStmt(
            con->prepareStatement("INSERT INTO friend_requests (from_uid, to_uid, `desc`, status, create_time) VALUES (?, ?, ?, 0, NOW())")
        );
        insertStmt->setInt(1, fromUid);
        insertStmt->setInt(2, toUid);
        insertStmt->setString(3, desc);
        insertStmt->execute();
        return true;
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao] SQLException in AddFriendRequest: " << e.what() << std::endl;
        return false;
    }
}

std::vector<ApplyInfo> MysqlDao::GetFriendRequests(int uid) {
    std::vector<ApplyInfo> requests;
    if (!pool_) return requests;
    auto con = pool_->getConnection();
    if (!con) return requests;
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "SELECT fr.from_uid, u.name, fr.`desc`, u.icon, u.nick, u.sex, fr.status "
                "FROM friend_requests fr "
                "JOIN user u ON fr.from_uid = u.uid "
                "WHERE fr.to_uid = ? AND fr.status = 0 "
                "AND NOT EXISTS ("
                "  SELECT 1 FROM friends f "
                "  WHERE (f.uid1 = fr.from_uid AND f.uid2 = fr.to_uid) "
                "     OR (f.uid1 = fr.to_uid AND f.uid2 = fr.from_uid)"
                ") "
                "ORDER BY fr.create_time DESC"
            )
        );
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res && res->next()) {
            ApplyInfo a(
                res->getInt("from_uid"),
                res->getString("name"),
                res->getString("desc"),
                res->getString("icon"),
                res->getString("nick"),
                res->getInt("sex"),
                res->getInt("status")
            );
            requests.push_back(a);
        }
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao] SQLException in GetFriendRequests: " << e.what() << std::endl;
    }
    return requests;
}

bool MysqlDao::ReplyFriendRequest(int fromUid, int toUid, bool agree) {
    if (!pool_) return false;
    auto con = pool_->getConnection();
    if (!con) return false;
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::unique_ptr<sql::PreparedStatement> updateStmt(
            con->prepareStatement("UPDATE friend_requests SET status = ? WHERE from_uid = ? AND to_uid = ? AND status = 0")
        );
        updateStmt->setInt(1, agree ? 1 : 2);
        updateStmt->setInt(2, fromUid);
        updateStmt->setInt(3, toUid);
        int updateCount = updateStmt->executeUpdate();
        if (updateCount <= 0) return false;

        // 同步清理对向仍处于待处理的申请，避免互发后残留 pending
        std::unique_ptr<sql::PreparedStatement> reverseUpdateStmt(
            con->prepareStatement("UPDATE friend_requests SET status = ? WHERE from_uid = ? AND to_uid = ? AND status = 0")
        );
        reverseUpdateStmt->setInt(1, agree ? 1 : 2);
        reverseUpdateStmt->setInt(2, toUid);
        reverseUpdateStmt->setInt(3, fromUid);
        reverseUpdateStmt->executeUpdate();

        if (agree) {
            std::unique_ptr<sql::PreparedStatement> insertFriendStmt(
                con->prepareStatement("INSERT INTO friends (uid1, uid2, create_time) VALUES (?, ?, NOW()), (?, ?, NOW())")
            );
            insertFriendStmt->setInt(1, fromUid);
            insertFriendStmt->setInt(2, toUid);
            insertFriendStmt->setInt(3, toUid);
            insertFriendStmt->setInt(4, fromUid);
            insertFriendStmt->execute();
        }
        return true;
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao] SQLException in ReplyFriendRequest: " << e.what() << std::endl;
        return false;
    }
}

std::vector<UserInfo> MysqlDao::GetMyFriends(int uid) {
    std::vector<UserInfo> friends;
    if (!pool_) return friends;
    auto con = pool_->getConnection();
    if (!con) return friends;
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "SELECT u.uid, u.name, u.email, u.nick, u.icon, u.sex, u.`desc` "
                "FROM friends f "
                "JOIN user u ON (f.uid2 = u.uid) "
                "WHERE f.uid1 = ? "
                "ORDER BY u.nick ASC"
            )
        );
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res && res->next()) {
            UserInfo u;
            u.uid = res->getInt("uid");
            u.name = res->getString("name");
            u.email = res->getString("email");
            u.nick = res->getString("nick");
            u.icon = res->getString("icon");
            u.sex = res->getInt("sex");
            u.desc = res->getString("desc");
            friends.push_back(u);
        }
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao] SQLException in GetMyFriends: " << e.what() << std::endl;
    }
    return friends;
}

bool MysqlDao::IsFriend(int uid1, int uid2) {
    if (!pool_) return false;
    auto con = pool_->getConnection();
    if (!con) return false;
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("SELECT COUNT(*) AS cnt FROM friends WHERE (uid1 = ? AND uid2 = ?) OR (uid1 = ? AND uid2 = ?)")
        );
        pstmt->setInt(1, uid1);
        pstmt->setInt(2, uid2);
        pstmt->setInt(3, uid2);
        pstmt->setInt(4, uid1);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (res && res->next()) {
            return res->getInt("cnt") > 0;
        }
        return false;
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao] SQLException in IsFriend: " << e.what() << std::endl;
        return false;
    }
}


MysqlDao::~MysqlDao() {
    // no-op
}

int MysqlDao::RegUser(const std::string& name,
    const std::string& email,
    const std::string& pwd)
{
    if (!pool_) {
        std::cerr << "[MysqlDao] RegUser: pool_ is null" << std::endl;
        return -1;
    }

    auto con = pool_->getConnection();
    if (!con) {
        std::cerr << "[MysqlDao] RegUser: getConnection returned null" << std::endl;
        return -1;
    }
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::unique_ptr<sql::PreparedStatement> stmt(
            con->prepareStatement("CALL reg_user(?,?,?,@result)")
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
        if (res && res->next()) {
            result = res->getInt("result");
            std::cout << "[MysqlDao] RegUser result: " << result << std::endl;
        }
        else {
            std::cout << "[MysqlDao] RegUser: no result from SELECT @result" << std::endl;
        }
        return result;
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao] SQLException in RegUser: " << e.what()
            << " (err:" << e.getErrorCode() << ", state:" << e.getSQLState() << ")" << std::endl;
        if (e.getErrorCode() == 1062) {
            std::cerr << "[MysqlDao] Duplicate entry (email or username exists)." << std::endl;
        }
        return -1;
    }
    catch (std::exception& e) {
        std::cerr << "[MysqlDao] std::exception in RegUser: " << e.what() << std::endl;
        return -1;
    }
    catch (...) {
        std::cerr << "[MysqlDao] unknown exception in RegUser" << std::endl;
        return -1;
    }
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email) {
    if (!pool_) return false;

    auto con = pool_->getConnection();
    if (!con) return false;
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("SELECT email FROM user WHERE name = ?")
        );
        pstmt->setString(1, name);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (res && res->next()) {
            std::string db_email = res->getString("email");
            std::cout << "[MysqlDao] CheckEmail db_email=" << db_email << std::endl;
            return (email == db_email);
        }
        // ? м ?
        return false;
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao] SQLException in CheckEmail: " << e.what()
            << " (err:" << e.getErrorCode() << ", state:" << e.getSQLState() << ")" << std::endl;
        return false;
    }
    catch (std::exception& e) {
        std::cerr << "[MysqlDao] std::exception in CheckEmail: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::UpdatePwd(const std::string& name, const std::string& newpwd) {
    if (!pool_) return false;

    auto con = pool_->getConnection();
    if (!con) return false;
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("UPDATE user SET pwd = ? WHERE name = ?")
        );
        //  ?      CheckPwd/UpdatePwdByEmail     ? ?    ?            sha256   ?
        //   ? ? ?  1 -> hashed(newpwd), 2 -> name
        std::string hashedPwd = sha256_hex(newpwd);
        pstmt->setString(1, hashedPwd);
        pstmt->setString(2, name);

        int updateCount = pstmt->executeUpdate();
        std::cout << "[MysqlDao] UpdatePwd updated rows: " << updateCount << std::endl;
        return (updateCount > 0);
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao] SQLException in UpdatePwd: " << e.what()
            << " (err:" << e.getErrorCode() << ", state:" << e.getSQLState() << ")" << std::endl;
        return false;
    }
    catch (std::exception& e) {
        std::cerr << "[MysqlDao] std::exception in UpdatePwd: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo) {
    if (!pool_) return false;

    auto con = pool_->getConnection();
    if (!con) return false;
    Defer d([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            //      и?   ?  email ?ν   ?  
            con->prepareStatement("SELECT uid, name, email, pwd FROM user WHERE TRIM(LOWER(email)) = LOWER(?)")
        );
        pstmt->setString(1, email);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (!res || !res->next()) {
            //  ?       
            std::cerr << "[MysqlDao] CheckPwd: no user found for email='" << email << "'\n";
            return false;
        }

        //   ?db ? 
        std::string origin_pwd = res->getString("pwd");
        userInfo.uid = res->getInt("uid");
        userInfo.name = res->getString("name");
        userInfo.email = res->getString("email");
        userInfo.pwd = origin_pwd;

        // helper lambdas
        auto rtrim_copy = [](std::string s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t' || s.back() == '\0')) {
                s.pop_back();
            }
            return s;
            };
        auto to_lower_copy = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
            };
        auto is_hex64 = [](const std::string& s) {
            if (s.size() != 64) return false;
            for (unsigned char c : s) if (!std::isxdigit(c)) return false;
            return true;
            };

        std::string db_pwd_norm = to_lower_copy(rtrim_copy(origin_pwd));
        std::string incoming = pwd;
        std::string incoming_trim = rtrim_copy(incoming);

        std::string incoming_hashed = sha256_hex(incoming);
        std::string incoming_hashed_trim = sha256_hex(incoming_trim);

        if (to_lower_copy(incoming_hashed) == db_pwd_norm || to_lower_copy(incoming_hashed_trim) == db_pwd_norm) {
            return true;
        }

        if (is_hex64(incoming) && to_lower_copy(incoming) == db_pwd_norm) {
            return true;
        }

        return false;
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MysqlDao] SQLException in CheckPwd: " << e.what()
            << " (err:" << e.getErrorCode() << ", state:" << e.getSQLState() << ")" << std::endl;
        return false;
    }
    catch (std::exception& e) {
        std::cerr << "[MysqlDao] std::exception in CheckPwd: " << e.what() << std::endl;
        return false;
    }
    catch (...) {
        std::cerr << "[MysqlDao] unknown exception in CheckPwd" << std::endl;
        return false;
    }
}
