#!/bin/bash
# CMA 内存分区自动化初始化脚本
# 1. 动态获取路径 (核心修改点)
# 获取当前脚本所在的绝对路径 (.../scripts)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
# 根据相对关系推导源码目录 (.../src/cma_avionics)
SRC_DIR="${SCRIPT_DIR}/../src/cma_avionics"

MODULE_NAME="avionics_cma"
MODULE_FILE="${MODULE_NAME}.ko"
DEVICE_NODE="/dev/avionics_cma"

# 权限校验
if [ "$EUID" -ne 0 ]; then
  echo "[-] 权限错误: 插入内核模块需要 Root 权限，请使用 sudo 运行此脚本。"
  exit 1
fi

echo "[+] 开始初始化 CMA 内存隔离分区..."

# 校验内核启动参数
if ! grep -q "cma=" /proc/cmdline; then
  echo "[-] 警告: 当前内核启动参数未检测到 cma= 配置！"
  echo "    请检查 /etc/default/grub 并重新执行 update-grub 后重启。"
  exit 1
else
  CMA_ARG=$(grep -o 'cma=[^ ]*' /proc/cmdline)
  echo "  -> 检测到内核 CMA 参数: $CMA_ARG"
fi

# 2. 进入源码目录执行编译和加载操作
echo "  -> 切换工作目录至: $SRC_DIR"
if [ ! -d "$SRC_DIR" ]; then
    echo "[-] 错误: 找不到源码目录 $SRC_DIR，请检查目录结构。"
    exit 1
fi
cd "$SRC_DIR" || exit 1

# 编译与模块检查
if [ ! -f "$MODULE_FILE" ]; then
  echo "[!] 未找到编译好的内核模块 $MODULE_FILE"
  echo "  -> 尝试自动执行 make 编译..."
  make clean && make
  if [ ! -f "$MODULE_FILE" ]; then
    echo "[-] 编译失败，请检查 Makefile 或内核头文件依赖。"
    exit 1
  fi
fi

# 驱动模块加载与刷新
if lsmod | grep -q "^${MODULE_NAME}"; then
  echo "  -> 发现已加载的旧模块，正在卸载重置..."
  rmmod ${MODULE_NAME}
  sleep 0.5
fi

echo "  -> 正在加载内核模块 ${MODULE_FILE} ..."
insmod ./$MODULE_FILE

if [ $? -ne 0 ]; then
  echo "[-] 模块加载失败，请通过 dmesg | tail 查看具体内核报错。"
  exit 1
fi

# 设备节点权限配置
if [ -c "$DEVICE_NODE" ]; then
  chmod 666 $DEVICE_NODE
  echo "[+] 成功: 设备节点已就绪 -> $(ls -l $DEVICE_NODE | awk '{print $1, $3, $4, $10}')"
else
  echo "[-] 错误: 模块加载成功，但未生成设备节点 $DEVICE_NODE，请检查 misc_register 逻辑。"
  exit 1
fi

# CMA 内存池状态验证
if ! mount | grep -q "/sys/kernel/debug"; then
  mount -t debugfs none /sys/kernel/debug
fi

if [ -d "/sys/kernel/debug/cma/reserved" ]; then
  TOTAL_PAGES=$(cat /sys/kernel/debug/cma/reserved/count)
  USED_PAGES=$(cat /sys/kernel/debug/cma/reserved/used)
  TOTAL_MB=$((TOTAL_PAGES * 4 / 1024))
  USED_MB=$((USED_PAGES * 4 / 1024))
  
  echo "[+] CMA 内存池状态 (节点: reserved):"
  echo "  -> 总容量: $TOTAL_MB MB ($TOTAL_PAGES 页)"
  echo "  -> 已分配: $USED_MB MB ($USED_PAGES 页)"
else
  echo "[!] 提示: 无法在 debugfs 中找到 CMA 统计信息目录，跳过状态打印。"
fi

echo "[+] 初始化完成！航电进程现在可以正常访问物理隔离内存了。"

# 可选：切回脚本原来的目录
cd "$SCRIPT_DIR" || exit 0