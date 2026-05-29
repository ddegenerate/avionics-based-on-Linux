# Linux 系统模拟航电多核处理器四层分区隔离

## 概述

航电实时系统对任务执行的确定性有严格要求——单次超时即可能导致系统级故障。然而在通用 Linux 多核平台上，后台进程的调度抢占、内存争用和缓存踩踏会引入毫秒级的不可预测抖动，使应用层程序的实际执行时间远偏离其理论计算时间。

本工程以 AMD Ryzen 5 5500U (6C/12T) + Ubuntu 24.04 为实验平台，从零构建了一套递进式四层分区隔离方案。通过组合 Linux 内核的 cgroups v2、SCHED_DEADLINE、CMA 以及硬件 CAT/QoS 等机制，在强干扰背景负载下将系统抖动从 **10085 μs 降低至 63 μs**（降幅约 99.4%），实现了接近裸金属的可预测时延。

**实验环境：**
- 处理器：AMD Ryzen 5 5500U（6 物理核 / 12 逻辑核，SMT 开启）
- 操作系统：Linux 6.x (Ubuntu 24.04)
- 干扰工具：stress-ng
- 追踪工具：trace-cmd + KernelShark

---

## 架构设计

分区策略从软件调度层向硬件微架构层逐级深入，共四个层次：

```
┌─────────────────────────────────────────────┐
│  第四层：L3 缓存分区 (resctrl CAT)            │
│  为隔离核心划分 2MB 专属 L3 缓存区域          │
├─────────────────────────────────────────────┤
│  第三层：物理内存分区 (CMA)                    │
│  通过 CMA 分配 512MB 专属连续物理内存          │
├─────────────────────────────────────────────┤
│  第二层：时间分区 (SCHED_DEADLINE)            │
│  实现 ARINC 653 风格时间窗口 (3ms/10ms)       │
├─────────────────────────────────────────────┤
│  第一层：CPU 核心隔离 (cgroups v2 cpuset)     │
│  将物理核心 10-11 从全局调度域中独立           │
└─────────────────────────────────────────────┘
```

| 层级 | 隔离对象 | 技术方案 | 解决的问题 |
| :--- | :--- | :--- | :--- |
| 第一层 | CPU 核心 | cgroups v2 `cpuset.cpus.partition=root` | 其他进程抢占目标核心 |
| 第二层 | 执行时间 | `SCHED_DEADLINE` (CBS 恒定带宽服务器) | 任务超时占用 CPU，保障周期确定性 |
| 第三层 | 物理内存 | CMA + `dma_alloc_coherent` + `mlockall` | 伙伴系统页面回收导致缺页抖动 |
| 第四层 | L3 缓存 | `resctrl` + AMD/Intel CAT 硬件 QoS | 共享缓存被其他核心踩踏导致 Cache Miss |

---

## 实验结果

在 `stress-ng --cpu 4 --cache 4 --vm 4` 三重背景干扰下，各隔离层叠加后的效果：

| 隔离配置 | Avg | WCET | Jitter | 相对基线抖动降幅 |
| :--- | :--- | :--- | :--- | :--- |
| 无隔离（基线） | 982 μs | 9941 μs | 8959 μs | — |
| CPU 核心隔离 | 522 μs | 761 μs | 239 μs | 97.3% |
| + CMA 内存分区 | 467 μs | 637 μs | 170 μs | 98.1% |
| **+ L3 缓存分区（全开）** | **470 μs** | **533 μs** | **63 μs** | **99.3%** |

---

## 仓库结构

```
.
├── README.md
├── 分区.md                         # 完整操作日志（第一步至第十步）
├── images/                         # 实验截图与 KernelShark 轨迹图
├── docs/                           # 按主题拆分的独立文档
│   ├── 01-靶标程序与基准.md         # 靶标程序编写与基线数据采集
│   ├── 02-CPU与时间分区.md          # cgroups 核心隔离 + SCHED_DEADLINE 时间分区
│   ├── 03-CMA内存分区.md            # CMA 连续物理内存隔离
│   ├── 04-L3缓存分区.md             # resctrl CAT 硬件级缓存分区
│   ├── 05-代码整合模板.md           # 统一源代码模板（编译期功能开关）
│   ├── 06-可视化验证方法.md          # trace-cmd + KernelShark 调度追踪方法
│   └── 07-脚本使用说明.md            # 自动化脚本参数修改指南
├── src/                            # 源代码
│   ├── avionics_target.c           # 基础靶标程序（矩阵乘法 + 计时）
│   ├── avionics_sche.c             # SCHED_DEADLINE 时间分区版本
│   ├── avionics_mem.c              # CMA 内存分区版本
│   ├── avionics_unified.c          # 统一模板（通过 #define 切换功能）
│   └── cma_avionics/
│       ├── avionics_cma.c          # CMA 内核驱动模块
│       └── Makefile
└── scripts/                        # 自动化配置脚本
    ├── setup_core.sh               # 一键配置 cgroups v2 核心隔离区
    ├── setup_memory.sh             # 一键编译并加载 CMA 内核驱动
    ├── setup_cache.sh              # 一键配置 resctrl L3 缓存分区
    └── run_stress.sh               # 封装 stress-ng 的系统压力测试
```

---

## 阅读指引

| 需求 | 对应文档 |
| :--- | :--- |
| 了解完整操作流程与记录 | [分区.md](分区.md)（实验日志，约 1500 行） |
| 构建航电靶标程序并获取基准数据 | [docs/01-靶标程序与基准](docs/01-靶标程序与基准.md) |
| 配置 CPU 核心隔离与 ARINC 653 时间分区 | [docs/02-CPU与时间分区](docs/02-CPU与时间分区.md) |
| 配置 CMA 连续物理内存隔离 | [docs/03-CMA内存分区](docs/03-CMA内存分区.md) |
| 配置 L3 缓存硬件分区 | [docs/04-L3缓存分区](docs/04-L3缓存分区.md) |
| 使用统一模板替代多版本代码 | [docs/05-代码整合模板](docs/05-代码整合模板.md) |
| 学习 trace-cmd + KernelShark 调度可视化方法 | [docs/06-可视化验证方法](docs/06-可视化验证方法.md) |
| 修改脚本参数以适配不同硬件 | [docs/07-脚本使用说明](docs/07-脚本使用说明.md) |

---

## 硬件与软件依赖

**硬件要求：**
- AMD Ryzen (Zen 2+) 或 Intel 处理器，需支持 CAT/QoS 扩展以启用 L3 缓存分区
- 若不使用缓存分区，任意多核 x86-64 处理器均可运行前三层隔离
- 内存建议 8GB 以上（CMA 需预留约 520MB）

**软件依赖：**

```bash
# 压力测试与调度追踪工具
sudo apt install stress-ng trace-cmd kernelshark

# 内核模块编译（CMA 驱动）
sudo apt install linux-headers-$(uname -r) build-essential
```
