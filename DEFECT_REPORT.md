# IM 项目缺陷审查报告

- **审查对象**：`~/cppworks/IM`（远端工作树，含未提交修改；本地按源码快照审查）
- **审查时间**：2026-10-28
- **审查方式**：静态代码审查 + 在测试 VM 上启动 GateServer / StatusServer / ChatServer 做黑盒验证
- **结论**：演示链路可以跑通，但当前版本**不具备对外部署条件**。主要问题不是个别边界条件，而是认证授权模型缺失、消息可靠性设计不闭环，以及多处并发/关停缺陷。

> 说明：动态验证结束后已停止测试进程，并清理了测试写入的 Redis 键；未修改数据库业务数据。行号对应审查时的源码快照。

---

## 0. 实测验证结果（不是纯理论推断）

| 验证项 | 操作 | 结果 |
|---|---|---|
| GateServer 越权读取 | 不带任何 token，`POST /get_my_friends {"uid":1}` / `{"uid":2}` | 分别返回 uid=1、uid=2 的好友列表，含对方邮箱 |
| StatusServer 任意签发 token | 调 gRPC `GetChatServer(uid=999999)` | 返回 `host/port/token`，且 Redis `utoken_999999` 被写入该 token |
| 登录接受密码哈希 | `POST /user_login`，`passwd` 填数据库中的 SHA-256 值 | `error=0`，登录成功 |
| 用户名登录 | `POST /user_login`，`user="111"`，密码用同一哈希 | `error=1009`（只支持邮箱） |
| 聊天登录泄露密码哈希 | TCP 登录 uid=1 | 回包 JSON 中直接含 `"pwd": "<sha256>"` |
| 长消息 | 登录后发送 3077 字节的 1017 消息 | 服务端打印 `msg_len too large: 3077 (max: 2048)`，连接被 RST |
| 优雅关停 | 对 ChatServer 发送 `SIGINT` | 进程抛出 `std::system_error: Resource deadlock avoided` 后终止 |

---

## 1. P0：认证、授权与凭据（必须先修）

### 1.1 GateServer 的好友/资料接口完全没有鉴权（IDOR）

**位置**：`GateServer/GateServer/LogicSystem.cpp:397`、`:460`、`:508`、`:558`、`:631`

`/search_friends`、`/get_friend_requests`、`/get_my_friends`、`/send_friend_request`、`/reply_friend_request` 都直接从请求体取 `uid` / `from_uid` / `to_uid`，整个 GateServer 没有校验登录 token，也没有“当前会话 uid”的概念。

**实测**：不携带 token 请求 uid=1、uid=2 的好友列表，均成功返回，并带出好友邮箱。

**影响**：

- 任意人可读取任意用户的好友列表、好友申请、邮箱等隐私数据；
- 可以伪造 `from_uid` 给任意人发好友申请；
- 可以伪造 `from_uid/to_uid` 替别人同意好友申请，建立任意好友关系。

**修复**：登录后签发短期 access token（或会话 cookie），所有接口从 token 解析 uid；请求体中的 uid 只作为“目标 uid”，且必须校验调用者权限（例如 reply 只能由 `to_uid` 本人操作）。

### 1.2 StatusServer 可为任意 uid 签发 token，且监听 0.0.0.0

**位置**：`StatusServer/StatusServer/StatusServiceImpl.cpp:127-157`、`:45-50`；`StatusServer/StatusServer/config.ini:3`

`GetChatServer` 只接收一个 `uid`，不校验密码、token 或调用方身份，直接生成 UUID 并写入 Redis `utoken_<uid>`，然后把 token 返回。同时 gRPC 监听 `0.0.0.0:50052`，docker-compose 还把 50052 映射到宿主机。

**实测**：对不存在的 uid=999999 调用也成功返回 token，并写入 Redis。

**影响**：只要知道受害者的数字 uid，就可以拿到该 uid 的合法 token，再按 ChatServer 的登录协议连上聊天服务器，**完整接管账号**。这不是“越权读数据”，而是账号冒充。

**修复**：

