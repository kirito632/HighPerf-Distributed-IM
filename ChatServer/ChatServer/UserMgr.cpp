#include "UserMgr.h"
#include"CSession.h"
#include"RedisMgr.h"

// 析构函数：清理所有会话
UserMgr::~UserMgr() {
	_uid_to_session.clear();
}

// 构造函数：初始化用户管理器
UserMgr::UserMgr() {

}

// 根据用户ID获取会话
// 
// 参数：
//   - uid: 用户ID
// 
// 返回值：
//   找到返回会话指针，否则返回nullptr
// 
// 实现逻辑：
//   1. 加锁保证线程安全
//   2. 从映射表中查找用户ID
//   3. 返回对应的会话指针
std::shared_ptr<CSession> UserMgr::GetSession(int uid) {
	std::lock_guard<std::mutex> lock(_session_mtx);
	auto iter = _uid_to_session.find(uid);
	if (iter == _uid_to_session.end()) {
		return nullptr;
	}
	return iter->second;
}

// 设置用户会话
// 
// 参数：
//   - uid: 用户ID
//   - session: 会话指针
// 
// 实现逻辑：
//   1. 加锁保证线程安全
//   2. 将用户ID和会话的映射关系存储到map中
void UserMgr::SetUserSession(int uid, std::shared_ptr<CSession> session)
{
	std::lock_guard<std::mutex> lock(_session_mtx);
	_uid_to_session[uid] = session;
}

// 移除用户会话
// 
// 参数：
//   - uid: 用户ID
// 
// 实现逻辑：
//   1. 从Redis删除用户IP映射（已注释，避免并发问题）
//   2. 加锁保证线程安全
//   3. 从map中删除用户ID和会话的映射
void UserMgr::RmvUserSession(int uid)
{
	auto uid_str = std::to_string(uid);
	// 如果再次登录，可能会创建新key，因为这里删除key，所以用户再次登录时可能key不存在
	// 可能还有并发问题：用户在登录中，这里删除key导致其他线程也删除key导致key不存在
	// 注释掉Redis的删除操作，避免并发问题
	// RedisMgr::GetInstance()->Del(USERIPREFIX + uid_str);

	{
		std::lock_guard<std::mutex> lock(_session_mtx);
		_uid_to_session.erase(uid);
	}
}
