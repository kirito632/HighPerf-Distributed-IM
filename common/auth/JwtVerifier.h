#pragma once

#include "JwtTypes.h"

#include <string>

namespace im::auth {

class JwtVerifier {
public:
    explicit JwtVerifier(JwtConfig config);

    // Verifies signature, issuer, audience, expiry and kid. On failure returns
    // false and fills error with a non-sensitive diagnostic string.
    bool Verify(const std::string& token, VerifiedJwt& out, std::string& error) const;

    const JwtConfig& config() const { return config_; }

private:
    JwtConfig config_;
};

}  // namespace im::auth
