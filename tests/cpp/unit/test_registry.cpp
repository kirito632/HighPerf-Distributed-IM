#include "EtcdRegistry.h"

#include <gtest/gtest.h>

#include <string>

TEST(RegistryTest, Base64RoundTrip) {
    const std::string values[] = {
        "", "a", "ab", "abc", "im/services/chatserver/chatserver1",
        std::string("\x00\x01\xff", 3),
    };
    for (const auto& value : values) {
        EXPECT_EQ(im::registry::Base64Decode(im::registry::Base64Encode(value)),
                  value);
    }
    EXPECT_EQ(im::registry::Base64Encode("a"), "YQ==");
    EXPECT_EQ(im::registry::Base64Encode("ab"), "YWI=");
    EXPECT_EQ(im::registry::Base64Encode("abc"), "YWJj");
    EXPECT_EQ(im::registry::Base64Decode("YWJj"), "abc");
}

TEST(RegistryTest, ServiceKeySanitizesInstanceId) {
    EXPECT_EQ(im::registry::ServiceKey("chatserver", "chatserver1"),
              "im/services/chatserver/chatserver1");
    EXPECT_EQ(im::registry::ServiceKey("chatserver", "bad/id"),
              "im/services/chatserver/bad-id");
}

TEST(RegistryTest, InstanceJsonRoundTrip) {
    im::registry::ServiceInstance input;
    input.id = "chatserver1";
    input.host = "127.0.0.1";
    input.port = 18090;

    im::registry::ServiceInstance output;
    ASSERT_TRUE(
        im::registry::ServiceInstance::FromJson(input.ToJson(), output));
    EXPECT_EQ(output.id, input.id);
    EXPECT_EQ(output.host, input.host);
    EXPECT_EQ(output.port, input.port);
}

TEST(RegistryTest, RejectsMalformedInstanceJson) {
    im::registry::ServiceInstance output;
    EXPECT_FALSE(im::registry::ServiceInstance::FromJson("not-json", output));
    EXPECT_FALSE(im::registry::ServiceInstance::FromJson("{\"id\":\"x\"}", output));
    EXPECT_FALSE(im::registry::ServiceInstance::FromJson(
        "{\"id\":\"x\",\"host\":\"127.0.0.1\",\"port\":0}", output));
}
