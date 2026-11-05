#include "TokenService.h"

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace im::auth {
namespace {

std::string RandomHex(size_t bytes) {
    std::string buffer(bytes, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(buffer.data()),
                   static_cast<int>(buffer.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : buffer) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

std::string Sha256Hex(const std::string& value) {
    unsigned char digest[SHA256_DIGEST_LENGTH]{};
    SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest);

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : digest) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

}  // namespace

TokenService::TokenService(JwtConfig jwt_config,
                           int refresh_ttl_seconds,
                           std::string default_scope,
                           std::shared_ptr<IRefreshTokenStore> store)
    : jwt_config_(std::move(jwt_config)),
      refresh_ttl_seconds_(refresh_ttl_seconds),
      default_scope_(std::move(default_scope)),
      store_(std::move(store)),
      signer_(jwt_config_) {
    if (refresh_ttl_seconds_ <= 0) {
        throw std::invalid_argument("refresh TTL must be positive");
    }
    if (!store_) {
        throw std::invalid_argument("refresh token store must not be null");
    }
}

TokenPair TokenService::SignPair(int uid, const std::string& scope) {
    const auto now = std::chrono::system_clock::now();
    JwtClaims claims;
    claims.uid = uid;
    claims.scope = scope.empty() ? default_scope_ : scope;
    claims.jti = RandomHex(16);
    claims.issued_at = now;
    claims.expires_at = now + std::chrono::seconds(jwt_config_.access_ttl_seconds);

    TokenPair pair;
    pair.uid = uid;
    pair.access_token = signer_.SignAccessToken(claims);
    pair.scope = claims.scope;
    pair.expires_in = jwt_config_.access_ttl_seconds;
    return pair;
}

TokenResult TokenService::Issue(int uid, const std::string& scope) {
    if (uid <= 0) {
        return {false, "uid must be positive", {}};
    }

    try {
        const std::string refresh_token = RandomHex(32);
        const std::string refresh_hash = Sha256Hex(refresh_token);
        const std::string family_id = RandomHex(16);
        const std::time_t expires_at = std::time(nullptr) + refresh_ttl_seconds_;

        std::string error;
        if (!store_->Create(uid, refresh_hash, family_id, expires_at, error)) {
            return {false, error.empty() ? "failed to store refresh token" : error, {}};
        }

        TokenPair pair = SignPair(uid, scope);
        pair.refresh_token = refresh_token;
        pair.refresh_expires_in = refresh_ttl_seconds_;
        return {true, "", pair};
    } catch (const std::exception& e) {
        return {false, e.what(), {}};
    }
}

TokenResult TokenService::Refresh(const std::string& refresh_token) {
    if (refresh_token.empty()) {
        return {false, "refresh token is empty", {}};
    }

    try {
        const std::string old_hash = Sha256Hex(refresh_token);
        const std::string new_refresh_token = RandomHex(32);
        const std::string new_hash = Sha256Hex(new_refresh_token);
        const std::time_t new_expires_at = std::time(nullptr) + refresh_ttl_seconds_;

        int uid = 0;
        std::string family_id;
        std::string error;
        const RotateResult result = store_->Rotate(
            old_hash, new_hash, new_expires_at, uid, family_id, error);

        if (result == RotateResult::kReused) {
            // A revoked/replaced token was replayed: revoke the whole family.
            std::string revoke_error;
            if (!family_id.empty()) {
                store_->RevokeFamily(family_id, revoke_error);
            }
            return {false, "refresh token reuse detected", {}};
        }
        if (result == RotateResult::kExpired) {
            return {false, "refresh token expired", {}};
        }
        if (result == RotateResult::kNotFound) {
            return {false, "refresh token not found", {}};
        }
        if (result != RotateResult::kOk) {
            return {false, error.empty() ? "failed to rotate refresh token" : error, {}};
        }

        TokenPair pair = SignPair(uid, default_scope_);
        pair.refresh_token = new_refresh_token;
        pair.refresh_expires_in = refresh_ttl_seconds_;
        return {true, "", pair};
    } catch (const std::exception& e) {
        return {false, e.what(), {}};
    }
}

bool TokenService::Revoke(const std::string& refresh_token, std::string& error) {
    if (refresh_token.empty()) {
        error.clear();
        return true;  // OAuth2 revoke is idempotent.
    }

    const std::string hash = Sha256Hex(refresh_token);
    RefreshTokenRecord record;
    if (!store_->Find(hash, record, error)) {
        return error.empty();
    }
    return store_->RevokeFamily(record.family_id, error);
}

}  // namespace im::auth