- StatusServer 只允许 GateServer 调用（mTLS 或服务间共享密钥），不要对公网/办公网暴露 50052；
- `GetChatServer` 必须和密码校验绑定，或改成“GateServer 校验密码后，用自己的服务身份代为申请”；
- 校验 uid 存在且合法，记录签发审计日志。

### 1.3 ChatServer 信任客户端提交的 uid/fromuid，登录后不绑定会话身份

**位置**：`ChatServer/ChatServer/LogicSystem.cpp:180-181`（fromuid）、`:264`（离线消息 uid）、`:310-311`（ACK uid）

登录时只把 uid 写进 `CSession::_user_uid`（`CSession.cpp:21-24`），但后续所有业务处理都不使用它：

- 发消息用请求体里的 `fromuid`；
- 拉离线消息用请求体里的 `uid`；
- ACK 也用请求体里的 `uid` 和 `max_msg_id`。

**影响**：

- 任意已连接客户端可以冒充任何 uid 发消息；
- 可以拉取任意用户的离线消息（其中包含聊天内容）；
- 可以替任意用户 ACK，把对方未读消息直接标记为已读，造成消息丢失。

此外，`DealChatTextMsg` 不校验 `touid` 是否存在、也不校验双方是否为好友，任何登录用户都可以向任意 uid 发消息，配合前面的 fromuid 冒充可直接形成 spam/骚扰链路。

**修复**：登录成功后只信任 `session->GetUserId()`；任何请求体中的 fromuid/uid 与 session uid 不一致时直接拒绝；离线拉取和 ACK 只能操作当前登录 uid。

### 1.4 登录接受密码哈希（pass-the-hash），且聊天登录回包直接返回密码哈希

**位置**：`GateServer/GateServer/MysqlDao.cpp:635-637`；`ChatServer/ChatServer/LogicSystem.cpp:135`；`ChatServer/ChatServer/crypto_utils.cpp:6`

GateServer 的 `CheckPwd` 有一分支：

```cpp
if (is_hex64(incoming) && to_lower_copy(incoming) == db_pwd_norm) {
    return true;
}
```

只要提交 64 位十六进制字符串且等于数据库中的哈希，就直接登录成功。**实测用邮箱 + 数据库中的 SHA-256 哈希登录，返回 error=0。**

同时 ChatServer 登录成功回包包含 `"pwd": "<sha256>"`，等于把“密码等价物”交给客户端；日志里也会打印。

**影响**：

- 数据库泄露、日志泄露、客户端抓包/内存读取，任何一处拿到哈希都可以直接登录；
- 哈希是无盐 SHA-256，离线爆破成本很低；
- 密码哈希本身不应离开服务端。

**修复**：

- 删除“哈希当密码”的分支；
- 使用 Argon2id/bcrypt/scrypt + 每用户随机盐，替换无盐 SHA-256；
- 登录响应、Redis 缓存、日志中都不出现密码哈希；
- 如果确实需要客户端预哈希，必须走 challenge-response 协议，而不是把哈希当密码。

另外，`CheckPwd` 的 SQL 实际只按 `email` 查询（`GateServer/GateServer/MysqlDao.cpp:589`），所以注释/API 声称的“用户名或邮箱二选一”只支持邮箱；实测用用户名 `111` 登录返回 `1009`。

### 1.5 仓库中提交了真实凭据

**位置**：`VerifyServer/config.json:4`；`GateServer/GateServer/config.ini:13`；`StatusServer/StatusServer/config.ini:8`、`:13`；`ChatServer/ChatServer/config.ini:13`、`:17`

`VerifyServer/config.json` 被 git 跟踪，内容包含：

```json
"email": { "user": "2668348774@qq.com", "pass": "cnqyagbmejxedjjj" }
```

这是 QQ 邮箱 SMTP 授权码形态的凭据，且从初始提交就在历史中。各服务 MySQL/Redis 也统一使用 `123456`，并写进配置和 docker-compose。

**影响**：拿到仓库的人可以直接用该邮箱发信；数据库/Redis 口令也是弱口令且公开。

**修复**：

