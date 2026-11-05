# C++ 后端改造实施计划（IM 项目）

> 目标：在不破坏现有 TCP 演示链路的前提下，把项目逐步改造成“可讲、可测、可故障注入”的 C++ 后端项目。
>
> **进度：Step 1 已完成（2026-10-31）**：GoogleTest 13 项单测通过，Gate/Chat 冒烟测试 2 项通过，CI 脚本可运行。  
> **1B.1 JWT 基础设施已完成（2026-10-31）**：RS256 signer/verifier、kid/issuer/audience/exp 校验、22 项单测通过。  
> **1B.2 core 已完成（2026-10-31）**：JWKS 生成、refresh token 存储接口/轮换/重用检测/吊销、`TokenService`，31 项单测通过。  
> **1B.2 已完成（2026-10-31）**：Gate `/oauth/token`、`/oauth/revoke`、`/.well-known/jwks.json`、MySQL refresh store、`user_login` 兼容返回 access/refresh；4 个 OAuth 集成测试 + 2 个原冒烟测试通过。  
> **1B.3 已完成（2026-10-31）**：Gate 好友接口 Bearer JWT 鉴权（401/403）、uid 从 token 派生；Qt `FriendManager` 加 Bearer + 401 单飞刷新；10/10 集成测试通过。  
> **1B.4 已完成（2026-10-31）**：ChatServer/ChatServer2 本地验签 JWT，客户端 TCP 登录带 access token；13/13 集成测试通过（含缺失/篡改 JWT 拒绝）。  
> **1B.5 已完成（2026-10-31，安全加固）**：access token `jti` 黑名单 + `/oauth/revoke` 即时吊销、验证码一次性消费、验证码/登录限流、密码/token/验证码日志脱敏；16/16 集成测试通过。  
> **1B.6 后端已完成（2026-10-31）**：Status 不再签发 UUID token、ChatServer 删除 Redis `utoken` 兜底并强制 JWT、Gate/Chat/Status/Verify 内部 gRPC 共享令牌；18/18 集成测试通过，31/31 单元测试通过。Qt 客户端编译/手工冒烟仍待有 Qt 环境时补做。生产启动前必须先运行 `bash scripts/dev/gen_jwt_keys.sh` 生成 `keys/jwt_{private,public}.pem`。  
> **Step 2 后端完成（2026-10-31）**：`client_msg_id` 幂等、`server_msg_id`、cursor 离线拉取、精确批量 ACK、session uid 绑定、SIGINT 优雅退出；TCP 协议升级为 `2B msgid + 2B flags + 4B len`，单包上限 64KB；Qt 历史改为最近 50 条；24/24 集成测试通过。ChatServer2 已同步相同代码并编译通过；Qt 实际编译冒烟仍待 Qt 环境。  
> **Step 3 完成（2026-11-01）**：`common/mq`（KafkaProducer/Consumer、EventEnvelope、TopicNames）、`mq_smoke`、可选 CMake 依赖、Kafka 3.8 KRaft 双 listener、6 个 topic、33/33 单测通过；`IM_KAFKA_E2E=1 pytest tests/integration/test_kafka_roundtrip.py` 通过。  
> **Step 4 完成（2026-11-01）**：`outbox` 表、消息写入与 outbox 同一 MySQL 事务、`OutboxPublisher` 轮询发布到 `im.message.created`、成功标记 published、失败重试/最大次数；25/25 集成测试通过，outbox 端到端发布测试通过。  
> **Step 5 完成（2026-11-01）**：`DeliveryConsumer` 消费 Kafka、`processed_events` 幂等、`DeliveryMode=kafka`、陈旧事件跳过、畸形事件入 `im.message.dlq`；27/27 集成测试通过，ChatServer2 同步并编译通过。  
> **Step 6 完成（2026-11-01）**：Gate/Chat Prometheus 文本指标端点、消息/Outbox/Delivery 计数、Prometheus 抓取、Grafana IM Overview 面板、3 条告警规则；29/29 集成测试通过。
>
> 主线：**消息可靠性（Kafka + Outbox）→ 可观测性（Prometheus/OTel）→ 服务治理（etcd/Nginx）→ 工程质量（GoogleTest/CI）**。
>
> 周期：约 **9 周全职**（按每天 8 小时）；业余每天 3~4 小时约 **14~18 周**。
> 最小可交付（Step 1、1A、2~5：安全基线 + 消息模型 + Kafka + Outbox + Delivery）约 **5 周全职 / 9~10 周业余**。
>
> 原则：每一步都可独立提交、独立回滚、独立演示；不做“大爆炸式重构”。

---

## 0. 范围与前置条件

### 0.1 本期做

1. 修消息模型：`server_msg_id`、`client_msg_id`、cursor、精确 ACK、会话绑定。
2. Kafka：生产者/消费者封装、Transactional Outbox、投递消费者、幂等、重试、DLQ。
3. 可观测性：Prometheus 指标 + Grafana 看板 + OpenTelemetry/Jaeger 链路追踪。
4. 服务治理：etcd 服务注册/发现，Nginx TLS + 限流。
5. 工程质量：GoogleTest、Python 集成测试、GitHub Actions、ASan/UBSan。
6. 故障注入与压测：Kafka 停机、消费者重启、重复事件、节点宕机。

### 0.2 本期不做

- 不做 MQTT/EMQX（C++ 后端主线，放到可选加分项）。
- 不做 Flink/Pulsar/Istio/DPDK。
- 不重写 Qt 客户端 UI，只做必要的协议适配和测试客户端。
- 不把 MySQL 替换成 RocksDB；RocksDB 只作为可选冷归档实验。

### 0.3 前置条件（必须先做）

当前项目还有若干 P0/P1 问题（见 `DEFECT_REPORT.md`）。其中以下三项是 Kafka 改造的前置：

- 认证/授权：GateServer 接口无鉴权、StatusServer 任意签发 token、ChatServer 信任 `fromuid`；
- 消息 ID/ACK：客户端时间戳 vs MySQL 自增 ID；
- 关停/连接池：ChatServer SIGINT 崩溃、Redis/MySQL 池挂死。

**如果这些不修，Kafka 的“消息不丢/不重”无法成立。** 本计划把其中与消息模型相关的部分放在 Step 2，把认证/关停作为 Step 1/2 的并行任务。

### 0.4 分支与开关

每个 Step 一个分支/PR：

```text
feature/step1-test-harness
feature/step2-message-model
feature/step3-kafka-infra
feature/step4-outbox
feature/step5-delivery
feature/step6-metrics
feature/step7-tracing
feature/step8-etcd
feature/step9-nginx
feature/step10-ci
feature/step11-chaos
```

所有新链路加 feature flag，默认关闭，灰度验证后再切主路径：

