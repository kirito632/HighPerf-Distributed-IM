# Kafka / MQTT 改造方案（面向简历，但按可落地标准设计）

> 结论先说：**不要为了简历同时堆 Kafka 和 MQTT。**
> 对当前这个 IM，最合理的定位是：
>
> - **Kafka = 内部消息主干**（消息投递、好友事件、在线状态、审计/分析）；
> - **MQTT = 移动端/弱网接入层**（EMQX 接入，桥接到 Kafka 主干）。
>
> 如果只做一个，按目标岗位选：C++ 后端/基础架构优先 Kafka；IM/移动端/IoT 优先 MQTT。
> 两个都做也可以，但必须分阶段，否则就是两个孤立的中间件 Demo。

---

## 1. 前置条件：先修消息模型，再上 Kafka

当前项目的 P0/P1 问题（见 `DEFECT_REPORT.md`）里，有三类必须先处理，否则 Kafka 接上去只会把问题放大：

1. **认证/授权**：GateServer 接口无鉴权、StatusServer 可任意签发 token、ChatServer 信任客户端 `fromuid`。Kafka/MQTT 网关会成为新的攻击面。
2. **消息 ID 与 ACK**：现在是客户端毫秒时间戳 vs MySQL 自增 ID，ACK 会误删消息。Kafka 投递必须建立在“服务端消息 ID + 精确 ACK”上。
3. **关停与连接池**：ChatServer SIGINT 会崩溃，Redis/MySQL 池会挂死。加上 Kafka 消费者后，关停顺序会更复杂。

**建议顺序**：Phase 0 修这些，再进入 Kafka/MQTT。否则面试官问“消息会不会丢/重复”，现有代码本身就无法自圆其说。

---

## 2. 目标岗位与选型

| 目标岗位 | 优先做 | 简历故事 |
|---|---|---|
| C++ 后端 / 基础架构 | Kafka | 事务 Outbox、分区顺序、幂等消费、重试/DLQ、消费 lag 观测 |
| IM / 移动端 / IoT | MQTT | EMQX 接入、QoS1、持久会话、LWT、JWT/ACL、弱网重连 |
| 两者都想覆盖 | Kafka 主干 + MQTT 边缘 | “MQTT 接入层 → Kafka 事件主干 → TCP/MQTT 双通道投递” |

**不推荐**：只把 Kafka 当“发消息的队列”，或者只把 MQTT 当“另一个 TCP”。面试官会追问为什么不用 Redis Streams/RabbitMQ、为什么不用 WebSocket，答不上来反而减分。

---

## 3. 目标架构

```text
Qt 桌面端 ──TCP──┐
                 ├──> ChatServer (TCP Adapter)
移动端 ──MQTT──> EMQX ──> MqttGateway ──┘
                                      │
                                      ▼
                             MessageService（核心）
                               ├─ MySQL：messages + outbox
                               ├─ KafkaProducer
                               └─ 返回 server_msg_id
                                      │
                                      ▼
                                   Kafka
        ┌─────────────────────────────┼─────────────────────────────┐
        ▼                             ▼                             ▼
 delivery-worker              friend-event-worker             audit-worker
 （TCP/MQTT 下发）             （好友申请/通过通知）            （审计/分析）
        │
        ▼
 im.message.delivered ──> 状态/已读回执
```

关键原则：

- **MySQL 仍是消息历史的事实来源**；Kafka 是事件日志/投递管道，不是数据库。
- **MQTT 只做边缘接入**；服务间通信用 Kafka/gRPC，不要让微服务互相订阅 MQTT。
- **TCP 和 MQTT 共用同一个 MessageService**，不要写两套业务逻辑。
- **先做“至少一次 + 幂等”**，不要一上来宣称 exactly-once。

---

## 4. 核心数据模型

### 4.1 消息表改造

```sql
ALTER TABLE messages
  ADD COLUMN client_msg_id VARCHAR(64) NOT NULL DEFAULT '',
  ADD COLUMN conversation_id BIGINT NOT NULL DEFAULT 0,
  ADD COLUMN delivered_at DATETIME NULL,
  ADD UNIQUE KEY uk_from_client (from_uid, client_msg_id),
  ADD KEY idx_conv_id (conversation_id, id),
  ADD KEY idx_to_status_id (to_uid, status, id);
```

