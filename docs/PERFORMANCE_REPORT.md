# IM 性能压测报告

- 日期：2026-11-10
- 对象：`feature/observability`（commit `bd8400f` 之后的运行态）
- 目的：在 Step 11（故障注入 + 压测）之前，先拿到当前数据面的容量基线，定位瓶颈。

## 1. 结论摘要

| 场景 | 并发 | 吞吐 | p50 | p95 | 错误 |
|---|---|---|---|---|---|
| TCP 发消息（持久化 + Outbox + Kafka） | 1 连接 | 84 msg/s | 10 ms | 22 ms | 0 |
| TCP 发消息 | 10 连接 | 98 msg/s | 90 ms | 194 ms | 0 |
| TCP 发消息 | 50 连接 | 91 msg/s | 483 ms | 958 ms | 0 |
| TCP 发消息 | 100 连接 | 93 msg/s | 957 ms | 1875 ms | 0 |
| Gate HTTP `/get_my_friends` | 8 线程 | 493 req/s | 15 ms | 28 ms | 0 |
| Outbox → Kafka → Delivery 端到端 | 1 对 | 100 msg/s（发送侧） | 573 ms | 1454 ms | 0 丢失 |

**一句话结论**：当前单 ChatServer 的消息吞吐硬顶在 **约 100 msg/s**，与连接数无关，延迟随并发线性恶化。根因不是 CPU 或 MySQL 打满，而是 **LogicSystem 只有一个消费线程**，串行执行「每条消息 11 条 SQL 的同步事务」。这套架构适合演示，但离“高并发 IM”还有明确的优化空间（见第 5 节）。

## 2. 环境与方法

### 2.1 硬件与部署

| 项 | 值 |
|---|---|
| CPU | 11th Gen Intel i5-11320H @ 3.20GHz，VM 分配 4 vCPU |
| 内存 | 9.7 GiB（压测时可用约 6 GiB） |
| 磁盘 | 69 GB，已用 90%（剩余 7.2 GB） |
| 部署 | 单 VM，Docker 29.8；MySQL 8 / Redis 7 / Kafka 3.8 / etcd / Jaeger 同机 |
| 服务 | `chat_server`（chatserver1）、`gate_server`、`status_server`，Release 构建 |

### 2.2 配置

- `JwtEnabled=true`，`KafkaEnabled=true`，`OutboxEnabled=true`，`DeliveryMode=kafka`。
- `TracingEnabled=false`（本报告为关闭 tracing 的基线；tracing 开启会额外导出 span）。
- `EtcdEnabled=true`（只影响启动注册，不在消息路径上）。
- 客户端与服务器同机，走 loopback，网络延迟可忽略。

### 2.3 压测工具

新增 `tests/load/load_im.py`，三种模式：

- `chat`：用测试私钥离线签发 RS256 access token（绕过 Gate 的 60 次/分钟登录限流），建立 N 条 TCP 连接，按真实 `1017/1018` 协议同步收发，记录 RTT 与吞吐；通过 `/proc/<pid>/stat` 采样服务 CPU/RSS。
- `http`：N 个线程复用 Bearer token 调 `/get_my_friends`，测 Gate HTTP 容量。
- `delivery`：uid 1 发送、uid 2 在线接收，按发送顺序配对通知，测 Outbox→Kafka→Delivery 的端到端延迟与丢失。

```bash
# 示例
~/venvs/im/bin/python tests/load/load_im.py chat --connections 100 --duration 8 \
  --pids chat=$(cat .run-test/chat.pid),gate=$(cat .run-test/gate.pid) --json
```

每个档位预热后运行 8 秒；`chat` 档位之间留 1~2 秒让 Outbox 队列排空。

## 3. 结果

### 3.1 TCP 消息吞吐

| 连接数 | 发送/接收 | 吞吐 | p50 | p95 | p99 | Chat CPU |
|---|---|---|---|---|---|---|
| 1 | 673 / 673 | 84.1 msg/s | 10.0 ms | 22.4 ms | 31.5 ms | 28% |
| 10 | 791 / 791 | 97.7 msg/s | 90.5 ms | 193.9 ms | 332.9 ms | 30% |
| 50 | 778 / 778 | 90.9 msg/s | 482.8 ms | 957.7 ms | 1260.6 ms | 28% |
| 100 | 847 / 847 | 93.1 msg/s | 957.1 ms | 1875.4 ms | 2381.5 ms | 28% |

