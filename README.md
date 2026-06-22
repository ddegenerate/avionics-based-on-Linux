# Parallel_Partition — 航电多核处理器时间分区框架

在 Linux 用户态模拟 **ARINC 653 并行分区（Synchronized Scheduling）** 的核心机制：多核时间边界垂直对齐、分区无干扰切换、双层接力调度。

---

## 项目结构

```
Parallel_Partition/
├── README.md
├── docs/
│   ├── 01-概念基础.md           # 串行分区与并行分区的概念解释
│   ├── 02-早期误区与修正.md      # cgroups/SCHED_DEADLINE 误区及修正
│   ├── 03-框架原理.md           # 三层架构、MAF 时钟、EWMA、双层接力调度
│   ├── 04-数据采集与分析.md      # 双通道可观测性、ftrace、CSV、plot_slots
│   ├── 05-实验结果.md           # 运行结果分析
│   └── 06-框架使用指南.md        # 如何修改配置、切换算法、自定义负载
├── images/                      # 文档所用图片
├── src/                         # 源代码
│   ├── main.cpp                 # 入口：分区配置 → 启动 → 监视 → 输出
│   ├── partition.hpp            # 分区配置 + 时间片查表 + 任务描述
│   ├── maf_clock.hpp/cpp        # MAF 全局基准时钟
│   ├── scheduler.hpp            # SCHED_FIFO + CPU 亲和性
│   ├── worker_thread.hpp/cpp    # 工作线程（EWMA + 双层接力调度）
│   ├── workload.hpp             # 矩阵乘法负载模板
│   ├── task_assigner.hpp        # 任务→核心分配策略
│   ├── trace_marker.hpp         # ftrace 用户态标记
│   ├── slot_logger.hpp          # CSV 事件日志
│   └── plot_slots.py            # 双层甘特图生成
└── script/
    └── setup_core.sh            # cgroups v2 核心隔离配置
```

---

## 环境要求

- **Linux**（需 `pthread`、`SCHED_FIFO`、`cgroup cpuset v2`）
- **GCC 8+** 或 Clang 10+
- **C++17**
- **Python 3** + `matplotlib`（仅画图脚本需要）
- `sudo` 权限

---

## 快速开始

### Step 1 — 内核启动参数（一次性）

编辑 `/etc/default/grub`，在 `GRUB_CMDLINE_LINUX` 中添加：

```
isolcpus=10-11 nohz_full=10-11 rcu_nocbs=10-11
```

执行 `sudo update-grub && sudo reboot`。

| 参数 | 作用 |
|------|------|
| `isolcpus=10-11` | 禁止内核将普通进程调度到 Core 10-11 |
| `nohz_full=10-11` | 关闭定时器中断（tickless） |
| `rcu_nocbs=10-11` | 迁移 RCU 回调线程 |

### Step 2 — cgroups 核心隔离

```bash
sudo bash script/setup_core.sh
```

该脚本依次完成：
1. 激活父级 cpuset 资源控制器
2. 创建分区专属 cgroup `avionics_partition`
3. 划拨 Core 10-11 的独占使用权
4. 将该 cgroup 跃迁为隔离根调度域

### Step 3 — 编译

```bash
cd src/
g++ -std=c++17 -O2 -pthread \
    main.cpp maf_clock.cpp worker_thread.cpp \
    -o avionics_parallel_v8
```

| 编译选项 | 说明 |
|---------|------|
| `-std=c++17` | C++17（`std::aligned_alloc`、`inline` 变量等） |
| `-O2` | 二级优化 |
| `-pthread` | 链接 pthread |

### Step 4 — 运行

```bash
# 将当前 shell 放入隔离分区
sudo sh -c "echo \$\$ > /sys/fs/cgroup/avionics_partition/cgroup.procs"

# 运行分区调度程序
sudo ./avionics_parallel_v8
```

程序启动后打印分区配置和实时进度，按 Ctrl+C 或等待完成后输出各核心统计结果。

---

## 自定义配置

> **详细使用指南请参阅 [docs/06-框架使用指南.md](docs/06-框架使用指南.md)**，涵盖分区配置、任务定义、切换分配/余量策略、自定义负载、参数调优等完整内容。

所有分区和任务配置在 `main.cpp` 的**配置区**直接修改。核心参数：

```cpp
constexpr int TOTAL_CYCLES = 20000;          // MAF 周期总数
constexpr MarginStrategy STRATEGY = EWMA_2SIGMA;  // 余量策略

// 分区示例：并行分区，Core 10-11，时间片 0，10ms
table.add({
    1, 10, 2, 0, 10,
    PartitionMode::PARALLEL,
    {
        {1, "sensor",  [&]{ ... }, 2000},        // 关键任务
        {4, "self_test", [&]{ ... }, 500, false}, // 弹性任务
    }
});
```

### 切换余量策略

```cpp
WorkerThread::MarginStrategy::EWMA_2SIGMA;   // 推荐：EWMA + 2σ
WorkerThread::MarginStrategy::LAST_TIMES_12; // 兼容旧版：last × 1.2
WorkerThread::MarginStrategy::FIXED;         // 固定值
```

### 添加串行分区

```cpp
table.add({
    3, 12, 1, 2, 10,                   // cpu_count=1 → 串行
    PartitionMode::SERIAL,
    { {10, "nav", [&]{ ... }, 3000} }
});
```

---

## 数据分析

### 生成甘特图

```bash
cd src/
python3 plot_slots.py trace_log.csv
# 可选：扩大显示范围
python3 plot_slots.py trace_log.csv --max-cycles 20 --detail-cycles 5
```

输出 `trace_log.png`：上层 Overview（聚合视图）+ 下层 Detail（逐任务放大视图）。

### 录制 ftrace 事件

```bash
# 终端 A
sudo trace-cmd record -e sched:sched_switch -e sched:sched_wakeup \
                       -e sched:sched_wakeup_new -e ftrace:print

# 终端 B
sudo ./avionics_parallel_v8

# 终端 A 按 Ctrl+C
kernelshark trace.dat
```

---

## 关键验证点

| 指标 | 预期值 | 说明 |
|------|--------|------|
| `overrun` | 0 | EWMA 自适应余量有效 |
| `yields` | > 0 | 自适应停手机制工作 |
| 多核 WAKE 偏差 | ≤ 1µs | `TIMER_ABSTIME` 对齐精度 |
| 负载比 | ≈ 1:1 | WCET 感知贪心分配均衡 |
| ELASTIC 事件 | 四核全非零 | 两阶段分配广播成功 |

---

## 技术栈

| 技术 | 用途 |
|------|------|
| `CLOCK_MONOTONIC` | 全局单调时间源 |
| `TIMER_ABSTIME` | 绝对时间睡眠，消除相位漂移 |
| `SCHED_FIFO(99)` | 实时优先级调度 |
| `pthread_setaffinity_np` | 精确单核绑定 |
| cgroups v2 (cpuset) | 硬件级核心隔离 |
| EWMA + 2σ | 自适应安全余量 |
| ftrace + trace_marker | 内核级事件录制 |
| CSV + matplotlib | 用户态数据日志与可视化 |