```ini
[Feature]
KafkaEnabled = false
OutboxEnabled = false
DeliveryMode = legacy   ; legacy | kafka
EtcdEnabled = false
TracingEnabled = false
```

---

## 1. 总览

| Step | 内容 | 交付物 | 预估 |
|---|---|---|---|
| 1 | 测试基线 + CI 骨架 | GoogleTest、pytest、compose.test、Makefile、Actions | 3 天 |
| 1A | 安全基线（前置） | Gate 鉴权、Status 服务间认证、验证码限流 | 3 天 |
| 2 | 消息模型/会话绑定/关停 | server_msg_id、cursor、精确 ACK、64KB、优雅退出 | 5 天 |
| 3 | Kafka 基础设施 + common/mq | Kafka compose、topic 初始化、producer/consumer 封装 | 3 天 |
| 4 | Transactional Outbox | outbox 表、MessageService、publisher | 5 天 |
| 5 | Delivery + 幂等 + DLQ | 消费投递、processed_events、重试、DLQ | 5 天 |
| 6 | Prometheus + Grafana | 指标、看板、告警 | 3 天 |
| 7 | OpenTelemetry + Jaeger | trace 传播、span、采样 | 4 天 |
| 8 | etcd 服务注册/发现 | lease、watch、故障摘除 | 4 天 |
| 9 | Nginx TLS/限流 | HTTPS、limit_req、upstream | 2 天 |
| 10 | CI 完整化 | build matrix、clang-tidy、ASan/UBSan、覆盖率 | 3 天 |
| 11 | 故障注入 + 压测 | chaos suite、load test、报告 | 4 天 |
| 12 | 可选加分项 | ES/MinIO/K8s/RocksDB 选 1~2 | 1~2 周 |

**关键路径**：1 → 1A → 2 → 3 → 4 → 5 → 6/7 → 8/9 → 10/11。  
**可以并行**：Step 6 与 7 可并行；Step 8 与 9 可并行；Step 10 从 Step 1 就开始，最后补齐。

---

## 2. 测试环境与目录约定

### 2.1 统一测试环境

新增 `docker-compose.test.yml`，只用于测试，不影响本地开发：

- MySQL 8（测试库 `chat_system_test`）
- Redis 7
- Kafka（KRaft 单节点）
- etcd
- Jaeger all-in-one
- Prometheus + Grafana
- Nginx（TLS 自签证书）

```bash
# 启动/停止
make test-up
make test-down

# 查看状态
make test-ps
```

端口约定（避免和现有服务冲突）：

| 服务 | 端口 |
|---|---|
| Gate | 18080 |
| Chat1 | 18090 |
| Chat2 | 18091 |
| Status gRPC | 15052 |
| Kafka | 19092 |
| etcd | 12379 |
| Prometheus | 19090 |
| Grafana | 13000 |
| Jaeger UI | 16686 |
| Nginx HTTPS | 18443 |
| Metrics | 19101/19102/19103 |

### 2.2 测试目录

```text
tests/
  cpp/
    unit/
      test_message_service.cpp
      test_outbox.cpp
      test_idempotency.cpp
      test_protocol.cpp
  requirements-test.txt
  py/
    conftest.py
    im_client.py          # HTTP 登录 + TCP 收发
    db_utils.py           # MySQL 查询/断言
    kafka_utils.py        # 生产/消费/查 lag
    fixtures.py           # 测试用户/好友数据
    send_one_message.py
    produce_events.py
    send_traced_request.py
  integration/
    test_smoke.py
    test_message_idempotency.py
    test_ack_precision.py
    test_auth_binding.py
    test_message_size.py
    test_verify_code_replay.sh
    test_rate_limit_login.sh
    test_kafka_roundtrip.py
    test_delivery_online.py
    test_delivery_duplicate.py
    test_metrics.py
    test_trace.py
    test_nginx.py
  chaos/
    test_graceful_shutdown.sh
    test_outbox_recovery.sh
    test_outbox_retry.sh
    test_consumer_restart.sh
    test_kafka_down.sh
    test_dlq.sh
    test_etcd_failover.sh
    run_all.sh
  load/
    load_tcp.py
    load_gate.py
scripts/
  dev/
    up_test_env.sh
    down_test_env.sh
    seed_test_data.sh
    init_kafka_topics.sh
  ci/
    run_ci.sh
  test/
    wait_for.sh
deploy/
  sql/
    002_message_model.sql
    003_outbox.sql
    004_processed_events.sql
  prometheus/prometheus.yml
  grafana/dashboards/im-overview.json
  nginx/nginx.conf
config_test.ini
```

### 2.3 测试数据

固定两个测试用户，避免依赖线上数据：

```text
imtest1@example.com / Test123456
imtest2@example.com / Test123456
```

`seed_test_data.sh` 通过 MySQL 存储过程/直接插入创建用户、好友关系和 token，测试结束可重建 schema。

### 2.4 Makefile 目标

```makefile
test-unit:        # ctest
test-integration: # pytest tests/integration
test-chaos:       # bash tests/chaos/*.sh
test-load:        # python tests/load/load_tcp.py
test-all:         # unit + integration + chaos
ci:               # scripts/ci/run_ci.sh
```

### 2.5 依赖清单

C++ 侧（建议用 vcpkg manifest 固定版本，避免系统包版本漂移）：

```text
librdkafka / rdkafka++     # Kafka
prometheus-cpp             # 指标
opentelemetry-cpp          # tracing（OTLP exporter）
etcd-cpp-apiv3             # etcd client（或 clientv3）
GoogleTest / GMock         # 单测
curl                       # HTTP client（已有依赖）
openssl                    # TLS / SHA
```

Python 测试侧：

```text
pytest
requests
pymysql
redis
confluent-kafka
grpcio
protobuf
pytest-timeout
```

`tests/requirements-test.txt` 固定版本；CI 缓存 pip 与 vcpkg。

### 2.6 通用等待脚本

异步测试不要 `sleep 10`，统一用轮询：

```bash
#!/usr/bin/env bash
# scripts/test/wait_for.sh
# 用法: wait_for "描述" 超时秒数 "命令"
set -euo pipefail
desc="$1"; timeout="$2"; shift 2
start=$(date +%s)
until eval "$*"; do
  now=$(date +%s)
  if (( now - start > timeout )); then
    echo "TIMEOUT: $desc"
    exit 1
  fi
  sleep 0.5
done
echo "OK: $desc"
```

---

## 3. 分步骤实施

### Step 1：测试基线 + CI 骨架（3 天）

#### 做什么