- `messages.id` 就是 `server_msg_id`（自增即可；量大再换雪花）。
- `client_msg_id` 由客户端生成（UUID），用于发送幂等：同一客户端重试不会产生两条消息。
- `conversation_id` 用 `min(uid1,uid2)`/`max(uid1,uid2)` 哈希或组合，作为 Kafka 分区键，保证同一会话内顺序。
- 客户端 ACK 只认 `server_msg_id`，不再用时间戳。

### 4.2 Outbox 表

```sql
CREATE TABLE outbox (
  id BIGINT PRIMARY KEY AUTO_INCREMENT,
  aggregate_type VARCHAR(32) NOT NULL,
  aggregate_id BIGINT NOT NULL,
  event_type VARCHAR(64) NOT NULL,
  payload JSON NOT NULL,
  status TINYINT NOT NULL DEFAULT 0, -- 0 pending, 1 sent, 2 failed
  retry_count INT NOT NULL DEFAULT 0,
  next_retry_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY idx_pending (status, next_retry_at)
);
```

发送消息时，在**同一个 MySQL 事务**里：

1. `INSERT messages`；
2. `INSERT outbox (event_type='MessageCreated', payload=...)`；
3. commit。

Outbox Publisher 轮询 `status=0 AND next_retry_at<=NOW()`，`SELECT ... FOR UPDATE SKIP LOCKED` 批量取，生产到 Kafka，成功后标记 `sent`；失败则 `retry_count++`、指数退避。这样即使 Kafka 暂时不可用，消息也不会丢。

> 进阶可换 Debezium CDC，但第一版用轮询 Outbox 更容易讲清楚，也更好测。

### 4.3 幂等消费表

```sql
CREATE TABLE processed_events (
  consumer_group VARCHAR(64) NOT NULL,
  event_id VARCHAR(128) NOT NULL,
  processed_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (consumer_group, event_id)
);
```

消费前先 `INSERT IGNORE`；影响行数为 0 说明重复，直接跳过。Kafka 至少一次投递 + 这张表 = 业务上不重复。

---

## 5. Kafka Topic 设计

| Topic | Key | 分区 | 保留 | 用途 |
|---|---|---:|---|---|
| `im.message.created` | `conversation_id` | 12 | 7d | 消息创建事件，投递/推送/审计的源头 |
| `im.message.delivered` | `server_msg_id` | 6 | 7d | 已投递/已读回执 |
| `im.friend.events` | `to_uid` | 6 | 7d | 好友申请、通过、拒绝 |
| `im.presence` | `uid` | 6 | compact | 在线/离线状态（可被 StatusServer 物化） |
| `im.message.dlq` | 原 key | 3 | 14d | 重试耗尽的消息 |
| `im.audit` | `server_msg_id` | 6 | 30d | 审计、分析、后续入 ClickHouse/ES |

设计要点：

- **顺序**：按 `conversation_id` 分区，保证同一会话内消息顺序；如果业务要求“每个接收者严格顺序”，改用 `to_uid`，但要接受热点用户导致分区倾斜。
- **重复**：Producer `acks=all`、`enable.idempotence=true`；Consumer 关闭自动提交，处理成功后手动 commit。
- **重试**：消费失败先本地重试（指数退避），超过 N 次进 DLQ，并打点告警。
- **不要**把图片/文件塞进 Kafka/MQTT；对象存储存文件，消息里只传 URL。

### 5.1 投递拓扑（两阶段演进）

**Phase 1（简单、适合 2~3 个 ChatServer）**：

- 每个 ChatServer 一个独立 consumer group：`delivery-chatserver1`、`delivery-chatserver2`；
- 每个节点都消费 `im.message.created` 全量消息，查本地 `UserMgr`，有 session 就下发，没有就忽略（消息已在 MySQL，登录时拉离线）；
- 优点：实现简单；缺点：N 个节点 = N 倍消费，规模上去要改。

