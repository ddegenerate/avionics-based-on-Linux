#pragma once
// ============================================================================
// task_assigner.hpp — 任务→核心分配策略
// ============================================================================
//
// C++ 并行处理核心抽象: 静态任务划分 (Static Task Partitioning)
//
// 在 slot 开始前 (配置期) 将任务分配到各核心:
//   - 分配结果在运行期不变 → 零跨核同步
//   - 各核心独立消费自己的任务子集
//   - 等价于 OpenMP 的 schedule(static) 策略
//
// 为什么不用动态调度 (work-stealing / shared queue)?
//   - 动态调度引入非确定性延迟 (锁竞争、缓存一致性协议开销)
//   - 航电分区要求 WCET 可预测 → 静态分配是唯一选择
//   - 静态分配的负载不均可通过离线 WCET 分析来优化

#include <vector>
#include <cstdint>
#include <algorithm>
#include <numeric>

namespace task_assign {

// 分配结果: assignment[核心索引] = {任务索引列表}
using Assignment = std::vector<std::vector<int>>;

// ============================================================
// 策略 1: 轮询分配 (Round-Robin)
// ============================================================
// 将任务按顺序轮流分配到各核心, 保证每个核心的任务数尽量均衡
//
// 示例: 5 个任务 + 2 个核心
//   → 核心 0: [0, 2, 4], 核心 1: [1, 3]
//
// 适用: 任务 WCET 相近、无需异构分配的场景
inline Assignment round_robin(int num_cores, int num_tasks) {
    Assignment result(num_cores);
    for (int t = 0; t < num_tasks; ++t)
        result[t % num_cores].push_back(t);
    return result;
}

// ============================================================
// 策略 2: WCET 感知贪心负载均衡
// ============================================================
// 按 WCET (budget_us) 降序排列, 每次将下一个任务分配给
// 当前总负载最小的核心 (贪心算法, 近似最优)
//
// 示例: 4 个任务 [4000, 3000, 2000, 1000] µs + 2 个核心
//   初始: 核心 0 = 0µs, 核心 1 = 0µs
//   task0(4000) → 核心 0 (0 vs 0, 选 0) → 核心0 = 4000
//   task1(3000) → 核心 1 (4000 vs 0, 选 1) → 核心1 = 3000
//   task2(2000) → 核心 1 (4000 vs 3000)    → 核心1 = 5000
//   task3(1000) → 核心 0 (4000 vs 5000)    → 核心0 = 5000
//   结果: 核心 0: [0, 3] (5000µs), 核心 1: [1, 2] (5000µs) ← 完美均衡
//
// 适用: 任务 WCET 差异大、需要负载均衡的场景
inline Assignment wcet_aware(int num_cores,
                              const std::vector<uint64_t>& task_budgets) {
    Assignment result(num_cores);
    std::vector<uint64_t> core_load(num_cores, 0);

    // 按 budget 降序建立任务索引
    std::vector<int> indices(task_budgets.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        // budget_us = 0 表示未知, 放在最后
        uint64_t ba = task_budgets[a];
        uint64_t bb = task_budgets[b];
        if (ba == 0) ba = 0;  // 0 的保持为 0 (最小)
        if (bb == 0) bb = 0;
        return ba > bb;
    });

    // 贪心分配
    for (int idx : indices) {
        // 找到当前负载最小的核心
        int best_core = 0;
        for (int c = 1; c < num_cores; ++c)
            if (core_load[c] < core_load[best_core])
                best_core = c;

        result[best_core].push_back(idx);
        if (task_budgets[idx] > 0)
            core_load[best_core] += task_budgets[idx];
    }

    return result;
}

// ============================================================
// 策略 3: WCET 贪心(仅关键任务) + 弹性任务广播到所有核心
// ============================================================
// 两阶段分配:
//   阶段1: 仅对关键任务做 WCET 贪心 → 保证计算负载均衡
//   阶段2: 所有弹性任务复制到每个核心 → 每个核心都有弹性填充能力
//
// 对比旧版 wcet_aware (不区分关键/弹性):
//   - 旧版: 弹性任务作为普通负载分配给单一核心 → 其他核心无弹性任务
//   - 新版: 弹性任务是公共资源, 每个核心一份 → 所有核心都能弹性接棒
//
// 示例: 3关键 + 1弹性, 2核心
//   关键任务: [nav(3500), ctrl(3000), sensor(2000)]
//   弹性任务: [self_test(500)]
//   阶段1 → 核心0: [nav], 核心1: [ctrl, sensor]  (负载 3500 vs 5000)
//   阶段2 → 核心0: [nav, self_test], 核心1: [ctrl, sensor, self_test]
//   结果: 两个核心都有 self_test 弹性填充
//
// 适用: 并行分区模式 (PartitionMode::PARALLEL + cpu_count > 1)
inline Assignment wcet_aware_2phase(int num_cores,
                                     const std::vector<uint64_t>& task_budgets,
                                     const std::vector<bool>& is_critical) {
    Assignment result(num_cores);
    std::vector<uint64_t> core_load(num_cores, 0);

    // ---- 阶段1: 仅关键任务做 WCET 贪心 ----
    std::vector<int> critical_idx;
    for (int i = 0; i < (int)task_budgets.size(); ++i)
        if (is_critical[i])
            critical_idx.push_back(i);

    // 按 budget 降序
    std::sort(critical_idx.begin(), critical_idx.end(), [&](int a, int b) {
        uint64_t ba = task_budgets[a];
        uint64_t bb = task_budgets[b];
        if (ba == 0) ba = 0;
        if (bb == 0) bb = 0;
        return ba > bb;
    });

    // 贪心分配关键任务
    for (int idx : critical_idx) {
        int best_core = 0;
        for (int c = 1; c < num_cores; ++c)
            if (core_load[c] < core_load[best_core])
                best_core = c;

        result[best_core].push_back(idx);
        if (task_budgets[idx] > 0)
            core_load[best_core] += task_budgets[idx];
    }

    // ---- 阶段2: 弹性任务广播到所有核心 ----
    for (int i = 0; i < (int)task_budgets.size(); ++i)
        if (!is_critical[i])
            for (int c = 0; c < num_cores; ++c)
                result[c].push_back(i);

    return result;
}

// ============================================================
// 策略 4: 固定映射
// ============================================================
// 直接使用外部指定的映射关系 (如从 YAML 配置文件读取)
//
// 适用: 需要精确控制哪个任务在哪个核心上运行的场景
inline Assignment fixed(const std::vector<std::vector<int>>& mapping) {
    return mapping;
}

} // namespace task_assign
