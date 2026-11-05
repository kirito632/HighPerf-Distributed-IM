#pragma once

#include "JwtSigner.h"
#include "JwtTypes.h"
#include "RefreshTokenStore.h"

#include <memory>
#include <string>

namespace im::auth {

struct TokenPair {
    int uid = 0;
    std::string access_token;
    std::string refresh_token;
    std::string scope;
    int expires_in = 0;
    int refresh_expires_in = 0;
};

struct TokenResult {
    bool ok = false;
    std::string error;
    TokenPair tokens;
};

class TokenService {
public:
    TokenService(JwtConfig jwt_config,
                 int refresh_ttl_seconds,
                 std::string default_scope,
                 std::shared_ptr<IRefreshTokenStore> store);

    TokenResult Issue(int uid, const std::string& scope = "");
    TokenResult Refresh(const std::string& refresh_token);
    bool Revoke(const std::string& refresh_token, std::string& error);

private:
    TokenPair SignPair(int uid, const std::string& scope);

    JwtConfig jwt_config_;
    int refresh_ttl_seconds_;
    std::string default_scope_;
    std::shared_ptr<IRefreshTokenStore> store_;
    JwtSigner signer_;
};

}  // namespace im::auth
