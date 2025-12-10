#include "MysqlDao.h"
#include"ConfigMgr.h"
#include"crypto_utils.h"
#include <sstream>
#include <iomanip>

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

        // 先查询 uid，用于后续缓存失效
        int uid = -1;
        {
            std::unique_ptr<sql::PreparedStatement> selectStmt(
                con->prepareStatement("SELECT uid FROM user WHERE email = ?")
            );
            selectStmt->setString(1, email);
            std::unique_ptr<sql::ResultSet> res(selectStmt->executeQuery());
            if (res->next()) {
                uid = res->getInt("uid");
            }
        }

        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("UPDATE user SET pwd = ? WHERE email = ?")
        );
        pstmt->setString(1, hashedPwd);
        pstmt->setString(2, email);

        int updateCount = pstmt->executeUpdate();
        std::cout << "Updated rows: " << updateCount << std::endl;

        // FlashCache: 密码修改后，主动失效缓存（多机同步）
        if (updateCount > 0 && uid > 0) {
            PublishCacheInvalidation(uid);
            std::cout << "[FlashCache] Published cache invalidation for uid: " << uid << std::endl;
        }

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
            
            // FlashCache: 登录成功，写入缓存
            userCache_.put(db_uid, userInfo, 300000);  // TTL 5分钟
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
            
            // FlashCache: 登录成功，写入缓存
            userCache_.put(db_uid, userInfo, 300000);  // TTL 5分钟
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
    // ==================== Step 1: 查本地缓存 ====================
    auto cachedUser = userCache_.get(uid);
    if (cachedUser) {
        userInfo = *cachedUser;
        return true;
    }

    // ==================== Step 2: Singleflight 归并回源 ====================
    // 
    // 缓存未命中时，检查是否有其他请求正在查询同一个 uid。
    // 如果有，等待那个请求的结果；如果没有，发起查询。
    // 这样可以避免缓存击穿（1000 个请求同时打到 DB）。
    
    std::shared_future<std::optional<UserInfo>> future;
    bool isLeader = false;  // 是否是第一个发起查询的请求
    std::shared_ptr<std::promise<std::optional<UserInfo>>> promise;
    
    {
        std::lock_guard<std::mutex> lock(inFlightMutex_);
        
        auto it = inFlight_.find(uid);
        if (it != inFlight_.end()) {
            // 已有请求在查询中，等待结果
            future = it->second;
        } else {
            // 第一个请求，创建 promise 并加入 inFlight_
            isLeader = true;
            promise = std::make_shared<std::promise<std::optional<UserInfo>>>();
            future = promise->get_future().share();
            inFlight_[uid] = future;
        }
    }
    
    if (isLeader) {
        // ==================== Step 3: Leader 查询数据库 ====================
        std::optional<UserInfo> result = LoadUserFromDB(uid);
        
        // 如果查询成功，写入缓存（回填）
        if (result) {
            // TTL 5 分钟 = 300000 毫秒
            userCache_.put(uid, *result, 300000);
        }
        
        // 设置 promise，唤醒所有等待的请求
        promise->set_value(result);
        
        // 从 inFlight_ 中删除
        {
            std::lock_guard<std::mutex> lock(inFlightMutex_);
            inFlight_.erase(uid);
        }
        
        if (result) {
            userInfo = *result;
            return true;
        }
        return false;
    } else {
        // ==================== Step 4: Follower 等待结果 ====================
        try {
            auto result = future.get();
            if (result) {
                userInfo = *result;
                return true;
            }
            return false;
        } catch (const std::exception& e) {
            std::cerr << "[MysqlDao] Singleflight wait failed: " << e.what() << std::endl;
            return false;
        }
    }
}

