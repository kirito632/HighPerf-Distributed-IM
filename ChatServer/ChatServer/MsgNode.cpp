#include "MsgNode.h"
#include"LogicSystem.h"

// RecvNode构造函数：初始化接收消息节点
// 参数：
//   - max_len: 消息最大长度
//   - msg_id: 消息ID
RecvNode::RecvNode(short max_len, short msg_id) :MsgNode(max_len),
_msg_id(msg_id) {

}

// SendNode构造函数：初始化发送消息节点
// 
// 参数：
//   - msg: 消息数据指针
//   - max_len: 消息数据长度
//   - msg_id: 消息ID
// 
// 实现逻辑：
//   1. 调用父类构造函数创建足够大的缓冲区（包含头部）
//   2. 将消息ID转换为网络字节序并写入头部
//   3. 将数据长度转换为网络字节序并写入头部
//   4. 将消息数据复制到缓冲区
// 
// 消息格式：
//   [0-1]: 消息ID（网络字节序）
//   [2-3]: 数据长度（网络字节序）
//   [4-]: 消息数据
SendNode::SendNode(const char* msg, short max_len, short msg_id) :MsgNode(max_len + HEAD_TOTAL_LEN)
, _msg_id(msg_id) {
    // 转换为id，转为网络字节序
    short msg_id_host = boost::asio::detail::socket_ops::host_to_network_short(msg_id);
    memcpy(_data, &msg_id_host, HEAD_ID_LEN);

    // 转为网络字节序
    short max_len_host = boost::asio::detail::socket_ops::host_to_network_short(max_len);
    memcpy(_data + HEAD_ID_LEN, &max_len_host, HEAD_DATA_LEN);

    // 复制消息数据
    memcpy(_data + HEAD_ID_LEN + HEAD_DATA_LEN, msg, max_len);
}
