#!/bin/bash
# =====================================================================
# 航电多核系统 - L3 缓存物理分区自动化配置脚本
# 适用平台: AMD Ryzen (Zen 2/3/4) 支持 QoS/CAT 功能的处理器
# 目标: 将 L3:0 节点的 2MB (掩码 f) 独占分配给核心 10,11
# =====================================================================

# 1. 检查是否使用 root 权限运行
if [ "$EUID" -ne 0 ]; then
  echo "错误: 配置 resctrl 底层硬件需要 root 权限。"
  echo "请使用 'sudo $0' 重新运行此脚本。"
  exit 1
fi

RESCTRL_DIR="/sys/fs/resctrl"
AVIONICS_DIR="$RESCTRL_DIR/avionics_cache"

echo "=========================================="
echo "开始配置航电 L3 缓存物理分区..."
echo "=========================================="

# 2. 检查并挂载 resctrl 文件系统
echo "[1/5] 正在检查 resctrl 挂载状态..."
if ! grep -qs "$RESCTRL_DIR " /proc/mounts; then
    echo "      -> 未挂载，正在执行挂载..."
    mount -t resctrl resctrl $RESCTRL_DIR
    if [ $? -ne 0 ]; then
        echo "错误: 挂载 resctrl 失败。请检查主板 BIOS 是否开启了缓存分配支持。"
        exit 1
    fi
else
    echo "      -> resctrl 已挂载。"
fi

# 3. 修改全局掩码，腾出空间防止重叠 (Schemata overlaps)
echo "[2/5] 正在重置全局 L3 缓存掩码 (退让后 4 个 bit)..."
# 注意：这里需要同时退让 L3:0 和 L3:1，否则创建独占组时内核会报错
echo "L3:0=fff0;1=fff0" > "$RESCTRL_DIR/schemata"
if [ $? -ne 0 ]; then
    echo "错误: 写入全局 schemata 失败。"
    exit 1
fi

# 4. 创建航电专属资源组
echo "[3/5] 正在创建航电专属资源组 (avionics_cache)..."
if [ ! -d "$AVIONICS_DIR" ]; then
    mkdir -p "$AVIONICS_DIR"
fi

# 5. 配置航电组专属掩码并开启独占模式
echo "[4/5] 正在分配物理缓存并锁定为独占模式 (Exclusive)..."
# 给 L3:0 分配 f (即 2MB)，L3:1 不分配 (0)
echo "L3:0=f;1=0" > "$AVIONICS_DIR/schemata"

# 开启底层硬件隔离防线
echo "exclusive" > "$AVIONICS_DIR/mode"
# 检查是否成功开启独占模式 (排查 overlaps 问题)
MODE_CHECK=$(cat "$AVIONICS_DIR/mode")
if [ "$MODE_CHECK" != "exclusive" ]; then
    echo "错误: 无法开启 exclusive 模式。内核报告:"
    cat "$RESCTRL_DIR/info/last_cmd_status"
    exit 1
fi

# 6. 绑定物理核心
echo "[5/5] 正在将目标核心 (CPU 10-11) 路由至专属缓存区..."
echo "10-11" > "$AVIONICS_DIR/cpus_list"

echo "=========================================="
echo "L3 缓存分区配置完成！"
echo "=========================================="

# 7. 打印最终硬件状态以供验证
echo "[硬件状态验证]"
echo "- 航电分区绑定核心: $(cat $AVIONICS_DIR/cpus_list)"
echo "- 全局 L3 掩码拓扑 (E为独占, S为共享, 0为闲置):"
cat $RESCTRL_DIR/info/L3/bit_usage
echo ""