#include "JwtVerifier.h"

#include <jwt-cpp/jwt.h>

#include <chrono>
#include <stdexcept>

namespace im::auth {

JwtVerifier::JwtVerifier(JwtConfig config) : config_(std::move(config)) {
    if (config_.issuer.empty()) {
        throw std::invalid_argument("JWT issuer must not be empty");
    }
    if (config_.audience.empty()) {
        throw std::invalid_argument("JWT audience must not be empty");
    }
    if (config_.public_key_pem.empty()) {
        throw std::invalid_argument("JWT public key must not be empty");
    }
    if (config_.clock_skew_seconds < 0) {
        throw std::invalid_argument("JWT clock skew must not be negative");
    }
}

bool JwtVerifier::Verify(const std::string& token, VerifiedJwt& out, std::string& error) const {
    error.clear();
    try {
        auto decoded = jwt::decode(token);

        if (!decoded.has_header_claim("kid")) {
            error = "missing kid header";
            return false;
        }
        if (!config_.key_id.empty() && decoded.get_key_id() != config_.key_id) {
            error = "unexpected kid";
            return false;
        }

        auto verifier = jwt::verify()
                            .allow_algorithm(jwt::algorithm::rs256(config_.public_key_pem))
                            .with_issuer(config_.issuer)
                            .with_audience(config_.audience)
                            .leeway(static_cast<size_t>(config_.clock_skew_seconds));
        verifier.verify(decoded);

        if (!decoded.has_subject()) {
            error = "missing sub claim";
            return false;
        }

        int uid = 0;
        try {
            uid = std::stoi(decoded.get_subject());
        } catch (const std::exception&) {
            error = "sub claim is not a valid uid";
            return false;
        }
        if (uid <= 0) {
            error = "sub claim must be a positive uid";
            return false;
        }

        out.claims = JwtClaims{};
        out.claims.uid = uid;
        if (decoded.has_payload_claim("scope")) {
            out.claims.scope = decoded.get_payload_claim("scope").as_string();
        }
        if (decoded.has_payload_claim("jti")) {
            out.claims.jti = decoded.get_payload_claim("jti").as_string();
        }
        out.claims.issued_at = decoded.get_issued_at();
        out.claims.expires_at = decoded.get_expires_at();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

}  // namespace im::auth