**Phase 3（规模化）**：

- 增加 `presence-router`：根据在线路由把消息转发到 `im.message.deliver.{server}`；
- 每个 ChatServer 只消费自己的 topic；
- 路由表用 Kafka compacted `im.presence` 或 Redis（带 TTL）维护；
- 断线/迁移时按“未投递”补偿。

第一版不要上 router，先把 Outbox + 幂等 + DLQ 跑通。

---

## 6. MQTT 接入层设计

### 6.1 组件

- **EMQX 5.x**：MQTT broker，开 TLS、JWT 认证、ACL、Dashboard；
- **MqttGateway（C++，Paho MQTT C++）**：
  - 订阅 `im/up/+`，校验 uid/权限；
  - 调 ChatServer 内部 gRPC `SendMessage`（mTLS/服务 token），拿 `server_msg_id`；
  - 消费 Kafka `im.message.created`，向 `im/down/{uid}` 发布 QoS1；
  - 处理 `im/ack/{uid}`，生产 `im.message.delivered`。
- **Qt 客户端**：可选加 QtMqtt 模式，或写一个 Python paho 客户端做演示，不必重写整个 UI。

### 6.2 Topic 与 QoS

| Topic | 方向 | QoS | 说明 |
|---|---|---:|---|
| `im/up/{uid}` | 客户端 → 网关 | 1 | 上行消息，payload 带 `client_msg_id` |
| `im/down/{uid}` | 网关 → 客户端 | 1 | 下行消息，带 `server_msg_id` |
| `im/ack/{uid}` | 客户端 → 网关 | 1 | 应用层 ACK，按 `server_msg_id` |
| `im/presence/{uid}` | 客户端/网关 | 1 | LWT 离线、retained 在线状态 |

- `clean_start=false` + `session_expiry_interval`，弱网重连可恢复订阅；
- QoS1 只保证 broker 收到，**业务确认要靠 `im/ack` + Kafka 回执**；
- 客户端按 `server_msg_id` 去重；
- 不要让 MQTT 客户端直接访问 MySQL/Redis。

### 6.3 认证与 ACL

- GateServer 登录成功后签发短期 JWT（`uid`、`exp`、`jti`）；
- EMQX 用 JWT 认证；ACL 限制：
  - 只能 publish `im/up/{自己的 uid}`、`im/ack/{自己的 uid}`；
  - 只能 subscribe `im/down/{自己的 uid}`、`im/presence/{自己的 uid}`；
- MqttGateway 再校验一次 uid/来源，防止 broker ACL 配置错误。

---

## 7. 代码改造清单

### 7.1 新增公共库 `common/mq`

```text
common/mq/
  EventEnvelope.h        // event_id/type/version/trace_id/payload
  KafkaProducer.h/.cpp   // librdkafka 封装：同步/异步 produce、回调、重试
  KafkaConsumer.h/.cpp   // 手动 commit、poll、rebalance 回调
  TopicNames.h           // 统一 topic/consumer group 常量
  IdempotencyStore.h/.cpp
```

CMake 增加：

```cmake
find_package(RdKafka CONFIG REQUIRED) # 或 pkg_check_modules(RDKAFKA REQUIRED rdkafka++)
add_library(im_mq ...)
target_link_libraries(im_mq PUBLIC RdKafka::rdkafka++)
```

> 当前 ChatServer 和 ChatServer2 是两份重复代码；建议先只把新 MQ 代码放 `common`，不要一次性大重构，降低风险。

### 7.2 ChatServer 改造

新增：

```text
ChatServer/ChatServer/MessageService.h/.cpp   // 校验 + 事务写 messages/outbox + 返回 server_msg_id
ChatServer/ChatServer/OutboxPublisher.h/.cpp  // 轮询 outbox -> Kafka
ChatServer/ChatServer/DeliveryConsumer.h/.cpp // Kafka -> 本地 session / MQTT 下行
ChatServer/ChatServer/PresencePublisher.h/.cpp
```

改造：