- 立即在邮箱后台吊销该授权码并重新生成；
- 从 git 历史中清除 `VerifyServer/config.json`（仅当前文件删除不够，历史仍可检出）；
- 改用环境变量/密钥管理，仓库只保留 `config.example`；
- 更换 MySQL/Redis 口令，禁止默认弱口令。

### 1.6 全链路明文传输

**位置**：`ChatClient/httpmgr.cpp`、`ChatClient/main.cpp:50`（`http://`）、`GateServer/GateServer/CServer.cpp`（裸 HTTP）、`ChatServer/ChatServer/CSession.cpp`（裸 TCP）、`GateServer/GateServer/StatusGrpcClient.h:49`、`VerifyGrpcClient.cpp:36-37`（`InsecureChannelCredentials`）

登录密码、token、聊天内容全部明文经过网络；gRPC 也是 insecure。

**影响**：同一局域网/云内网/可抓包链路上的攻击者可以直接拿到密码和 token，前面所有认证修复都会被绕过。

**修复**：GateServer 上 HTTPS（或至少反向代理 TLS），ChatServer 上 TLS，gRPC 使用 mTLS；客户端校验证书。

### 1.7 ChatServer 的内部 gRPC 接口无鉴权，可向任意用户注入消息

**位置**：`ChatServer/ChatServer/ChatServiceImpl.cpp:70-114`、`:16-58`；`docker-compose.yml:73-74`

`NotifyTextChatMsg` 直接信任请求中的 `fromuid/touid/textmsgs`，找到目标 session 就下发 1019；`NotifyAddFriend` 同样直接下发通知。gRPC 端口 50055 在 docker-compose 中映射到宿主机。

**影响**：任何能访问 50055 的人都可以伪造任意发送者，向任意在线用户推送消息/好友通知，不需要登录。

**修复**：内部 gRPC 加 mTLS/服务 token；不要映射到宿主机；校验来源服务器身份；对 `fromuid` 做合法性校验。

---

## 2. P1：消息可靠性与核心功能缺陷

### 2.1 单条消息上限实际只有 2KB，长消息会直接断连

**位置**：`ChatServer/ChatServer/CSession.h:11`（`#define MAX_LENGTH 1024 * 2`）、`CSession.cpp:211-216`；客户端 `ChatClient/tcpmgr.h:14` 是 4096

服务端先判断 `msg_len > MAX_LENGTH`（2048），再判断 10MB 的 `MAX_PACKAGE_SIZE`，所以 10MB 检查永远不可达。实测 3077 字节消息触发 `msg_len too large: 3077 (max: 2048)` 并被断开。

**影响**：用户发送稍长的中文文本/JSON（含转义后很容易超过 2KB）会直接掉线；客户端和服务端上限还不一致。

**修复**：统一协议常量（例如 64KB 或 1MB），服务端和客户端共用同一份定义；超限时返回明确错误码而不是直接断开。

### 2.2 离线消息 ACK 的 ID 语义完全错位，会误删/重复消息

**位置**：客户端 `ChatClient/mainwindow.cpp:367`、`:384`；服务端 `ChatServer/ChatServer/MysqlDao.cpp:530`、`LogicSystem.cpp:259-301`

- 客户端把服务端下发的 `msgid`（实际是发送端用毫秒时间戳生成的字符串）转成 `long long` 存本地；
- ACK 时发送 `max_msg_id = 本地 MAX(msg_id)`，即一个约 1.7e12 的时间戳；
- 服务端 `messages.id` 是 `AUTO_INCREMENT`（实测当前只到 46），SQL 是 `WHERE to_uid=? AND id <= ? AND status=0`。

因此一个时间戳会匹配该用户**所有**未读记录，包括还没下发、甚至还没插入的记录。客户端 ACK 是“收到一条就立即 ACK”，DB 插入又是异步的，存在“ACK 先执行、INSERT 后执行”的竞态，后插入的消息仍为未读，下次登录重复下发；而如果 ACK 先覆盖了本应保留的消息，则会丢消息。

另外，客户端用毫秒时间戳做 `msgid`，同一毫秒内两条消息会撞本地 `UNIQUE` 约束，其中一条被当成重复丢弃。

**修复**：

