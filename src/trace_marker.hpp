#pragma once
// ============================================================================
// trace_marker.hpp — ftrace 用户态标记 (任务级可观测性)
// ============================================================================
//
// 用法:
//   trace_marker::begin("P1-sensor");   // 任务开始
//   ... 执行任务 ...
//   trace_marker::end("P1-sensor", et); // 任务结束, 带耗时
//
// 录制:
//   sudo trace-cmd record -e sched:sched_switch -e ftrace:print
//   sudo ./avionics_parallel_v7
//
// KernelShark 中:
//   - 勾选 "print" 事件 → 看到任务边界标记
//   - 在 Event List 中可以按 task name 过滤
//
// 原理:
//   写入 /sys/kernel/tracing/trace_marker → 成为 ftrace 的 print 事件
//   与 sched_switch 在同一时间轴上, 无需重新编译内核

#include <cstdio>
#include <cstdint>
#include <string>

namespace trace_marker {

// 延迟打开文件, 避免在还没挂载 tracefs 时失败
inline FILE* get_marker() {
    static FILE* fp = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        // tracefs 可能挂载在两个位置之一
        fp = std::fopen("/sys/kernel/tracing/trace_marker", "w");
        if (!fp)
            fp = std::fopen("/sys/kernel/debug/tracing/trace_marker", "w");
    }
    return fp;
}

// 写一条带时间戳的消息到 ftrace
inline void write(const std::string& msg) {
    FILE* fp = get_marker();
    if (fp) {
        std::fputs(msg.c_str(), fp);
        std::fputc('\n', fp);  // ftrace 以换行为记录边界
    }
}

// 任务开始
inline void task_begin(int pid, int core, const std::string& task_name) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "P%d-C%d ▶ %s", pid, core, task_name.c_str());
    write(buf);
}

// 任务结束 (带耗时)
inline void task_end(int pid, int core, const std::string& task_name,
                     uint64_t elapsed_us) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "P%d-C%d ■ %s (%llu us)", pid, core, task_name.c_str(),
        static_cast<unsigned long long>(elapsed_us));
    write(buf);
}

// slot 边界 (休眠/唤醒)
inline void slot_event(int pid, int core, const std::string& event) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "P%d-C%d ⏱ %s", pid, core, event.c_str());
    write(buf);
}

} // namespace trace_marker