- 引入 GoogleTest，给现有纯逻辑（协议解析、MsgNode、配置、工具函数）补第一批单测。
- 建 `tests/py/im_client.py`：HTTP 登录 Gate，拿 token/host/port，建立 TCP，按现有 4 字节头协议收发。
- 建 `docker-compose.test.yml` 和 `scripts/dev/up_test_env.sh`。
- 建 GitHub Actions 骨架：Ubuntu + gcc/clang + CMake + ctest；先不接 Kafka。
- 记录基线：现有压测脚本的 QPS/P50/P99、连接数、错误率，写入 `docs/benchmark-baseline.md`。

#### 改动文件

```text
CMakeLists.txt                 # enable_testing / add_subdirectory(tests)
tests/CMakeLists.txt
tests/cpp/unit/...
tests/py/im_client.py
tests/py/conftest.py
docker-compose.test.yml
Makefile
.github/workflows/ci.yml
docs/benchmark-baseline.md
```

#### 验收标准

- `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && ctest --test-dir build --output-on-failure` 通过。
- `pytest tests/integration/test_smoke.py -v` 能完成：注册/登录/连 Chat/发一条消息/收到 1018。
- GitHub Actions 首次运行绿色，耗时 < 10 分钟。
- 基线指标有记录，后续每步可对比。

#### 测试脚本

```bash
# 1. 单元测试
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

# 2. 冒烟测试
make test-up
pytest tests/integration/test_smoke.py -v
make test-down
```

`tests/integration/test_smoke.py` 核心：

```python
def test_login_and_send(im_client, db):
    rsp = im_client.login("imtest1@example.com", "Test123456")
    assert rsp["error"] == 0
    cid = str(uuid.uuid4())
    result = im_client.send_text(to_uid=2, content="hello", client_msg_id=cid)
    assert result["server_msg_id"] > 0
    assert db.message_exists(client_msg_id=cid)
```

#### 风险/回滚

- Qt 客户端不参与测试，用 Python 客户端代替；协议不变。
- 不影响现有构建；`tests/` 独立目录，失败不阻塞旧流程（CI 先 warning，Step 10 转 blocking）。

---

### Step 1A：安全基线（3 天，前置）

#### 做什么

- GateServer：登录后签发 access token；所有好友/资料接口从 token 解析 uid，拒绝请求体冒充；`reply_friend_request` 只允许 `to_uid` 本人操作。
- StatusServer：`GetChatServer` 只允许 Gate 调用（服务间共享密钥或 mTLS），校验 uid 存在；不要再对 `0.0.0.0` 裸露 50052。
- VerifyServer/Gate：验证码成功即删除，限制每邮箱/IP 的尝试次数与发送频率；token 加 TTL。
- 日志脱敏：password/pwd/token/code 字段全部打码。

#### 测试脚本

```bash
pytest tests/integration/test_auth_binding.py -v   # 无 token 403；伪造 uid 403
bash tests/integration/test_verify_code_replay.sh  # 同一 code 第二次失败
bash tests/integration/test_rate_limit_login.sh    # 超限 429/错误码
```

#### 验收

- 不带 token 调 `/get_my_friends` 返回 401/403，不返回数据。
- 用 A 的 token 请求 B 的 uid 返回 403。
- 同一验证码第二次使用失败；连续失败触发限流。
- 日志中搜不到明文密码/完整 token。

> 如果这一步没做，先不要进入 Step 2；Kafka/MQTT 只会扩大攻击面。

---

### Step 2：消息模型、会话绑定、大小与优雅关停（5 天）

#### 做什么

1. `messages` 表增加 `client_msg_id`、`conversation_id`、`delivered_at`，唯一键 `(from_uid, client_msg_id)`。
2. 协议：客户端发 `client_msg_id`（UUID），服务端返回 `server_msg_id`（MySQL 自增）。
3. 登录拉离线：客户端带 `cursor`（上次最大 `server_msg_id`），服务端只发 `id > cursor`。
4. ACK：客户端按 `server_msg_id` 精确 ACK，服务端更新 `status/delivered_at`。
5. 会话绑定：所有消息处理只信 `session->GetUserId()`，拒绝请求体中的 `fromuid/uid` 冒充。
6. 消息大小：当前协议头是 `2B msgid + 2B short len`，最大只能 32767，**不能直接设 64KB**。建议协议升级为 `2B msgid + 2B flags/version + 4B uint32 len`，统一上限 64KB（或先保守定 32KB 继续用 short）。同步修改 `MsgNode`、`SendNode`、`CSession`、客户端 `TcpMgr`。超限返回错误码，不直接断连。
7. 优雅关停：信号处理器只置标志，主线程停止 acceptor、停止 IO 池、join；`Stop()` 幂等且不 join 当前线程。
8. 初始化 `_user_uid=0`，`ClearSession` 只在 uid>0 且 session 匹配时删除映射。

#### 改动文件

```text
ChatServer/ChatServer/CSession.h/.cpp
ChatServer/ChatServer/CServer.cpp
ChatServer/ChatServer/LogicSystem.cpp
ChatServer/ChatServer/MysqlDao.h/.cpp
ChatServer/ChatServer/AsioIOServicePool.cpp
ChatServer/ChatServer/ChatServer.cpp
ChatServer2/...（同步修改，或抽公共库）
ChatClient/tcpmgr.h/.cpp
ChatClient/mainwindow.cpp
ChatClient/localdb.cpp
deploy/sql/002_message_model.sql
```

#### 验收标准

- 同一 `client_msg_id` 重发 10 次，DB 只有 1 条，返回同一个 `server_msg_id`。
- 3 条未读消息，只 ACK 第 1 条，DB 只把第 1 条置为已读；第 2/3 条仍为未读。
- 用 uid=1 登录后，请求体伪造 `fromuid=2`，服务端拒绝（或忽略并回错误）。
- 发送 64KB 消息成功；65KB 返回明确错误，连接不崩。
- `kill -INT chat_server` 退出码 0，无 `std::system_error`/core；重启后能继续收消息。
- 客户端历史记录显示“最近 50 条”，不是最旧 50 条。

#### 测试脚本

```bash
pytest tests/integration/test_message_idempotency.py -v
pytest tests/integration/test_ack_precision.py -v
pytest tests/integration/test_auth_binding.py -v
pytest tests/integration/test_message_size.py -v
bash tests/chaos/test_graceful_shutdown.sh
```

`test_message_idempotency.py`：

```python
def test_same_client_msg_id_is_idempotent(im_client, db):
    cid = str(uuid.uuid4())
    r1 = im_client.send_text(to_uid=2, content="a", client_msg_id=cid)
    r2 = im_client.send_text(to_uid=2, content="a", client_msg_id=cid)
    assert r1["server_msg_id"] == r2["server_msg_id"]
    assert db.count("messages", from_uid=1, client_msg_id=cid) == 1
```

