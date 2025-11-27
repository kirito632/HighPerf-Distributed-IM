#include "LogicSystem.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "UserMgr.h"
#include "AsyncDBPool.h"

#include "ChatGrpcClient.h"

// 析构函数：清理资源
// 
// 实现逻辑：
//   1. 设置停止标志
//   2. 通知工作线程退出
//   3. 等待工作线程结束
LogicSystem::~LogicSystem()
{
	_b_stop = true;
	_consume.notify_one();
	_worker_thread.join();
}

// 投递消息到队列
// 
// 参数：
//   - msg: 消息节点指针
// 
// 实现逻辑：
//   1. 加锁保证线程安全
//   2. 将消息加入到队列
//   3. 如果队列只有一条消息，通知工作线程处理
void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg)
{
	std::unique_lock<std::mutex> unique_lk(_mutex);
	_msg_que.push(msg);

	// 如果队列只有一条消息，通知工作线程
	if (_msg_que.size() == 1) {
		_consume.notify_one();
	}
}

// 构造函数：初始化逻辑系统
// 
// 实现逻辑：
//   1. 注册回调函数
//   2. 启动工作线程
LogicSystem::LogicSystem() :_b_stop(false) {
	RegisterCallBacks();
	_worker_thread = std::thread(&LogicSystem::DealMsg, this);
	AsyncDBPool::GetInstance()->Init();
}

// 注册回调函数
// 
// 作用：
//   将消息ID和对应的处理函数建立映射关系
// 
// 当前注册的回调：
//   MSG_CHAT_LOGIN -> LoginHandler
void LogicSystem::RegisterCallBacks() {
	_fun_callbacks[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginHandler, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);


	_fun_callbacks[ID_TEXT_CHAT_MSG_REQ] = std::bind(&LogicSystem::DealChatTextMsg, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

	_fun_callbacks[ID_GET_OFFLINE_MSG_REQ] = std::bind(&LogicSystem::GetOfflineMsgHandler, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

	_fun_callbacks[ID_NOTIFY_TEXT_CHAT_MSG_RSP] = std::bind(&LogicSystem::OfflineMsgAckHandler, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}

// 登录处理函数
// 
// 功能：
//   验证用户token，获取用户信息，建立会话
// 
// 实现逻辑：
//   1. 解析JSON消息，获取uid和token
//   2. 从Redis验证token
//   3. 获取用户基础信息（优先从Redis获取，没有则从MySQL获取）
//   4. 更新登录计数（Redis中的LOGIN_COUNT）
//   5. 建立用户会话映射（UserMgr、CSession、Redis）
//   6. 发送登录成功响应
void LogicSystem::LoginHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	int uid = root["uid"].asInt();
	std::string token = root["token"].asString();
	std::cout << "[LoginHandler] recv uid=" << uid << " token=" << token << std::endl;

	Json::Value rtvalue;
	// 校验 token 是否存在于 redis
	std::string uid_str = std::to_string(uid);
	std::string token_key = USERTOKENPREFIX + uid_str;
	std::string token_value;
	bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
	if (!success) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		std::string return_str = rtvalue.toStyledString();
		std::cout << "[LoginHandler TEST] send before return, body=" << return_str
			<< " msgid=" << MSG_CHAT_LOGIN_RSP << std::endl;
		session->Send(return_str, MSG_CHAT_LOGIN_RSP);
		return;
	}
	// 验证token是否匹配
	if (token_value != token) {
		rtvalue["error"] = ErrorCodes::TokenInvalid;
		std::string return_str = rtvalue.toStyledString();
		std::cout << "[LoginHandler TEST] send before return, body=" << return_str
			<< " msgid=" << MSG_CHAT_LOGIN_RSP << std::endl;
		session->Send(return_str, MSG_CHAT_LOGIN_RSP);
		return;
	}

	// token 验证成功，获取用户信息
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = USER_BASE_INFO + uid_str;
	auto user_info = std::make_shared<UserInfo>();
	bool b_base = GetBaseInfo(base_key, uid, user_info);
	if (!b_base) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		std::string return_str = rtvalue.toStyledString();
		std::cout << "[LoginHandler TEST] send before return, body=" << return_str
			<< " msgid=" << MSG_CHAT_LOGIN_RSP << std::endl;
		session->Send(return_str, MSG_CHAT_LOGIN_RSP);
		return;
	}

	// 设置返回的用户信息
	rtvalue["uid"] = uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;

	// 更新登录计数和状态
	auto server_name = ConfigMgr::Inst().GetValue("SelfServer", "Name");
	std::transform(server_name.begin(), server_name.end(), server_name.begin(), ::tolower);

	// 从Redis读取当前登录计数并加1
	auto rd_res = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server_name);
	int count = 0;
	if (!rd_res.empty()) {
		try { count = std::stoi(rd_res); }
		catch (...) { count = 0; }
	}
	++count;
	RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, std::to_string(count));

	// 在 session中写入 ipkey，同时在 session 和 UserMgr 建立映射
	session->SetUserId(uid);
	std::string ipkey = USERIPPREFIX + uid_str;
	RedisMgr::GetInstance()->Set(ipkey, server_name);
	UserMgr::GetInstance()->SetUserSession(uid, session);

	// 统一返回统一发送成功包
	std::string return_str = rtvalue.toStyledString();
	std::cout << "[LoginHandler TEST] send success, uid=" << uid
		<< " body=" << return_str << " msgid=" << MSG_CHAT_LOGIN_RSP << std::endl;
	session->Send(return_str, MSG_CHAT_LOGIN_RSP);

	return;
}



