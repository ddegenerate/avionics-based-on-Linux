#pragma once
// ============================================================================
// scheduler.hpp — 调度策略 & CPU 亲和性工具
// ============================================================================
//
// 提供设置 SCHED_FIFO + 单核绑定的便捷函数。
// SCHED_FIFO 替代 SCHED_DEADLINE 的原因:
//   SCHED_DEADLINE 要求 CPU 亲和性覆盖整个 Root Domain,
//   在 cgroup root 分区 {10,11} 下无法缩小到单核心 → EBUSY。
//   SCHED_FIFO 无此限制, 可精确绑定到指定物理核心。
//
// 三级降级链:
//   set_fifo(99) → 成功: RT 优先级, 不被 CFS 抢占
//                → 失败: 自动以 SCHED_OTHER (CFS) 运行, 仅用于调试

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
#include <sched.h>
#include <cstring>
#include <cerrno>
#include <string>
#include <cstdio>
//命名空间scheduler
namespace scheduler {

// 设置 SCHED_FIFO + 指定优先级
// 返回 true = 成功, false = 降级
inline bool set_fifo(int priority = 99) {
    struct sched_param sp{};
    sp.sched_priority = priority;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        fprintf(stderr, "  [警告] SCHED_FIFO(%d) 失败 (%s), 降级为 CFS, 时序将不可靠\n",
                priority, strerror(errno));
        return false;
    }
    return true;
}

// 绑定当前线程到指定物理核心
// 返回空字符串 = 成功, 非空 = 错误描述
inline std::string bind_cpu(int cpu) {
    cpu_set_t cpuset;  // CPU集合数据类型
    CPU_ZERO(&cpuset); // 先清零，都不能用核心
    CPU_SET(cpu, &cpuset); // 再把指定核心假如这个集合    假如有12个核心，那就有12位，对应把第几位设置为1
    int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);  //把当前去线程只搞到允许的核心上面。
    if (ret != 0)
        return std::string(strerror(ret));
    return {};
}

} // namespace scheduler
