#pragma once
// ============================================================================
// slot_logger.hpp — 轻量 CSV 事件日志 (线程安全 + 即时刷盘)
// ============================================================================
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <atomic>
#include <mutex>

namespace slot_log {

// ★ inline: 保证所有 .cpp 共享同一份 (C++17), 不能用 static!
inline FILE*              g_fp = nullptr;
inline std::mutex         g_mutex;
inline std::atomic<bool>  g_base_set{false};
inline uint64_t           g_base_us = 0;

inline void init(const char* path) {
    g_fp = std::fopen(path, "w");
    if (g_fp) {
        std::setvbuf(g_fp, nullptr, _IONBF, 0);  // 无缓冲, 直写内核
        std::fprintf(g_fp, "timestamp_us,core,partition_id,event,task,duration_us\n");
    }
}

inline void event(int core, int pid, const char* type,
                   const char* task, uint64_t dur_us) {
    if (!g_fp) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t us = static_cast<uint64_t>(now.tv_sec) * 1000000ULL
                + static_cast<uint64_t>(now.tv_nsec) / 1000ULL;

    if (!g_base_set.load(std::memory_order_acquire)) {
        g_base_us = us;
        g_base_set.store(true, std::memory_order_release);
    }
    uint64_t rel = us - g_base_us;

    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(g_fp, "%llu,%d,%d,%s,%s,%llu\n",
                 static_cast<unsigned long long>(rel),
                 core, pid, type, task,
                 static_cast<unsigned long long>(dur_us));
}

inline void close() {
    if (g_fp) { std::fclose(g_fp); g_fp = nullptr; }
}

} // namespace slot_log
