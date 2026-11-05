# 🚀 High-Performance Distributed IM System (基于微服务的分布式即时通讯系统)

![C++](https://img.shields.io/badge/language-C++17-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Build](https://img.shields.io/badge/build-CMake-green.svg)
![Architecture](https://img.shields.io/badge/Architecture-Microservices-orange.svg)

> 一个基于 **C++17** 和 **Boost.Asio** 实现的高性能分布式 IM 后端。
> 采用 **Reactor** 模型与 **gRPC** 微服务架构，设计了全异步数据库连接池与应用层可靠传输协议。
> **实测性能**：单机支持 **20,000+** 并发长连接，峰值吞吐量 **5,000+ QPS**。

---

## 📚 目录
- [核心特性](#-核心特性-features)
- [系统架构与目录说明](#-系统架构与目录说明-structure)
- [性能压测报告](#-性能压测报告-benchmark)
- [技术难点复盘](#-技术难点复盘-troubleshooting)
- [构建与运行](#-构建与运行-build)

---

## ✨ 核心特性 (Features)
*   **底层网络封装**：基于 Linux **Epoll** + **Boost.Asio** 封装，采用 **One Loop Per Thread** 线程模型，最大化多核利用率。
*   **分布式架构**：
    *   **网关服务 (GateServer)**：处理 TCP 长连接接入与负载均衡。
    *   **业务服务 (ChatServer 集群)**：分布式部署，处理消息转发与存储逻辑。
    *   **状态服务 (StatusServer)**：通过 gRPC 维护全局用户在线状态与 Session 路由。
    *   **异构服务 (VerifyServer)**：引入 Node.js 生态处理邮件/验证码等 IO 密集型任务。
*   **深度稳定性优化**：
    *   **异步 DB 连接池**：设计支持超时熔断与自动重连的 MySQL 连接池，消除同步阻塞。
    *   **协议设计**：自定义 **Header+Body** 双阶段读取协议，彻底解决 TCP 粘包/半包问题。
    *   **RAII 管理**：利用 `std::shared_ptr` 与 `weak_ptr` 锁定回调中的对象生命周期，防止 CoreDump。

---

## 📂 系统架构与目录说明 (Structure)

本项目采用微服务拆分，主要包含以下模块：

```text
.
├── ChatServer       // [核心] 业务逻辑服务节点 1 (C++, Asio, MySQL)
├── ChatServer2      // [扩展] 业务逻辑服务节点 2 (用于模拟分布式集群部署)
├── GateServer       // [网关] 负载均衡与连接分发 (C++, Asio)
├── StatusServer     // [状态] 全局路由与状态管理 (C++, gRPC)
├── VerifyServer     // [验证] 验证码/邮件服务 (Node.js 异构微服务)
├── ChatClient       // [客户端] 基于 Qt 编写的桌面端聊天界面
├── docs             // [文档] 技术难点复盘与 Bug 修复报告
├── tests            // [测试] Python 自动化压测脚本
├── CMakeLists.txt   // 全局构建脚本
└── README.md        // 项目说明文档
```

---

## 📊 性能压测报告 (Benchmark)

### 1. 测试环境
*   **环境**: Ubuntu 22.04 LTS (VMware), 2 Cores, 10GB RAM
*   **内核优化**: `ulimit -n = 1,048,576`

### 2. 核心数据
*   **并发连接数**: 20,000+ (连接成功率 100%)
*   **峰值 QPS**: 4,923 msg/s (单节点处理能力)
*   **稳定性**: 30分钟满载压测，无崩溃，无内存泄漏。

### 3. 详细测试记录 (Test Logs)

| ID | 阶段 | 并发数 | 持续时间 | 成功/失败 | 总消息数 | QPS (msg/s) | 备注 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **T0** | 修复前 | 1000 | 120s | 1000 / 0 | 193k | 1,601 | 发生 DB 连接池阻塞 Crash |
| **T1** | 修复后 | 1000 | 120s | 1000 / 0 | 593k | **4,923** | 峰值吞吐量测试 |
| **T3** | 修复后 | 2000 | 120s | 2000 / 0 | 375k | 3,116 | 并发翻倍测试 |
| **T6** | 修复后 | 2000 | 1800s | 2000 / 0 | 6.7M+ | 3,771 | **30分钟长稳测试 (Stable)** |

---

## 🛠️ 技术难点复盘 (Troubleshooting)

针对高并发场景下遇到的 **MySQL 连接池死锁** 与 **性能抖动** 问题，进行了深度排查并输出修复报告。

👉 **点击阅读：[MySQL 连接池严重逻辑缺陷修复报告 (P0级故障复盘)](docs/BUG_FIX_REPORT_CONNECTION_POOL.md)**

*   **问题根因**：同步锁竞争导致 Reactor 线程被数据库 IO 阻塞，且缺乏连接探活机制导致“毒丸效应”。
*   **解决方案**：重构为全异步任务队列模型，引入 `wait_for` 超时机制与 `ConnectionGuard` (RAII) 自动管理。

---

## 🔨 构建与运行 (Build)

### 前置依赖
*   CMake >= 3.10
*   Boost >= 1.70
*   MySQL Connector/C++
*   Protobuf & gRPC
*   Qt5 (仅客户端需要)

### 编译服务端

```bash
mkdir build && cd build
cmake ..
make -j4
```

### JWT 密钥（必须）

服务端现在强制 JWT 登录。首次启动前生成密钥：

```bash
cd <project-root>
bash scripts/dev/gen_jwt_keys.sh ./keys
```

确保 GateServer 和两个 ChatServer 的工作目录下都有：

```text
keys/jwt_private.pem   # 仅 GateServer 需要
keys/jwt_public.pem    # GateServer / ChatServer 都需要
```

`JwtEnabled` 默认已开启；密钥缺失时 Gate 登录会返回 503，而不是退回旧 UUID token。

### 内部 gRPC 令牌

Gate/Chat/Status/Verify 之间的内部 RPC 使用共享令牌：

```ini
[Internal]
Token = dev-internal-token
```

生产环境务必替换该值，并保证四个服务一致。

### 启动集群

```bash
# 启动顺序建议：
./StatusServer
./GateServer
./ChatServer
./ChatServer2  # 启动第二个业务节点
```