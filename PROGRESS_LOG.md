# IM 后端改造进度记录

> 记录每个阶段的完成内容、验证结果、遗留问题和复现命令。  
> 详细计划见 [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)。

## 2026-10-31 ~ 2026-11-01

### Step 1：测试基线与 CI 骨架

- GoogleTest 单元测试
- Python 集成测试
- `docker-compose.test.yml`（MySQL 8 / Redis 7，可选 Kafka/etcd/监控）
- `Makefile`、`scripts/test/wait_for.sh`、`scripts/ci/run_ci.sh`
- GitHub Actions 骨架

验收：

```text
31 unit tests
Gate/Chat smoke 2 tests
```

### 1B.1：JWT 基础设施

- RS256 JWT signer/verifier
- `kid/iss/aud/exp` 校验
- 22 项单测

### 1B.2：JWKS + Refresh Token

- JWKS 生成
- refresh token 存储、轮换、重用检测、family 撤销
- `TokenService`
- Gate `/oauth/token`、`/oauth/revoke`、`/.well-known/jwks.json`
- MySQL `refresh_tokens`

验收：

```text
31 unit tests
OAuth 4 + smoke 2 integration tests
```

### 1B.3：Gate Bearer 鉴权

- 好友接口 Bearer JWT
- uid 从 token 派生
- 401/403
- Qt `FriendManager` Bearer + 401 单飞刷新
- `test_auth_binding.py`

验收：

```text
10 integration tests
```

### 1B.4：ChatServer JWT 登录

- ChatServer/ChatServer2 本地验签
- 客户端 TCP 登录带 access token
- 缺失/篡改 JWT 拒绝
- 旧 Redis `utoken` 路径不再用于 JWT 模式

验收：

```text
13 integration tests
```

### 1B.5：安全加固

- access token `jti` 黑名单，`/oauth/revoke` 即时吊销
- 验证码一次性消费（`GETDEL`）
- 验证码/登录限流
- 密码、token、验证码日志脱敏
- Chat 登录响应移除 `pwd` 哈希

验收：

```text
16 integration tests
```

### 1B.6：移除 legacy token + 内部 gRPC 鉴权

- Status 不再签发 UUID token
- ChatServer 删除 Redis `utoken` 兜底
- Gate/Chat/Status/Verify 内部共享令牌 `x-internal-token`
- 生产启动前必须生成 JWT 密钥

验收：

```text
31 unit tests
18 integration tests
```

### Step 2：消息模型

- `messages.client_msg_id / conversation_id / delivered_at`
- 唯一键 `(from_uid, client_msg_id)`
- 消息幂等，返回同一个 `server_msg_id`
- cursor 离线拉取
- 精确 ACK：`ids` 数组
- session uid 绑定，拒绝伪造 `fromuid`
- TCP 协议升级：`2B msgid + 2B flags + 4B len`，64KB
- SIGINT 优雅退出
- Qt 精确 ACK、cursor、最近 50 条历史

验收：

```text
33 unit tests
24 integration tests（含 55KB 消息、超长帧恢复）
```

### Step 3：Kafka 基础设施

- `common/mq`：KafkaProducer / KafkaConsumer / EventEnvelope / TopicNames
- `mq_smoke` 工具
- Kafka 3.8 KRaft，双 listener：
  - `kafka-test:9092`
  - `127.0.0.1:19092`
- 6 个 topic
- CMake 可选依赖

验收：

```text
33 unit tests
Kafka roundtrip 1 test
```

### Step 4：Transactional Outbox

- `outbox` 表
- 消息插入 + 最终 payload 更新 + outbox 插入同一 MySQL 事务
- `OutboxPublisher` 发布 `im.message.created`
- 成功标记 published，失败重试，最大 5 次
- `deploy/sql/005_outbox.sql`

验收：

```text
25 integration tests
outbox 端到端发布通过
```

### Step 5：Delivery + 幂等 + DLQ

- `processed_events` 表
- `DeliveryConsumer` 消费 `im.message.created`
- `DeliveryMode=kafka` 时由消费者负责实时投递
- 消费端按 `event_id` 幂等
- 检查消息仍未读，跳过陈旧/已 ACK 事件
- 畸形事件写入 `im.message.dlq`
- ChatServer2 同步
- `deploy/sql/006_processed_events.sql`