`test_ack_precision.py`：

```python
def test_ack_only_marks_one_message(im_client, db, peer):
    ids = [im_client.send_text(to_uid=2, content=f"m{i}")["server_msg_id"] for i in range(3)]
    peer.login_and_ack(ids[0])
    rows = db.query("SELECT id,status FROM messages WHERE to_uid=2 ORDER BY id")
    assert rows[0].status == 1
    assert all(r.status == 0 for r in rows[1:])
```

`test_graceful_shutdown.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail
pid=$(pgrep -f './chat_server' | head -1)
kill -INT "$pid"
for _ in $(seq 1 20); do
  kill -0 "$pid" 2>/dev/null || { echo "PASS: exited"; exit 0; }
  sleep 0.5
done
echo "FAIL: still running"; exit 1
```

#### 风险/回滚

- 协议变更影响 Qt 客户端：长度字段从 2B 扩到 4B 是破坏性变更；加 `flags/version`，服务端同时兼容旧 4B 头一个版本，或者先定 32KB 不扩字段。
- 数据库迁移：先加列/索引，再切代码；保留 `msgid` 旧字段。
- 关停改动涉及信号/线程，先在测试环境验证，不影响 Kafka。

---

### Step 3：Kafka 基础设施 + common/mq（3 天）

#### 做什么

- `docker-compose.test.yml` 增加 Kafka（KRaft 单节点）和 kafka-ui。
- 建 topic 初始化脚本 `scripts/dev/init_kafka_topics.sh`。
- `common/mq`：
  - `KafkaProducer`：`acks=all`、`enable.idempotence=true`、同步/异步 produce、错误回调。
  - `KafkaConsumer`：手动 commit、poll、rebalance 回调、优雅停止。
  - `TopicNames.h`：统一 topic/consumer group 常量。
- `config.ini` 增加 `[Kafka]`；`Feature.KafkaEnabled=false` 默认关闭。
- 健康检查：启动时 `ListTopics`/metadata 超时 3s，失败不阻塞主服务（除非开关强制）。

#### 改动文件

```text
common/mq/KafkaProducer.h/.cpp
common/mq/KafkaConsumer.h/.cpp
common/mq/TopicNames.h
common/mq/EventEnvelope.h
scripts/dev/init_kafka_topics.sh
docker-compose.test.yml
ChatServer/ChatServer/config.ini
CMakeLists.txt
```

#### Topic 初始化

```bash
#!/usr/bin/env bash
set -euo pipefail
BROKER="${KAFKA_BROKER:-localhost:19092}"
topics=(
  "im.message.created:12"
  "im.message.delivered:6"
  "im.friend.events:6"
  "im.presence:6"
  "im.message.dlq:3"
  "im.audit:6"
)
for item in "${topics[@]}"; do
  name="${item%%:*}"; parts="${item##*:}"
  docker compose -f docker-compose.test.yml exec -T kafka \
    kafka-topics.sh --bootstrap-server "$BROKER" \
    --create --if-not-exists --topic "$name" --partitions "$parts" --replication-factor 1
done
```

#### 验收标准

- `docker compose -f docker-compose.test.yml up -d kafka` 后，`kafka-topics.sh --list` 有上述 6 个 topic，分区数正确。
- C++ 小工具/单测能生产 100 条并消费 100 条，消息体、key、header 完整。
- Kafka 不可用时，服务启动不崩（开关关闭时降级；开关打开时明确报错）。
- `common/mq` 单测覆盖：produce 成功/失败、consumer 手动 commit、停止。

#### 测试脚本

```bash
bash scripts/dev/init_kafka_topics.sh
pytest tests/integration/test_kafka_roundtrip.py -v
bash tests/chaos/test_kafka_down.sh
```

`test_kafka_roundtrip.py`：

```python
def test_produce_consume_roundtrip(kafka_admin, kafka_producer, kafka_consumer):
    topic = "im.message.created"
    key = "conv:1:2"
    payload = {"event_id": "e1", "type": "MessageCreated", "server_msg_id": 1}
    kafka_producer.produce(topic, key=key, value=payload)
    kafka_producer.flush()
    msg = kafka_consumer.poll_one(topic, timeout=10)
    assert msg.key == key
    assert msg.value["server_msg_id"] == 1
```

`test_kafka_down.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail
docker compose -f docker-compose.test.yml stop kafka
# 启动服务，断言进程仍在、健康检查通过（Kafka 开关关闭）
./build/bin/chat_server --config config_test.ini &
pid=$!
sleep 2
kill -0 "$pid"
kill -INT "$pid"; wait "$pid" || true
docker compose -f docker-compose.test.yml start kafka
echo "PASS: service survives kafka down"
```

#### 风险/回滚

- Kafka 镜像版本/端口冲突：固定版本，测试环境独立端口。
- 依赖 librdkafka 编译：用系统包/vcpkg，CI 缓存。
- 默认关闭，不影响旧链路。

---

### Step 4：Transactional Outbox + MessageService（5 天）

#### 做什么

1. 新增 `outbox` 表（见 `KAFKA_MQTT_PLAN.md`）。
2. 新增 `MessageService`：
   - 校验（session uid、好友关系、大小、频率）；
   - 事务内 `INSERT messages` + `INSERT outbox`；
   - 返回 `server_msg_id`；
   - 唯一键 `(from_uid, client_msg_id)` 做发送幂等。
3. `OutboxPublisher` 线程：
   - `SELECT ... FOR UPDATE SKIP LOCKED` 批量取 pending；
   - 生产 Kafka `im.message.created`；
   - 成功标记 `sent`；失败 `retry_count++`、指数退避；
   - 指标：pending、sent、failed、oldest_age。
4. `LogicSystem::DealChatTextMsg` 改为调用 `MessageService`，不再直接 `SaveChatMessage` + 本地 `UserMgr` 下发。
5. `Feature.OutboxEnabled` 控制；先双写（旧路径 + outbox）观察，再切主路径。

#### 改动文件

```text
deploy/sql/003_outbox.sql
ChatServer/ChatServer/MessageService.h/.cpp
ChatServer/ChatServer/OutboxPublisher.h/.cpp
ChatServer/ChatServer/MysqlDao.h/.cpp
ChatServer/ChatServer/LogicSystem.cpp
ChatServer/ChatServer/ChatServer.cpp
tests/cpp/unit/test_outbox.cpp
tests/integration/test_outbox_transaction.py
```

#### 验收标准

- 发送消息后，`messages` 和 `outbox` 在同一事务提交；模拟异常时两者都不写入。
- Kafka 正常：outbox 在 1s 内变为 sent，topic 有对应事件。
- Kafka 停机：outbox 保持 pending，消息仍写入 MySQL；Kafka 恢复后自动补发。
- 同一 `client_msg_id` 重发：messages 只有 1 条，outbox 只有 1 条。
- Outbox publisher 重启后不重复发送（已 sent 不重发）；重复发送由消费端幂等兜底。

