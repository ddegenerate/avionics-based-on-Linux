# CMA 连续物理内存分区

CPU 隔离解决了"谁在核心上跑"，但航电程序的数据还在系统全局内存里。其他进程大量申请内存时，伙伴系统的页面回收和迁移会把航电的热数据挤出 LLC，甚至触发换页。这一步用 CMA 划出一块专属连续物理内存。

---

## 1. 方案选择

尝试过 GRUB 的 `memmap=512M$0x110000000` 在 4GB 以上硬挖一块，但 Ryzen 5500U 上系统起不来——现代 UEFI 固件对 e820 内存映射有严格校验，AMD IOMMU 也做了保护，启动阶段硬改内存布局直接 kernel panic。

改用 Linux 内核的 **CMA（Contiguous Memory Allocator）**。CMA 在启动时把一段物理页标为可迁移区域，平时伙伴系统可以临时借给可移动页面用，DMA API 真正来申请时自动把占用页面挪走，还你一块连续物理内存。不跟 e820 硬碰硬，隔离效果和 `memmap` 一样。

---

## 2. 开启 CMA 预留

### 2.1 GRUB 配置

```bash
sudo nano /etc/default/grub
# 在原有参数后追加 cma=520M
# 示例: quiet splash nohz_full=10,11 rcu_nocbs=10,11 cma=520M
sudo update-grub
sudo reboot
```

> **一个坑：** 设 512M 时实际永远申请不到完整的 512M，池子有一部分被内核元数据占用。改成 520M 留余量才正常。

重启后验证：

```bash
sudo dmesg | grep -i cma
```

<img width="1919" height="164" alt="image" src="https://github.com/user-attachments/assets/71e8bae1-5ea0-4e90-8cfd-daf259cc969c" />

CMA 地址由内核启动时自动选，用户无需也无法指定。航电场景只需要"一块连续隔离的内存"，不关心具体物理地址。

---

## 3. CMA 内核驱动

驱动的作用是从 CMA 池子申请 512MB 连续物理内存，通过 `/dev/avionics_cma` 设备节点暴露给用户态 `mmap`。

完整代码见 `src/cma_avionics/avionics_cma.c`，关键流程：

1. 注册虚拟 `platform_device`，绑定 64 位 DMA 掩码
2. 调用 `dma_alloc_coherent()` 从 CMA 区域申请 512MB
3. 注册 `miscdevice`，实现 `cma_mmap()` 回调——内部调 `dma_mmap_coherent()` 自动处理缓存一致性

**Makefile：**

```makefile
obj-m += avionics_cma.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

编译并加载：

```bash
make
sudo insmod avionics_cma.ko
ls -l /dev/avionics_cma   # 确认设备节点生成
```

<img width="698" height="54" alt="image" src="https://github.com/user-attachments/assets/5084b131-2f90-43cb-84a5-9218ce24deaa" />

---

## 4. 用户态接管 CMA 内存

完整代码见 `src/avionics_mem.c`。相比基础版 `avionics_sensor`，改动就两处：

1. **内存来源：** 不再用栈/堆，改为 `mmap("/dev/avionics_cma")`
2. **锁定：** `mlockall(MCL_CURRENT | MCL_FUTURE)` 防止被换出到 Swap

另外用 `memset` 预热前 6MB，让 MMU 提前建好大页映射，消除首次访问的缺页开销。

编译：

```bash
gcc -O0 avionics_mem.c -o avionics_mem
```

---

## 5. 架构边界

| 能力 | 状态 | 说明 |
| :--- | :--- | :--- |
| 防止其他进程占用这块内存 | 成功 | `dma_alloc_coherent` 后页面从伙伴系统移出 |
| 保证数据落在连续物理地址上 | 成功 | CMA + DMA API 天然保证物理连续性 |
| x86 平台内存访问性能 | 无退化 | x86 是缓存一致架构，返回的是普通 cached 内存 |
| 防止自身野指针越界 | 不支持 | 需要 MMU/MPU 硬件级保护，用户态做不了 |
| 指定精确物理起始地址 | 不支持 | CMA 地址由内核自动选 |

---

## 6. 效果验证

### 6.1 静态验证

```bash
sudo cat /sys/kernel/debug/cma/reserved/count   # 总容量（页数）
sudo cat /sys/kernel/debug/cma/reserved/used    # 已分配页数
```

换算：`133120 页 * 4KB = 520 MB`，与 GRUB 设置的 `cma=520M` 一致。加载驱动后已分配约 513.7MB，差的 6MB 是内核元数据和 DMA 2MB 对齐开销。

<img width="919" height="164" alt="image" src="https://github.com/user-attachments/assets/1d6e0114-106e-46f3-90ca-cff71a8fd091" />

### 6.2 缺页事件对比

用 `trace-cmd record -e exceptions:page_fault_user` 录制缺页事件：

- **CMA 版本：** 只在 `memset` 预热阶段有缺页，进入矩阵运算后零缺页

<img width="1923" height="780" alt="image" src="https://github.com/user-attachments/assets/baf60b27-560e-41f1-9e6b-831f30a409f2" />

- **普通版本：** 首次访问每个虚拟页都会缺页，内存压力下页面可能被换出导致更多缺页

<img width="1923" height="780" alt="image" src="https://github.com/user-attachments/assets/87f0f2c1-5d41-4c6f-ab4b-ef2b5ab532dc" />

### 6.3 抗干扰测试

终端 A（隔离区）运行测试程序，终端 B 制造内存压力：

```bash
stress-ng --vm 4 --vm-bytes 2G --timeout 60s
```

先测无压力的 CMA 基准：

<img width="906" height="293" alt="image" src="https://github.com/user-attachments/assets/c9497bb4-b907-496e-bafe-b3f2bb9c4eba" />

再在压力下分别测 CMA 版本和普通版本：

<img width="1731" height="626" alt="image" src="https://github.com/user-attachments/assets/b3aa6d4f-6bb1-42c1-930a-dcfa3cd43be6" />

| 测试场景 | Avg | Jitter |
| :--- | :--- | :--- |
| **CMA 内存（无压力）** | 455 us | 146 us |
| **CMA 内存（有压力）** | 467 us | 170 us |
| **普通内存（有压力）** | 471 us | 571 us |

普通内存在压力下 Jitter 是 CMA 的三倍多。虽然 Avg 差距不大，但 Jitter 才是实时系统最关心的指标。CMA 页面在 `dma_alloc_coherent` 以后就钉死了，加上 `mlockall`，不管外面怎么折腾都纹丝不动。
