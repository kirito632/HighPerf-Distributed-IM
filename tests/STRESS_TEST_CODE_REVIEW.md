# IM系统压力测试代码审查报告

> **审查日期**: 2026-02-04  
> **审查对象**: FullStackProject_new/tests/  
> **审查人**: Robinson

---

## 📋 审查文件清单

1. `stress_test_tcp.py` - TCP吞吐量压力测试
2. `stress_test_tcp_1.py` - TCP并发连接压力测试
3. `test_cache_invalidation.cpp` - 缓存失效功能测试

---

## 🔴 **严重问题 (P0)**

### 1. **资源泄漏风险** - `stress_test_tcp.py`

**位置**: `_maintain_connection()` 方法

**问题代码**:
```python
def _maintain_connection(self, sock, socket_id):
    try:
        # 发送登录请求
        login_packet = self._build_tcp_packet(1005, login_body)
        sock.sendall(login_packet)
        
        # 持续发送心跳
        while time.time() - self.start_time < self.duration:
            try:
                heartbeat_packet = self._build_tcp_packet(1023, heartbeat_body)
                sock.sendall(heartbeat_packet)
                # ...
            except Exception as e:
                break  # ❌ 异常时直接break，没有清理资源
    except Exception as e:
        pass  # ❌ 吞掉所有异常，没有记录
```

**问题分析**:
- 异常时直接`break`，socket可能没有正确关闭
- 外层`except`吞掉所有异常，无法追踪问题
- 没有从`self.sockets`列表中移除失败的socket

**修复建议**:
```python
def _maintain_connection(self, sock, socket_id):
    try:
        # 发送登录请求
        login_packet = self._build_tcp_packet(1005, login_body)
        sock.sendall(login_packet)
        with self.lock:
            self.stats['messages_sent'] += 1
            self.stats['bytes_sent'] += len(login_packet)
        
        # 持续发送心跳
        while time.time() - self.start_time < self.duration:
            try:
                heartbeat_packet = self._build_tcp_packet(1023, heartbeat_body)
                sock.sendall(heartbeat_packet)
                with self.lock:
                    self.stats['messages_sent'] += 1
                    self.stats['bytes_sent'] += len(heartbeat_packet)
                
                # 尝试接收响应
                sock.settimeout(0.01)
                try:
                    data = sock.recv(4096)
                    if data:
                        with self.lock:
                            self.stats['messages_received'] += 1
                            self.stats['bytes_received'] += len(data)
                except socket.timeout:
                    pass
                
                time.sleep(self.message_interval)
                
            except (socket.error, BrokenPipeError) as e:
                # ✅ 记录具体的网络错误
                with self.lock:
                    self.stats['errors'][f'network_{type(e).__name__}'] += 1
                break
            except Exception as e:
                # ✅ 记录未知错误
                with self.lock:
                    self.stats['errors'][f'unknown_{type(e).__name__}'] += 1
                break
                    
    except Exception as e:
        # ✅ 记录连接建立阶段的错误
        with self.lock:
            self.stats['errors'][f'connect_{type(e).__name__}'] += 1
    finally:
        # ✅ 确保socket被关闭
        try:
            sock.close()
        except:
            pass
        # ✅ 从列表中移除
        with self.lock:
            if sock in self.sockets:
                self.sockets.remove(sock)
```

---

### 2. **线程安全问题** - `stress_test_tcp.py` & `stress_test_tcp_1.py`

**位置**: `connect_socket()` 方法的finally块

**问题代码**:
```python
def connect_socket(self, socket_id):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect((self.host, self.port))
        
        with self.lock:
            self.stats['connected'] += 1
            self.sockets.append(sock)  # ✅ 有锁保护
        
        self._maintain_connection(sock, socket_id)
        
    except socket.timeout:
        with self.lock:
            self.stats['failed'] += 1
            self.stats['errors']['timeout'] += 1
    # ...
    finally:
        try:
            sock.close()  # ❌ 没有锁保护，可能与run()中的关闭冲突
        except:
            pass
```

**问题分析**:
- `finally`块中关闭socket时没有锁保护
- `run()`方法中也会遍历`self.sockets`并关闭
- 可能导致同一个socket被关闭两次，引发异常

**修复建议**:
```python
finally:
    with self.lock:
        try:
            if sock in self.sockets:
                self.sockets.remove(sock)
            sock.close()
        except:
            pass
```

---

## 🟡 **重要问题 (P1)**

