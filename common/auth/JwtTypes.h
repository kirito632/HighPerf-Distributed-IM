#pragma once

#include <chrono>
#include <string>

namespace im::auth {

struct JwtConfig {
    std::string issuer = "im-gate";
    std::string audience = "im-backend";
    std::string key_id = "im-key-1";
    std::string private_key_pem;
    std::string public_key_pem;
    int access_ttl_seconds = 900;
    int clock_skew_seconds = 60;
};

struct JwtClaims {
    int uid = 0;
    std::string scope;
    std::string jti;
    std::chrono::system_clock::time_point issued_at{};
    std::chrono::system_clock::time_point expires_at{};
};

struct VerifiedJwt {
    JwtClaims claims;
};

}  // namespace im::auth
