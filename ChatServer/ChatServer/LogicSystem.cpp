#include "LogicSystem.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "UserMgr.h"
#include "AsyncDBPool.h"
#include "ConfigMgr.h"
#include "JwtKeys.h"
#include "JwtVerifier.h"
#include "Metrics.h"
#include <chrono>
#include <algorithm>

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
	try {
		auto& cfg = ConfigMgr::Inst();
		auto feature_cfg = cfg["Feature"];
		auto jwt_cfg = cfg["Jwt"];
		if (feature_cfg["JwtEnabled"] == "true") {
			im::auth::JwtConfig jwt;
			jwt.issuer = jwt_cfg["Issuer"];
			jwt.audience = jwt_cfg["Audience"];
			jwt.key_id = jwt_cfg["KeyId"];
			jwt.public_key_pem = im::auth::ReadPemFile(jwt_cfg["PublicKeyPath"]);
			const std::string skew = jwt_cfg["ClockSkewSeconds"];
			if (!skew.empty()) {
				jwt.clock_skew_seconds = std::stoi(skew);
			}
			_jwt_verifier = std::make_unique<im::auth::JwtVerifier>(jwt);
			std::cout << "[Auth] ChatServer JWT enabled, issuer=" << jwt.issuer
				<< " audience=" << jwt.audience << " kid=" << jwt.key_id << std::endl;
		}
	}
	catch (const std::exception& e) {
		std::cerr << "[Auth] ChatServer JWT init failed, fallback to Redis token: "
			<< e.what() << std::endl;
		_jwt_verifier.reset();
	}

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
	std::cout << "[LoginHandler] recv uid=" << uid << std::endl;

	Json::Value rtvalue;
	// JWT is mandatory; the legacy Redis utoken path is removed.
	bool token_ok = false;
	if (_jwt_verifier) {
		const std::string access_token = root["access_token"].asString();
		im::auth::VerifiedJwt verified;
		std::string error;
		if (_jwt_verifier->Verify(access_token, verified, error) &&
			verified.claims.uid == uid &&
			verified.claims.scope.find("im.message") != std::string::npos) {
			token_ok = true;
		}
		else {
			std::cout << "[LoginHandler] JWT verify failed: " << error << std::endl;
		}
	}
	else {
		std::cerr << "[LoginHandler] JWT verifier is not configured" << std::endl;
	}

	if (!token_ok) {
		rtvalue["error"] = ErrorCodes::TokenInvalid;
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, MSG_CHAT_LOGIN_RSP);
		return;
	}

	// token 验证成功，获取用户信息
	rtvalue["error"] = ErrorCodes::Success;

	std::string uid_str = std::to_string(uid);
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

	Json::Value rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	auto send_response = [&rtvalue, &session]() {
		session->Send(rtvalue.toStyledString(), ID_TEXT_CHAT_MSG_RSP);
	};

	// Session binding: never trust uid/fromuid from the request body.
	const int uid = session->GetUserId();
	if (uid <= 0) {
		rtvalue["error"] = ErrorCodes::TokenInvalid;
		send_response();
		return;
	}
	if (root.isMember("fromuid") && root["fromuid"].asInt() != uid) {
		std::cout << "[TextChat] reject spoofed fromuid=" << root["fromuid"].asInt()
			<< " auth_uid=" << uid << std::endl;
		rtvalue["error"] = ErrorCodes::TokenInvalid;
		send_response();
		return;
	}

	const int touid = root["touid"].asInt();
	const Json::Value arrays = root["text_array"];
	if (touid <= 0 || !arrays.isArray() || arrays.empty()) {
		rtvalue["error"] = ErrorCodes::Error_Json;
		send_response();
		return;
	}

	std::string client_msg_id = root.get("client_msg_id", "").asString();
	if (client_msg_id.empty()) {
		const Json::Value& first = arrays[0];
		if (first.isMember("client_msg_id")) {
			client_msg_id = first["client_msg_id"].asString();
		}
		else {
			// Legacy clients send "msgid" (their local UUID/timestamp).
			client_msg_id = first.get("msgid", "").asString();
		}
	}
	if (client_msg_id.empty()) {
		static std::atomic<long long> fallback_seq{ 0 };
		const auto now = std::chrono::system_clock::now().time_since_epoch().count();
		client_msg_id = "srv-" + std::to_string(uid) + "-" +
			std::to_string(now) + "-" + std::to_string(++fallback_seq);
	}

	const std::string conversation_id =
		std::to_string(std::min(uid, touid)) + ":" + std::to_string(std::max(uid, touid));

	rtvalue["fromuid"] = uid;
	rtvalue["touid"] = touid;
	rtvalue["client_msg_id"] = client_msg_id;
	rtvalue["text_array"] = arrays;

	long long server_msg_id = 0;
	const std::string initial_payload = rtvalue.toStyledString();

	// Build the final payload (with server_msg_id) inside the DB transaction;
	// the same transaction writes the outbox row when enabled.
	auto build_final_payload = [&](long long assigned_id) {
		rtvalue["server_msg_id"] = static_cast<Json::Int64>(assigned_id);
		Json::Value out_array(Json::arrayValue);
		for (const auto& txt_obj : arrays) {
			Json::Value item = txt_obj;
			item["server_msg_id"] = static_cast<Json::Int64>(assigned_id);
			item["client_msg_id"] = client_msg_id;
			out_array.append(item);
		}
		rtvalue["text_array"] = out_array;
		return rtvalue.toStyledString();
	};

	auto feature_cfg = ConfigMgr::Inst()["Feature"];
	const bool write_outbox = feature_cfg["OutboxEnabled"] == "true";
	if (!MysqlMgr::GetInstance()->SaveChatMessage(
		uid, touid, client_msg_id, conversation_id, initial_payload,
		write_outbox, build_final_payload, server_msg_id)) {
		rtvalue["error"] = ErrorCodes::RPCFailed;
		send_response();
		return;
	}

	const std::string notify_str_cache = rtvalue.toStyledString();
	im::metrics::IncCounter("im_messages_received_total", 1,
	                        "Total persisted chat messages");
	rtvalue["error"] = ErrorCodes::Success;
	send_response();

	// Kafka delivery mode: the DeliveryConsumer owns live delivery.
	if (ConfigMgr::Inst()["Feature"]["DeliveryMode"] == "kafka") {
		return;
	}

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
				<< " server_msg_id=" << server_msg_id
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
		auto msgid = txt_obj.get("client_msg_id", client_msg_id).asString();
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
		std::string offline_key = OFFLINE_MSG_PREFIX + std::to_string(touid);
		RedisMgr::GetInstance()->LPush(offline_key, notify_str_cache);
	}
}