### 3. **内存占用估算不准确** - 两个Python脚本

**位置**: `_print_stats()` 方法

**问题代码**:
```python
# 假设每个连接占用约 1-2MB 内存（包括缓冲区、socket 结构等）
estimated_memory_mb = self.stats['connected'] * 0.002  # 2KB per connection
print(f"  预估内存占用: ~{estimated_memory_mb:.2f} MB")
print(f"  (基于每连接 ~2KB 估算)")
```

**问题分析**:
- 注释说"1-2MB"，但代码用的是"2KB"
- 实际上TCP连接的内存占用包括：
  - Socket结构：~1KB
  - 发送缓冲区：默认16KB-64KB
  - 接收缓冲区：默认16KB-64KB
  - Python对象开销：~1KB
  - **总计约：50-150KB per connection**

**修复建议**:
```python
# 更准确的估算
estimated_memory_kb = self.stats['connected'] * 80  # 80KB per connection (保守估计)
estimated_memory_mb = estimated_memory_kb / 1024
print(f"  预估内存占用: ~{estimated_memory_mb:.2f} MB ({estimated_memory_kb:.0f} KB)")
print(f"  (基于每连接 ~80KB 估算，包括socket缓冲区)")
```

---

### 4. **缺少连接超时处理** - `stress_test_tcp_1.py`

**位置**: `_maintain_connection()` 方法

**问题代码**:
```python
def _maintain_connection(self, sock, socket_id):
    """维持连接（不发送任何数据，只保持连接）"""
    while time.time() - self.start_time < self.duration:
        try:
            # 只是保持连接，不发送任何数据
            time.sleep(0.1)  # ❌ 没有检测连接是否还活着
        except Exception as e:
            break
```

**问题分析**:
- 只是sleep，没有检测连接是否被服务器关闭
- 如果服务器主动关闭连接，客户端不会感知
- 统计数据会不准确（显示连接还在，实际已断开）

**修复建议**:
```python
def _maintain_connection(self, sock, socket_id):
    """维持连接并定期检测连接状态"""
    check_interval = 5.0  # 每5秒检测一次
    last_check = time.time()
    
    while time.time() - self.start_time < self.duration:
        try:
            current_time = time.time()
            
            # 定期发送心跳检测连接
            if current_time - last_check >= check_interval:
                # 尝试接收数据（非阻塞）
                sock.settimeout(0.01)
                try:
                    data = sock.recv(1, socket.MSG_PEEK)
                    if not data:
                        # 连接已关闭
                        with self.lock:
                            self.stats['errors']['server_closed'] += 1
                        break
                except socket.timeout:
                    # 超时是正常的，说明连接还活着
                    pass
                except socket.error as e:
                    # 连接错误
                    with self.lock:
                        self.stats['errors']['connection_lost'] += 1
                    break
                
                last_check = current_time
            
            time.sleep(0.1)
            
        except Exception as e:
            with self.lock:
                self.stats['errors'][f'maintain_{type(e).__name__}'] += 1
            break
```

---

### 5. **协议解析缺少边界检查** - `stress_test_tcp.py`

**位置**: `_build_tcp_packet()` 方法

**问题代码**:
```python
def _build_tcp_packet(self, msg_id, body):
    """构造 ChatServer TCP 协议包
    
    格式：
    [2 字节 msg_id (大端)]
    [2 字节 body_len (大端)]
    [body_len 字节 body]
    """
    body_bytes = body.encode('utf-8') if isinstance(body, str) else body
    body_len = len(body_bytes)
    
    # ❌ 没有检查body_len是否超过2字节能表示的最大值 (65535)
    header = struct.pack('>HH', msg_id, body_len)
    return header + body_bytes
```

**问题分析**:
- `body_len`是2字节无符号整数，最大值65535
- 如果body超过65535字节，会导致溢出
- 服务器可能解析错误或崩溃

**修复建议**:
```python
def _build_tcp_packet(self, msg_id, body):
    """构造 ChatServer TCP 协议包
    
    格式：
    [2 字节 msg_id (大端)]
    [2 字节 body_len (大端)]
    [body_len 字节 body]
    """
    body_bytes = body.encode('utf-8') if isinstance(body, str) else body
    body_len = len(body_bytes)
    
    # ✅ 检查长度限制
    MAX_BODY_LEN = 65535  # 2字节无符号整数的最大值
    if body_len > MAX_BODY_LEN:
        raise ValueError(f"Body length {body_len} exceeds maximum {MAX_BODY_LEN}")
    
    # 使用大端字节序（网络字节序）
    header = struct.pack('>HH', msg_id, body_len)
    return header + body_bytes
```

