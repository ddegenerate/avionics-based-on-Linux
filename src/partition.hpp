#pragma once
// ============================================================================
// partition.hpp — 分区配置 + 时间片查表 + 任务描述
// ============================================================================
//
// PartitionConfig: 描述一个分区的完整属性
//   - 核心范围 (cpu_base, cpu_count)
//   - MAF 时间片 (maf_slot, slot_duration_ms)
//   - 分区模式 (SERIAL / PARALLEL)
//   - 任务列表 (TaskConfig[])
//
// TaskConfig: 描述分区内的一个执行任务
//   - 可替换的计算负载 (std::function)
//   - CPU 预算 (用于 slot 内调度判断)
//
// PartitionTable: 管理全部分区配置, 提供时间片→时间参数的查表服务
//   所有查表函数返回微秒值, 与毫秒级配置入口解耦。
//
// 设计要点:
//   - 同一 maf_slot 可被多个分区共享 → 这些分区并行执行
//   - SERIAL 模式: 所有任务在一个核心上串行执行
//   - PARALLEL 模式: 任务按策略分配到多个核心并行执行

#include <cstdint>
#include <cstdio>
#include <vector>
#include <initializer_list>
#include <functional>
#include <string>

// ==================== 分区运行模式 ====================
enum class PartitionMode {
    PARALLEL,  // 多核并行: 各核心独立运行分配给自己的任务子集
    SERIAL,    // 单核串行: 所有任务在一个核心上按优先级顺序执行
};

// ==================== 任务配置 ====================
struct TaskConfig {
    int         task_id;           // 任务 ID
    std::string name;              // 可读名称 (日志/统计用)
    std::function<void()> workload; // ★ 可替换的计算负载 (C++ 函数式并行抽象)
    uint64_t    budget_us = 0;     // 单次执行的 CPU 预算 (µs), 0=不限
    bool        is_critical = true; // true=关键任务(必须跑完), false=弹性任务(利用余量)
};

// ==================== 分区配置 ====================
struct PartitionConfig {
    int partition_id;       // 分区 ID
    int cpu_base;           // 起始核心号
    int cpu_count;          // 占用的核心数量 (SERIAL=1, PARALLEL>=1)
    int maf_slot;           // 在 MAF 中占据的时间片编号 (0, 1, 2...)
    int slot_duration_ms;   // 时间片时长 (毫秒)
    PartitionMode mode = PartitionMode::PARALLEL;  // 分区模式 (默认并行)
    std::vector<TaskConfig> tasks;  // 本分区的任务列表 (空=使用默认矩阵乘法负载)
};

// ==================== 分区表 (时间片查表) ====================
class PartitionTable {
public:
    PartitionTable() = default;

    // 构造函数
    PartitionTable(std::initializer_list<PartitionConfig> cfgs) {
        for (auto& c : cfgs) add(c);
    }

    // 向分区表中添加一个新的分区配置，同时维护 MAF 总时长
    void add(const PartitionConfig& cfg) {
        // ★ 一致性校验: 同一时间片被多个分区共享时, 时长必须一致
        bool slot_already_seen = false;
        for (auto& existing : m_partitions) {
            if (existing.maf_slot == cfg.maf_slot) {
                slot_already_seen = true;
                if (existing.slot_duration_ms != cfg.slot_duration_ms) {
                    std::fprintf(stderr,
                        "[PartitionTable] 警告: 分区 %d 和 %d 共享时间片 %d, "
                        "但时长不一致 (%dms vs %dms), 将使用先注册的值 %dms\n",
                        existing.partition_id, cfg.partition_id, cfg.maf_slot,
                        cfg.slot_duration_ms, existing.slot_duration_ms,
                        existing.slot_duration_ms);
                }
            }
        }
        m_partitions.push_back(cfg);
        // ★ 仅对新时间片累加 MAF 总时长
        if (!slot_already_seen) {
            m_total_ms += cfg.slot_duration_ms;
            m_total_us = static_cast<uint64_t>(m_total_ms) * 1000ULL;
        }
    }

    // --- 访问器 ---
    const std::vector<PartitionConfig>& partitions() const { return m_partitions; }
    size_t size()                  const { return m_partitions.size(); }
    int    total_duration_ms()     const { return m_total_ms; }
    uint64_t total_duration_us()   const { return m_total_us; }

    // 所有分区占用的总核心数
    int total_cores() const {
        int n = 0;
        for (auto& p : m_partitions) n += p.cpu_count;
        return n;
    }

    // --- 时间片查表 (全部返回微秒) ---

    // 给定时间片编号, 返回其时长 (微秒)
    uint64_t slot_duration_us(int slot) const {
        for (auto& p : m_partitions)
            if (p.maf_slot == slot)
                return static_cast<uint64_t>(p.slot_duration_ms) * 1000ULL;
        return 0;
    }

    // 给定时间片编号, 返回其在 MAF 周期内的起始偏移 (微秒)
    uint64_t slot_start_us(int slot) const {
        uint64_t start = 0;
        for (int s = 0; s < slot; ++s)
            start += slot_duration_us(s);
        return start;
    }

    // 判断给定时刻属于哪个时间片
    int find_slot_at(uint64_t cycle_us) const {
        uint64_t cursor = 0;
        for (int s = 0; s < 16; ++s) {
            uint64_t dur = slot_duration_us(s);
            if (dur == 0) continue;
            if (cycle_us >= cursor && cycle_us < cursor + dur)
                return s;
            cursor += dur;
        }
        return -1;
    }

    // 给定时刻距其所在时间片结束还有多少微秒
    uint64_t us_until_slot_end_at(uint64_t cycle_us) const {
        uint64_t cursor = 0;
        for (int s = 0; s < 16; ++s) {
            uint64_t dur = slot_duration_us(s);
            if (dur == 0) continue;
            if (cycle_us >= cursor && cycle_us < cursor + dur)
                return (cursor + dur - cycle_us);
            cursor += dur;
        }
        return 0;
    }

private:
    std::vector<PartitionConfig> m_partitions;
    int      m_total_ms = 0;
    uint64_t m_total_us = 0;
};