// 内部方法：从数据库加载用户（不带 Singleflight）
std::optional<UserInfo> MysqlDao::LoadUserFromDB(int uid)
{
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return std::nullopt;
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
            return std::nullopt;
        }

        UserInfo user;
        user.uid = res->getInt("uid");
        user.name = res->getString("name");
        user.email = res->getString("email");
        user.pwd = res->getString("pwd");

        return user;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in LoadUserFromDB: " << e.what() << std::endl;
        return std::nullopt;
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
            
            // FlashCache: 好友关系变更，失效双方缓存
            InvalidateUserCache(fromUid);
            InvalidateUserCache(toUid);
            std::cout << "[FlashCache] Invalidated cache for uid: " << fromUid << " and " << toUid << std::endl;
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

bool MysqlDao::SaveChatMessage(int fromUid, int toUid, const std::string& payload)
{
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        sql::Connection* con = guard.get();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("INSERT INTO messages (from_uid, to_uid, payload, status, create_time) VALUES (?, ?, ?, 0, NOW())")
        );
        pstmt->setInt(1, fromUid);
        pstmt->setInt(2, toUid);
        pstmt->setString(3, payload);
        pstmt->execute();

        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in SaveChatMessage: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::GetUnreadChatMessagesWithIds(int uid, std::vector<long long>& ids, std::vector<std::string>& payloads)
{
    ids.clear();
    payloads.clear();
    
    // 使用 RAII ConnectionGuard，自动归还连接
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        sql::Connection* con = guard.get();
        
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("SELECT id, payload FROM messages WHERE to_uid = ? AND status = 0 ORDER BY id ASC")
        );
        pstmt->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            ids.push_back(res->getInt64("id"));
            payloads.push_back(res->getString("payload"));
        }

        // 不需要手动 returnConnection，Guard 析构时自动执行
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

        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in DeleteChatMessagesByIds: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::AckOfflineMessages(int uid, long long max_msg_id)
{
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cerr << "[MysqlDao] Failed to get connection from pool." << std::endl;
        return false;
    }

    try {
        sql::Connection* con = guard.get();
        // 只更新status=0的消息
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement("UPDATE messages SET status=1 WHERE to_uid = ? AND id <= ? AND status = 0")
        );
        pstmt->setInt(1, uid);
        pstmt->setInt64(2, max_msg_id);
        int affected_rows = pstmt->executeUpdate();
        
        std::cout << "[AckOfflineMessages] uid=" << uid 
                  << " max_msg_id=" << max_msg_id 
                  << " affected_rows=" << affected_rows << std::endl;

        return true;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[MysqlDao] SQLException in AckOfflineMessages: " << e.what() << std::endl;
        return false;
    }
}

// ==================== FlashCache 监控 ====================

void MysqlDao::LogCacheMetrics() const
{
    auto stats = GetUserCacheStats();
    
    std::cout << "\n==================== FlashCache Metrics ====================" << std::endl;
    
    // 基础统计
    std::cout << "[Size]     Current: " << stats.current_size 
              << " / " << stats.capacity 
              << " (Peak: " << stats.peak_size << ")" << std::endl;
    std::cout << "[Usage]    " << std::fixed << std::setprecision(1) 
              << (stats.usage_rate() * 100.0) << "%" << std::endl;
    
    // 命中率统计
    std::cout << "[Gets]     Total: " << stats.total_gets() 
              << " (Hits: " << stats.hits 
              << ", Misses: " << stats.misses << ")" << std::endl;
    std::cout << "[HitRate]  " << std::fixed << std::setprecision(2) 
              << (stats.hit_rate() * 100.0) << "%" << std::endl;
    
    // 写入和删除统计
    std::cout << "[Puts]     " << stats.puts << std::endl;
    std::cout << "[Removes]  " << stats.removes << std::endl;
    std::cout << "[Evictions] " << stats.evictions << std::endl;
    std::cout << "[Expired]  " << stats.expired << std::endl;
    
    // 时间统计
    std::cout << "[Uptime]   " << std::fixed << std::setprecision(1) 
              << stats.uptime_seconds() << " seconds" << std::endl;
    std::cout << "[AvgQPS]   " << std::fixed << std::setprecision(1) 
              << stats.avg_qps() << " queries/sec" << std::endl;
    
    std::cout << "============================================================\n" << std::endl;
}

