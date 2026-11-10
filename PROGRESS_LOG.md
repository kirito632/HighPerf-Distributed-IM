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

### Step 8：etcd 服务注册/发现

- `common/registry/EtcdRegistry.{h,cpp}`：基于 etcd v3 HTTP JSON gateway 的轻量客户端（libcurl），支持 lease grant/keepalive/revoke、kv put/range、range delete 和 base64；不引入 etcd gRPC 依赖。
- `tools/registry_probe.cpp`：`register` / `discover` 小工具，集成测试用它验证租约注册、发现和主动摘除。
- ChatServer/ChatServer2 启动时以 10s 租约注册 `im/services/chatserver/<name>`，每 3s keepalive，SIGINT/SIGTERM 退出时 revoke 删除 key。
- StatusServer `getChatServer` 每次请求从 etcd 发现 `chatserver` 实例，再按原最小连接数策略挑选；发现失败或为空时回退到 `[chatservers]` 配置。
- 开关：`[Feature] EtcdEnabled`（默认 false）+ `[Etcd] Endpoints`；测试用 `IM_ETCD_ENABLED=1` / `IM_ETCD_ENDPOINT` 覆盖。
- `tests/integration/test_etcd_registry.py`：注册可见性、Status gRPC 解析结果、probe 租约注册与主动 revoke 摘除。
- 文档：`docs/ETCD_REGISTRY.md`。

验收：

```text
39 unit tests（含 4 个 registry 测试）
37 integration tests（1 个 Kafka 默认跳过）
etcd: chatserver1 -> 127.0.0.1:18090
status: [getChatServer] etcd discovered 1 chatserver instance(s)
probe: 租约内可发现，revoke 后 5s 内消失
```

### 安全清理：pass-the-hash 与明文凭据

- Gate `CheckPwd` 删除“把数据库里的 SHA-256 哈希当密码提交即可登录”的分支：只接受用户明文口令的 SHA-256 与库内哈希比对，泄露的哈希不再等价于口令。
- `VerifyServer/config.json` 从 git 索引移除并加入 `.gitignore`；新增脱敏的 `VerifyServer/config.example.json`。
- `VerifyServer/config.js` 支持 `VERIFY_CONFIG` 指定仓库外配置，并支持 `SMTP_USER`/`SMTP_PASS`/`MYSQL_*`/`REDIS_*`/`INTERNAL_TOKEN` 环境变量覆盖，Docker/K8s 不再需要明文配置文件。
- 新增回归测试：正确密码仍可登录；把数据库哈希当密码提交会被拒绝。
- 注意：历史提交中的旧 SMTP 授权码与数据库口令应视为已泄露，需在服务商侧轮换；彻底清理 git 历史需团队统一执行 `git filter-repo`。

验收：

```text
39 unit tests
39 integration tests（1 个 Kafka 默认跳过）
test_auth_hardening.py: 5 passed（含哈希登录拒绝、明文登录成功）
```

## 2026-11-10

### Step 11（部分）：容量压测与瓶颈定位

- 新增 `tests/load/load_im.py`：chat/http/delivery 三种模式；用测试私钥离线签发 JWT（绕过 Gate 60 次/分钟登录限流）；通过 `/proc` 采样服务 CPU/RSS。
- 环境：4 vCPU i5-11320H VM，MySQL/Redis/Kafka/etcd 同机，Kafka+Outbox 开启、tracing 关闭、单 ChatServer。
- TCP 消息：1/10/50/100 连接 → 84 / 98 / 91 / 93 msg/s；p50 10 / 90 / 483 / 957 ms；0 错误。
  - 吞吐与连接数无关，`p50 ≈ 连接数 × 10ms`，说明存在串行化点。
  - 根因：`LogicSystem` 只有 1 个 `_worker_thread`（`LogicSystem.h:92`），所有消息串行执行同步 DB 事务。
  - 每条消息约 11 条 SQL（882 条消息 / 9659 `Questions`）；四语句事务本机 p50 7.4 ms，commit 含 redo+binlog 两次 fsync。
- Gate HTTP `/get_my_friends`：8 线程 493 req/s，p50 14.9 ms，Gate 单核约 80%，16 线程不再增长。
- 投递：Outbox→Kafka→Delivery p50 573 ms / p95 1454 ms；Outbox 轮询 300ms + consumer poll 500ms 决定下限。
- 数据面 0 丢失；期间发现残留 chat_server 进程导致同 consumer group 双消费者、50% 投递“丢失”，清理后 100% 送达。
- 报告：`docs/PERFORMANCE_REPORT.md`（含优化收益排序：多 worker、减少 SQL 往返、缩短 Outbox 轮询、多实例分流）。
- 未完成：Step 11 的故障注入（Kafka 停机、消费者重启、DLQ 注入）、TLS/tracing 开启后的对照压测、长稳测试。

验收：

```text
chat 100 连接：93.1 msg/s，p50 957 ms，0 错误
gate 8 线程：493 req/s，p50 14.9 ms
delivery：100/100 送达，p50 573 ms
MySQL：11 条 SQL/消息，峰值 ~118% CPU
```

### ChatServer 吞吐优化（4 worker 分片）

