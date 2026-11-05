#pragma once

#include "MysqlDao.h"
#include "RefreshTokenStore.h"

#include <memory>

namespace im::auth {

class MySqlRefreshTokenStore : public IRefreshTokenStore {
public:
    explicit MySqlRefreshTokenStore(std::shared_ptr<MySqlPool> pool);

    bool Create(int uid,
                const std::string& token_hash,
                const std::string& family_id,
                std::time_t expires_at,
                std::string& error) override;

    RotateResult Rotate(const std::string& old_hash,
                        const std::string& new_hash,
                        std::time_t new_expires_at,
                        int& out_uid,
                        std::string& out_family_id,
                        std::string& error) override;

    bool Find(const std::string& token_hash,
              RefreshTokenRecord& out,
              std::string& error) override;

    bool RevokeFamily(const std::string& family_id, std::string& error) override;

private:
    std::shared_ptr<MySqlPool> pool_;
};

}  // namespace im::auth