- 服务端生成全局消息 ID（雪花/自增），在 1019 中下发；
- 客户端按服务端 ID ACK，服务端按 ID 精确更新；
- 登录时带 cursor，服务端只发 `id > cursor` 的消息；
- 用 (fromuid, msgid) 或服务端 ID 做幂等，避免时间戳碰撞。

### 2.3 “先持久化再投递”实际是异步的，失败也不回传

**位置**：`ChatServer/ChatServer/LogicSystem.cpp:193-203`、`:288-300`；`ChatGrpcClient.cpp:84-108`、`:128-152`

`DealChatTextMsg` 先把 `SaveChatMessage` 投递到 `AsyncDBPool`，不等待结果，随后 `Defer` 无条件给发送方回 1018 success。DB 写失败、进程在任务执行前崩溃，发送方仍然看到成功。

`ChatGrpcClient` 里的 `Defer` 更严重：函数开头注册的 `Defer` 在返回时把 `rsp.error` 重置为 `Success`，所以即使下面设置了 `RPCFailed`，最终也变成成功。`NotifyAddFriend` 还缺少 `stub` 空指针检查。

**影响**：跨服消息、离线消息在 RPC 失败/DB 失败时被静默丢弃，发送方和 UI 都以为成功。

**修复**：

- 持久化成功后再回 ACK，或引入 outbox/事务消息；
- 删除会覆盖错误的 `Defer`，只在成功路径设置默认值；
- 对 `getConnection()` 判空；
- 给跨服 RPC 设置 deadline，并做重试/离线落库。

### 2.4 重复登录后，旧连接关闭会删掉新连接的 uid 映射

**位置**：`ChatServer/ChatServer/UserMgr.cpp:45-49`；`ChatServer/ChatServer/CServer.cpp:61-72`

`SetUserSession` 直接覆盖 `uid -> session`；`ClearSession` 在任意 session 关闭时都调用 `RmvUserSession(uid)`，不检查当前映射是否还是自己。

**影响**：用户重连/换设备后，旧连接关闭会把新连接的映射删掉，用户变成“在线但不可路由”；旧连接也没有被踢掉，仍可继续发消息。

**修复**：`ClearSession` 先比较 `UserMgr` 中保存的 session 是否等于当前 session，只有相等才删除；重复登录时主动关闭旧 session；会话表保存 session_id 而不是裸 uid。

### 2.5 `_user_uid` 未初始化，未登录连接断开可能误删任意用户映射

**位置**：`ChatServer/ChatServer/CSession.h:62`；构造函数 `CSession.cpp:4-10`；使用点 `CServer.cpp:69`

`int _user_uid;` 没有在构造函数初始化列表中赋值。任何“连上但未登录”就断开的连接，`ClearSession` 都会用这个 indeterminate 值调用 `RmvUserSession`，可能删掉某个真实 uid 的映射；同时 `Send`/日志读取该值也是数据竞争 + UB。

**修复**：构造时 `_user_uid(0)`；`ClearSession` 仅在 `uid > 0` 且映射匹配时删除；`_user_uid` 改为 `std::atomic<int>` 或在同一线程内读写。

### 2.6 ChatServer 优雅关停必然崩溃（已实测）

**位置**：`ChatServer/ChatServer/ChatServer.cpp:348-357`；`ChatServer/ChatServer/AsioIOServicePool.cpp:42-55`

信号处理器绑定在 `pool->GetIOService()` 上，因此它在 IO 池的某个工作线程里执行；处理器里调用 `pool->Stop()`，`Stop()` 又对**所有** `_threads` 执行 `join()`，包括当前线程。

实测发送 SIGINT：

```
signal 2 received, stopping...
terminate called after throwing an instance of 'std::system_error'
  what():  Resource deadlock avoided
```

此外池单例析构时还会再次调用 `Stop()`，第二次访问已 reset 的 `_works` 会空指针。

**影响**：Ctrl+C / systemd stop 都不能优雅退出，连接和 DB 任务没有清理机会。

**修复**：信号处理器只置停止标志并通知主线程；由主线程调用 `pool->Stop()`；`Stop()` 做幂等保护并跳过当前线程；不要 join 自己；先停止 acceptor，再停止 IO，再 join。