- 吞吐基本恒定在 90~100 msg/s，说明系统内部有一个**串行化点**。
- 延迟与并发近似线性：`p50 ≈ 连接数 × 10ms`，排队发生在同一个处理线程前。
- Chat 进程 CPU 只有约 0.28 核（4 核的 7%），说明时间花在等待 MySQL，而不是计算。

### 3.2 Gate HTTP

| 线程 | 请求数 | 吞吐 | p50 | p95 | Gate CPU |
|---|---|---|---|---|---|
| 1 | 1738 | 217 req/s | 4.2 ms | 7.2 ms | 33% |
| 4 | 2818 | 351 req/s | 9.3 ms | 22.9 ms | 70% |
| 8 | 3947 | 493 req/s | 14.9 ms | 28.3 ms | 80% |
| 16 | 3796 | 472 req/s | 31.7 ms | 60.4 ms | 78% |

- 8 线程达到约 **493 QPS**，继续加线程吞吐不再增长、延迟上升，Gate 接近单核饱和。
- 该接口每次请求包含 JWT 校验 + MySQL 查询；CPU 花在 JSON/JWT/DB 驱动上。

### 3.3 投递链路

100 条消息，发送侧 100 msg/s，接收侧 100% 收到：

```text
delivery p50 = 572.6 ms
delivery p95 = 1453.9 ms
delivery p99 = 1463.2 ms
loss        = 0
```

延迟构成：

- `OutboxPublisher` 空闲轮询 500 ms、有积压时 300 ms（`OutboxPublisher.cpp:46,50`）；
- `DeliveryConsumer::Consume(500)` 最长 500 ms 的拉取等待（`DeliveryConsumer.cpp:60`）；
- Kafka produce flush 上限 5 s（正常不会触发）。

### 3.4 数据库与资源

| 指标 | 值 |
|---|---|
| 每条消息 SQL 数 | 882 条消息产生 9659 次 `Questions` ≈ **11 条/消息** |
| 每条消息事务提交 | 1 次 `Com_commit`（含 redo + binlog 两次 fsync） |
| 单条 `INSERT` commit p50（本机直连） | 2.9 ms |
| 四语句事务 p50（模拟 SaveChatMessage） | 7.4 ms |
| MySQL CPU（100 连接压测中） | 峰值 ~118%（约 1.2 核），空闲 7% |
| MySQL 连接峰值 | 43 |
| Outbox 积压 | 压测后 0（Kafka lag 0） |
| Chat RSS | 59 MB |
| Gate RSS | 32 MB |

## 4. 瓶颈定位

### 4.1 单消费线程（首要瓶颈）

`ChatServer/ChatServer/LogicSystem.h:89-92`：

```cpp
std::queue<std::shared_ptr<LogicNode>> _msg_que;
std::condition_variable _consume;
std::thread _worker_thread;
```

`PostMsgToQue` 只把消息入队，**唯一的工作线程**串行取出并执行回调。`DealChatTextMsg` 在这条线程上同步调用 `MysqlMgr::SaveChatMessage`（`MysqlDao.cpp:419`），事务完成后才回 ACK。因此：

```text
吞吐上限 = 1 / 单条处理时间 ≈ 1 / 10ms ≈ 100 msg/s
```

这与实测的 84~98 msg/s 完全吻合；连接数增加只会让消息在 `_msg_que` 里排队，表现为延迟线性上升。虽然 `AsioIOServicePool` 把连接分散到了 4 个 io_context，但业务处理仍然汇聚到这一个线程。

### 4.2 每条消息 11 条 SQL

`SaveChatMessage` 的事务包含：

1. `INSERT messages ... ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id)`
2. `SELECT id FROM messages WHERE from_uid=? AND client_msg_id=?`（与上一步的 `LAST_INSERT_ID` 重复）
3. `UPDATE messages SET payload=?`
4. `INSERT outbox ...`
5. `commit`

