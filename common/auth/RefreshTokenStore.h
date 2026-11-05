#pragma once

#include <ctime>
#include <string>

namespace im::auth {

struct RefreshTokenRecord {
    int uid = 0;
    std::string family_id;
    std::time_t expires_at = 0;
    bool revoked = false;
    std::string replaced_by_hash;
};

enum class RotateResult {
    kOk,
    kNotFound,
    kExpired,
    kReused,
    kError,
};

// Durable refresh-token store. Implementations must make Rotate atomic:
// the old token is revoked and the new token is inserted in one transaction.
class IRefreshTokenStore {
public:
    virtual ~IRefreshTokenStore() = default;

    virtual bool Create(int uid,
                        const std::string& token_hash,
                        const std::string& family_id,
                        std::time_t expires_at,
                        std::string& error) = 0;

    virtual RotateResult Rotate(const std::string& old_hash,
                                const std::string& new_hash,
                                std::time_t new_expires_at,
                                int& out_uid,
                                std::string& out_family_id,
                                std::string& error) = 0;

    virtual bool Find(const std::string& token_hash,
                      RefreshTokenRecord& out,
                      std::string& error) = 0;

    virtual bool RevokeFamily(const std::string& family_id, std::string& error) = 0;
};

}  // namespace im::auth