- `LogicSystem::DealChatTextMsg`：不再直接 `SaveChatMessage` + `UserMgr` 下发，改为调用 `MessageService::Send`；
- `ChatGrpcClient::NotifyTextChatMsg`：跨服消息路径改为 Kafka（或保留 gRPC 作为降级，但默认走 Kafka）；
- 登录成功：发布 `im.presence` 在线事件；
- 离线拉取：按 `server_msg_id > cursor` 查询，ACK 精确按 ID；
- 好友通知：`ChatServer.cpp` 里的 Redis `friend.apply/reply` 订阅改为 Kafka `im.friend.events` 消费。

`MessageService::Send` 伪代码：

```cpp
SendResult Send(int fromUid, const SendRequest& req) {
    ValidateSession(fromUid, req);
    if (!IsFriend(fromUid, req.to_uid)) return kNotFriend;

    auto tx = mysql.Begin();
    auto msgId = messages.Insert({
        .from_uid = fromUid,
        .to_uid = req.to_uid,
        .client_msg_id = req.client_msg_id,
        .conversation_id = MakeConversationId(fromUid, req.to_uid),
        .payload = req.payload,
    }); // 依赖 uk_from_client 做幂等
    outbox.Insert({
        .event_type = "MessageCreated",
        .aggregate_id = msgId,
        .payload = BuildEvent(msgId, fromUid, req),
    });
    tx.Commit();

    return {.server_msg_id = msgId};
}
```

### 7.3 GateServer 改造

- 好友申请/通过：在同一个 DB 事务里写 `friend_requests/friends` + `outbox`，由 `friend-event-worker` 消费后推送；
- 删除 `RedisMgr::Publish("friend.apply"/"friend.reply")` 的 fire-and-forget 逻辑（或保留兼容，但不再作为主路径）；
- 登录成功后签发 JWT（给 MQTT 用），并发布 `im.presence`（可选）。

### 7.4 StatusServer 改造

- 消费 `im.presence`（compacted）维护 `uid -> server` 路由，替代单纯依赖 Redis `uip_`；
- `GetChatServer` 保留同步 gRPC，但增加健康检查/租约；
- token 加 TTL、登出失效。

### 7.5 新增 `MqttGateway/`

```text
MqttGateway/
  CMakeLists.txt
  main.cpp
  MqttBridge.h/.cpp      // Paho MQTT 连接、订阅、发布
  ChatServiceClient.h/.cpp // 调 ChatServer 内部 gRPC SendMessage
  KafkaDownlink.h/.cpp   // 消费 im.message.created
  JwtVerifier.h/.cpp
  Dockerfile
```

### 7.6 部署与配置

`docker-compose.yml` 增加：

- Kafka（KRaft 单节点即可，简历项目不需要 3 broker；但要知道生产是 3+）；
- `kafka-ui`（可选，展示 topic/lag）；
- EMQX（1883/8883/18083）；
- `mqtt-gateway`。

`config.ini` 增加：

```ini
[Kafka]
Brokers = 127.0.0.1:9092
TopicPrefix = im
ClientId = chat-server-1

[Outbox]
PollIntervalMs = 100
BatchSize = 100
MaxRetry = 10

[Mqtt]
Broker = 127.0.0.1:1883
ClientId = mqtt-gateway-1
```

---

## 8. 分阶段实施

### Phase 0：消息模型与稳定性（3~5 天）

- 服务端消息 ID + cursor + 精确 ACK；
- 会话绑定 uid、拒绝 fromuid 冒充；
- 统一消息大小上限（建议 64KB，别再用 2KB）；
- 修 ChatServer 关停、Redis/MySQL 池；
- 给现有 TCP 协议加 `client_msg_id`。

**验收**：客户端重发同一 `client_msg_id` 不产生两条消息；ACK 不会误删其他消息；Ctrl+C 不崩溃。

### Phase 1：Kafka 主干（7~10 天）

- Kafka + kafka-ui 进 compose；
- `common/mq` producer/consumer；
- `outbox` 表 + publisher；
- `MessageService` 事务写消息/outbox；
- `DeliveryConsumer`（每节点独立 group，先做简单版）；
- 好友事件迁移到 Kafka；
- DLQ + 重试 + 幂等表；
- 指标：outbox pending、produce 延迟、consume lag、DLQ 数。

