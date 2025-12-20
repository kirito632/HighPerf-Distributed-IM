#include "CSession.h"
#include "CServer.h"

CSession::CSession(boost::asio::io_context& io_context, CServer* server) :
	_socket(io_context), _server(server), _b_close(false), _b_head_parse(false),
	_strand(io_context.get_executor()) {
	boost::uuids::uuid  a_uuid = boost::uuids::random_generator()();
	_session_id = boost::uuids::to_string(a_uuid);
	_recv_head_node = make_shared<MsgNode>(HEAD_TOTAL_LEN);
}

boost::asio::ip::tcp::socket& CSession::GetSocket() {
	return _socket;
}

std::string& CSession::GetSessionId()
{
	return _session_id;
}

void CSession::SetUserId(int uid)
{
	_user_uid = uid;
}

int CSession::GetUserId()
{
	return _user_uid;
}


CSession::~CSession() {
	std::cout << "~CSession destruct " << std::endl;
}

void CSession::Start() {
	AsyncReadHead(HEAD_TOTAL_LEN);
}

void CSession::Close() {
	_socket.close();
	_b_close = true;
}

std::shared_ptr<CSession>CSession::SharedSelf() {
	return shared_from_this();
}

void CSession::Send(std::string msg, short msgid) {
	std::lock_guard<std::mutex> lock(_send_lock);
	int send_que_size = _send_que.size();
	
	// ✅ 修复：增强的发送队列流控
	if (send_que_size > MAX_SENDQUE) {
		std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << std::endl;
		
		// P2级修复：踢掉慢消费者，而不是简单丢包
		// 在高并发IM系统中，保护服务器内存比保护单条消息更重要
		std::cout << "[FlowControl] Slow consumer detected for session " << _session_id 
		          << ", uid=" << _user_uid << ". Closing connection to prevent memory exhaustion." << std::endl;
		
		// 异步关闭连接，让客户端重连
		// 这比静默丢包更好，客户端能感知到问题并重连
		auto self = SharedSelf();
		boost::asio::post(_socket.get_executor(), [self]() {
			self->Close();
			self->_server->ClearSession(self->_session_id);
		});
		return;
	}

	_send_que.push(std::make_shared<SendNode>(msg.c_str(), msg.length(), msgid));
	if (send_que_size > 0) {
		return;
	}
	auto& msgnode = _send_que.front();
	short out_msgid = 0;
	::memcpy(&out_msgid, msgnode->_data, HEAD_ID_LEN);
	out_msgid = boost::asio::detail::socket_ops::network_to_host_short(out_msgid);
	short out_len = 0;
	::memcpy(&out_len, msgnode->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
	out_len = boost::asio::detail::socket_ops::network_to_host_short(out_len);
	int uid = _user_uid;
	auto self = SharedSelf();
	boost::asio::async_write(
		_socket,
		boost::asio::buffer(msgnode->_data, msgnode->_total_len),
		boost::asio::bind_executor(_strand,
			[self, out_msgid, out_len, uid](const boost::system::error_code& ec, std::size_t bytes_transferred) {
				if (!ec) {
					std::cout << "[TCP][Write] ok uid=" << uid << " msgid=" << out_msgid
						<< " body_len=" << out_len << " bytes=" << bytes_transferred << std::endl;
				}
				else {
					std::cout << "[TCP][Write] fail uid=" << uid << " msgid=" << out_msgid
						<< " body_len=" << out_len << " err=" << ec.message() << std::endl;
				}
				self->HandleWrite(ec, self);
			}
		)
	);
}

void CSession::Send(char* msg, short max_length, short msgid) {
	std::lock_guard<std::mutex> lock(_send_lock);
	int send_que_size = _send_que.size();
	
	// ✅ 修复：增强的发送队列流控（与上面的Send方法保持一致）
	if (send_que_size > MAX_SENDQUE) {
		std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << std::endl;
		
		std::cout << "[FlowControl] Slow consumer detected for session " << _session_id 
		          << ", uid=" << _user_uid << ". Closing connection to prevent memory exhaustion." << std::endl;
		
		auto self = SharedSelf();
		boost::asio::post(_socket.get_executor(), [self]() {
			self->Close();
			self->_server->ClearSession(self->_session_id);
		});
		return;
	}

	_send_que.push(std::make_shared<SendNode>(msg, max_length, msgid));
	if (send_que_size > 0) {
		return;
	}
	auto& msgnode = _send_que.front();
	short out_msgid = 0;
	::memcpy(&out_msgid, msgnode->_data, HEAD_ID_LEN);
	out_msgid = boost::asio::detail::socket_ops::network_to_host_short(out_msgid);
	short out_len = 0;
	::memcpy(&out_len, msgnode->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
	out_len = boost::asio::detail::socket_ops::network_to_host_short(out_len);
	int uid = _user_uid;
	auto self = SharedSelf();
	boost::asio::async_write(
		_socket,
		boost::asio::buffer(msgnode->_data, msgnode->_total_len),
		boost::asio::bind_executor(_strand,
			[self, out_msgid, out_len, uid](const boost::system::error_code& ec, std::size_t bytes_transferred) {
				if (!ec) {
					std::cout << "[TCP][Write] ok uid=" << uid << " msgid=" << out_msgid
						<< " body_len=" << out_len << " bytes=" << bytes_transferred << std::endl;
				}
				else {
					std::cout << "[TCP][Write] fail uid=" << uid << " msgid=" << out_msgid
						<< " body_len=" << out_len << " err=" << ec.message() << std::endl;
				}
				self->HandleWrite(ec, self);
			}
		)
	);
}

void CSession::AsyncReadHead(int total_len)
{
	auto self = shared_from_this();
	asyncReadFull(HEAD_TOTAL_LEN, [self, this](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			if (ec) {
				std::cout << "handle read failed, error is " << ec.what() << std::endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			if (bytes_transfered < HEAD_TOTAL_LEN) {
				std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
					<< HEAD_TOTAL_LEN << "]" << std::endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			_recv_head_node->Clear();
			memcpy(_recv_head_node->_data, _data, bytes_transfered);

			//获取头部MSGID数据
			short msg_id = 0;
			memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
			//网络字节序转化为本地字节序
			msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
			std::cout << "msg_id is " << msg_id << std::endl;
			
			// ✅ 修复：严格的消息ID验证
			// 检查消息ID是否在合法范围内（避免负数和过大值）
			if (msg_id < 0 || msg_id > MAX_LENGTH) {
				std::cout << "invalid msg_id is " << msg_id << " (must be 0-" << MAX_LENGTH << ")" << std::endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}
			
			short msg_len = 0;
			memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
			//网络字节序转化为本地字节序
			msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
			std::cout << "msg_len is " << msg_len << std::endl;

			// ✅ 修复：严格的消息长度验证
			// 1. 检查负数（防止整数溢出攻击）
			// 2. 检查最大长度（防止内存耗尽攻击）
			// 3. 添加合理的最大包体限制（10MB）
			const int MAX_PACKAGE_SIZE = 10 * 1024 * 1024;  // 10MB
			if (msg_len < 0) {
				std::cout << "invalid negative msg_len: " << msg_len << std::endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}
			if (msg_len > MAX_LENGTH) {
				std::cout << "msg_len too large: " << msg_len << " (max: " << MAX_LENGTH << ")" << std::endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}
			if (msg_len > MAX_PACKAGE_SIZE) {
				std::cout << "potential DoS attack: msg_len=" << msg_len << " exceeds " << MAX_PACKAGE_SIZE << " bytes" << std::endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			_recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);
			AsyncReadBody(msg_len);
		}
		catch (boost::system::error_code& e) {
			std::cout << "Exception code is " << e.what() << std::endl;
		}
		});
}

//读取完整长度
void CSession::asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
	::memset(_data, 0, MAX_LENGTH);
	asyncReadLen(0, maxLength, handler);
}

//读取指定字节数
void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len,
	std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
	auto self = shared_from_this();
	_socket.async_read_some(boost::asio::buffer(_data + read_len, total_len - read_len),
		[read_len, total_len, handler, self](const boost::system::error_code& ec, std::size_t  bytesTransfered) {
			if (ec) {
				// 出现错误，调用回调函数
				handler(ec, read_len + bytesTransfered);
				return;
			}

			if (read_len + bytesTransfered >= total_len) {
				//长度够了就调用回调函数
				handler(ec, read_len + bytesTransfered);
				return;
			}

			// 没有错误，且长度不足则继续读取
			self->asyncReadLen(read_len + bytesTransfered, total_len, handler);
		});
}

void CSession::AsyncReadBody(int total_len)
{
	auto self = shared_from_this();
	asyncReadFull(total_len, [self, this, total_len](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			if (ec) {
				std::cout << "handle read failed, error is " << ec.what() << std::endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			if (bytes_transfered < total_len) {
				std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
					<< total_len << "]" << std::endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			memcpy(_recv_msg_node->_data, _data, bytes_transfered);
			_recv_msg_node->_cur_len += bytes_transfered;
			_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
			std::cout << "receive data is " << _recv_msg_node->_data << std::endl;
			//此处将消息投递到逻辑队列中
			LogicSystem::GetInstance()->PostMsgToQue(std::make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
			//继续监听头部接受事件
			AsyncReadHead(HEAD_TOTAL_LEN);
		}
		catch (boost::system::error_code& e) {
			std::cout << "Exception code is " << e.what() << std::endl;
		}
		});
}

void CSession::HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self) {
	//增加异常处理
	try {
		if (!error) {
			std::lock_guard<std::mutex> lock(_send_lock);
			//cout << "send data " << _send_que.front()->_data+HEAD_LENGTH << std::endl;
			_send_que.pop();
			if (!_send_que.empty()) {
				auto& msgnode = _send_que.front();
				auto self = shared_self;
				short out_msgid = 0;
				::memcpy(&out_msgid, msgnode->_data, HEAD_ID_LEN);
				out_msgid = boost::asio::detail::socket_ops::network_to_host_short(out_msgid);
				short out_len = 0;
				::memcpy(&out_len, msgnode->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
				out_len = boost::asio::detail::socket_ops::network_to_host_short(out_len);
				int uid = _user_uid;
				std::cout << "[TCP][Write] next uid=" << uid << " msgid=" << out_msgid
					<< " body_len=" << out_len << std::endl;
				boost::asio::async_write(
					_socket,
					boost::asio::buffer(msgnode->_data, msgnode->_total_len),
					boost::asio::bind_executor(_strand,
						[self, out_msgid, out_len, uid](const boost::system::error_code& ec, std::size_t bytes_transferred) {
							if (!ec) {
								std::cout << "[TCP][Write] ok uid=" << uid << " msgid=" << out_msgid
									<< " body_len=" << out_len << " bytes=" << bytes_transferred << std::endl;
							}
							else {
								std::cout << "[TCP][Write] fail uid=" << uid << " msgid=" << out_msgid
									<< " body_len=" << out_len << " err=" << ec.message() << std::endl;
							}
							self->HandleWrite(ec, self);
						}
					)
				);
			}
		}
		else {
			std::cout << "handle write failed, error is " << error.what() << std::endl;
			Close();
			_server->ClearSession(_session_id);
		}
	}
	catch (std::exception& e) {
		std::cerr << "Exception code : " << e.what() << std::endl;
	}
}

LogicNode::LogicNode(std::shared_ptr<CSession> session, std::shared_ptr<RecvNode> recvnode) :_session(session), _recvnode(recvnode)
{
}