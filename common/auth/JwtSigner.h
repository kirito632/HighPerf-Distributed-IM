#pragma once

#include "JwtTypes.h"

#include <string>

namespace im::auth {

class JwtSigner {
public:
    explicit JwtSigner(JwtConfig config);

    // Signs an access token. Empty issued_at/expires_at are filled from now
    // and access_ttl_seconds. Throws std::invalid_argument / std::runtime_error
    // for invalid claims or key material.
    std::string SignAccessToken(const JwtClaims& claims) const;

    const JwtConfig& config() const { return config_; }

private:
    JwtConfig config_;
};

}  // namespace im::auth
