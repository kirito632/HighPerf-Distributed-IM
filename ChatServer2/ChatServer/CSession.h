#pragma once
#include<iostream>
#include<sstream>
#include"LogicSystem.h"
#include<boost/asio.hpp>
#include"MsgNode.h"
#include<queue>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include "const.h"
#define MAX_LENGTH 1024 * 2

#define HEAD_TOTAL_LEN 4

#define HEAD_ID_LEN 2

#define HEAD_DATA_LEN  2
#define MAX_RECVQUE 10000
#define MAX_SENDQUE 1000

class CServer;

class CSession : public std::enable_shared_from_this<CSession>
{
public:
	CSession(boost::asio::io_context& io_context, CServer* server);
	void Start();
	void Close();
	void Send(char* msg, short max_length, short msgid);
	void Send(std::string msg, short msgid);
	void AsyncReadHead(int total_len);
	void asyncReadFull(std::size_t maxLength,
		std::function<void(const boost::system::error_code&, std::size_t)> handler);
	void asyncReadLen(std::size_t read_len, std::size_t total_len,
		std::function<void(const boost::system::error_code&, std::size_t)> handler);
	void AsyncReadBody(int total_len);
	boost::asio::ip::tcp::socket& GetSocket();
	std::string& GetSessionId();
	void SetUserId(int uid);
	int GetUserId();
	~CSession();

	std::shared_ptr<CSession> SharedSelf();

private:
	void HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self);

	bool _b_close;
	tcp::socket _socket;
	std::string _session_id;
	char _data[MAX_LENGTH];
	CServer* _server;
	std::queue<std::shared_ptr<MsgNode> > _send_que;
	std::mutex _send_lock;
	//收到的消息结构
	std::shared_ptr<RecvNode> _recv_msg_node;
	bool _b_head_parse;
	//收到的头部结构
	std::shared_ptr<MsgNode> _recv_head_node;
	std::function<void()> func_;

	int _user_uid;

	boost::asio::strand<boost::asio::io_context::executor_type> _strand;
};

class LogicNode {
	friend class LogicSystem;
public:
	LogicNode(std::shared_ptr<CSession>, std::shared_ptr<RecvNode>);

private:
	std::shared_ptr<CSession> _session;
	std::shared_ptr<RecvNode> _recvnode;
};




