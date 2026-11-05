#include <gtest/gtest.h>

#include <string>

#include "Jwks.h"
#include "JwtKeys.h"

TEST(JwksTest, BuildsRsaJwksWithKidAndModulus) {
    const std::string pem =
        im::auth::ReadPemFile(std::string(JWT_TEST_KEY_DIR) + "/public.pem");
    const std::string jwks = im::auth::BuildJwksJson(pem, "test-key-1");

    EXPECT_NE(jwks.find("\"keys\""), std::string::npos);
    EXPECT_NE(jwks.find("\"kty\":\"RSA\""), std::string::npos);
    EXPECT_NE(jwks.find("\"use\":\"sig\""), std::string::npos);
    EXPECT_NE(jwks.find("\"alg\":\"RS256\""), std::string::npos);
    EXPECT_NE(jwks.find("\"kid\":\"test-key-1\""), std::string::npos);
    EXPECT_NE(jwks.find("\"n\":\""), std::string::npos);
    EXPECT_NE(jwks.find("\"e\":\""), std::string::npos);
}

TEST(JwksTest, RejectsEmptyInput) {
    EXPECT_THROW(im::auth::BuildJwksJson("", "kid"), std::runtime_error);
    const std::string pem =
        im::auth::ReadPemFile(std::string(JWT_TEST_KEY_DIR) + "/public.pem");
    EXPECT_THROW(im::auth::BuildJwksJson(pem, ""), std::runtime_error);
}

TEST(JwksTest, RejectsMalformedPem) {
    EXPECT_THROW(im::auth::BuildJwksJson("not a pem", "kid"), std::runtime_error);
}