验收：

```text
33 unit tests
27 integration tests（1 个 Kafka 默认跳过）
test_delivery.py 通过
test_delivery_dlq.py 通过
```

### Step 6：Prometheus + Grafana

- `common/metrics/Metrics.*`：无外部依赖的 Prometheus 文本注册表
- `common/metrics/MetricsServer.*`：HTTP `/metrics` 端点
- Gate 端口 19103，ChatServer1 端口 19101
- 指标：
  - `im_messages_received_total`
  - `im_outbox_published_total` / `im_outbox_failed_total`
  - `im_delivery_processed_total` / `im_delivery_duplicate_total` / `im_delivery_dlq_total`
- Prometheus 抓取 `host.docker.internal:19101/19103`
- Grafana `IM Overview` 面板
- 告警：
  - `ImOutboxPublishFailures`
  - `ImDeliveryDlqEvents`
  - `ImServiceDown`

验收：

```text
29 integration tests（1 个 Kafka 默认跳过）
Prometheus targets up=1
Grafana /api/health = 200
Dashboard IM Overview 已 provision
test_metrics.py 通过
```

## 当前状态

- 当前测试环境（MySQL/Redis/Kafka/Chat/Gate/Status/Prometheus/Grafana）已启动并验证。
- Prometheus targets `19101` / `19103` 均为 `up=1`，Grafana `IM Overview` 已 provision。
- `Feature.KafkaEnabled` 与 `DeliveryMode` 目前由测试配置控制。
- 生产默认：
  - `JwtEnabled=true`（启动前必须生成密钥）
  - `OutboxEnabled=false`
  - `DeliveryMode=legacy`
- Qt 客户端仍未实际编译（环境无 Qt 头文件/qmake）。

### Step 7：OpenTelemetry tracing（实现中）

- Gate HTTP span + W3C `traceparent` 请求/响应头。
- Qt 把登录 HTTP context 传播到 TCP 登录、发消息、离线拉取与 ACK。
- ChatServer/ChatServer2 TCP span、Outbox 持久化 `traceparent`、Kafka header、Delivery span 与接收端 ACK context。
- `IM_WITH_OPENTELEMETRY` 可选构建；使用官方 OTLP/HTTP exporter，默认关闭，不增加默认构建依赖。
- Jaeger 使用 OTLP/HTTP 4318；Grafana provision Jaeger datasource。
- 环境限制：当前 Docker registry 对 `jaegertracing/all-in-one:1.62` 返回 `denied`，故容器端到端 UI 验收待 registry 可用后补做。

## 复现命令

```bash
cd ~/cppworks/IM

# 依赖
sudo apt install -y librdkafka-dev

# 生成生产 JWT 密钥
bash scripts/dev/gen_jwt_keys.sh ./keys

# Kafka
docker compose -f docker-compose.test.yml --profile kafka up -d kafka-test
bash scripts/dev/init_kafka_topics.sh

# MySQL/Redis + 服务
docker compose -f docker-compose.test.yml up -d --wait
bash scripts/dev/seed_test_data.sh
BIN_DIR=$PWD/build_test/bin bash scripts/dev/run_test_services.sh

# Observability
docker compose -f docker-compose.test.yml --profile observability up -d prometheus-test grafana-test
# Prometheus: http://127.0.0.1:19090
# Grafana:    http://127.0.0.1:13000  (admin/admin)

# 测试
./build_test/bin/im_unit_tests
IM_E2E=1 IM_GATE_URL=http://127.0.0.1:18080 pytest tests/integration -q
IM_KAFKA_E2E=1 MQ_SMOKE_BIN=build_test/bin/mq_smoke \
  pytest tests/integration/test_kafka_roundtrip.py -q
```

## 下一步

- Step 7：OpenTelemetry + Jaeger
- Step 8：etcd
- Step 9：Nginx
- Qt 客户端编译与手工冒烟
- ChatServer2 的 metrics server 运行态接入（当前只验证 ChatServer1/Gate）