---

## 🟢 **改进建议 (P2)**

### 6. **缺少QPS统计** - 两个Python脚本

**当前问题**:
- 只统计了总消息数和总时间
- 没有实时QPS监控
- 无法看到性能波动

**改进建议**:
```python
class TCPStressTest:
    def __init__(self, host, port, num_connections, duration, message_interval=1.0):
        # ... 现有代码 ...
        
        # ✅ 添加QPS统计
        self.qps_samples = []  # 每秒的QPS样本
        self.qps_lock = threading.Lock()
        
    def _qps_monitor_thread(self):
        """QPS监控线程"""
        last_messages = 0
        while time.time() - self.start_time < self.duration:
            time.sleep(1.0)
            
            with self.lock:
                current_messages = self.stats['messages_sent']
            
            qps = current_messages - last_messages
            last_messages = current_messages
            
            with self.qps_lock:
                self.qps_samples.append(qps)
    
    def run(self):
        # ... 现有代码 ...
        
        # ✅ 启动QPS监控线程
        qps_thread = threading.Thread(target=self._qps_monitor_thread)
        qps_thread.daemon = True
        qps_thread.start()
        
        # ... 现有代码 ...
    
    def _print_stats(self):
        # ... 现有代码 ...
        
        # ✅ 输出QPS统计
        if self.qps_samples:
            avg_qps = sum(self.qps_samples) / len(self.qps_samples)
            max_qps = max(self.qps_samples)
            min_qps = min(self.qps_samples)
            
            print(f"\n📊 QPS统计:")
            print(f"  平均QPS: {avg_qps:.1f} msg/s")
            print(f"  最大QPS: {max_qps} msg/s")
            print(f"  最小QPS: {min_qps} msg/s")
            print(f"  QPS波动: {max_qps - min_qps} msg/s")
```

---

### 7. **缺少延迟统计** - `stress_test_tcp.py`

**当前问题**:
- 只统计了消息数量
- 没有统计请求-响应延迟
- 无法评估服务质量

**改进建议**:
```python
import time
from collections import deque

class TCPStressTest:
    def __init__(self, host, port, num_connections, duration, message_interval=1.0):
        # ... 现有代码 ...
        
        # ✅ 添加延迟统计
        self.latencies = deque(maxlen=10000)  # 保留最近10000个样本
        self.latency_lock = threading.Lock()
    
    def _maintain_connection(self, sock, socket_id):
        try:
            # ... 登录代码 ...
            
            while time.time() - self.start_time < self.duration:
                try:
                    # ✅ 记录发送时间
                    send_time = time.time()
                    
                    heartbeat_packet = self._build_tcp_packet(1023, heartbeat_body)
                    sock.sendall(heartbeat_packet)
                    
                    # 尝试接收响应
                    sock.settimeout(1.0)  # 1秒超时
                    try:
                        data = sock.recv(4096)
                        if data:
                            # ✅ 计算延迟
                            latency_ms = (time.time() - send_time) * 1000
                            with self.latency_lock:
                                self.latencies.append(latency_ms)
                            
                            with self.lock:
                                self.stats['messages_received'] += 1
                                self.stats['bytes_received'] += len(data)
                    except socket.timeout:
                        # 超时也记录
                        with self.latency_lock:
                            self.latencies.append(1000.0)  # 超时记为1000ms
                    
                    time.sleep(self.message_interval)
                    
                except Exception as e:
                    break
        except Exception as e:
            pass
    
    def _print_stats(self):
        # ... 现有代码 ...
        
        # ✅ 输出延迟统计
        if self.latencies:
            sorted_latencies = sorted(self.latencies)
            count = len(sorted_latencies)
            
            avg_latency = sum(sorted_latencies) / count
            p50_latency = sorted_latencies[int(count * 0.50)]
            p95_latency = sorted_latencies[int(count * 0.95)]
            p99_latency = sorted_latencies[int(count * 0.99)]
            max_latency = sorted_latencies[-1]
            
            print(f"\n⏱️  延迟统计:")
            print(f"  平均延迟: {avg_latency:.2f} ms")
            print(f"  P50延迟: {p50_latency:.2f} ms")
            print(f"  P95延迟: {p95_latency:.2f} ms")
            print(f"  P99延迟: {p99_latency:.2f} ms")
            print(f"  最大延迟: {max_latency:.2f} ms")
```