### 2.7 Redis 连接池会无限等待、空指针崩溃、泄漏连接

**位置**：`ChatServer/ChatServer/RedisMgr.h:17-29`、`:41-56`、`:34-39`；`RedisMgr.cpp:516-523`（GateServer 的副本同样存在）

- 构造时 `redisCommand(AUTH)` 可能返回 `nullptr`，代码直接 `reply->type`；
- 所有连接失败时池为空，`getConnection()` 的 `cond_.wait` 没有超时，第一次 Redis 操作就永久挂起；
- 析构只 `pop()`，不 `redisFree()`，连接和 fd 泄漏；
- `Close()` 直接 `con_pool_.reset()`，其他线程可能还在使用池指针/连接，存在 UAF。

**影响**：Redis 抖动/重启会导致整个服务卡死；长期运行泄漏 fd；关停时崩溃。

**修复**：初始化失败 fail fast 或后台重连；`getConnection` 加超时；对连接做 ping/健康检查，坏连接销毁重建；析构和 Close 里 `redisFree` 所有 context；Close 先阻止新请求并等待在途请求。

### 2.8 MySQL 连接池在持锁期间做网络 IO；GateServer 池耗尽会永久等待

**位置**：`ChatServer/ChatServer/MysqlDao.h:152-219`、`:224-254`；`GateServer/GateServer/MysqlDao.cpp:110-118`

- ChatServer 的 `getConnection()` 持有 `mutex_` 时执行 `isConnectionValid()`（`SELECT 1`）和 `driver_->connect()`；`returnConnection()` 也在持锁时重连。这会把所有 DB 使用者串行阻塞在锁上。
- GateServer 的 `getConnection()` 是 `cond_.wait` 无超时；池被 10 个慢查询占满后，HTTP 处理线程会永久阻塞，而 GateServer 的 HTTP handler 本身就跑在 IO 线程上。

**影响**：DB 变慢 → 所有业务线程被拖死 → 健康检查/心跳也无法处理，表现为“服务假死”。

**修复**：网络 IO 放在锁外；池等待加超时并返回可识别错误；GateServer 的 DB 操作移到独立线程池，不要阻塞 Asio IO 线程。

### 2.9 LogicSystem 在持锁状态下执行所有业务回调，且接收队列无上限

**位置**：`ChatServer/ChatServer/LogicSystem.cpp:31-40`、`:388-428`；`CSession.h:18`（`MAX_RECVQUE` 定义但从未使用）

`DealMsg` 在循环开头拿 `unique_lock`，直到回调执行完、`_msg_que.pop()` 之后才释放。回调里包含同步 Redis、MySQL、gRPC 调用，所以整个服务器的消息处理被一把锁串行化；同时 `PostMsgToQue` 无队列上限，客户端可以持续灌消息把内存打满。

**影响**：一个慢查询/慢 RPC 会阻塞所有连接的消息入队；高并发下内存无界增长。

**修复**：取出消息后立刻释放锁，再执行回调；接收队列设上限并做背压/丢包策略；如果业务确实需要并行，改为多 worker + 每用户串行。

### 2.10 验证码可重放、可暴力尝试；token 永不过期

**位置**：`GateServer/GateServer/LogicSystem.cpp:138-168`（注册）、`:227-240`（重置）；`StatusServer/StatusServer/StatusServiceImpl.cpp:45-50`

- 注册/重置成功后都没有 `DEL code_<email>`，同一个验证码在 3 分钟 TTL 内可以反复使用；
- 没有失败次数限制/锁定，6 位数字验证码可被在线爆破；
- `insertToken` 只 `SET`，没有 `EXPIRE`，token 永久有效；没有登出/失效机制。

**影响**：验证码泄露或爆破后可反复重置密码；token 泄露后永久可用。

**修复**：验证成功原子性删除 code；按邮箱+IP 限制尝试次数；验证码用密码学随机数；token 设置 TTL 并支持刷新/吊销。

### 2.11 密码、token、验证码、配置口令大量写入日志

