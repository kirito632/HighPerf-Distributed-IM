# ChatServer 吞吐优化：调查、实现与复测

- 日期：2026-11-10
- 目标：在不改变消息语义（幂等、精确 ACK、会话内顺序）的前提下，解决单 ChatServer 约 100 msg/s 的吞吐上限。
- 结论：默认 4 个 worker 分片后，100 连接下从 **93 msg/s / p50 957 ms** 提升到 **235 msg/s / p50 417 ms**（约 2.5×），消息 0 丢失、顺序保持；`IM_CHAT_WORKERS=1` 可一键回退旧行为。

## 1. 调查：为什么吞吐卡在 100 msg/s

### 1.1 代码路径

`CSession::HandleRead` 解析完一条 TCP 消息后调用：

```cpp
LogicSystem::GetInstance()->PostMsgToQue(std::make_shared<LogicNode>(...));
```

旧实现里 `LogicSystem` 只有 **一个** `_worker_thread`：

```cpp
std::queue<std::shared_ptr<LogicNode>> _msg_que;
std::thread _worker_thread;
```

`DealMsg()` 从队列取一条、执行回调、再取下一条，并且**全程持有 `_mutex`**。`DealChatTextMsg` 在回调里同步调用 `MysqlMgr::SaveChatMessage`（4 条 SQL + commit），完成后才回 1018 ACK。

因此吞吐上限 = `1 / 单条处理时间`。实测单条 p50 约 10 ms，正好对应 100 msg/s；连接数增加只是让消息在队列里排队，延迟线性上升。

`AsioIOServicePool` 虽然把 TCP 连接分到 4 个 io_context，但业务处理全部汇聚到这一个线程，IO 多线程没有转化为业务吞吐。

### 1.2 线程安全审计

在改成多 worker 之前，逐个确认了共享组件是否支持并发调用：

| 组件 | 同步机制 | 结论 |
|---|---|---|
| `UserMgr` | `std::mutex` 保护 uid→session map | 安全 |
| `RedisMgr` / `RedisConPool` | 连接池 mutex + condvar，`RedisConnectionGuard` 归还连接 | 安全 |
| `MySqlPool` / `ConnectionGuard` | 连接池 mutex + condvar，每线程独占一条连接 | 安全 |
| `ChatGrpcClient` / `ChatConPool` | 每目标服务器一个 stub 池，mutex 保护 | 安全 |
| `ConfigMgr` | 构造后只读，`operator[]` 返回值拷贝 | 安全 |
| `Metrics` | 全局 mutex | 安全 |
| `Tracing` | 全局 mutex + OpenTelemetry SDK（线程安全） | 安全 |
| `CSession::Send` | `_send_lock` + strand，单连接写串行 | 安全（Delivery 线程本来就会并发 Send） |
| `_fun_callbacks` | 构造期填充，运行期只读 | 安全 |

唯一的额外约束是**同一会话的消息必须按顺序处理**（登录要先于发消息，ACK 顺序要与发送顺序一致）。

## 2. 方案对比

| 方案 | 会话内顺序 | 改动量 | 风险 |
|---|---|---|---|
| 单队列 + N worker | ❌ 会乱序 | 小 | 不可接受 |
| **按 session id 哈希分片，每片一个队列/线程** | ✅ | 中 | 低 |
| 在 session 的 io_context/strand 里直接处理 | ✅ | 中 | 阻塞 DB 会拖住同 io_context 的其他连接 |
| 全异步 DB + 回调恢复 | ✅ | 大 | 改动可靠性语义，暂不做 |

选择**按 session id 哈希分片**：同一会话固定落到同一片，天然保序；不同会话并行。

## 3. 实现

### 3.1 LogicSystem 分片

`ChatServer/ChatServer/LogicSystem.h`：

```cpp
struct WorkerShard {
    std::queue<std::shared_ptr<LogicNode>> queue;
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
};
std::vector<std::unique_ptr<WorkerShard>> _shards;
std::atomic<bool> _b_stop{false};
```

`PostMsgToQue` 按 `session->GetSessionId()` 哈希选片；`DealMsg(shard)` 每片一个线程，**在锁外执行回调**，因此入队不会被 DB 调用阻塞。

Worker 数量：

- `IM_CHAT_WORKERS` 环境变量优先；
- 其次 `[Feature] ChatWorkers`；
- 默认 `min(8, max(2, hardware_concurrency))`，本机为 4；
- `IM_CHAT_WORKERS=1` 即旧行为，作为回退开关。

`ChatServer2` 同步了相同实现。

### 3.2 顺带修复关停卡死

压测中发现 `SIGTERM` 后进程偶尔不退出。逐段加日志后定位到两处：

1. **信号回调里做了阻塞操作**：`signals.async_wait` 中直接调用 `grpc_server->Shutdown()`，若它阻塞，`cond_quit.notify_one()` 永远执行不到，主线程一直等。
   - 修复：信号回调只置 `bstop` + `notify`，`grpc_server->Shutdown()` 移到主线程，并补上 `[Shutdown]` 阶段日志。
2. **MetricsServer 阻塞 accept**：`common/metrics/MetricsServer.cpp` 用阻塞 `accept()`，`Stop()` 里 `close()` 不能可靠唤醒另一线程的 accept，`join()` 永久挂起（Prometheus 不抓取时必现）。
   - 修复：acceptor 设为 non-blocking，worker 用 20 ms 轮询；请求读取改为 non-blocking + 2 s deadline；`Stop()` 先 join 再 close。

