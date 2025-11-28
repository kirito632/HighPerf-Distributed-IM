import socket
import threading
import time
import argparse
import sys
from datetime import datetime
from collections import defaultdict

class TCPStressTest:
    def __init__(self, host, port, num_connections, duration, message_interval=1.0):
        self.host = host
        self.port = port
        self.num_connections = num_connections
        self.duration = duration
        self.message_interval = message_interval
        
        # 统计数据
        self.stats = {
            'connected': 0,
            'failed': 0,
            'messages_sent': 0,
            'messages_received': 0,
            'bytes_sent': 0,
            'bytes_received': 0,
            'errors': defaultdict(int),
        }
        self.lock = threading.Lock()
        self.start_time = None
        self.sockets = []
        
    def log(self, msg):
        """带时间戳的日志"""
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {msg}")
    
    def connect_socket(self, socket_id):
        """建立单个连接"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)  # 5秒超时
            sock.connect((self.host, self.port))
            
            with self.lock:
                self.stats['connected'] += 1
                self.sockets.append(sock)
            
            # 保持连接并接收数据
            self._maintain_connection(sock, socket_id)
            
        except socket.timeout:
            with self.lock:
                self.stats['failed'] += 1
                self.stats['errors']['timeout'] += 1
        except ConnectionRefusedError:
            with self.lock:
                self.stats['failed'] += 1
                self.stats['errors']['refused'] += 1
        except Exception as e:
            with self.lock:
                self.stats['failed'] += 1
                self.stats['errors'][str(type(e).__name__)] += 1
        finally:
            try:
                sock.close()
            except:
                pass
    
    def _maintain_connection(self, sock, socket_id):
        """维持连接（不发送任何数据，只保持连接）"""
        while time.time() - self.start_time < self.duration:
            try:
                # 只是保持连接，不发送任何数据
                # 这样可以测试服务器能支持多少并发连接
                time.sleep(0.1)  # 每 100ms 检查一次是否超时
                
            except Exception as e:
                break
    
    def run(self):
        """运行压力测试"""
        self.log(f"开始压力测试")
        self.log(f"目标: {self.host}:{self.port}")
        self.log(f"并发连接数: {self.num_connections}")
        self.log(f"测试时长: {self.duration}秒")
        self.log("")
        
        self.start_time = time.time()
        threads = []
        
        # 创建连接线程
        self.log(f"正在建立 {self.num_connections} 个连接...")
        for i in range(self.num_connections):
            t = threading.Thread(target=self.connect_socket, args=(i,))
            t.daemon = True
            t.start()
            threads.append(t)
            
            # 控制连接建立速率，避免过快
            if (i + 1) % 100 == 0:
                self.log(f"  已启动 {i + 1} 个连接线程")
                time.sleep(0.1)
        
        # 等待所有连接建立
        for t in threads:
            t.join(timeout=10)
        
        self.log(f"连接建立完成: {self.stats['connected']} 成功, {self.stats['failed']} 失败")
        self.log("")
        
        # 等待测试时长
        self.log(f"维持连接中，测试时长 {self.duration} 秒...")
        while time.time() - self.start_time < self.duration:
            elapsed = int(time.time() - self.start_time)
            sys.stdout.write(f"\r进度: {elapsed}/{self.duration}秒 | 连接: {self.stats['connected']} | 消息: {self.stats['messages_sent']} 发送")
            sys.stdout.flush()
            time.sleep(1)
        
        print()  # 换行
        self.log("")
        
        # 关闭所有连接
        self.log("关闭连接...")
        for sock in self.sockets:
            try:
                sock.close()
            except:
                pass
        
        # 输出统计结果
        self._print_stats()
    
    def _print_stats(self):
        """输出统计数据"""
        elapsed = time.time() - self.start_time
        total_connections = self.stats['connected'] + self.stats['failed']
        
        print("\n" + "="*70)
        print("📊 并发连接数压力测试结果")
        print("="*70)
        
        print(f"\n⏱️  测试时长: {elapsed:.2f} 秒")
        print(f"🎯 目标连接数: {self.num_connections}")
        
        print(f"\n🔗 连接统计:")
        print(f"  ✅ 成功连接: {self.stats['connected']:,}")
        print(f"  ❌ 失败连接: {self.stats['failed']:,}")
        if total_connections > 0:
            success_rate = self.stats['connected']/total_connections*100
            print(f"  📊 成功率: {success_rate:.1f}%")
        
        if self.stats['errors']:
            print(f"\n⚠️  错误分布:")
            for error_type, count in self.stats['errors'].items():
                print(f"  - {error_type}: {count}")
        
        print(f"\n📈 性能指标:")
        if elapsed > 0:
            conn_per_sec = self.stats['connected'] / elapsed
            print(f"  连接建立速率: {conn_per_sec:.1f} conn/s")
            print(f"  平均连接建立时间: {1000/conn_per_sec:.2f} ms/conn")
        
        print(f"\n💾 资源占用（预估）:")
        # 假设每个连接占用约 1-2MB 内存（包括缓冲区、socket 结构等）
        estimated_memory_mb = self.stats['connected'] * 0.002  # 2KB per connection
        print(f"  预估内存占用: ~{estimated_memory_mb:.2f} MB")
        print(f"  (基于每连接 ~2KB 估算)")
        
        print(f"\n✅ 测试结论:")
        if total_connections > 0:
            success_rate = self.stats['connected']/total_connections*100
            if success_rate >= 99.0:
                print(f"  ✨ 优秀！服务器稳定支持 {self.stats['connected']:,} 个并发连接")
            elif success_rate >= 95.0:
                print(f"  ✓ 良好！服务器支持 {self.stats['connected']:,} 个并发连接（成功率 {success_rate:.1f}%）")
            else:
                print(f"  ⚠️  警告！连接成功率仅 {success_rate:.1f}%，可能存在系统限制")
        
        print("\n" + "="*70)
        print(f"✨ 测试完成！")
        print("="*70 + "\n")

def main():
    parser = argparse.ArgumentParser(description='TCP 并发连接压力测试')
    parser.add_argument('--host', default='127.0.0.1', help='服务器地址 (默认: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=8090, help='服务器端口 (默认: 8090)')
    parser.add_argument('--connections', type=int, default=100, help='并发连接数 (默认: 100)')
    parser.add_argument('--duration', type=int, default=30, help='测试时长秒数 (默认: 30)')
    parser.add_argument('--interval', type=float, default=0.0, help='消息发送间隔秒数 (默认: 0.0 = 高频)')
    
    args = parser.parse_args()
    
    test = TCPStressTest(
        host=args.host,
        port=args.port,
        num_connections=args.connections,
        duration=args.duration,
        message_interval=args.interval
    )
    
    try:
        test.run()
    except KeyboardInterrupt:
        print("\n\n⚠️  测试被中断")
        sys.exit(1)

if __name__ == '__main__':
    main()