void LogicSystem::GetOfflineMsgHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	// Session binding: ignore any uid supplied in the body.
	const int uid = session->GetUserId();
	const long long cursor = root.get("cursor", root.get("max_msg_id", 0)).asInt64();
	if (uid <= 0) {
		std::cout << "[OfflineMsg] reject unauthenticated session" << std::endl;
		return;
	}

	std::cout << "[OfflineMsg] recv get offline msg req, uid=" << uid
		<< " cursor=" << cursor << std::endl;

	std::string offline_key = OFFLINE_MSG_PREFIX + std::to_string(uid);

	// 使用 weak_ptr 防止回调时 session 已销毁
	std::weak_ptr<CSession> weak_sess = session;

	// 投递异步任务到 DB 线程池
	AsyncDBPool::GetInstance()->PostTask([uid, cursor, offline_key, weak_sess]() {
		std::shared_ptr<CSession> shared_sess = weak_sess.lock();
		if (!shared_sess) {
			std::cout << "[OfflineMsg][Async] session expired, abort db query for uid=" << uid << std::endl;
			return;
		}

		std::vector<long long> ids;
		std::vector<std::string> db_payloads;
		if (MysqlMgr::GetInstance()->GetUnreadChatMessages(uid, cursor, 50, ids, db_payloads)) {
			std::cout << "[OfflineMsg][Async] get " << db_payloads.size()
				<< " unread messages for uid=" << uid << " cursor=" << cursor << std::endl;

			for (const auto& payload : db_payloads) {
				shared_sess->Send(payload, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
			}

			// 消息已从 MySQL 下发，清理 Redis 缓存 key；真正已读仍由 ACK 驱动
			RedisMgr::GetInstance()->Del(offline_key);
			std::cout << "[OfflineMsg][Async] cleared redis cache key=" << offline_key << std::endl;
		}
	});
}

void LogicSystem::OfflineMsgAckHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	const int uid = session->GetUserId();
	if (uid <= 0) {
		std::cout << "[OfflineMsg][Ack] reject unauthenticated session" << std::endl;
		return;
	}

	std::vector<long long> ids;
	const Json::Value& id_array = root.isMember("ids") ? root["ids"] : root["msg_ids"];
	if (id_array.isArray()) {
		for (const auto& item : id_array) {
			const long long id = item.asInt64();
			if (id > 0) {
				ids.push_back(id);
			}
		}
	}

	std::cout << "[OfflineMsg][Ack] uid=" << uid << " ids=" << ids.size() << std::endl;
	if (ids.empty()) {
		return;
	}

	AsyncDBPool::GetInstance()->PostTask([uid, ids]() {
		MysqlMgr::GetInstance()->AckOfflineMessages(uid, ids);
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
			<< " user email is " << userinfo->email << std::endl;
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
