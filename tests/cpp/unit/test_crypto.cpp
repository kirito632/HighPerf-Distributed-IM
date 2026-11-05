#include <gtest/gtest.h>

#include <string>

#include "crypto_utils.h"

TEST(CryptoUtilsTest, Sha256KnownVectorAbc) {
    EXPECT_EQ(sha256_hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(CryptoUtilsTest, Sha256KnownVectorEmpty) {
    EXPECT_EQ(sha256_hex(""),
              "e3b0c44298fc1c149afbf4c8996fb924"
              "27ae41e4649b934ca495991b7852b855");
}

TEST(CryptoUtilsTest, Sha256HandlesEmbeddedNul) {
    const std::string input("a\0b", 3);
    const std::string hash = sha256_hex(input);

    EXPECT_EQ(hash.size(), 64U);
    EXPECT_EQ(hash,
              "59b271ae1bbcb1d31d41929817f4b16f"
              "b439eb4f31520b5ad1d5ce98920a7138");
}

TEST(CryptoUtilsTest, Sha256IsDeterministic) {
    const std::string first = sha256_hex("same-input");
    const std::string second = sha256_hex("same-input");
    EXPECT_EQ(first, second);
    EXPECT_EQ(first.size(), 64U);
}
