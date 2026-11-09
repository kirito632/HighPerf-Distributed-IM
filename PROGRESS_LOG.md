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

## 2026-11-05 ~ 2026-11-09

### Step 7：OpenTelemetry tracing

- Gate HTTP span + W3C `traceparent` 请求/响应头。
- Qt 把登录 HTTP context 传播到 TCP 登录、发消息、离线拉取与 ACK。
- ChatServer/ChatServer2 TCP span、Outbox 持久化 `traceparent`、Kafka header、Delivery span 与接收端 ACK context。
- `IM_WITH_OPENTELEMETRY` 可选构建；使用官方 OTLP/HTTP exporter，默认关闭，不增加默认构建依赖。
- 2026-11-09 补齐端到端验收：
  - 用 `build_otel` 从源码构建 OTel C++ SDK（`FetchContent` 拉取 `opentelemetry-cpp 1.19.0` / proto 1.4.0）；
  - Jaeger 改用 v2 单二进制镜像 `jaegertracing/jaeger:2.6.0`（旧 `all-in-one:1.62` 在测试 VM 的镜像源上返回 `denied`），OTLP/HTTP 4318 + UI 16686 开箱可用；
  - `tests/py/im_client.py` 支持把 `traceparent` 传入 Gate 登录、TCP 登录和发消息；
  - 新增 `tests/integration/test_trace.py`：校验 HTTP→TCP→Outbox→Kafka→Delivery 全链路 trace id 一致，并通过 Jaeger Query API 断言 `gate-server` / `chat-server` 两侧 span；
  - `deploy/sql/007_outbox_trace_context.sql` 已在测试库补应用。

验收：

```text
35 unit tests（含 2 个 tracing 测试，SDK 构建）
31 integration tests（1 个 Kafka 默认跳过）
test_trace.py: 2 passed
Jaeger /api/traces/{trace_id} 返回 gate-server + chat-server span
outbox.traceparent 与 HTTP 请求 trace id 一致
```

### Step 9：Nginx TLS + 限流

- `deploy/nginx/nginx.conf`：TLS 1.2/1.3 + HTTP/2 终止，反向代理 Gate，带 `X-Real-IP`/`X-Forwarded-For`/`X-Forwarded-Proto` 与 upstream keepalive。
- 两个限流 zone：`/user_login`、`/oauth/token` 5 r/s（burst 5/3）；其余路径 20 r/s（burst 20），统一返回 429。
- `scripts/dev/gen_nginx_certs.sh` 生成自签证书；`deploy/nginx/certs/` 已加入 `.gitignore`。
- `docker-compose.test.yml` 新增 `nginx-test`（profile `gateway`，18443→443，通过 `host.docker.internal` 回源宿主机 Gate）。
- `tests/integration/test_nginx.py`：HTTPS 登录、`traceparent` 透传、120 次快速请求触发限流。
- 访问日志格式记录 `trace=$http_traceparent`，可从边缘日志直接跳到 Jaeger trace。

验收：

```text
3 nginx integration tests passed
HTTPS 登录 200，Gate 返回 access_token（nginx 终止 TLS）
traceparent 经 nginx 透传，trace id 不变
120 次快速请求出现 429
```

## 当前状态

- 测试环境（MySQL/Redis/Kafka/Jaeger/Nginx/Chat/Gate/Status）已启动并验证。
- Prometheus targets `19101` / `19103` 均为 `up=1`，Grafana `IM Overview` 已 provision；Grafana 已 provision Jaeger datasource（`http://jaeger-test:16686`）。
- `Feature.KafkaEnabled` 与 `DeliveryMode` 目前由测试配置控制。
- `TracingEnabled` 默认关闭；测试时用 `IM_TRACING_ENABLED=1` 覆盖，不改仓库配置。
- Nginx 测试入口：`https://127.0.0.1:18443`（自签证书，仅测试环境）。
- 生产默认：
  - `JwtEnabled=true`（启动前必须生成密钥）
  - `OutboxEnabled=false`
  - `DeliveryMode=legacy`
- Qt 客户端仍未实际编译（环境无 Qt 头文件/qmake）。

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
docker compose -f docker-compose.test.yml --profile observability up -d prometheus-test grafana-test jaeger-test
# Prometheus: http://127.0.0.1:19090
# Grafana:    http://127.0.0.1:13000  (admin/admin)
# Jaeger:     http://127.0.0.1:16686

# Trace-enabled build（可选，首次配置会从 GitHub 拉 OTel 源码）
cmake -S . -B build_otel -DIM_WITH_OPENTELEMETRY=ON -DBUILD_TESTING=ON
cmake --build build_otel --target gate_server chat_server chat_server2 status_server im_unit_tests -j4
IM_TRACING_ENABLED=1 BIN_DIR=$PWD/build_otel/bin bash scripts/dev/run_test_services.sh

# Nginx TLS gateway
bash scripts/dev/gen_nginx_certs.sh
docker compose -f docker-compose.test.yml --profile gateway up -d nginx-test
# https://127.0.0.1:18443

# 测试（依赖装在 ~/venvs/im，见 tests/requirements-test.txt）
./build_test/bin/im_unit_tests
IM_E2E=1 ~/venvs/im/bin/python -m pytest tests/integration -q
IM_KAFKA_E2E=1 MQ_SMOKE_BIN=build_test/bin/mq_smoke \
  ~/venvs/im/bin/python -m pytest tests/integration/test_kafka_roundtrip.py -q
IM_TRACE_E2E=1 ~/venvs/im/bin/python -m pytest tests/integration/test_trace.py -q
IM_NGINX_E2E=1 ~/venvs/im/bin/python -m pytest tests/integration/test_nginx.py -q
```

## 下一步

- Step 8：etcd 服务注册/发现
- 清理 pass-the-hash 分支与 `VerifyServer/config.json` 明文凭据
- Qt 客户端编译与手工冒烟
- ChatServer2 的 metrics server 运行态接入（当前只验证 ChatServer1/Gate）
