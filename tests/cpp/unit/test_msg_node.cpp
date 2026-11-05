#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cstring>
#include <string>

#include "MsgNode.h"

namespace {

uint16_t ReadNet16(const char* p) {
    uint16_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return ntohs(value);
}

uint32_t ReadNet32(const char* p) {
    uint32_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return ntohl(value);
}

}  // namespace

TEST(MsgNodeTest, AllocatesZeroedBufferAndTracksLength) {
    MsgNode node(10);

    EXPECT_EQ(node._total_len, 10);
    EXPECT_EQ(node._cur_len, 0);
    ASSERT_NE(node._data, nullptr);
    EXPECT_EQ(node._data[10], '\0');
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(node._data[i], '\0') << "index " << i;
    }
}

TEST(MsgNodeTest, ClearResetsContentAndLength) {
    MsgNode node(4);
    std::memcpy(node._data, "abc", 3);
    node._cur_len = 3;

    node.Clear();

    EXPECT_EQ(node._cur_len, 0);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(node._data[i], '\0') << "index " << i;
    }
}

TEST(SendNodeTest, WritesNetworkOrderHeaderAndBody) {
    SendNode node("hello", 5, 1005);

    ASSERT_EQ(node._total_len, 5 + HEAD_TOTAL_LEN);
    EXPECT_EQ(ReadNet16(node._data), 1005);
    EXPECT_EQ(ReadNet16(node._data + HEAD_ID_LEN), 0);
    EXPECT_EQ(ReadNet32(node._data + HEAD_ID_LEN + HEAD_FLAGS_LEN), 5);
    EXPECT_EQ(std::string(node._data + HEAD_TOTAL_LEN, 5), "hello");
}

TEST(SendNodeTest, PreservesEmbeddedNulBytes) {
    const char payload[] = {'a', '\0', 'b'};
    SendNode node(payload, 3, 1017);

    ASSERT_EQ(node._total_len, 3 + HEAD_TOTAL_LEN);
    EXPECT_EQ(node._data[HEAD_TOTAL_LEN + 0], 'a');
    EXPECT_EQ(node._data[HEAD_TOTAL_LEN + 1], '\0');
    EXPECT_EQ(node._data[HEAD_TOTAL_LEN + 2], 'b');
}

TEST(RecvNodeTest, KeepsMessageIdAndCapacity) {
    RecvNode node(8, 1019);

    EXPECT_EQ(node._total_len, 8);
    EXPECT_EQ(node._cur_len, 0);
    ASSERT_NE(node._data, nullptr);
    EXPECT_EQ(node._data[8], '\0');
}

TEST(MsgNodeTest, LargeButValidBodyStillAllocates) {
    // 64KB is the current protocol ceiling.
    MsgNode node(MAX_LENGTH);
    EXPECT_EQ(node._total_len, MAX_LENGTH);
    EXPECT_EQ(node._data[MAX_LENGTH], '\0');
}
