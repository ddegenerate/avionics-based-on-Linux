// ============================================================================
// maf_clock.cpp — MAF 时钟实现
// ============================================================================

#include "maf_clock.hpp"
#include <cstdio>

// ==================== 构造 ====================

MafClock::MafClock(const PartitionTable& table)
    : m_table(table)
{}

// ==================== 纪元初始化 ====================

void MafClock::init_epoch() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t now_us = static_cast<uint64_t>(now.tv_sec) * 1000000ULL
                    + static_cast<uint64_t>(now.tv_nsec) / 1000ULL;

    uint64_t total = m_table.total_duration_us();
    // 对齐到未来 2 个 MAF 周期, 给线程创建留出充足时间
    m_epoch_us = ((now_us / total) + 2) * total;
}

// ==================== 时间查询 ====================

uint64_t MafClock::offset_us() const {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t us = static_cast<uint64_t>(now.tv_sec) * 1000000ULL
                + static_cast<uint64_t>(now.tv_nsec) / 1000ULL;
    return us - m_epoch_us;
}

bool MafClock::in_slot(int slot) const {
    uint64_t cycle = offset_us() % m_table.total_duration_us();
    uint64_t start = m_table.slot_start_us(slot);
    uint64_t dur   = m_table.slot_duration_us(slot);
    return (cycle >= start && cycle < start + dur);
}

uint64_t MafClock::us_until_slot_end() const {
    uint64_t cycle = offset_us() % m_table.total_duration_us();
    return m_table.us_until_slot_end_at(cycle);
}

// ==================== 睡眠操作 ====================

void MafClock::sleep_until_next_slot(int target_slot) {
    uint64_t slot_start = m_table.slot_start_us(target_slot);
    uint64_t slot_dur   = m_table.slot_duration_us(target_slot);
    uint64_t total      = m_table.total_duration_us();

    uint64_t now_abs  = m_epoch_us + offset_us();
    uint64_t candidate = m_epoch_us + slot_start;

    // 推进到第一个尚未结束的时间片
    while (candidate + slot_dur <= now_abs)
        candidate += total;

    // 当前正好在时间片内 → 跳到下个 MAF 周期 (消除忙等)
    if (candidate <= now_abs && now_abs < candidate + slot_dur)
        candidate += total;

    // TIMER_ABSTIME — 绝对时间, 无相位漂移
    struct timespec ts;
    ts.tv_sec  = static_cast<time_t>(candidate / 1000000ULL);
    ts.tv_nsec = static_cast<long>((candidate % 1000000ULL) * 1000UL);
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
}

void MafClock::align_to_next_boundary() {
    uint64_t now_abs = m_epoch_us + offset_us();
    uint64_t total   = m_table.total_duration_us();
    uint64_t cycles  = offset_us() / total;
    uint64_t next    = m_epoch_us + (cycles + 1) * total;

    if (now_abs >= next)
        next += total;

    struct timespec ts;
    ts.tv_sec  = static_cast<time_t>(next / 1000000ULL);
    ts.tv_nsec = static_cast<long>((next % 1000000ULL) * 1000UL);
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
}
