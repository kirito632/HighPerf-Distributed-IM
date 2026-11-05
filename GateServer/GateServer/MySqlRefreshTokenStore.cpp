#include "MySqlRefreshTokenStore.h"

#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <cstdint>
#include <ctime>
#include <stdexcept>
#include <iostream>

namespace im::auth {

MySqlRefreshTokenStore::MySqlRefreshTokenStore(std::shared_ptr<MySqlPool> pool)
    : pool_(std::move(pool)) {
    if (!pool_) {
        throw std::invalid_argument("MySqlRefreshTokenStore requires a pool");
    }
}

bool MySqlRefreshTokenStore::Create(int uid,
                                    const std::string& token_hash,
                                    const std::string& family_id,
                                    std::time_t expires_at,
                                    std::string& error) {
    error.clear();
    auto con = pool_->getConnection();
    if (!con) {
        error = "no MySQL connection";
        return false;
    }
    Defer guard([this, &con]() {
        con->setAutoCommit(true);
        pool_->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> stmt(con->prepareStatement(
            "INSERT INTO refresh_tokens "
            "(uid, token_hash, family_id, expires_at, revoked) "
            "VALUES (?, ?, ?, FROM_UNIXTIME(?), 0)"));
        stmt->setInt(1, uid);
        stmt->setString(2, token_hash);
        stmt->setString(3, family_id);
        stmt->setInt64(4, static_cast<int64_t>(expires_at));
        stmt->executeUpdate();
        return true;
    } catch (const sql::SQLException& e) {
        error = e.what();
        return false;
    }
}

RotateResult MySqlRefreshTokenStore::Rotate(const std::string& old_hash,
                                            const std::string& new_hash,
                                            std::time_t new_expires_at,
                                            int& out_uid,
                                            std::string& out_family_id,
                                            std::string& error) {
    error.clear();
    auto con = pool_->getConnection();
    if (!con) {
        error = "no MySQL connection";
        return RotateResult::kError;
    }
    Defer guard([this, &con]() {
        con->setAutoCommit(true);
        pool_->returnConnection(std::move(con));
    });

    try {
        con->setAutoCommit(false);

        std::unique_ptr<sql::PreparedStatement> select(con->prepareStatement(
            "SELECT uid, family_id, UNIX_TIMESTAMP(expires_at) AS expires_unix, "
            "revoked FROM refresh_tokens WHERE token_hash = ? FOR UPDATE"));
        select->setString(1, old_hash);
        std::unique_ptr<sql::ResultSet> result(select->executeQuery());
        if (!result->next()) {
            con->rollback();
            return RotateResult::kNotFound;
        }

        const int uid = result->getInt("uid");
        const std::string family_id = result->getString("family_id");
        const auto expires_unix = result->getInt64("expires_unix");
        const bool revoked = result->getInt("revoked") != 0;

        out_uid = uid;
        out_family_id = family_id;

        if (revoked) {
            con->rollback();
            return RotateResult::kReused;
        }
        if (expires_unix <= static_cast<int64_t>(std::time(nullptr))) {
            con->rollback();
            return RotateResult::kExpired;
        }

        std::unique_ptr<sql::PreparedStatement> revoke(con->prepareStatement(
            "UPDATE refresh_tokens SET revoked = 1, replaced_by_hash = ? "
            "WHERE token_hash = ?"));
        revoke->setString(1, new_hash);
        revoke->setString(2, old_hash);
        revoke->executeUpdate();

        std::unique_ptr<sql::PreparedStatement> insert(con->prepareStatement(
            "INSERT INTO refresh_tokens "
            "(uid, token_hash, family_id, expires_at, revoked) "
            "VALUES (?, ?, ?, FROM_UNIXTIME(?), 0)"));
        insert->setInt(1, uid);
        insert->setString(2, new_hash);
        insert->setString(3, family_id);
        insert->setInt64(4, static_cast<int64_t>(new_expires_at));
        insert->executeUpdate();

        con->commit();
        return RotateResult::kOk;
    } catch (const sql::SQLException& e) {
        try {
            con->rollback();
        } catch (...) {
        }
        error = e.what();
        return RotateResult::kError;
    }
}

bool MySqlRefreshTokenStore::Find(const std::string& token_hash,
                                  RefreshTokenRecord& out,
                                  std::string& error) {
    error.clear();
    auto con = pool_->getConnection();
    if (!con) {
        error = "no MySQL connection";
        return false;
    }
    Defer guard([this, &con]() {
        con->setAutoCommit(true);
        pool_->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> stmt(con->prepareStatement(
            "SELECT uid, family_id, UNIX_TIMESTAMP(expires_at) AS expires_unix, "
            "revoked, replaced_by_hash FROM refresh_tokens WHERE token_hash = ?"));
        stmt->setString(1, token_hash);
        std::unique_ptr<sql::ResultSet> result(stmt->executeQuery());
        if (!result->next()) {
            return false;
        }

        out.uid = result->getInt("uid");
        out.family_id = result->getString("family_id");
        out.expires_at = static_cast<std::time_t>(result->getInt64("expires_unix"));
        out.revoked = result->getInt("revoked") != 0;
        if (!result->isNull("replaced_by_hash")) {
            out.replaced_by_hash = result->getString("replaced_by_hash");
        }
        return true;
    } catch (const sql::SQLException& e) {
        error = e.what();
        return false;
    }
}

bool MySqlRefreshTokenStore::RevokeFamily(const std::string& family_id, std::string& error) {
    error.clear();
    auto con = pool_->getConnection();
    if (!con) {
        error = "no MySQL connection";
        return false;
    }
    Defer guard([this, &con]() {
        con->setAutoCommit(true);
        pool_->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> stmt(con->prepareStatement(
            "UPDATE refresh_tokens SET revoked = 1 WHERE family_id = ?"));
        stmt->setString(1, family_id);
        stmt->executeUpdate();
        return true;
    } catch (const sql::SQLException& e) {
        error = e.what();
        return false;
    }
}

}  // namespace im::auth