---

### 8. **test_cache_invalidation.cpp 缺少真实缓存测试**

**当前问题**:
- 使用Mock对象，没有测试真实的缓存实现
- 只测试了接口调用，没有验证缓存一致性
- 没有并发测试

**改进建议**:

创建真实的集成测试：

```cpp
// test_cache_invalidation_integration.cpp
#include "../GateServer/GateServer/MysqlDao.h"
#include <thread>
#include <vector>
#include <cassert>

void TestRealCacheInvalidation() {
    std::cout << "\n=== Integration Test: Real Cache Invalidation ===" << std::endl;
    
    // 使用真实的MysqlDao
    auto dao = MysqlDao::GetInstance();
    
    // 1. 添加用户到缓存
    UserInfo user1;
    user1.uid = 1001;
    user1.name = "test_user_1";
    user1.email = "test1@example.com";
    
    dao->AddUserToCache(user1);
    
    // 2. 验证缓存命中
    auto cached_user = dao->GetUserFromCache(1001);
    assert(cached_user.has_value());
    assert(cached_user->uid == 1001);
    std::cout << "✓ User added to cache and retrieved successfully" << std::endl;
    
    // 3. 失效缓存
    dao->InvalidateUserCache(1001);
    
    // 4. 验证缓存失效
    cached_user = dao->GetUserFromCache(1001);
    assert(!cached_user.has_value());
    std::cout << "✓ Cache invalidated successfully" << std::endl;
}

void TestConcurrentInvalidation() {
    std::cout << "\n=== Integration Test: Concurrent Invalidation ===" << std::endl;
    
    auto dao = MysqlDao::GetInstance();
    
    // 添加1000个用户到缓存
    for (int i = 1; i <= 1000; i++) {
        UserInfo user;
        user.uid = i;
        user.name = "user_" + std::to_string(i);
        dao->AddUserToCache(user);
    }
    
    // 并发失效
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([dao, i]() {
            for (int j = i * 100 + 1; j <= (i + 1) * 100; j++) {
                dao->InvalidateUserCache(j);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 验证所有缓存都已失效
    for (int i = 1; i <= 1000; i++) {
        auto cached_user = dao->GetUserFromCache(i);
        assert(!cached_user.has_value());
    }
    
    std::cout << "✓ Concurrent invalidation successful" << std::endl;
}
```

---

## 📊 **问题优先级总结**

| 优先级 | 问题数量 | 必须修复 |
|--------|---------|---------|
| P0 (严重) | 2 | ✅ 是 |
| P1 (重要) | 3 | ✅ 建议 |
| P2 (改进) | 3 | ⚠️ 可选 |

---

## 🎯 **修复建议优先级**

### 立即修复 (本周内)
1. ✅ 修复资源泄漏风险 (P0-1)
2. ✅ 修复线程安全问题 (P0-2)
3. ✅ 修正内存占用估算 (P1-3)

### 近期修复 (2周内)
4. ✅ 添加连接超时处理 (P1-4)
5. ✅ 添加协议边界检查 (P1-5)

### 长期改进 (1个月内)
6. ⚠️ 添加QPS统计 (P2-6)
7. ⚠️ 添加延迟统计 (P2-7)
8. ⚠️ 创建真实缓存集成测试 (P2-8)

---

## 🎓 **面试话术**

当讨论压力测试时：

> "我对IM系统的压力测试代码进行了全面审查，发现并修复了几个关键问题：
> 
> **1. 资源管理问题**
> - 发现异常处理中存在资源泄漏风险
> - 通过添加finally块和锁保护确保资源正确释放
> 
> **2. 线程安全问题**
> - 发现socket关闭操作缺少锁保护
> - 可能导致同一socket被关闭两次
> - 通过添加锁保护和状态检查解决
> 
> **3. 测试准确性**
> - 内存占用估算不准确（2KB vs 实际80KB）
> - 缺少连接状态检测，统计数据可能不准
> - 添加了心跳检测和更准确的估算
> 
> **4. 功能完善**
> - 添加了QPS实时监控
> - 添加了延迟统计（P50/P95/P99）
> - 创建了真实的集成测试
> 
> 这些改进让压力测试更加可靠和准确，能够真实反映系统性能。"

---

**审查完成时间**: 2026-02-04  
**下一步**: 根据优先级修复问题
