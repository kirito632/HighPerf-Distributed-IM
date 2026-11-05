#include <gtest/gtest.h>

#include <chrono>
#include <ctime>
#include <map>
#include <string>

#include "JwtKeys.h"
#include "JwtVerifier.h"
#include "TokenService.h"

namespace {

using namespace std::chrono_literals;

class InMemoryRefreshTokenStore : public im::auth::IRefreshTokenStore {
public:
    bool Create(int uid,
                const std::string& token_hash,
                const std::string& family_id,
                std::time_t expires_at,
                std::string& error) override {
        error.clear();
        if (records_.count(token_hash) != 0) {
            error = "duplicate refresh token hash";
            return false;
        }
        records_[token_hash] = {uid, family_id, expires_at, false, ""};
        return true;
    }

    im::auth::RotateResult Rotate(const std::string& old_hash,
                                  const std::string& new_hash,
                                  std::time_t new_expires_at,
                                  int& out_uid,
                                  std::string& out_family_id,
                                  std::string& error) override {
        error.clear();
        auto it = records_.find(old_hash);
        if (it == records_.end()) {
            return im::auth::RotateResult::kNotFound;
        }
        if (it->second.revoked) {
            out_family_id = it->second.family_id;
            return im::auth::RotateResult::kReused;
        }
        if (it->second.expires_at <= std::time(nullptr)) {
            return im::auth::RotateResult::kExpired;
        }
        if (records_.count(new_hash) != 0) {
            error = "duplicate new refresh token hash";
            return im::auth::RotateResult::kError;
        }

        it->second.revoked = true;
        it->second.replaced_by_hash = new_hash;
        out_uid = it->second.uid;
        out_family_id = it->second.family_id;
        records_[new_hash] = {out_uid, out_family_id, new_expires_at, false, ""};
        return im::auth::RotateResult::kOk;
    }

    bool Find(const std::string& token_hash,
              im::auth::RefreshTokenRecord& out,
              std::string& error) override {
        error.clear();
        auto it = records_.find(token_hash);
        if (it == records_.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    bool RevokeFamily(const std::string& family_id, std::string& error) override {
        error.clear();
        for (auto& entry : records_) {
            if (entry.second.family_id == family_id) {
                entry.second.revoked = true;
            }
        }
        return true;
    }

    const im::auth::RefreshTokenRecord* Get(const std::string& hash) const {
        auto it = records_.find(hash);
        return it == records_.end() ? nullptr : &it->second;
    }

private:
    std::map<std::string, im::auth::RefreshTokenRecord> records_;
};

im::auth::JwtConfig MakeConfig() {
    im::auth::JwtConfig config;
    config.issuer = "im-test";
    config.audience = "im-test-api";
    config.key_id = "test-key-1";
    config.private_key_pem = im::auth::ReadPemFile(std::string(JWT_TEST_KEY_DIR) + "/private.pem");
    config.public_key_pem = im::auth::ReadPemFile(std::string(JWT_TEST_KEY_DIR) + "/public.pem");
    config.access_ttl_seconds = 900;
    config.clock_skew_seconds = 0;
    return config;
}

struct Fixture {
    std::shared_ptr<InMemoryRefreshTokenStore> store =
        std::make_shared<InMemoryRefreshTokenStore>();
    std::unique_ptr<im::auth::TokenService> service;
    im::auth::JwtVerifier verifier{MakeConfig()};

    Fixture() {
        service = std::make_unique<im::auth::TokenService>(
            MakeConfig(), 604800, "im.friend im.message", store);
    }
};

}  // namespace

TEST(TokenServiceTest, IssueCreatesVerifiableAccessAndRefreshToken) {
    Fixture fixture;
    const auto result = fixture.service->Issue(1001, "im.friend");

    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_FALSE(result.tokens.access_token.empty());
    ASSERT_FALSE(result.tokens.refresh_token.empty());
    EXPECT_EQ(result.tokens.expires_in, 900);
    EXPECT_EQ(result.tokens.refresh_expires_in, 604800);

    im::auth::VerifiedJwt verified;
    std::string error;
    ASSERT_TRUE(fixture.verifier.Verify(result.tokens.access_token, verified, error)) << error;
    EXPECT_EQ(verified.claims.uid, 1001);
    EXPECT_EQ(verified.claims.scope, "im.friend");
}

TEST(TokenServiceTest, RefreshRotatesAndKeepsUid) {
    Fixture fixture;
    const auto issued = fixture.service->Issue(1001, "im.message");
    ASSERT_TRUE(issued.ok) << issued.error;

    const auto refreshed = fixture.service->Refresh(issued.tokens.refresh_token);
    ASSERT_TRUE(refreshed.ok) << refreshed.error;
    EXPECT_EQ(refreshed.tokens.uid, 1001);
    EXPECT_NE(refreshed.tokens.refresh_token, issued.tokens.refresh_token);

    im::auth::VerifiedJwt verified;
    std::string error;
    ASSERT_TRUE(fixture.verifier.Verify(refreshed.tokens.access_token, verified, error)) << error;
    EXPECT_EQ(verified.claims.uid, 1001);
}

TEST(TokenServiceTest, ReusingOldRefreshTokenRevokesFamily) {
    Fixture fixture;
    const auto issued = fixture.service->Issue(1001, "im.message");
    ASSERT_TRUE(issued.ok) << issued.error;

    const auto refreshed = fixture.service->Refresh(issued.tokens.refresh_token);
    ASSERT_TRUE(refreshed.ok) << refreshed.error;

    const auto replay = fixture.service->Refresh(issued.tokens.refresh_token);
    EXPECT_FALSE(replay.ok);
    EXPECT_NE(replay.error.find("reuse"), std::string::npos);

    // The rotated token must also be invalid after family revocation.
    const auto after_reuse = fixture.service->Refresh(refreshed.tokens.refresh_token);
    EXPECT_FALSE(after_reuse.ok);
}

TEST(TokenServiceTest, RevokeInvalidatesRefreshFamily) {
    Fixture fixture;
    const auto issued = fixture.service->Issue(1001, "im.message");
    ASSERT_TRUE(issued.ok) << issued.error;

    std::string error;
    ASSERT_TRUE(fixture.service->Revoke(issued.tokens.refresh_token, error)) << error;

    const auto refreshed = fixture.service->Refresh(issued.tokens.refresh_token);
    EXPECT_FALSE(refreshed.ok);
}

TEST(TokenServiceTest, RevokeIsIdempotentForUnknownToken) {
    Fixture fixture;
    std::string error;
    EXPECT_TRUE(fixture.service->Revoke("unknown-token", error));
    EXPECT_TRUE(error.empty());
}

TEST(TokenServiceTest, RejectsInvalidUid) {
    Fixture fixture;
    const auto result = fixture.service->Issue(0);
    EXPECT_FALSE(result.ok);
}
