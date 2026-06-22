#!/bin/bash
# setup_cgroups.sh -脚本自动配置 cgroups v2 核心隔离区

# 检查是否以 root 权限运行
if [ "$EUID" -ne 0 ]; then
  echo " 配置 cgroups 需要管理员权限，请使用 sudo 运行此脚本。"
  exit 1
fi

# ==== 配置区 ====
CGROUP_BASE="/sys/fs/cgroup"
PARTITION_NAME="avionics_partition"
PARTITION_DIR="$CGROUP_BASE/$PARTITION_NAME"
# 你需要隔离的物理核心（这里可以根据需求自行修改）
ISOLATED_CPUS="10-11"
# ================

echo "=> 开始配置 cgroups v2 隔离区 (隔离核心: $ISOLATED_CPUS)..."

# 1. 激活父级 cpuset 资源控制器
echo "+cpuset" > "$CGROUP_BASE/cgroup.subtree_control"
echo "   [OK] 已激活父级 cpuset 控制器"

# 2. 创建分区目录 (如果不存在的话)
if [ ! -d "$PARTITION_DIR" ]; then
    mkdir "$PARTITION_DIR"
    echo "   [OK] 已创建分区目录: $PARTITION_DIR"
else
    echo "   [INFO] 分区目录已存在: $PARTITION_DIR"
fi

# 3. 为分区划拨独占的物理核心
echo "$ISOLATED_CPUS" > "$PARTITION_DIR/cpuset.cpus"
echo "   [OK] 已划拨物理核心: $ISOLATED_CPUS"

# 4. 将该 cgroup 跃迁为隔离的根调度域
echo "root" > "$PARTITION_DIR/cpuset.cpus.partition"
echo "   [OK] 已将该分区设置为隔离的根调度域 (状态: root)"

echo "------------------------------------------------"
echo "✅ 配置完成！"
echo ""
echo "要将当前终端丢进这个隔离区，请直接在你的命令行中执行以下命令："
echo "sudo sh -c \"echo \$\$ > $PARTITION_DIR/cgroup.procs\""
echo "可以再敲下面两行代码检验一下分区效果"
echo "cat /proc/self/cgroup    cat /proc/self/status | grep Cpus_allowed_list"
echo "------------------------------------------------"
