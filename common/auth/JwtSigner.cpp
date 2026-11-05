#include "JwtSigner.h"

#include <jwt-cpp/jwt.h>
#include <openssl/rand.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace im::auth {
namespace {

std::string RandomJti() {
    unsigned char bytes[16]{};
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("RAND_bytes failed while generating jti");
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

bool IsUnset(std::chrono::system_clock::time_point value) {
    return value.time_since_epoch().count() == 0;
}

}  // namespace

JwtSigner::JwtSigner(JwtConfig config) : config_(std::move(config)) {
    if (config_.issuer.empty()) {
        throw std::invalid_argument("JWT issuer must not be empty");
    }
    if (config_.audience.empty()) {
        throw std::invalid_argument("JWT audience must not be empty");
    }
    if (config_.private_key_pem.empty()) {
        throw std::invalid_argument("JWT private key must not be empty");
    }
    if (config_.public_key_pem.empty()) {
        throw std::invalid_argument("JWT public key must not be empty");
    }
    if (config_.access_ttl_seconds <= 0) {
        throw std::invalid_argument("JWT access TTL must be positive");
    }
}

std::string JwtSigner::SignAccessToken(const JwtClaims& claims) const {
    if (claims.uid <= 0) {
        throw std::invalid_argument("JWT uid must be positive");
    }

    const auto now = std::chrono::system_clock::now();
    const auto issued_at = IsUnset(claims.issued_at) ? now : claims.issued_at;
    const auto expires_at = IsUnset(claims.expires_at)
                                ? issued_at + std::chrono::seconds(config_.access_ttl_seconds)
                                : claims.expires_at;

    if (expires_at <= issued_at) {
        throw std::invalid_argument("JWT expires_at must be after issued_at");
    }

    const std::string jti = claims.jti.empty() ? RandomJti() : claims.jti;

    auto builder = jwt::create()
                       .set_type("JWT")
                       .set_issuer(config_.issuer)
                       .set_audience(config_.audience)
                       .set_subject(std::to_string(claims.uid))
                       .set_issued_at(issued_at)
                       .set_expires_at(expires_at)
                       .set_key_id(config_.key_id)
                       .set_payload_claim("scope", jwt::claim(claims.scope))
                       .set_payload_claim("jti", jwt::claim(jti));

    return builder.sign(
        jwt::algorithm::rs256(config_.public_key_pem, config_.private_key_pem));
}

}  // namespace im::auth