#### 测试脚本

```bash
pytest tests/integration/test_outbox_transaction.py -v
bash tests/chaos/test_outbox_recovery.sh
bash tests/chaos/test_outbox_retry.sh
```

`test_outbox_recovery.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail
source scripts/test/wait_for.sh

docker compose -f docker-compose.test.yml stop kafka
python tests/py/send_one_message.py --from 1 --to 2 --content "outbox-test"
mysql ... -e "SELECT COUNT(*) FROM outbox WHERE status=0" | grep -q '^1$'
echo "PASS: message persisted with kafka down"

docker compose -f docker-compose.test.yml start kafka
wait_for "outbox drained" 30 \
  "mysql ... -N -e 'SELECT COUNT(*) FROM outbox WHERE status=0' | grep -q '^0$'"
kafka-console-consumer ... --max-messages 1 | grep -q 'MessageCreated'
echo "PASS: outbox recovered"
```

`test_outbox_transaction.py`：

```python
def test_outbox_and_message_are_atomic(db, message_service):
    before = db.count("messages"), db.count("outbox")
    with pytest.raises(Exception):
        message_service.send_with_fault("after_outbox_insert")
    after = db.count("messages"), db.count("outbox")
    assert before == after
```

#### 风险/回滚

- 事务边界写错：用集成测试 + `SELECT ... FOR UPDATE` 验证。
- Publisher 重复发送：消费端必须幂等，不能只靠 producer。
- 轮询延迟：默认 100ms，按量调整；不要一开始上 CDC。
- 回滚：`Feature.OutboxEnabled=false` 切回旧路径；outbox 表保留不影响。

---

### Step 5：Delivery Consumer + 幂等 + 重试 + DLQ（5 天）

#### 做什么

1. `DeliveryConsumer` 消费 `im.message.created`：
   - 每节点独立 consumer group（`delivery-<server>`），先做“全量消费 + 本地过滤”；
   - 查 `UserMgr`，有 session 就发 1019；没有就忽略（消息已在 MySQL）；
   - 成功后生产 `im.message.delivered`；
   - 失败重试（指数退避），超过 N 次进 `im.message.dlq`。
2. `processed_events` 表做幂等：`INSERT IGNORE (consumer_group, event_id)`，影响行数 0 直接跳过。
3. 关闭自动提交，处理成功后再 commit；consumer 停止时先停止 poll，再 commit 最后 offset。
4. 指标：消费成功/失败、重试、DLQ、lag、处理延迟。
5. `DeliveryMode=legacy|kafka` 切换；先双跑对比（legacy 投递 + kafka 只记录不投递），再切 kafka。

#### 改动文件

```text
deploy/sql/004_processed_events.sql
ChatServer/ChatServer/DeliveryConsumer.h/.cpp
ChatServer/ChatServer/IdempotencyStore.h/.cpp
ChatServer/ChatServer/ChatGrpcClient.cpp（跨服路径改 Kafka）
ChatServer/ChatServer/LogicSystem.cpp
tests/integration/test_delivery_online.py
tests/integration/test_delivery_duplicate.py
tests/chaos/test_consumer_restart.sh
tests/chaos/test_dlq.sh
```

#### 验收标准

- 在线用户：发送消息后 500ms 内收到 1019。
- 离线用户：消息不丢；下次登录按 cursor 拉取。
- 同一条 Kafka 事件重复 3 次：客户端只收到 1 次，`processed_events` 只有 1 行。
- 消费者 kill -9 后重启：不丢消息；最多重复，但被幂等表挡住。
- 注入非法事件：进入 DLQ，主消费不阻塞；DLQ 可重放。
- `im.message.created` 的 lag 在 1 分钟内回落到 < 100。

#### 测试脚本

```bash
pytest tests/integration/test_delivery_online.py -v
pytest tests/integration/test_delivery_duplicate.py -v
bash tests/chaos/test_consumer_restart.sh
bash tests/chaos/test_dlq.sh
```

`test_delivery_duplicate.py`：

```python
def test_duplicate_event_delivered_once(im_client, kafka_producer, db):
    event = build_message_created_event(server_msg_id=1001, to_uid=2)
    for _ in range(3):
        kafka_producer.produce("im.message.created", key="conv:1:2", value=event)
    kafka_producer.flush()
    msgs = im_client.recv_notifications(timeout=5)
    assert len([m for m in msgs if m.server_msg_id == 1001]) == 1
    assert db.count("processed_events", event_id="1001") == 1
```

`test_consumer_restart.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail
# 1. 停止消费者进程
kill -9 "$(pgrep -f delivery_consumer | head -1)"
# 2. 生产 10 条消息
python tests/py/produce_events.py --count 10
# 3. 重启消费者
./build/bin/delivery_consumer --config config_test.ini &
# 4. 等待并断言：10 条消息最终都投递/处理
pytest tests/integration/test_delivery_online.py -v
```

#### 风险/回滚

- 每节点全量消费的放大：先用于 2 节点；规模上去改 server 专属 topic（见计划文档）。
- 重复投递：必须有幂等表；不要只依赖 offset。
- DLQ 泛滥：加告警和上限；DLQ 消息保留原 key/trace。
- 回滚：`DeliveryMode=legacy`，Kafka 消费者停掉。

---

### Step 6：Prometheus + Grafana（3 天）

#### 做什么

- 接入 `prometheus-cpp`（或等价库），每个服务暴露 `/metrics`：
  - Gate：`im_gate_requests_total{path,code}`、`im_gate_request_duration_seconds`、`im_gate_active_connections`
  - Chat：`im_chat_active_sessions`、`im_message_send_duration_seconds`、`im_send_queue_size`、`im_slow_consumer_total`
  - Kafka：`im_kafka_produce_total`、`im_kafka_consume_total`、`im_kafka_consumer_lag`、`im_outbox_pending`、`im_dlq_total`
  - DB/Redis：`im_db_pool_wait_seconds`、`im_redis_pool_wait_seconds`
- Prometheus 配置 scrape 所有服务；Grafana 预置 dashboard JSON。
- 告警规则：
  - `im_kafka_consumer_lag > 1000` 持续 5m
  - `im_outbox_pending > 1000` 持续 5m
  - `im_dlq_total > 0`
  - `im_message_send_duration_seconds{quantile="0.99"} > 0.5`
  - 服务 `up == 0`

#### 改动文件

