#!/bin/bash

# IM系统云服务器压测脚本
# 用途：在本地虚拟机上运行，测试云服务器性能

set -e

CLOUD_IP="110.40.228.20"
CLOUD_PORT="8090"

echo "========================================="
echo "📊 IM系统云服务器压力测试"
echo "========================================="
echo ""
echo "🎯 目标服务器: $CLOUD_IP:$CLOUD_PORT"
echo ""

# 1. 检查网络连通性
echo "🔍 步骤1：检查网络连通性..."
if ping -c 3 $CLOUD_IP > /dev/null 2>&1; then
    echo "✅ 网络连通正常"
else
    echo "❌ 无法连接到云服务器！"
    exit 1
fi
echo ""

# 2. 检查端口连通性
echo "🔍 步骤2：检查端口连通性..."
if nc -zv $CLOUD_IP $CLOUD_PORT 2>&1 | grep -q "succeeded"; then
    echo "✅ 端口 $CLOUD_PORT 可访问"
else
    echo "❌ 端口 $CLOUD_PORT 无法访问！"
    echo "请确认："
    echo "  1. ChatServer已启动"
    echo "  2. 防火墙已开放8090端口"
    exit 1
fi
echo ""

# 3. 运行测试
echo "========================================="
echo "🚀 开始压力测试"
echo "========================================="
echo ""

# 测试1：10连接（快速验证）
echo "📊 测试1：10连接（快速验证）"
echo "----------------------------------------"
python3 stress_test_tcp.py \
    --host $CLOUD_IP \
    --port $CLOUD_PORT \
    --connections 10 \
    --duration 10 \
    | tee test_10conn.log
echo ""
sleep 2

# 测试2：100连接（中等规模）
echo "📊 测试2：100连接（中等规模）"
echo "----------------------------------------"
python3 stress_test_tcp.py \
    --host $CLOUD_IP \
    --port $CLOUD_PORT \
    --connections 100 \
    --duration 30 \
    --interval 0.1 \
    | tee test_100conn.log
echo ""
sleep 5

# 测试3：500连接（大规模）
echo "📊 测试3：500连接（大规模）"
echo "----------------------------------------"
python3 stress_test_tcp.py \
    --host $CLOUD_IP \
    --port $CLOUD_PORT \
    --connections 500 \
    --duration 60 \
    --interval 0.1 \
    | tee test_500conn.log
echo ""
sleep 5

# 测试4：1000连接（超大规模）
echo "📊 测试4：1000连接（超大规模）"
echo "----------------------------------------"
python3 stress_test_tcp.py \
    --host $CLOUD_IP \
    --port $CLOUD_PORT \
    --connections 1000 \
    --duration 60 \
    --interval 0.1 \
    | tee test_1000conn.log
echo ""
sleep 5

# 测试5：2000连接（极限挑战）
echo "📊 测试5：2000连接（极限挑战）"
echo "----------------------------------------"
python3 stress_test_tcp.py \
    --host $CLOUD_IP \
    --port $CLOUD_PORT \
    --connections 2000 \
    --duration 60 \
    --interval 0.1 \
    | tee test_2000conn.log
echo ""

# 4. 汇总结果
echo "========================================="
echo "✅ 测试完成！"
echo "========================================="
echo ""
echo "📁 测试结果已保存："
ls -lh test_*conn.log
echo ""
echo "📊 快速查看结果："
echo "   grep '成功率' test_*conn.log"
echo "   grep 'QPS' test_*conn.log"
echo ""
echo "========================================="
