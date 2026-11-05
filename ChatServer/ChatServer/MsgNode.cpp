#include "MsgNode.h"

// RecvNode构造函数：初始化接收消息节点
// 参数：
//   - max_len: 消息最大长度
//   - msg_id: 消息ID
RecvNode::RecvNode(int max_len, short msg_id) :MsgNode(max_len),
_msg_id(msg_id) {

}

// SendNode构造函数：初始化发送消息节点
// 
// 消息格式：
//   [0-1]: 消息ID（网络字节序）
//   [2-3]: flags/version（当前为0）
//   [4-7]: 数据长度 uint32（网络字节序）
//   [8-]: 消息数据
SendNode::SendNode(const char* msg, int max_len, short msg_id) :MsgNode(max_len + HEAD_TOTAL_LEN)
, _msg_id(msg_id) {
    if (max_len < 0 || max_len > MAX_LENGTH) {
        std::cerr << "[SendNode] invalid body length=" << max_len
                  << " max=" << MAX_LENGTH << std::endl;
        return;
    }

    short msg_id_host = boost::asio::detail::socket_ops::host_to_network_short(msg_id);
    memcpy(_data, &msg_id_host, HEAD_ID_LEN);

    uint16_t flags = 0;
    memcpy(_data + HEAD_ID_LEN, &flags, HEAD_FLAGS_LEN);

    uint32_t max_len_host = boost::asio::detail::socket_ops::host_to_network_long(
        static_cast<uint32_t>(max_len));
    memcpy(_data + HEAD_ID_LEN + HEAD_FLAGS_LEN, &max_len_host, HEAD_DATA_LEN);

    memcpy(_data + HEAD_TOTAL_LEN, msg, static_cast<size_t>(max_len));
}