```text
common/metrics/Metrics.h/.cpp
GateServer/GateServer/LogicSystem.cpp
ChatServer/ChatServer/ChatServer.cpp
StatusServer/StatusServer/StatusServer.cpp
deploy/prometheus/prometheus.yml
deploy/grafana/dashboards/im-overview.json
deploy/grafana/provisioning/...
docker-compose.test.yml
tests/integration/test_metrics.py
```

#### 验收标准

- `curl localhost:19101/metrics` 有 Gate 指标；`19102` 有 Chat；`19103` 有 Status。
- Prometheus Targets 全部 UP。
- Grafana 看板能看到 QPS、P99、在线连接、Kafka lag、Outbox backlog、DLQ。
- 停止一个服务，`up==0` 告警在 1 分钟内触发。
- 指标开销 < 3% CPU/延迟。

#### 测试脚本

```bash
bash tests/integration/test_metrics.sh
pytest tests/integration/test_metrics.py -v
```

`test_metrics.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail
curl -fsS localhost:19101/metrics | grep -q 'im_gate_requests_total'
curl -fsS localhost:19102/metrics | grep -q 'im_chat_active_sessions'
curl -fsS localhost:19103/metrics | grep -q 'im_status_requests_total'
curl -fsS localhost:19090/-/ready | grep -q 'Prometheus is Ready'
echo "PASS: metrics endpoints"
```

`test_metrics.py`：

```python
def test_kafka_lag_metric_exists(prometheus):
    value = prometheus.query('im_kafka_consumer_lag')
    assert value is not None
    assert value >= 0
```

#### 风险/回滚

- 指标基数爆炸：不要用 uid/session_id 做 label；用 path/status 等低基数。
- 端口冲突：测试环境用 191xx。
- 回滚：关闭 metrics 开关，不影响业务。

---

### Step 7：OpenTelemetry + Jaeger（4 天）

#### 做什么

- 接入 `opentelemetry-cpp`，OTLP exporter 指向 Jaeger。
- 传播：
  - HTTP：W3C `traceparent`；
  - gRPC：metadata `traceparent`；
  - TCP：协议增加 `trace_id` 字段（或复用 `client_msg_id` 关联）；
  - Kafka：消息 header `traceparent`。
- 关键 span：
  - `gate.http.request` → `status.grpc.GetChatServer`
  - `chat.tcp.login` → `chat.message.handle` → `chat.db.insert` → `chat.kafka.produce`
  - `delivery.consume` → `delivery.send` → `delivery.ack`
- 采样：开发 100%，生产 1%~10%；错误全采样。
- 日志注入 `trace_id`，便于串联。

#### 改动文件

```text
common/tracing/Tracer.h/.cpp
common/tracing/TraceContext.h
GateServer/GateServer/HttpConnection.cpp
ChatServer/ChatServer/CSession.cpp
ChatServer/ChatServer/LogicSystem.cpp
ChatServer/ChatServer/DeliveryConsumer.cpp
deploy/jaeger/...
tests/integration/test_trace.sh
```

#### 验收标准

- 发一条消息，Jaeger 能查到一条完整 trace：Gate → Status → Chat → Kafka → Delivery，至少 4 个 span。
- 每个 span 有 `trace_id/span_id/parent_span_id`，服务名正确。
- 开启 tracing 后 P99 延迟增加 < 5%。
- 错误请求的 span 标记 `error=true`，带错误码。

#### 测试脚本

```bash
bash tests/integration/test_trace.sh
```

```bash
#!/usr/bin/env bash
set -euo pipefail
TRACE_ID=$(python tests/py/send_traced_request.py --print-trace-id)
sleep 2
curl -fsS "http://localhost:16686/api/traces/$TRACE_ID" | jq -e '.data[0].spans | length >= 4'
curl -fsS "http://localhost:16686/api/traces/$TRACE_ID" | jq -e '.data[0].spans[].operationName | select(test("delivery"))'
echo "PASS: trace spans"
```

#### 风险/回滚

- SDK 依赖重：用 vcpkg/Conan 固定版本；CI 缓存。
- 采样率过高：默认 1%，测试环境 100%。
- 回滚：`Feature.TracingEnabled=false`。

---

### Step 8：etcd 服务注册/发现（4 天）

#### 做什么

- ChatServer 启动时向 etcd 注册 `im/chat/<server>`，value `{host,port,rpc_port,ts}`，lease 10s，后台 keepalive。
- StatusServer 启动时 watch `im/chat/` 前缀，维护 `server -> endpoint`；lease 过期自动摘除。
- 兼容旧配置：`Feature.EtcdEnabled=false` 时用 `config.ini` 静态列表；开启后 etcd 优先，空列表回退静态。
- 管理接口：`/admin/servers` 只读展示当前注册节点（仅内网）。

#### 改动文件

```text
common/etcd/EtcdClient.h/.cpp
ChatServer/ChatServer/ServiceRegistry.h/.cpp
StatusServer/StatusServer/ServiceDiscovery.h/.cpp
StatusServer/StatusServer/StatusServiceImpl.cpp
deploy/etcd/...
tests/chaos/test_etcd_failover.sh
```

#### 验收标准

- 启动 ChatServer，`etcdctl get --prefix /im/chat` 能看到节点，TTL 约 10s。
- kill -9 ChatServer，5~12s 内 key 消失；StatusServer 不再返回该节点。
- 重启 ChatServer，5s 内重新注册并可被选中。
- etcd 停机：已注册节点继续服务（缓存），新节点注册失败有明确日志；恢复后自动重连。
- 不依赖静态 `config.ini` 的 chatserver 列表也能完成登录分配。

#### 测试脚本

```bash
bash tests/chaos/test_etcd_failover.sh
```

```bash
#!/usr/bin/env bash
set -euo pipefail
source scripts/test/wait_for.sh
ETCD="docker compose -f docker-compose.test.yml exec -T etcd etcdctl"

$ETCD get --prefix /im/chat | grep -q 'chatserver1'
kill -9 "$(pgrep -f 'chat_server.*config_test' | head -1)"
wait_for "etcd lease expired" 15 "$ETCD get --prefix /im/chat | grep -q -v chatserver1 || true"
! $ETCD get --prefix /im/chat | grep -q 'chatserver1'
# 重新启动并注册
./build/bin/chat_server --config config_test.ini &
wait_for "re-registered" 10 "$ETCD get --prefix /im/chat | grep -q 'chatserver1'"
echo "PASS: etcd failover"
```

#### 风险/回滚

- etcd client 依赖 gRPC/Protobuf 版本冲突：固定版本，独立编译。
- 脑裂/网络分区：lease 是租约，不是强一致锁；不要用它做唯一主。
- 回滚：`Feature.EtcdEnabled=false` 回静态配置。

---

### Step 9：Nginx TLS + 限流（2 天）