**位置**：`GateServer/GateServer/LogicSystem.cpp:265`（完整登录 body）、`:301-313`（密码十六进制）；`ChatServer/ChatServer/LogicSystem.cpp:92`（token）、`:135`、`:356`（pwd）；`VerifyServer/server.js:84`（验证码）；`ChatServer/ChatServer/ConfigMgr.cpp:44-52`、Gate `ConfigMgr.cpp:54-62`（打印全部配置，含 MySQL/Redis 口令）；`RedisMgr::Set` 会打印 value

**影响**：任何能看日志的人都能拿到密码/token/验证码/数据库口令；日志还会被 nohup 落到 `/tmp` 长期保留。

**修复**：统一日志脱敏（password/token/code/pwd 字段打码）；禁止打印完整请求体；配置启动日志只打印非敏感项。

---

## 3. P2：客户端、部署、构建与文档

### 3.1 客户端聊天记录只显示“最旧的 50 条”，不是最近 50 条

**位置**：`ChatClient/localdb.cpp:101-104`；`ChatClient/chatdialog.cpp:423`

SQL 是 `ORDER BY msg_id ASC LIMIT ? OFFSET ?`，调用方 offset=0、limit=50，所以拿到的是最早 50 条；注释和 UI 预期都是“最近 50 条”。重启客户端后，新消息可能完全不在窗口里。

**修复**：`ORDER BY msg_id DESC LIMIT 50` 后在内存反转；或按 cursor 分页。

### 3.2 部署配置文件名与仓库文件不一致，干净构建无法直接启动

**位置**：`ChatServer/ChatServer/ConfigMgr.cpp:18`（读 `config_chat1.ini`）、`ChatServer2/ChatServer/ConfigMgr.cpp:18`（`config_chat2.ini`）、`GateServer/GateServer/ConfigMgr.cpp:27`（`config_gate.ini`）、`StatusServer/StatusServer/ConfigMgr.cpp:6`（`config_status.ini`）；仓库里实际是各自的 `config.ini`

CMake 没有 install/copy 规则，README 的 `mkdir build && cmake .. && make` 产物目录里没有这些重命名后的配置。当前 `build_release/bin` 能跑，是因为手工放了 `config_chat1.ini` 等文件。另外 `ChatServer.cpp:122` 打印的是 `config_chat2.ini`，但 ConfigMgr 实际读 `config_chat1.ini`，排障会被误导。

**修复**：统一配置文件名（或 CMake `configure_file`/install 到 bin）；用 `--config` 参数或环境变量；修正日志。

### 3.3 Docker 部署把聊天服务器地址返回成 127.0.0.1

**位置**：`docker-compose.yml:48-61`（status-server 未设置 `CHAT_HOST`）；`StatusServer/entrypoint.sh` 的 `CHAT_HOST` 默认 `127.0.0.1`

StatusServer 的 entrypoint 会把 `[chatserver1] Host` 改成 `CHAT_HOST`，compose 没传，所以容器内是 `127.0.0.1:8090`。GateServer 把该地址返回给客户端，客户端连自己的 localhost，必然失败。

**修复**：compose 给 status-server 增加 `CHAT_HOST=chat-server`（以及 chat2 对应值），或让 StatusServer 直接使用服务名；端到端跑一次 Docker 登录。

### 3.4 `UrlDecode` 仍使用 assert 和未初始化返回值；安全文档声称已修复

**位置**：`GateServer/GateServer/HttpConnection.cpp:120-139`、`:65-73`

`assert(i + 2 < length)` 在 Release 下被移除；`FromHex` 对非法字符只 `assert(0)`，Release 下返回未初始化的 `y`。畸形 GET 查询（如 `?a=%`）会走到 UB 路径。当前没有注册 GET 路由，实际影响有限，但代码与 `docs/SECURITY_FIX_REPORT_P0_VULNERABILITIES.md` 中“已修复”的描述不符。

**修复**：显式边界检查 + `isxdigit` 校验 + 非法输入直接拒绝；删除 assert 依赖。

### 3.5 VerifyServer 的 proto 字段名写错，验证码字段实际为空

**位置**：`VerifyServer/server.js:108-112` 返回 `verifycode`；`VerifyServer/message.proto:16` 字段名是 `code`