这两处是原有的关停缺陷，被本轮压测暴露；修复后连续 5 次 SIGTERM 均能在 1 秒内退出。

## 4. SQL 路径调查（本轮未改）

打开 MySQL `general_log` 抓一条消息，事务实际包含：

```text
SET autocommit=0
INSERT messages ... ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id)
SELECT id FROM messages WHERE from_uid=? AND client_msg_id=?
UPDATE messages SET payload=? WHERE id=?
INSERT outbox ...
COMMIT
SET autocommit=1
```

按协议/Execute 计数约 **11 次 Questions/消息**（882 条消息 9659 次 Questions）。

可优化点与结论：

- **去掉 `SELECT id`**：本想用 `getGeneratedKeys()`，但 Ubuntu 22.04 的 MySQL Connector/C++ 1.1.12 的 `PreparedStatement` 没有该接口（`Statement` 也没有），无法零成本拿到自增 id；改用 `SELECT LAST_INSERT_ID()` 仍是 1 次往返。**收益有限，未改。**
- **去掉 `SET autocommit` 两次往返**：需要把连接池连接固定为 `autocommit=false`，并在 `ConnectionGuard` 归还时提交；要审计 ChatServer 所有 DAO 写方法（约 10 个），并让读方法也承担一次 commit。**风险/收益不划算，未改。**
- **存储过程一次往返完成 insert/select/update/outbox**：可行，但需要 schema 迁移与参数化 JSON，改动大，留作后续。
- **减少 UPDATE**：需要把 `server_msg_id` 从持久化 payload 中移除并在离线拉取时回填，会影响 Qt/离线协议，暂不做。

真正限制当前吞吐的是 **commit 的两次 fsync（redo + binlog）**：把测试库改成 `innodb_flush_log_at_trx_commit=2`、`sync_binlog=0` 后，同样 4 worker 从 210~235 msg/s 提升到 **331 msg/s**，说明 SQL 往返不是首要矛盾，磁盘持久化才是。该设置仅用于测试环境量化，不进入仓库默认配置。

## 5. 复测结果

环境同 `PERFORMANCE_REPORT.md`：4 vCPU、MySQL/Redis/Kafka 同机、Kafka+Outbox 开启、tracing 关闭、单 ChatServer。

### 5.1 优化前后（100 连接 / 8 秒）

| 版本 | 吞吐 | p50 | p95 | 错误 |
|---|---|---|---|---|
| 单 worker（优化前） | 93 msg/s | 957 ms | 1875 ms | 0 |
| 4 worker（默认） | 235 msg/s | 417 ms | 501 ms | 0 |
| 6 worker | 269 msg/s | 357 ms | 473 ms | 0 |
| 8 worker | 203 msg/s | 387 ms | 950 ms | 0 |

### 5.2 全并发档位（4 worker）

| 连接数 | 吞吐 | p50 | p95 |
|---|---|---|---|
| 1 | 103 msg/s | 9.6 ms | 13.8 ms |
| 10 | 197 msg/s | 48 ms | 95 ms |
| 50 | 189 msg/s | 246 ms | 380 ms |
| 100 | 235 msg/s | 417 ms | 501 ms |
| 200 | 232 msg/s | 789 ms | 994 ms |

- 100 连接：吞吐 **2.5×**，p50 从 957 ms 降到 417 ms。
- 200 连接不再像旧版那样延迟爆炸到 2+ 秒。
- 4 worker 时 Chat 进程约 0.5 核；MySQL 峰值约 1.5 核、1 commit/消息，瓶颈转移到数据库 commit。
- 6/8 worker 提升不稳定（8 worker 在 100 连接反而下降），默认取 4 与硬件线程数一致。

### 5.3 回归验证

```text
40 integration tests passed, 1 skipped（Kafka opt-in）
含：消息幂等、精确 ACK、cursor 拉取、spoof 拒绝、投递幂等/DLQ、metrics、tracing、etcd、nginx
新增 test_concurrency.py：8 会话 × 15 条，验证会话内 server_msg_id 递增、跨会话不重复
IM_CHAT_WORKERS=1：并发回归测试通过，100 连接回落到 104 msg/s（与旧版一致）
```

## 6. 风险与回退

- 回退：`IM_CHAT_WORKERS=1` 恢复单队列单线程；代码级回退为单个 commit。
- 顺序保证依赖 `session id` 稳定；同一会话的所有消息（登录/发消息/拉取/ACK）都走同一分片。
- 关停：主线程按 acceptor → gRPC → IO 池 → 注册中心 → Kafka → Redis → subscriber 的顺序停止，`LogicSystem` 析构时各分片 drain 队列后 join。
- 长稳/故障注入尚未做，属于 Step 11 剩余部分。

## 7. 下一步

1. 存储过程或批量提交，把每条消息的 SQL 往返从 11 降到 1~4；
2. 测试环境量化 `innodb_flush_log_at_trx_commit=2`（生产需评估持久性），或使用独立 binlog 盘；
3. 多 ChatServer 实例 + etcd 分流，验证线性扩容；
4. Step 11 故障注入：Kafka 停机、消费者重启、DLQ、节点宕机。