void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	int uid = root["fromuid"].asInt();
	int touid = root["touid"].asInt();
	const Json::Value arrays = root["text_array"];

	Json::Value rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["text_array"] = arrays;
	rtvalue["fromuid"] = uid;
	rtvalue["touid"] = touid;

	// 统一构造用于下发和持久化的 JSON 文本
	std::string notify_str_cache = rtvalue.toStyledString();

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_TEXT_CHAT_MSG_RSP);
		});

	// 核心修改：先持久化，再投递。
	// 无论对方是在线、离线还是跨服，先将消息入库 (Status=0)。
	// 这样保证了消息不丢失。当对方收到消息回 ACK 时，再将其删除。
	AsyncDBPool::GetInstance()->PostTask([uid, touid, notify_str_cache]() {
		MysqlMgr::GetInstance()->SaveChatMessage(uid, touid, notify_str_cache);
		});

	std::string to_ip_key = USERIPPREFIX + std::to_string(touid);
	std::string to_ip_value;
	bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
	if (!b_ip) {
		std::cout << "[TextChat][Route] redis miss key=" << to_ip_key << " -> no route (msg saved)" << std::endl;
		return;
	}

	auto server_name = ConfigMgr::Inst().GetValue("SelfServer", "Name");
	std::transform(server_name.begin(), server_name.end(), server_name.begin(), ::tolower);
	std::cout << "[TextChat][Route] to_ip=" << to_ip_value << " self=" << server_name
		<< " same_server=" << std::boolalpha << (to_ip_value == server_name) << std::endl;

	if (to_ip_value == server_name) {
		auto to_sess = UserMgr::GetInstance()->GetSession(touid);
		if (to_sess) {
			std::cout << "[TextChat][Route] local deliver TCP 1019 to uid=" << touid
				<< " body_len=" << notify_str_cache.size() << std::endl;
			to_sess->Send(notify_str_cache, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
		}
		else {
			// 用户在本机，但离线，存入Redis离线消息（加速拉取）
			std::string offline_key = OFFLINE_MSG_PREFIX + std::to_string(touid);
			RedisMgr::GetInstance()->LPush(offline_key, notify_str_cache);
			std::cout << "[OfflineMsg] user " << touid << " is offline, saved message to redis key=" << offline_key << std::endl;
		}
		return;
	}

	TextChatMsgReq text_msg_req;
	text_msg_req.set_fromuid(uid);
	text_msg_req.set_touid(touid);
	for (const auto& txt_obj : arrays) {
		auto content = txt_obj["content"].asString();
		auto msgid = txt_obj["msgid"].asString();
		auto* text_msg = text_msg_req.add_textmsgs();
		text_msg->set_msgid(msgid);
		text_msg->set_msgcontent(content);
	}

	std::cout << "[TextChat][Route] cross-server deliver via gRPC target=" << to_ip_value
		<< " fromuid=" << uid << " touid=" << touid
		<< " msgs=" << arrays.size() << std::endl;
	auto rsp = ChatGrpcClient::GetInstance()->NotifyTextChatMsg(to_ip_value, text_msg_req);

	// 如果RPC调用成功，但业务逻辑返回对方离线
	if (rsp.error() == ErrorCodes::RecipientOffline) {
		std::cout << "[TextChat][Route] gRPC target offline, saving to local redis" << std::endl;
		// 将消息存入本地Redis的离线消息队列（加速拉取）
		std::string offline_key = OFFLINE_MSG_PREFIX + std::to_string(touid);
		RedisMgr::GetInstance()->LPush(offline_key, notify_str_cache);
	}
}

void LogicSystem::GetOfflineMsgHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	int uid = root["uid"].asInt();

	std::cout << "[OfflineMsg] recv get offline msg req, uid=" << uid << std::endl;

	std::string offline_key = OFFLINE_MSG_PREFIX + std::to_string(uid);
	std::vector<std::string> messages;
	RedisMgr::GetInstance()->GetAllList(offline_key, messages);

	std::cout << "[OfflineMsg] get " << messages.size() << " offline messages for uid=" << uid << std::endl;

	// 使用 weak_ptr 防止回调时 session 已销毁
	std::weak_ptr<CSession> weak_sess = session;

	// 投递异步任务到 DB 线程池
	AsyncDBPool::GetInstance()->PostTask([uid, weak_sess]() {
		// 在 DB 线程中执行
		std::shared_ptr<CSession> shared_sess = weak_sess.lock();
		if (!shared_sess) {
			std::cout << "[OfflineMsg][Async] session expired, abort db query for uid=" << uid << std::endl;
			return;
		}

		std::vector<long long> ids;
		std::vector<std::string> db_payloads;
		
		// 阻塞式查询，但现在是在 Worker 线程中，不会阻塞主 Logic 线程
		if (MysqlMgr::GetInstance()->GetUnreadChatMessages(uid, ids, db_payloads)) {
			std::cout << "[OfflineMsg][Async] get " << db_payloads.size() << " unread messages for uid=" << uid << std::endl;
			
			// 发送消息 (Session::Send 是线程安全的)
			for (const auto& payload : db_payloads) {
				shared_sess->Send(payload, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
			}
			// 移除删除逻辑，等待客户端ACK确认后再删除
		}
	});
}