#### 做什么

- GateServer 前加 Nginx：
  - TLS 终止（测试自签证书）；
  - `limit_req_zone` 按 IP 限流登录/验证码接口；
  - `proxy_pass` 到 Gate；
  - upstream keepalive；
  - 健康检查 `/health`；
  - 访问日志带 request_id。
- 客户端/测试改用 HTTPS。

#### 改动文件

```text
deploy/nginx/nginx.conf
deploy/nginx/certs/...
docker-compose.test.yml
tests/integration/test_nginx.sh
```

#### 验收标准

- `curl -k https://localhost:18443/health` 返回 200。
- 登录接口连续请求超过阈值返回 429，恢复后正常。
- `openssl s_client` 能完成 TLS 握手，证书 SAN 正确。
- Gate 挂掉时 Nginx 返回 502/503，恢复后自动恢复。
- HTTP 请求被重定向到 HTTPS 或拒绝。

#### 测试脚本

```bash
bash tests/integration/test_nginx.sh
```

```bash
#!/usr/bin/env bash
set -euo pipefail
curl -kfsS https://localhost:18443/health | grep -q 'ok'
# 连续请求触发限流
codes=$(for i in $(seq 1 50); do
  curl -k -s -o /dev/null -w '%{http_code}\n' \
    -X POST https://localhost:18443/user_login \
    -H 'Content-Type: application/json' -d '{"user":"x","passwd":"y"}'
done)
grep -q '429' <<<"$codes"
echo "PASS: nginx tls + rate limit"
```

#### 风险/回滚

- 自签证书只用于测试；生产用 ACME/企业 CA。
- 限流阈值太低影响压测：测试环境单独配置。
- 回滚：Nginx 摘掉，直连 Gate。

---

### Step 10：CI 完整化与质量门禁（3 天）

#### 做什么

- GitHub Actions：
  - build matrix：gcc / clang，Debug / Release；
  - `clang-format --dry-run --Werror`；
  - `clang-tidy`（至少新增文件）；
  - ASan/UBSan 单测；
  - `ctest`；
  - Python 集成测试（docker compose）；
  - Docker build；
  - 覆盖率（gcov/lcov）上传。
- 缓存：ccache、vcpkg/Conan、docker layer。
- 失败即 block merge；主分支保护。

#### 改动文件

```text
.github/workflows/ci.yml
scripts/ci/run_ci.sh
CMakePresets.json
.vclang-format
.clang-tidy
```

#### 验收标准

- PR 触发 CI，10~15 分钟内完成。
- 单测覆盖率 ≥ 60%（新增模块 ≥ 70%）。
- ASan/UBSan 无报错。
- `clang-tidy` 新增代码 0 error。
- 集成测试在 CI 中跑通（Kafka/MySQL/Redis 用 service 或 compose）。

#### 测试脚本

```bash
bash scripts/ci/run_ci.sh
```

`run_ci.sh` 核心：

```bash
#!/usr/bin/env bash
set -euo pipefail
cmake --preset ci
cmake --build --preset ci -j"$(nproc)"
ctest --preset ci --output-on-failure
pytest tests/integration -v
```

#### 风险/回滚

- CI 时间过长：拆分 unit / integration job；缓存依赖。
- 集成测试 flaky：统一 `wait_for` 轮询，禁止裸 sleep。
- 覆盖率工具链：先统计新增模块，不追求全仓。

---

### Step 11：故障注入 + 压测（4 天）

#### 做什么

- 故障矩阵：
  - Kafka 停机/恢复；
  - 消费者 kill -9；
  - MySQL 慢查询/连接池耗尽；
  - Redis 停机；
  - ChatServer 宕机；
  - etcd 停机；
  - 网络延迟/丢包（tc netem，可选）。
- 压测：
  - 连接数 1k/5k/10k；
  - 消息 QPS 1k/5k；
  - 长连接稳定性 30min；
  - P50/P95/P99、错误率、内存、CPU。
- 输出 `docs/benchmark-after.md` 和 `docs/failure-matrix.md`。

#### 验收标准

- Kafka 停机 30s：消息 0 丢失；恢复后 60s 内追平。
- 消费者 kill -9：消息 0 丢失；重复 ≤ 1 次且被幂等挡住。
- MySQL 慢查询 2s：服务不崩；超时返回明确错误；恢复后自动恢复。
- 10k 连接：内存/CPU 在预算内；无 fd 泄漏。
- P99 投递延迟 < 200ms（本地测试环境，按基线调整）。
- 30 分钟长稳：无崩溃、内存增长 < 10%。

#### 测试脚本

```bash
bash tests/chaos/run_all.sh
python tests/load/load_tcp.py --connections 1000 --qps 1000 --duration 60
python tests/load/load_tcp.py --connections 10000 --duration 1800
```

`run_all.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail
for t in tests/chaos/test_*.sh; do
  echo "=== $t"
  bash "$t"
done
```

#### 风险/回滚

- 压测打爆测试机：限制连接/QPS，监控资源。
- 故障注入影响共享环境：只在本机 compose 网络内。
- 数据污染：每次压测前重建测试库。

---

### Step 12：可选加分项（1~2 周，选 1~2 个）

| 选项 | 做什么 | 验收 | 适合方向 |
|---|---|---|---|
| Elasticsearch | Kafka 消费消息写索引，提供搜索 API | 100 万条搜索 P95 < 200ms；与 MySQL LIKE 对比 | IM/内容 |
| MinIO | 图片/文件上传，消息只存 object key | 上传/下载/预签名 URL；消息体 < 1KB | IM |
| Kubernetes/Helm | kind/minikube 部署，探针/HPA/Secret | 滚动更新不丢连接；Pod 故障自愈 | 云原生 |
| RocksDB | 冷消息归档/本地缓存 | 写入 10 万条，读 P99、压缩比、恢复测试 | 存储/基础架构 |

**建议**：主投 C++ 后端 + IM，优先 **Elasticsearch** 或 **MinIO**；如果目标公司偏云原生，选 **K8s/Helm**；如果偏存储，选 **RocksDB**。不要四个都做。

---

### 3.13 测试脚本索引