gRPC 序列化只认 proto 字段，`verifycode` 会被忽略，所以 `code` 永远为空。当前 GateServer 不使用该字段，所以没有直接功能失败；但如果后续客户端/测试依赖它，会得到空值。

**修复**：改为 `code: verifyCode`，并补一个 gRPC 集成测试。

### 3.6 验证码使用 `Math.random()`，且没有请求频率限制

**位置**：`VerifyServer/server.js:33-39`、`:56-123`

`Math.random()` 不是密码学随机数；同一邮箱可无限次请求验证码，既可用于邮件轰炸，也增加爆破窗口。

**修复**：`crypto.randomInt(0, 10)`；按邮箱/IP 做冷却和每日上限；记录发送审计。

### 3.7 客户端没有断线重连/失败反馈，发送长度也不校验

**位置**：`ChatClient/tcpmgr.cpp:139-147`（errorOccurred/disconnected 只 qDebug）、`:391-417`（发送）、`ChatClient/httpmgr.cpp`（无超时）

TCP 连接失败时没有 `sig_con_success(false)`，登录窗口会一直等待；连接断开后 UI 不知道，消息 `write` 后直接显示成功，实际可能丢失。`slot_send_data` 把 `dataBytes.size()` 强转 `quint16`，超过 65535 会回绕。

**修复**：补连接失败/断开信号、重连与心跳；`write` 返回值/bytesWritten 检查；发送前校验上限；HTTP 请求加超时。

### 3.8 聊天内容可能按富文本渲染

**位置**：`ChatClient/chatbubble.cpp:79` 使用 `QTextBrowser::setText(text)`

`setText` 会检测内容是否像 HTML；以 `<` 开头或包含 HTML 标签的消息可能被当作富文本解析，存在 HTML 注入/外部资源加载风险。聊天内容应始终按纯文本显示。

**修复**：`setPlainText(text)`；如确需富文本，先做严格白名单清洗。

### 3.9 本地聊天库明文存放在当前工作目录

**位置**：`ChatClient/localdb.cpp:18`

`./chat_<uid>.db` 相对当前工作目录，未加密，消息内容明文；切换工作目录会“丢历史”，多用户/共享机器上也可能读到别人的库。

**修复**：`QStandardPaths::AppDataLocation`；SQLCipher 或至少文件权限 0600；按 uid 分库并做生命周期管理。

### 3.10 客户端/服务端错误码枚举不一致

**位置**：`ChatClient/global.h:57-61` 只有 `SUCCESS/ERR_JSON/ERR_NETWORK`；服务端 `ChatServer/ChatServer/const.h:27-42` 是 1001+

`TcpMgr` 把服务端错误码 `static_cast<ErrorCodes>` 后发信号，`LoginDialog::slot_login_failed` 只显示“登录失败”，没有可读原因；后续如果按枚举分支处理，会全部落到未知值。

**修复**：共享一份错误码定义（生成头文件），或客户端保留原始 int 并做映射。

### 3.11 代码重复与文档失真

- `ChatServer` 与 `ChatServer2` 是两份几乎相同的 `CSession/LogicSystem/RedisMgr/MysqlDao`，修复容易只改一边；`ChatServer2` 的配置文件名改动就是例子。
- `docs/SECURITY_FIX_REPORT_P0_VULNERABILITIES.md` 声称 Redis 命令已改为 `redisCommandArgv`，实际 `RedisMgr.cpp` 仍使用 `redisCommand`；该文档还引用了不存在的 `CacheInvalidationSync.cpp`、`GateServer_fixed.cpp`、`STABILITY_OPTIMIZATION_REPORT.md`。
- 需要说明：我用 hiredis 的 `redisFormatCommand` 验证过，`%s` 参数会按长度封装（`$7\r\nv\r\nPING`），CRLF 并不会拆成两条命令，所以“Redis 命令注入”这个前提本身也不准确；但“已修复”的声明与代码不一致是事实，且 URL 解码部分确实未修复。
- `message.pb.*` 被 git 跟踪，但 CMake 没有 `protoc` 生成规则；改了 `.proto` 不会自动重生成，容易 schema 漂移。
- `tests/` 只有压测脚本，没有认证、好友、离线消息、关停的自动化测试。

