#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "JwtKeys.h"
#include "JwtSigner.h"
#include "JwtVerifier.h"

namespace {

using namespace std::chrono_literals;

std::string KeyDir() {
    return std::string(JWT_TEST_KEY_DIR);
}

im::auth::JwtConfig MakeConfig() {
    im::auth::JwtConfig config;
    config.issuer = "im-test";
    config.audience = "im-test-api";
    config.key_id = "test-key-1";
    config.private_key_pem = im::auth::ReadPemFile(KeyDir() + "/private.pem");
    config.public_key_pem = im::auth::ReadPemFile(KeyDir() + "/public.pem");
    config.access_ttl_seconds = 900;
    config.clock_skew_seconds = 0;
    return config;
}

im::auth::JwtClaims MakeClaims(int uid = 1001) {
    const auto now = std::chrono::system_clock::now();
    im::auth::JwtClaims claims;
    claims.uid = uid;
    claims.scope = "im.friend im.message";
    claims.jti = "jti-test-1";
    claims.issued_at = now;
    claims.expires_at = now + 15min;
    return claims;
}

TEST(JwtSignerTest, SignsAndRoundTripsClaims) {
    const auto config = MakeConfig();
    im::auth::JwtSigner signer(config);
    im::auth::JwtVerifier verifier(config);

    const std::string token = signer.SignAccessToken(MakeClaims());
    im::auth::VerifiedJwt verified;
    std::string error;

    ASSERT_TRUE(verifier.Verify(token, verified, error)) << error;
    EXPECT_EQ(verified.claims.uid, 1001);
    EXPECT_EQ(verified.claims.scope, "im.friend im.message");
    EXPECT_EQ(verified.claims.jti, "jti-test-1");
    EXPECT_GT(verified.claims.expires_at, std::chrono::system_clock::now());
}

TEST(JwtSignerTest, GeneratesJtiWhenMissing) {
    const auto config = MakeConfig();
    im::auth::JwtSigner signer(config);
    im::auth::JwtVerifier verifier(config);

    auto claims = MakeClaims();
    claims.jti.clear();
    const std::string token = signer.SignAccessToken(claims);

    im::auth::VerifiedJwt verified;
    std::string error;
    ASSERT_TRUE(verifier.Verify(token, verified, error)) << error;
    EXPECT_FALSE(verified.claims.jti.empty());
}

TEST(JwtSignerTest, RejectsInvalidUid) {
    im::auth::JwtSigner signer(MakeConfig());
    auto claims = MakeClaims();
    claims.uid = 0;
    EXPECT_THROW(signer.SignAccessToken(claims), std::invalid_argument);
}

TEST(JwtVerifierTest, RejectsWrongIssuer) {
    auto config = MakeConfig();
    im::auth::JwtSigner signer(config);

    auto other = config;
    other.issuer = "evil-issuer";
    im::auth::JwtSigner evil_signer(other);
    const std::string token = evil_signer.SignAccessToken(MakeClaims());

    im::auth::JwtVerifier verifier(config);
    im::auth::VerifiedJwt verified;
    std::string error;
    EXPECT_FALSE(verifier.Verify(token, verified, error));
    EXPECT_FALSE(error.empty());
}

TEST(JwtVerifierTest, RejectsWrongAudience) {
    auto config = MakeConfig();
    im::auth::JwtSigner signer(config);

    auto other = config;
    other.audience = "other-api";
    im::auth::JwtSigner other_signer(other);
    const std::string token = other_signer.SignAccessToken(MakeClaims());

    im::auth::JwtVerifier verifier(config);
    im::auth::VerifiedJwt verified;
    std::string error;
    EXPECT_FALSE(verifier.Verify(token, verified, error));
}

TEST(JwtVerifierTest, RejectsWrongKid) {
    auto config = MakeConfig();
    im::auth::JwtSigner signer(config);

    auto other = config;
    other.key_id = "other-key";
    im::auth::JwtSigner other_signer(other);
    const std::string token = other_signer.SignAccessToken(MakeClaims());

    im::auth::JwtVerifier verifier(config);
    im::auth::VerifiedJwt verified;
    std::string error;
    EXPECT_FALSE(verifier.Verify(token, verified, error));
}

TEST(JwtVerifierTest, RejectsExpiredToken) {
    const auto config = MakeConfig();
    im::auth::JwtSigner signer(config);
    im::auth::JwtVerifier verifier(config);

    const auto now = std::chrono::system_clock::now();
    auto claims = MakeClaims();
    claims.issued_at = now - 2h;
    claims.expires_at = now - 1h;
    const std::string token = signer.SignAccessToken(claims);

    im::auth::VerifiedJwt verified;
    std::string error;
    EXPECT_FALSE(verifier.Verify(token, verified, error));
}

TEST(JwtVerifierTest, RejectsTamperedSignature) {
    const auto config = MakeConfig();
    im::auth::JwtSigner signer(config);
    im::auth::JwtVerifier verifier(config);

    std::string token = signer.SignAccessToken(MakeClaims());
    ASSERT_FALSE(token.empty());

    // Tamper a character in the middle of the signature. Changing the final
    // base64 character can be a no-op because of padding bits, so use the
    // signature segment's midpoint instead.
    const auto signature_start = token.rfind('.');
    ASSERT_NE(signature_start, std::string::npos);
    const auto signature_begin = signature_start + 1;
    ASSERT_LT(signature_begin, token.size());
    size_t index = signature_begin + (token.size() - signature_begin) / 2;
    while (index > signature_begin && token[index] == '=') {
        --index;
    }
    token[index] = token[index] == 'A' ? 'B' : 'A';

    im::auth::VerifiedJwt verified;
    std::string error;
    EXPECT_FALSE(verifier.Verify(token, verified, error));
}

TEST(JwtVerifierTest, RejectsGarbageToken) {
    im::auth::JwtVerifier verifier(MakeConfig());
    im::auth::VerifiedJwt verified;
    std::string error;
    EXPECT_FALSE(verifier.Verify("not-a-jwt", verified, error));
    EXPECT_FALSE(verifier.Verify("", verified, error));
}

}  // namespace