- 调查确认根因：`LogicSystem` 单 `_worker_thread` 串行执行所有回调，且回调内同步等 MySQL 事务。
- 实现：按 `session id` 哈希分片的 worker 队列，每个分片独立线程；同一会话固定同一分片，保持登录→发消息→拉取/ACK 顺序；回调在锁外执行。
- Worker 数：`IM_CHAT_WORKERS` > `[Feature] ChatWorkers` > 默认 `min(8, max(2, hardware_concurrency))`（本机 4）；`=1` 可回退旧行为。
- ChatServer2 同步相同实现；新增 `tests/integration/test_concurrency.py`（8 会话 × 15 条，校验会话内递增、跨会话不重复）。
- 修复压测中暴露的关停缺陷：
  - 信号回调不再调用阻塞的 `grpc_server->Shutdown()`，改为只置标志并唤醒主线程；Shutdown 移到主线程并补 `[Shutdown]` 阶段日志。
  - `MetricsServer` 改为 non-blocking accept + 带 deadline 的 non-blocking 读，`Stop()` 先 join 再 close；解决 Prometheus 不抓取时 SIGTERM 卡死。
- SQL 路径调查：`general_log` 确认 11 次 Questions/消息；Connector/C++ 1.1.12 无 `getGeneratedKeys()`，去掉 `SELECT id` 需要存储过程/改 payload 语义，风险大于收益，本轮不改。
- 复测（100 连接 / 8s）：93 → **235 msg/s**，p50 957 → **417 ms**；200 连接 789 ms（旧版 2269 ms）。
- 量化持久化成本：测试库 `innodb_flush_log_at_trx_commit=2` + `sync_binlog=0` 时 4 worker 达 331 msg/s（仅测试环境，不提交）。
- 回归：40 integration tests passed（1 个 Kafka opt-in skipped），含 tracing/etcd/nginx/并发；`IM_CHAT_WORKERS=1` 下并发测试通过，100 连接 104 msg/s。
- 文档：`docs/CHAT_THROUGHPUT_OPTIMIZATION.md`；`docs/PERFORMANCE_REPORT.md` 追加优化后复测。

验收：

```text
40 integration tests passed, 1 skipped
chat 100 连接：235 msg/s，p50 417 ms，0 错误
chat 200 连接：232 msg/s，p50 789 ms
worker=1 回退：104 msg/s，p50 904 ms
SIGTERM 连续 5 次均在 1s 内退出
```

## 当前状态

- 测试环境（MySQL/Redis/Kafka/etcd/Jaeger/Nginx/Chat/Gate/Status）已启动并验证。
- Prometheus targets `19101` / `19103` 均为 `up=1`，Grafana `IM Overview` 已 provision；Grafana 已 provision Jaeger datasource（`http://jaeger-test:16686`）。
- `Feature.KafkaEnabled` 与 `DeliveryMode` 目前由测试配置控制。
- `TracingEnabled`、`EtcdEnabled` 默认关闭；测试时用 `IM_TRACING_ENABLED=1`、`IM_ETCD_ENABLED=1` 覆盖，不改仓库配置。
- Nginx 测试入口：`https://127.0.0.1:18443`（自签证书，仅测试环境）。
- 容量基线：单 ChatServer（4 worker）约 235 msg/s（p50 417 ms @100 连接），Gate 约 493 QPS；详见 `docs/PERFORMANCE_REPORT.md` 与 `docs/CHAT_THROUGHPUT_OPTIMIZATION.md`。
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

# etcd（Step 8）
docker compose -f docker-compose.test.yml --profile governance up -d --wait etcd-test
IM_ETCD_ENABLED=1 IM_ETCD_ENDPOINT=http://127.0.0.1:12379 \
  BIN_DIR=$PWD/build_test/bin bash scripts/dev/run_test_services.sh
build_test/bin/registry_probe discover --service chatserver

# Nginx TLS gateway
bash scripts/dev/gen_nginx_certs.sh
docker compose -f docker-compose.test.yml --profile gateway up -d nginx-test
# https://127.0.0.1:18443

# 测试（依赖装在 ~/venvs/im，见 tests/requirements-test.txt）
./build_test/bin/im_unit_tests
IM_E2E=1 IM_TRACE_E2E=1 IM_NGINX_E2E=1 IM_ETCD_E2E=1 \
  ~/venvs/im/bin/python -m pytest tests/integration -q
IM_KAFKA_E2E=1 MQ_SMOKE_BIN=build_test/bin/mq_smoke \
  ~/venvs/im/bin/python -m pytest tests/integration/test_kafka_roundtrip.py -q
IM_TRACE_E2E=1 ~/venvs/im/bin/python -m pytest tests/integration/test_trace.py -q
IM_NGINX_E2E=1 ~/venvs/im/bin/python -m pytest tests/integration/test_nginx.py -q
IM_ETCD_E2E=1 ~/venvs/im/bin/python -m pytest tests/integration/test_etcd_registry.py -q
```

## 下一步

- 口令哈希升级：SHA-256 → Argon2id/bcrypt（需种子数据与登录时自动升级）
- 在 SMTP/数据库服务商侧轮换历史提交中出现过的旧凭据
- 继续压测优化：存储过程/批量提交把 11 条 SQL 降到 1~4 条；评估 MySQL 持久化参数；多 ChatServer 实例分流
- Qt 客户端编译与手工冒烟
- ChatServer2 的 metrics server 运行态接入（当前只验证 ChatServer1/Gate）
- Step 10：CI 完整化（clang-tidy / ASan / 覆盖率）
- Step 11 剩余：故障注入（Kafka 停机、消费者重启、DLQ）、长稳与 TLS/tracing 对照压测