再加上连接池的 autocommit 切换、健康检查等，实测约 11 次 `Questions`/消息。本机单事务 p50 已有 7.4 ms，叠加 SQL 往返与两次 fsync，单条 10 ms 是合理的。

### 4.3 投递延迟

Outbox 轮询 + Kafka 消费轮询决定了投递延迟的下限（约 300~500 ms），与消息处理线程无关。对 IM 的“实时感”来说，p50 573 ms 偏高，但消息不丢、可重放。

### 4.4 Gate HTTP

Gate 在 8 线程时约 493 QPS、单核 80%；继续加压只增加排队。HTTP 接口如果要更高吞吐，需要多进程/多线程事件循环，或把 JWT/JSON 热点下放。

## 5. 优化建议（按收益排序）

| 优先级 | 改动 | 预期收益 | 风险 |
|---|---|---|---|
| P0 | LogicSystem 改为 N 个 worker（按 session/uid 分片保序），或把回调投递到 session 所属的 io_context | 4 worker 约 4×（~400 msg/s），延迟不再线性恶化 | 需保证同一会话消息顺序；幂等键已具备 |
| P0 | 去掉 `SELECT id`，直接用 `LAST_INSERT_ID()`；连接建立时固定 `autocommit=false`，减少往返 | SQL 从 11 条降到 ~4 条，单条事务有望 <5 ms | 低 |
| P1 | Outbox 轮询 300→50 ms；DeliveryConsumer 用更短 poll 或事件驱动 | 投递 p50 从 ~570 ms 降到 ~100 ms 量级 | 空转 CPU 略升 |
| P1 | 多 ChatServer 实例 + 注册中心分流（etcd 已具备） | 近线性扩容 | 需要会话路由/在线状态一致性 |
| P2 | MySQL 测试环境 `innodb_flush_log_at_trx_commit=2`、`sync_binlog=0` | commit 延迟显著下降（牺牲崩溃安全性，仅测试） | 不适合生产 |
| P2 | 消息 ACK 与持久化解耦（先入队/批量提交，再 ACK） | 吞吐大幅提升 | 可靠性语义变化，需重新设计 |

## 6. 异常观察：重复 chat_server 导致 50% 投递丢失

压测初期曾出现 delivery 只收到约一半通知。排查发现是上一轮手工启动的 `chat_server` 未退出（PID 937326，端口未监听），但它仍以同一个 consumer group 消费 Kafka；两个消费者分摊分区，没有会话的那个实例把事件标记为已处理但无法投递。

- 清理残留进程后，100/100 消息全部送达。
- 运维建议：启动脚本先校验端口/进程；消费者组出现多成员时告警；优雅退出要确保 `DeliveryConsumer` 停止后再退出进程。
- 该问题不影响吞吐数据（消息路径未经过该进程），但会直接造成“消息丢失”的假象。

## 7. 复现命令

```bash
cd ~/cppworks/IM
# 基础设施（MySQL/Redis/Kafka/etcd）
docker compose -f docker-compose.test.yml --profile kafka --profile governance up -d --wait
bash scripts/dev/init_kafka_topics.sh

# 服务（tracing 关闭，Kafka/Outbox 开启）
IM_ETCD_ENABLED=1 BIN_DIR=$PWD/build_otel/bin bash scripts/dev/run_test_services.sh

# 压测
~/venvs/im/bin/python tests/load/load_im.py chat --connections 100 --duration 8 --json
~/venvs/im/bin/python tests/load/load_im.py http --threads 8 --duration 8 --json
~/venvs/im/bin/python tests/load/load_im.py delivery --messages 100 --json
```

## 8. 局限

- 单机部署：客户端、MySQL、Kafka、服务都在同一台 4 vCPU VM 上，磁盘剩余空间仅 7.2 GB，数据量增大后结果会变化。
- 客户端是 Python 线程模型，100 连接、~100 msg/s 时客户端不是瓶颈；更高并发需要压测机或 C++/Go 客户端。
- 未测试 TLS（Nginx）链路下的吞吐，也未开启 tracing 做对照；这两项的建议单独补测。
- 未做长时间稳定性（>30 分钟）与故障注入，属于 Step 11 的剩余部分。