**验收**：

- 停 Kafka 后发消息，恢复后消息仍能投递；
- kill ChatServer 后重启，未投递消息从 outbox/Kafka 继续；
- 重复消费同一条事件，客户端只收到一次；
- 消费失败进入 DLQ，可重放。

### Phase 2：MQTT 接入（7~10 天）

- EMQX + JWT/ACL；
- MqttGateway 上行/下行/ACK；
- LWT + 在线状态；
- QoS1 + 持久会话；
- Python/QtMqtt 客户端 demo；
- 弱网测试（断网重连、重复投递）。

**验收**：MQTT 客户端发消息，TCP 客户端能收到；TCP 发消息，MQTT 客户端能收到；broker 重启后会话恢复；越权 pub/sub 被拒绝。

### Phase 3：规模化与可观测性（3~5 天）

- `presence-router` + `im.message.deliver.{server}`（可选）；
- Prometheus/Grafana：Kafka lag、outbox backlog、端到端 P99、DLQ depth；
- 压测：连接数、QPS、故障注入；
- 架构文档、失败模式、benchmark。

---

## 9. 简历怎么写

不要写：

> 使用 Kafka、MQTT 改造 IM 系统。

建议写：

> 将消息投递从同步 gRPC/Redis PubSub 重构为 Kafka 事件驱动架构：MySQL 事务内 Outbox 保证消息不丢，按会话分区保证顺序，消费端幂等 + 重试 + DLQ；故障注入下消息 0 丢失，P99 投递延迟从 X ms 降到 Y ms。

> 新增 MQTT 接入层（EMQX + Paho C++），JWT/ACL、QoS1、持久会话与 LWT 在线状态；MQTT/TCP 共用 Kafka 消息主干，弱网重连成功率从 X% 提升到 Y%。

> 补齐可观测性：Kafka consumer lag、outbox backlog、DLQ 深度、端到端 P99；压测 X 连接 / Y QPS。

面试要能回答：

- 为什么用 Kafka，不用 Redis Streams/RabbitMQ？
- 分区键为什么选 `conversation_id`？热点用户怎么办？
- 如何保证不丢、不重、顺序？
- Consumer rebalance 时消息会不会重复/丢？
- Outbox 为什么要和业务表同事务？轮询延迟怎么控制？
- MQTT QoS1 为什么还需要应用层 ACK？
- MQTT 会话恢复和 Kafka offset 的关系？
- DLQ 的消息怎么重放？

---

## 10. 常见坑

1. **把 Kafka 当数据库**：离线消息仍要 MySQL，Kafka 只做事件流。
2. **宣称 exactly-once**：没有事务/幂等表就别这么说；说“至少一次 + 业务幂等”。
3. **MQTT 直接连业务库**：网关必须做鉴权、限流、协议转换。
4. **把大文件塞 Kafka/MQTT**：图片/文件走对象存储。
5. **Kafka 单节点就吹高可用**：简历项目可以单节点，但要能讲生产 3 broker、ISR、min.insync.replicas。
6. **不做故障测试**：Kafka 项目最值钱的是“broker/consumer 挂掉后怎么办”，不是 `docker run`。
7. **忽略已有 P0**：新的 MQTT 网关如果无鉴权，等于多一个入口。
8. **两个系统各写一套消息逻辑**：TCP/MQTT 必须共用 MessageService，否则消息 ID/ACK 会对不上。

---

## 11. 建议的下一步

如果目标是 **C++ 后端**，我建议先做 **Phase 1（Kafka）**，不要同时开 MQTT。最小可交付：

1. `docker-compose` 加 Kafka + kafka-ui；
2. `common/mq` 的 producer/consumer；
3. `outbox` 表 + publisher；
4. `MessageService` 事务写消息；
5. `DeliveryConsumer` 替换跨服 gRPC；
6. 三个故障测试：Kafka 停机、消费者重启、重复事件。

这 6 步做完，再决定是否加 MQTT。到那时你有一个能讲 30 分钟的 Kafka 故事，而不是两个只能讲 5 分钟的中间件名词。