| Step | 脚本 | 运行方式 | 通过标准 |
|---|---|---|---|
| 1 | `tests/integration/test_smoke.py` | `pytest tests/integration/test_smoke.py -v` | 注册/登录/连 Chat/发消息全通 |
| 1 | `scripts/ci/run_ci.sh` | `make ci` | build + ctest 通过 |
| 1A | `tests/integration/test_auth_binding.py` | pytest | 无 token 401/403；跨 uid 403 |
| 1A | `tests/integration/test_verify_code_replay.sh` | bash | 同一 code 第二次失败 |
| 1A | `tests/integration/test_rate_limit_login.sh` | bash | 超限 429/错误码 |
| 2 | `tests/integration/test_message_idempotency.py` | pytest | 同 client_msg_id 只落 1 条 |
| 2 | `tests/integration/test_ack_precision.py` | pytest | 只标记被 ACK 的那条 |
| 2 | `tests/integration/test_auth_binding.py` | pytest | 伪造 fromuid 被拒绝 |
| 2 | `tests/integration/test_message_size.py` | pytest | 64KB 通过、65KB 明确报错 |
| 2 | `tests/chaos/test_graceful_shutdown.sh` | bash | SIGINT 退出码 0、无 core |
| 3 | `tests/integration/test_kafka_roundtrip.py` | pytest | 生产/消费 100 条不丢 |
| 3 | `tests/chaos/test_kafka_down.sh` | bash | Kafka 停机服务不崩 |
| 4 | `tests/integration/test_outbox_transaction.py` | pytest | messages/outbox 原子提交 |
| 4 | `tests/chaos/test_outbox_recovery.sh` | bash | Kafka 恢复后 outbox 清空 |
| 4 | `tests/chaos/test_outbox_retry.sh` | bash | 失败重试/退避生效 |
| 5 | `tests/integration/test_delivery_online.py` | pytest | 在线用户 500ms 内收到 |
| 5 | `tests/integration/test_delivery_duplicate.py` | pytest | 同事件重复只投一次 |
| 5 | `tests/chaos/test_consumer_restart.sh` | bash | kill -9 后不丢消息 |
| 5 | `tests/chaos/test_dlq.sh` | bash | 坏事件进 DLQ、可重放 |
| 6 | `tests/integration/test_metrics.sh` | bash | 三个服务 /metrics 可抓取 |
| 6 | `tests/integration/test_metrics.py` | pytest | lag/outbox/DLQ 指标存在 |
| 7 | `tests/integration/test_trace.sh` | bash | Jaeger 查到 ≥4 个 span |
| 8 | `tests/chaos/test_etcd_failover.sh` | bash | 节点 5~12s 内摘除/重注册 |
| 9 | `tests/integration/test_nginx.sh` | bash | HTTPS 200、限流 429 |
| 10 | `scripts/ci/run_ci.sh` | bash | clang-tidy/ASan/集成测试全绿 |
| 11 | `tests/chaos/run_all.sh` | bash | 所有故障场景通过 |
| 11 | `tests/load/load_tcp.py` | python | 达到目标 QPS/P99、无泄漏 |
| 12 | 按选项 | 见对应 README | 搜索/文件/K8s/RocksDB 验收 |

**统一约定**：

- 所有异步断言用 `scripts/test/wait_for.sh` 轮询，禁止裸 `sleep 30`；
- 每个脚本失败时打印最近 100 行服务日志；
- 测试库每次运行前重建，避免脏数据；
- 脚本返回 0 表示通过，非 0 表示失败；CI 直接依赖退出码。

---

## 4. 每一步的 Definition of Done

每个 Step 合并前必须满足：

- [ ] 代码 + 单元测试 + 集成测试
- [ ] 至少一个可重复执行的测试脚本（在 `tests/`/`scripts/`）
- [ ] 指标/日志/追踪（如果该 Step 涉及）
- [ ] 配置项 + feature flag + 回滚说明
- [ ] 文档更新（`docs/`）
- [ ] 不引入新的 P0；不破坏现有 TCP 登录/发消息
- [ ] 本地 `make test-all` 通过
- [ ] 有一个 5 分钟 demo/演示命令

---

## 5. 风险清单

| 风险 | 影响 | 缓解 |
|---|---|---|
| 依赖库编译复杂（OTel/etcd/prometheus） | 进度拖延 | 用 vcpkg/Conan 固定版本；先做 spike |
| Kafka 单节点被误认为高可用 | 面试减分 | 文档写清“开发单节点，生产 3 broker + RF=3” |
| 重复投递 | 用户收到重复消息 | 幂等表 + 客户端按 server_msg_id 去重 |
| 消息丢失 | 数据事故 | Outbox + 手动 commit + 故障注入 |
| 指标基数爆炸 | Prometheus 内存高 | 禁止 uid/session_id 做 label |
| 集成测试 flaky | CI 不稳定 | 统一 wait_for 轮询；隔离测试库 |
| 改造范围过大 | 烂尾 | 每步 feature flag；先 Kafka 后其他 |

---

## 6. 面试/简历对应关系

| Step | 简历关键词 | 面试可讲 |
|---|---|---|
| 2 | 消息 ID、幂等、精确 ACK | 为什么时间戳不能当消息 ID；如何保证不重 |
| 3/4 | Kafka、Outbox、事务消息 | 为什么不用直接双写；Kafka 挂了怎么办 |
| 5 | 消费幂等、重试、DLQ、rebalance | 至少一次怎么做到业务不重；offset 什么时候提交 |
| 6 | Prometheus、Grafana、告警 | P99 怎么算；lag 怎么排查 |
| 7 | OpenTelemetry、Jaeger | trace 怎么跨 HTTP/gRPC/Kafka |
| 8 | etcd、Lease、Watch、Raft | 服务摘除为什么用 lease；和 ZooKeeper 对比 |
| 9 | Nginx、TLS、限流 | 502/504 排查；限流算法 |
| 10 | GoogleTest、CI、ASan | 怎么测并发/内存问题 |
| 11 | 故障注入、压测 | 消息 0 丢失怎么证明 |

---

## 7. 建议的执行顺序（最小可交付）

如果时间有限，按这个顺序做，保证每一到两周有一个可演示成果：

1. **Week 1**：Step 1 + Step 1A（测试基线 + 安全基线）。
2. **Week 2**：Step 2（消息模型/会话绑定/大小/优雅关停）。
3. **Week 3**：Step 3 + 开始 Step 4（Kafka 基础设施 + Outbox）。
4. **Week 4**：Step 4 + 开始 Step 5（Outbox + Delivery）。
5. **Week 5**：Step 5 + Step 6（Delivery/幂等/DLQ + Metrics）。
6. **Week 6**：Step 7 + 开始 Step 8（Tracing + etcd）。
7. **Week 7**：Step 8 + Step 9（etcd + Nginx）。
8. **Week 8**：Step 10（CI 完整化）。
9. **Week 9**：Step 11（故障注入 + 压测 + 报告）。
10. **可选**：Step 12 选一个。

这是全职 9 周、每周 5 天的排期；业余每天 3~4 小时，按月历大约翻倍。

**不要跳过 Step 1/1A/2 直接上 Kafka。** 否则你会花大量时间在“消息为什么重复/丢失”上，而这些问题本来应该在消息模型阶段解决。