// ==================== 缓存失效接口实现 ====================

void MysqlDao::InvalidateUserCacheMultiple(const std::vector<int>& uids)
{
    for (int uid : uids) {
        userCache_.remove(uid);
    }
    std::cout << "[FlashCache] Invalidated " << uids.size() << " users from cache" << std::endl;
}

void MysqlDao::ClearUserCacheAll()
{
    auto stats = GetUserCacheStats();
    size_t cleared_count = stats.current_size;
    
    // 清空所有缓存数据
    userCache_.clear();
    
    std::cout << "[FlashCache] Cleared all " << cleared_count << " users from cache" << std::endl;
}

// ==================== 多机缓存同步实现 ====================

void MysqlDao::StartCacheSync()
{
    if (cacheSync_) {
        std::cout << "[FlashCache] Cache sync already started" << std::endl;
        return;
    }
    
    cacheSync_ = std::make_unique<CacheInvalidationSync>("cache:user:invalidation");
    
    // 设置回调函数
    cacheSync_->SetInvalidateCallback([this](int uid) {
        InvalidateUserCache(uid);
    });
    
    cacheSync_->SetInvalidateBatchCallback([this](const std::vector<int>& uids) {
        InvalidateUserCacheMultiple(uids);
    });
    
    cacheSync_->SetClearAllCallback([this]() {
        ClearUserCacheAll();
    });
    
    if (cacheSync_->Start()) {
        std::cout << "[FlashCache] Cache sync started, channel: " 
                  << cacheSync_->GetChannel() << std::endl;
    } else {
        std::cout << "[FlashCache] Failed to start cache sync" << std::endl;
        cacheSync_.reset();
    }
}

void MysqlDao::StopCacheSync()
{
    if (cacheSync_) {
        cacheSync_->Stop();
        cacheSync_.reset();
        std::cout << "[FlashCache] Cache sync stopped" << std::endl;
    }
}

void MysqlDao::PublishCacheInvalidation(int uid)
{
    // 先失效本地缓存
    InvalidateUserCache(uid);
    
    // 发布到 Redis，通知其他机器
    if (cacheSync_) {
        cacheSync_->PublishInvalidation(uid);
    }
}

void MysqlDao::PublishCacheInvalidationBatch(const std::vector<int>& uids)
{
    // 先失效本地缓存
    InvalidateUserCacheMultiple(uids);
    
    // 发布到 Redis，通知其他机器
    if (cacheSync_) {
        cacheSync_->PublishInvalidationBatch(uids);
    }
}

void MysqlDao::PublishCacheClearAll()
{
    // 先清空本地缓存
    ClearUserCacheAll();
    
    // 发布到 Redis，通知其他机器
    if (cacheSync_) {
        cacheSync_->PublishClearAll();
    }
}

// ==================== 缓存预热实现 ====================

