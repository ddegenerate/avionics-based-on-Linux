# CPU 核心隔离与 ARINC 653 时间分区

CPU 层面的隔离分两层：先用 cgroups v2 把物理核心从调度器里独立出来（核心隔离），再在隔离核心上用 SCHED_DEADLINE 实现 ARINC 653 风格的时间窗口（时间分区）。

---

## 1. 识别 CPU 拓扑

6 核 12 线程，操作系统看到 12 个逻辑核。超线程（SMT）让两个逻辑核共享同一个物理核的微架构资源，必须把一整对兄弟核一起隔离。

```bash
cat /sys/devices/system/cpu/cpu11/topology/thread_siblings_list
# 结果: 10-11
```

逻辑核 10 和 11 共享同一个物理核，后续把这两个绑在一起作为航电专用分区。

---

## 2. 用 cgroups v2 构建独立调度域

> **为什么不用 `isolcpus`？** `isolcpus` 是 GRUB 内核参数，虽然能隔离核心，但与 `SCHED_DEADLINE` 的准入控制器冲突——后者需要在全局调度域中计算带宽，`isolcpus` 把核心从根调度域开除了，导致 `sched_setattr` 直接报 `Operation not permitted`。cgroups v2 的 `cpuset.cpus.partition = root` 同样能做到独立运行队列，且调度器仍能感知这些核心的存在。

### 2.1 GRUB 参数（替代 isolcpus）

```bash
sudo nano /etc/default/grub
# 追加: nohz_full=10,11 rcu_nocbs=10,11
# 完整示例: GRUB_CMDLINE_LINUX_DEFAULT="quiet splash nohz_full=10,11 rcu_nocbs=10,11"
sudo update-grub
sudo reboot
```

`nohz_full` 关闭定时器中断，`rcu_nocbs` 把 RCU 回调挪走，减少隔离核心上的内核后台噪声。

### 2.2 创建 cgroup 分区

```bash
cd /sys/fs/cgroup

# 1. 激活 cpuset 控制器
sudo bash -c 'echo "+cpuset" > cgroup.subtree_control'
```

`cgroup.subtree_control` 中出现 `cpuset` 即为激活成功：

![cpuset控制器激活](../images/02-cpuset激活.png)

```bash
# 2. 创建子 cgroup
sudo mkdir avionics_partition
cd avionics_partition
```

cgroups v2 中 `mkdir` 即自动生成子 cgroup，内核会在该目录内自动填充接口文件：

![cgroup目录自动生成](../images/02-cgroup目录内容.png)

```bash
# 3. 划拨 CPU 10 和 11
sudo bash -c 'echo "10-11" > cpuset.cpus'

# 4. 设为独立根调度域（核心从全局负载均衡中切出）
sudo bash -c 'echo "root" > cpuset.cpus.partition'
```

`cpuset.cpus.partition = root` 意味着核心 10/11 拥有专属运行队列和独立算力账本，不再参与全局负载均衡。状态由 `member` 跃迁为 `root`。

### 2.3 把终端放入分区

```bash
# 获取当前终端 PID
echo $$

# 写入 cgroup
sudo sh -c "echo <你的PID> > /sys/fs/cgroup/avionics_partition/cgroup.procs"

# 验证
cat /proc/self/cgroup          # 应输出 0::/avionics_partition
cat /proc/self/status | grep Cpus_allowed_list  # 应输出 10-11
```

终端及其所有子进程现在只能在核心 10 和 11 上运行。此时 SCHED_DEADLINE 可以在隔离核心上正常使用了：

![cgroups分区后SCHED_DEADLINE运行](../images/02-cgroups分区就绪.png)

---

## 3. SCHED_DEADLINE 时间分区

### 3.1 程序代码

glibc 没有封装 `SCHED_DEADLINE`，需要直接走 `syscall`。完整代码见 `src/avionics_sche.c`，核心部分：

```c
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/sched.h>

struct sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

int sched_setattr(pid_t pid, const struct sched_attr *attr, unsigned int flags) {
    return syscall(SYS_sched_setattr, pid, attr, flags);
}

// 在 main() 中：
struct sched_attr attr = {0};
attr.size = sizeof(attr);
attr.sched_policy = 6;  // SCHED_DEADLINE
attr.sched_runtime  = 3 * 1000000;   // 3ms 运行时间
attr.sched_deadline = 10 * 1000000;  // 10ms 截止期限
attr.sched_period   = 10 * 1000000;  // 10ms 周期

if (sched_setattr(0, &attr, 0) == -1) {
    perror("SCHED_DEADLINE 设置失败 (需 root 并在 cgroups 分区内)");
    exit(EXIT_FAILURE);
}
```

CBS（恒定带宽服务器）算法在每个周期内给任务 3ms CPU 预算，用完即挂起，7ms 后下个周期再唤醒。即使程序死循环也不会拖垮整个核心。

编译：

```bash
gcc -O0 avionics_sche.c -o avionics_sche
```

### 3.2 运行

```bash
# 无时间窗口（纯核心隔离）
sudo ./avionics_sensor

# 有时间窗口（核心隔离 + SCHED_DEADLINE）
sudo ./avionics_sche
```

---

## 4. 验证结果

用 `trace-cmd` + KernelShark 录制调度事件进行验证，方法详见 [06-可视化验证方法](06-可视化验证方法.md)。

### 4.1 核心隔离效果（无干扰）

先在不加负载的情况下验证核心隔离本身是否生效。

