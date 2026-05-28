#!/bin/bash
# run_stress.sh - 封装 stress-ng 的系统压力测试脚本

# ==== 配置区 ====
# CPU 工作线程数 (我的cpu是6核心12线程，隔离了两个逻辑核，一般设置成没被隔离的逻辑核数量)
CPU_WORKERS=10
# Cache 冲击线程数
CACHE_WORKERS=2
# VM (内存读写) 线程数
VM_WORKERS=2
# 测试持续时间
TIMEOUT="60s"
# ================

# 检查是否安装了 stress-ng 工具
if ! command -v stress-ng &> /dev/null; then
    echo "未检测到 stress-ng 工具，正在尝试使用 apt 安装..."
    sudo apt update
    sudo apt install -y stress-ng
    echo "stress-ng 安装完成。"
fi

echo "================================================"
echo "准备启动压力测试..."
echo "   -> CPU 运算线程: $CPU_WORKERS"
echo "   -> 缓存冲击线程: $CACHE_WORKERS"
echo "   -> 内存读写线程: $VM_WORKERS"
echo "   -> 持续时间:     $TIMEOUT"
echo "================================================"
echo "测试进行中，请在另一个已隔离的终端运行航电靶标程序..."
echo ""

# 启动压力测试
stress-ng --cpu "$CPU_WORKERS" --cache "$CACHE_WORKERS" --vm "$VM_WORKERS" --timeout "$TIMEOUT"

echo ""
echo "压力测试结束。"