**修复**：抽公共库消除 ChatServer/ChatServer2 重复；用 `protobuf_generate`；补集成测试；修正安全报告并把“已验证”与“未验证”分开写。

### 3.12 登录计数只增不减、没有健康检查，负载均衡会失真

**位置**：`ChatServer/ChatServer/LogicSystem.cpp:147-155`；`ChatServer/ChatServer/UserMgr.cpp:60-71`；`StatusServer/StatusServer/StatusServiceImpl.cpp:81-103`

登录时 `HINCR` 语义的 HGet+HSet 只加不减；断开连接不减少计数（`RmvUserSession` 里 Redis 删除被注释掉），只在进程优雅退出时 `HDEL`。StatusServer 只看这个历史计数，不探测 ChatServer 是否存活；某个节点挂掉后仍可能被选中。

**影响**：连接数统计长期偏高，流量分配越来越偏；节点故障时登录会拿到不可用的 host/port。

**修复**：连接建立/断开时原子增减；定期心跳/健康检查；StatusServer 选择前探测或依赖注册中心；计数加 TTL 兜底。

### 3.13 `short` 长度字段存在溢出风险

**位置**：`ChatServer/ChatServer/CSession.cpp:72`（`SendNode(..., msg.length(), ...)`）；`ChatServer/ChatServer/MsgNode.h:52`（`short _total_len`）

`std::string::length()` 是 `size_t`，传给 `short max_len`；超过 32767 会变成负数，`new char[_total_len + 1]` 会按巨大 size_t 分配或抛 `bad_alloc`。当前 2KB 入站限制掩盖了问题，但离线消息 payload 或未来放宽上限后会触发。

**修复**：协议长度统一用 `uint32_t`，所有分配前做上限检查；`MsgNode` 用 `size_t`。

---

## 4. 建议修复顺序

1. **立即（今天）**
   - 下线/防火墙封禁 50051、50052、50055，只允许内网调用；
   - 轮换 SMTP 授权码、MySQL/Redis 口令；
   - 删除 `VerifyServer/config.json` 并清理 git 历史；
   - 删除 pass-the-hash 分支，停止回传/打印密码哈希、token、验证码。
2. **本周（阻断账号接管）**
   - GateServer 全接口 token 鉴权，uid 从 token 派生；
   - StatusServer/VerifyServer/ChatServer gRPC 加服务间认证；
   - ChatServer 会话绑定 uid，拒绝 fromuid/uid 冒充；
   - 验证码一次性消费 + 尝试次数限制；token 加 TTL。
3. **下一迭代（消息可靠性）**
   - 服务端消息 ID + cursor + 精确 ACK；
   - 持久化成功再回 ACK，跨服 RPC 失败可见；
   - 统一 64KB/1MB 消息上限；
   - 修复重复登录/旧会话清理。
4. **稳定性**
   - 修 ChatServer 关停崩溃、Redis/MySQL 池、LogicSystem 锁和无界队列；
   - 加心跳/空闲超时、健康检查、断线重连。
5. **工程化**
   - 统一配置文件名和部署方式；修 Docker 的 `CHAT_HOST`；
   - 抽公共库、生成 protobuf、补集成测试；
   - 修正安全文档，禁止再把“计划”写成“已修复”。

---

## 5. 未覆盖/说明

- 未做真实邮件发送测试，避免向真实邮箱发信；VerifyServer 的结论来自代码和 proto 对比。
- 未做 Qt 客户端实机 UI 操作，客户端结论来自代码审查。
- 未做 30 分钟以上长稳/压测；README 中的性能数据未复现。
- 未审计 K8s manifests 的全部密钥，仅抽查 docker-compose。
- 未检查 `libs/` 预编译库的来源与完整性。

---

**总体评价**：这是一个功能演示完成度较高、但安全与可靠性设计尚未闭环的项目。最危险的不是“某个函数写错”，而是“客户端传什么服务端就信什么”的信任模型；在修掉 P0 之前，不应把 50051/50052/50055/8080/8090 暴露到任何不可信网络。