终端输出结果：

![无干扰-终端结果](../images/02-无干扰-终端结果.png)

左侧终端显示 Jitter 已降至 13 μs，初步说明核心隔离有效。

KernelShark 整体调度轨迹：

![无干扰-KernelShark整体](../images/02-无干扰-KernelShark整体.png)

上方非隔离核心（CPU 7-9）黑线密布、色块挤压，系统调度器频繁切换进程。下方隔离核心（CPU 10-11）几乎为空，仅有零星黑线。

选取隔离核心上一条黑线放大分析——这是终端 bash 进程在隔离区内的背景调度，关注点聚焦于测试进程 `avionics_sensor`：

![无干扰-黑线放大](../images/02-无干扰-黑线放大.png)

在 Search 窗口搜索 `avionics_sensor`，其生命周期内有两个关键事件：

**事件 1：程序启动：**

![无干扰-事件1启动](../images/02-无干扰-事件1启动.png)

**事件 2：程序结束：**

![无干扰-事件2结束](../images/02-无干扰-事件2结束.png)

| 事件 | CPU | 时间戳 (s) | 详情 |
| :--- | :--- | :--- | :--- |
| `sched_switch` | 10 | 5009.9582 | `sudo:8376 [120] D ==> swapper/10:0 [120]` |
| `sched_switch` | 10 | 5014.5678 | `avionics_sensor:8376 [120] Z ==> swapper/10:0 [120]` |

时间跨度: 5014.5678 - 5009.9582 = 4.6096s。终端打印的平均单次耗时 454 μs × 10000 次 = 4.54s，加上初始化开销，与 KernelShark 抓取的时间完全吻合。

在这 4.6 秒内，除了头尾两次 switch 外，`avionics_sensor` 没有任何被动切换记录。程序在 CPU 10 上一口气跑完，未被抢占。

### 4.2 核心隔离效果（有干扰）

外部施加 `stress-ng --cpu 10` 压力后再次验证。

终端输出结果：

![有干扰-终端结果](../images/02-有干扰-终端结果.png)

Jitter 依然保持极低水平，平均执行时间仅上升几十微秒。

KernelShark 整体调度轨迹：

![有干扰-KernelShark整体](../images/02-有干扰-KernelShark整体.png)

非隔离核心（CPU 6-9）已被压力负载完全填满，隔离核心（CPU 10-11）几乎不受波及。

搜索 `avionics_sensor`，同样只有头尾两个关键事件：

![有干扰-事件1启动](../images/02-有干扰-事件1启动.png)

![有干扰-事件2结束](../images/02-有干扰-事件2结束.png)

| 事件 | CPU | 时间戳 (s) | 详情 |
| :--- | :--- | :--- | :--- |
| `sched_switch` | 10 | 14562.397 | `sudo:15353 [120] D ==> swapper/10:0 [120]` |
| `sched_switch` | 10 | 14567.648 | `avionics_sensor:15353 [120] Z ==> swapper/10:0 [120]` |

时间跨度: 5.251s。522 μs × 10000 = 5.22s 纯计算时间，加上系统开销后与 KernelShark 数据吻合。高负载下在 CPU 10 上依然一跑到底。

核心隔离的对比汇总：

![核心隔离结果对比](../images/02-核心隔离结果.png)

| 运行核心 | Avg | WCET | Jitter |
| :--- | :--- | :--- | :--- |
| **核心 9（未隔离）** | 1430 us | 15975 us | 14545 us |
| **核心 10（已隔离）** | 561 us | 761 us | 200 us |
| **核心 11（已隔离）** | 526 us | 956 us | 430 us |

### 4.3 时间窗口测试

有 `stress-ng --cpu 10` 背景负载下，运行 `avionics_sche`（时间窗口设置为 Runtime=3ms, Period=10ms）。

终端输出结果：

![时间窗口-终端结果](../images/02-时间窗口-终端结果.png)

平均执行时间 2154 μs，最坏 8938 μs。开启了时间窗口后，程序运行 3ms 就被强制休眠 7ms，单次运行时长 + 休眠时长 + 调度开销导致最坏情况比不开窗口时高，符合预期。

KernelShark 远观整体调度：

![时间窗口-KernelShark远观](../images/02-时间窗口-KernelShark远观.png)

非隔离核心被压力填满，隔离核心（CPU 10-11）上的紫色条呈等长周期状出现。

放大观察紫色色块部分：

![时间窗口-KernelShark近观](../images/02-时间窗口-KernelShark近观.png)

核心 10 和核心 11 的紫色色块等长且周期出现，时间分区生效。

在 Search 窗口检索 `avionics_sche`，选取连续三个调度事件分析一个完整周期：

![时间窗口-事件1](../images/02-时间窗口-事件1.png)

![时间窗口-事件2](../images/02-时间窗口-事件2.png)

![时间窗口-事件3](../images/02-时间窗口-事件3.png)

| 事件 | CPU | 时间戳 (s) | 详情 |
| :--- | :--- | :--- | :--- |
| switch | 10 | 1627.5632 | idle → avionics_sche |
| switch | 10 | 1627.5663 | avionics_sche → idle |
| switch | 11 | 1627.5732 | idle → avionics_sche |

- 实际运行时间: 3.1 ms
- 强制休眠时间: 6.9 ms
- 总周期: 10.0 ms

与设置值一致，SCHED_DEADLINE 的时间窗口在硬件层面生效。
