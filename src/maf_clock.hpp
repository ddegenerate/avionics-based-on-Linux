#pragma once
// ============================================================================
// maf_clock.hpp — MAF 主时间框架时钟
// ============================================================================
//
// MafClock 封装了整个 MAF (Major Frame) 时间系统:
//   - 基准时间 (maf_epoch_us): 所有线程共享的绝对时间基准
//   - 时间查询: offset_us(), in_slot(), us_until_slot_end()
//   - 精确睡眠: sleep_until_next_slot(), align_to_next_boundary()
//
// 所有时间计算以微秒为单位。睡眠使用 TIMER_ABSTIME,
// 内核 hrtimer 子系统以纳秒精度触发, 消除相对睡眠的相位漂移。
//
// 线程安全: MafClock 的所有 const 方法可被多线程并发调用。
//   内部只读 PartitionTable + CLOCK_MONOTONIC (系统级, 天然线程安全)。

#include "partition.hpp"
#include <cstdint>
#include <ctime>

class MafClock {
public:
    explicit MafClock(const PartitionTable& table);

    // --- 纪元初始化 ---
    // 将 m_epoch_us 对齐到未来 2 个 MAF 周期边界,
    // 确保所有工作线程创建完毕时纪元尚未到达
    void init_epoch();

    // --- 时间查询 (全部 const, 多线程安全) ---

    // 当前时间距离 MAF 纪元的偏移 (微秒)
    uint64_t offset_us() const;

    // 当前时刻是否在指定时间片内
    bool in_slot(int slot) const;

    // 距离当前时间片结束还有多少微秒 (不在任何时间片内返回 0)
    uint64_t us_until_slot_end() const;

    // --- 睡眠操作 ---

    // 绝对时间睡到目标时间片的下一次出现
    // - 当前在时间片之外 → 睡到本周期该时间片起始
    // - 当前在时间片之内 → 睡到下个 MAF 周期 (消除忙等)
    void sleep_until_next_slot(int target_slot);

    // 睡到下一个 MAF 边界 (用于线程启动时的呼吸灯同步)
    void align_to_next_boundary();

    // --- 访问器 ---
    uint64_t epoch_us()  const { return m_epoch_us; }
    uint64_t total_us()  const { return m_table.total_duration_us(); }
    int      total_ms()  const { return m_table.total_duration_ms(); }
    const PartitionTable& table() const { return m_table; }

private:
    uint64_t m_epoch_us = 0;
    const PartitionTable& m_table;
};
