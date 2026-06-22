#pragma once
// ============================================================================
// worker_thread.hpp — 分区工作线程 (v7: EWMA + 任务序列 + 弹性任务)
// ============================================================================
//
// WorkerThread 封装了一个分区核心上的完整执行流程:
//   ① 内存分配 + 数据随机化 + 矩阵预热 (SCHED_OTHER 下)
//   ② MAF 边界对齐 (呼吸灯同步)
//   ③ 设置 SCHED_FIFO(99) + 精确单核绑定
//   ④ 主循环: sleep_until_next_slot → 任务序列 → 弹性任务 → 循环
//
// v7 新增:
//   - EWMA + 2σ 自适应余量 (替代 last × 1.2)
//   - 任务序列支持 (TaskConfig 列表, 空则回退到默认矩阵乘法)
//   - 弹性任务层 (利用余量执行低优先级任务)
//   - 可配置余量策略枚举
//
// 并行处理模式:
//   静态任务划分: 任务在 slot 开始前已分配到各核心 (编译/配置期确定)
//   核心本地队列: 每个核心独立消费自己的任务列表, 零跨核同步
//   Fork-Join: Slot 开始时各核心"同时"fork, slot 结束时自适应 join

#include "partition.hpp"
#include "maf_clock.hpp"
#include <pthread.h>
#include <atomic>
#include <cstdint>
#include <vector>
#include <cmath>

class WorkerThread {
public:
    // === 基础常量 ===
    static constexpr int    MATRIX_SIZE         = 50;
    static constexpr int    WARMUP_ROUNDS       = 10;
    static constexpr int    MATRIX_ALIGN        = 64;

    static constexpr size_t MAT_RAW_BYTES = MATRIX_SIZE * MATRIX_SIZE * sizeof(float);
    static constexpr size_t MAT_ALLOC_BYTES =
        ((MAT_RAW_BYTES + MATRIX_ALIGN - 1) / MATRIX_ALIGN) * MATRIX_ALIGN;

    // === 余量策略 (v7 新增) ===
    enum class MarginStrategy {
        LAST_TIMES_12,  // 当前策略: last × 1.2 (向后兼容)
        EWMA_2SIGMA,    // ★ 推荐: EWMA + 2σ 统计安全余量
        FIXED,          // 固定值
    };

    // === EWMA 参数 (v7 新增) ===
    static constexpr double EWMA_ALPHA      = 0.3;   // 平滑系数 (0~1, 越小越平滑)
    static constexpr double SAFETY_K        = 2.0;   // σ 倍数 (2.0 ≈ 95% 置信)
    static constexpr uint64_t MIN_MARGIN_US = 500;   // 最小余量保护

    // === 自适应参数 (向后兼容) ===
    static constexpr uint64_t SLOT_MARGIN_INIT_US = 2000;
    static constexpr uint64_t MARGIN_FACTOR_NUM   = 6;    // × 6/5 = ×1.2
    static constexpr uint64_t MARGIN_FACTOR_DEN   = 5;

    // === 统计 ===
    struct Stats {
        int partition_id;
        int core_id;
        int physical_core;
        std::atomic<uint64_t> max_time_us{0};
        std::atomic<uint64_t> total_time_us{0};
        std::atomic<uint64_t> iterations{0};
        std::atomic<uint64_t> slot_overruns{0};
        std::atomic<uint64_t> voluntary_yields{0};
        // v7 新增统计
        std::atomic<uint64_t> critical_completed{0};   // 完成的关键任务数
        std::atomic<uint64_t> best_effort_completed{0}; // 完成的弹性任务数
        std::atomic<uint64_t> total_margin_us{0};       // 累计余量 (用于分析浪费)
    };

    // === 构造/析构 ===
    // partition:    所属分区配置 (含任务列表)
    // core_index:   在该分区内的核心索引 (0, 1, ...)
    // clock:        MAF 时钟引用 (所有线程共享同一实例)
    // global_run:   全局运行标志 (Ctrl+C 时置 false)
    // total_iter:   每个线程的总迭代次数 (传统模式) / MAF 周期数 (任务模式)
    // strategy:     余量计算策略 (默认 EWMA_2SIGMA)
    //
    // ★ 任务分配: 如果 partition.tasks 非空, 使用任务序列模式
    //             如果 partition.tasks 为空, 回退到传统的矩阵乘法循环

    WorkerThread(const PartitionConfig& partition, int core_index,
                 MafClock& clock, const std::atomic<bool>& global_run,
                 int total_iterations,
                 MarginStrategy strategy = MarginStrategy::EWMA_2SIGMA);

    ~WorkerThread();

    // 禁止拷贝
    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    // === 生命周期 ===
    void start();
    void join();
    bool is_done() const;

    // === 访问器 ===
    const Stats&    stats()         const { return m_stats; }
    int             physical_core() const { return m_physical_core; }

private:
    // pthread 入口
    static void* trampoline(void* arg);
    void run();

    // 初始化步骤
    void allocate_and_warmup();
    void setup_scheduling();

    // 计时工具
    static uint64_t elapsed_us(const struct timespec& t0,
                               const struct timespec& t1);

    // === v7 新增方法 ===

    // 计算自适应余量 (根据策略)
    uint64_t calculate_margin() const;

    // 更新 EWMA 统计
    void update_ewma(uint64_t elapsed_us);

    // 传统模式: 单负载循环 (向后兼容)
    void run_legacy_loop();

    // 任务模式: 任务序列 + 弹性任务
    void run_task_loop();

    // 执行单个任务并返回耗时
    uint64_t execute_task(const TaskConfig& task);

    // === 成员 ===
    PartitionConfig         m_cfg;
    int                     m_core_index;
    int                     m_physical_core;
    int                     m_slot;
    int                     m_total_iter;
    MafClock&               m_clock;
    const std::atomic<bool>& m_global_run;
    MarginStrategy          m_strategy;

    // 统计数据
    Stats           m_stats;
    pthread_t       m_thread = 0;
    bool            m_started = false;

    // === v7 新增: 任务列表 ===
    // 关键任务: 每个 slot 必须尝试执行
    std::vector<TaskConfig> m_critical_tasks;
    // 弹性任务: 仅在余量充足时执行 (利用浪费的时间)
    std::vector<TaskConfig> m_best_effort_tasks;

    // === v7 新增: EWMA 统计 ===
    double m_ewma_us     = 0.0;   // 指数加权移动平均
    double m_ewma_var_us = 0.0;   // 方差 EWMA
    uint64_t m_fixed_margin_us = SLOT_MARGIN_INIT_US;  // FIXED 策略的值

    // 工作负载数据 (对齐分配, 传统模式使用)
    using Matrix = float[MATRIX_SIZE][MATRIX_SIZE];
    Matrix* m_A = nullptr;
    Matrix* m_B = nullptr;
    Matrix* m_C = nullptr;
};