void LogicSystem::OfflineMsgAckHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	// 客户端回包格式: { "uid": 1001, "max_msg_id": 10005 }
	int uid = root["uid"].asInt();
	long long max_msg_id = root["max_msg_id"].asInt64();
	
	std::cout << "[OfflineMsg][Ack] recv ack for uid=" << uid << " max_msg_id=" << max_msg_id << std::endl;

	// 异步更新 DB 状态
	AsyncDBPool::GetInstance()->PostTask([uid, max_msg_id]() {
		MysqlMgr::GetInstance()->AckOfflineMessages(uid, max_msg_id);
		});
}

// 获取用户基础信息
// 
// 参数：
//   - base_key: Redis键名（USER_BASE_INFO + uid）
//   - uid: 用户ID
//   - userinfo: 输出参数，用户信息
// 
// 返回值：
//   成功返回true，否则返回false
// 
// 实现逻辑：
//   1. 先从Redis获取用户信息（提高性能）
//   2. 如果Redis没有，从MySQL查询
//   3. 将从MySQL查询到的用户信息写入Redis（缓存）
bool LogicSystem::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
	// 先查 Redis
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		// Redis中有数据，解析JSON
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		userinfo = std::make_shared<UserInfo>();
		userinfo->uid = root["uid"].asInt();
		userinfo->name = root["name"].asString();
		userinfo->email = root["email"].asString();
		userinfo->pwd = root["pwd"].asString();
		// 默认填充字段，防止 JSON 缺失字段导致错误
		userinfo->nick = root.isMember("nick") ? root["nick"].asString() : "";
		userinfo->desc = root.isMember("desc") ? root["desc"].asString() : "";
		userinfo->sex = root.isMember("sex") ? root["sex"].asInt() : 0;
		userinfo->icon = root.isMember("icon") ? root["icon"].asString() : "";
		std::cout << "user login uid is " << userinfo->uid << " user name is " << userinfo->name
			<< " user email is " << userinfo->email << " pwd is " << userinfo->pwd << std::endl;
	}
	else {
		// Redis 没有数据，从 MySQL 查询
		userinfo = MysqlMgr::GetInstance()->GetUser(uid);
		if (userinfo == nullptr) {
			return false;
		}

		// 写入 Redis
		Json::Value redis_root;
		redis_root["uid"] = userinfo->uid;
		redis_root["name"] = userinfo->name;
		redis_root["email"] = userinfo->email;
		redis_root["pwd"] = userinfo->pwd;
		redis_root["nick"] = userinfo->nick; // 空值
		redis_root["desc"] = userinfo->desc; // 空值
		redis_root["sex"] = userinfo->sex;   // 0
		redis_root["icon"] = userinfo->icon; // 空值
		RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	}
	return true;
}

// 处理消息（在工作线程中运行）
// 
// 实现逻辑：
//   1. 无限循环，等待消息
//   2. 队列为空且未停止时，等待条件变量
//   3. 如果已停止，处理完剩余消息后退出
//   4. 从队列中取出消息，根据消息ID查找回调函数
//   5. 调用回调函数处理消息
void LogicSystem::DealMsg()
{
	for (;;) {
		std::unique_lock<std::mutex> unique_lk(_mutex);

		// 判断队列为空，等待条件变量唤醒
		while (_msg_que.empty() && !_b_stop) {
			_consume.wait(unique_lk);
		}

		// 判断系统为关闭状态，取出消息队列中的所有剩余数据，处理完再退出循环
		if (_b_stop) {
			while (!_msg_que.empty()) {
				auto msg_node = _msg_que.front();
				std::cout << "recv msg id is" << msg_node->_recvnode->_msg_id << std::endl;
				auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
				if (call_back_iter == _fun_callbacks.end()) {
					_msg_que.pop();
					continue;
				}
				call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
					std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
				_msg_que.pop();
			}

			break;
		}

		// 如果没有停止，且消息队列不空，取出一条处理
		auto msg_node = _msg_que.front();
		std::cout << "recv msg id is" << msg_node->_recvnode->_msg_id << std::endl;

		auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
		if (call_back_iter == _fun_callbacks.end()) {
			_msg_que.pop();
			continue;
		}

		call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
			std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
		_msg_que.pop();
	}
}