MysqlDao::WarmupResult MysqlDao::WarmupCache(size_t limit)
{
    WarmupResult result;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "[FlashCache] Starting cache warmup, limit: " << limit << std::endl;
    
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cout << "[FlashCache] Warmup failed: cannot get database connection" << std::endl;
        return result;
    }
    
    try {
        sql::Connection* con = guard.get();
        
        // 查询最近活跃的用户（按 uid 降序，假设新用户更活跃）
        // 实际项目中可以根据登录时间、活跃度等字段排序
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->prepareStatement(
                "SELECT uid, name, email, nick, `desc`, sex, icon, pwd "
                "FROM user ORDER BY uid DESC LIMIT ?"
            )
        );
        pstmt->setInt(1, static_cast<int>(limit));
        
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        
        while (res->next()) {
            result.total_users++;
            
            try {
                UserInfo user;
                user.uid = res->getInt("uid");
                user.name = res->getString("name");
                user.email = res->getString("email");
                user.nick = res->getString("nick");
                user.desc = res->getString("desc");
                user.sex = res->getInt("sex");
                user.icon = res->getString("icon");
                user.pwd = res->getString("pwd");
                
                // 写入缓存（TTL 5 分钟）
                userCache_.put(user.uid, user, 300000);
                result.loaded_users++;
                
                // 每 100 个用户输出一次进度
                if (result.loaded_users % 100 == 0) {
                    std::cout << "[FlashCache] Warmup progress: " 
                              << result.loaded_users << "/" << result.total_users << std::endl;
                }
            }
            catch (const std::exception& e) {
                result.failed_users++;
                std::cerr << "[FlashCache] Warmup error for user: " << e.what() << std::endl;
            }
        }
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[FlashCache] Warmup SQL error: " << e.what() << std::endl;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    std::cout << "[FlashCache] Warmup completed!" << std::endl;
    std::cout << "  - Total users: " << result.total_users << std::endl;
    std::cout << "  - Loaded: " << result.loaded_users << std::endl;
    std::cout << "  - Failed: " << result.failed_users << std::endl;
    std::cout << "  - Success rate: " << std::fixed << std::setprecision(1) 
              << result.success_rate() << "%" << std::endl;
    std::cout << "  - Elapsed: " << std::fixed << std::setprecision(2) 
              << result.elapsed_ms << " ms" << std::endl;
    
    return result;
}

MysqlDao::WarmupResult MysqlDao::WarmupCacheByUids(const std::vector<int>& uids)
{
    WarmupResult result;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (uids.empty()) {
        return result;
    }
    
    std::cout << "[FlashCache] Starting warmup for " << uids.size() << " specific users" << std::endl;
    
    ConnectionGuard guard(pool_);
    if (!guard) {
        std::cout << "[FlashCache] Warmup failed: cannot get database connection" << std::endl;
        return result;
    }
    
    try {
        sql::Connection* con = guard.get();
        
        // 构建 IN 查询
        std::string placeholders;
        for (size_t i = 0; i < uids.size(); ++i) {
            if (i > 0) placeholders += ",";
            placeholders += "?";
        }
        
        std::string sql = "SELECT uid, name, email, nick, `desc`, sex, icon, pwd "
                          "FROM user WHERE uid IN (" + placeholders + ")";
        
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
        
        for (size_t i = 0; i < uids.size(); ++i) {
            pstmt->setInt(static_cast<int>(i + 1), uids[i]);
        }
        
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        result.total_users = uids.size();
        
        while (res->next()) {
            try {
                UserInfo user;
                user.uid = res->getInt("uid");
                user.name = res->getString("name");
                user.email = res->getString("email");
                user.nick = res->getString("nick");
                user.desc = res->getString("desc");
                user.sex = res->getInt("sex");
                user.icon = res->getString("icon");
                user.pwd = res->getString("pwd");
                
                userCache_.put(user.uid, user, 300000);
                result.loaded_users++;
            }
            catch (const std::exception& e) {
                result.failed_users++;
            }
        }
        
        // 未找到的用户也算失败
        result.failed_users = result.total_users - result.loaded_users;
    }
    catch (sql::SQLException& e) {
        guard.markBad();
        std::cerr << "[FlashCache] Warmup SQL error: " << e.what() << std::endl;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    std::cout << "[FlashCache] Warmup by UIDs completed: " 
              << result.loaded_users << "/" << result.total_users 
              << " (" << std::fixed << std::setprecision(1) << result.success_rate() << "%)"
              << " in " << std::fixed << std::setprecision(2) << result.elapsed_ms << " ms" << std::endl;
    
    return result;
}

void MysqlDao::WarmupCacheAsync(size_t limit)
{
    std::thread warmup_thread([this, limit]() {
        WarmupCache(limit);
    });
    warmup_thread.detach();
    
    std::cout << "[FlashCache] Async warmup started in background thread" << std::endl;
}