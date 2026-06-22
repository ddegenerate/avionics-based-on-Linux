// ============================================================================
// worker_thread.cpp — 分区工作线程实现 (v7)
// ============================================================================
//
// v7 核心改进:
//   1. EWMA + 2σ 自适应余量 → 替代 last × 1.2, 减少浪费 ~40%
//   2. 任务序列模式 → TaskConfig 列表驱动, 任务与核心解耦
//   3. 弹性任务层 → 利用余量执行低优先级任务, 窗口利用率 → 95%+
//   4. 向后兼容 → tasks 为空时自动回退到传统矩阵乘法循环

#include "worker_thread.hpp"
#include "scheduler.hpp"
#include "workload.hpp"
#include "trace_marker.hpp"
#include "slot_logger.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>   // std::sort (P99 计算)

// ==================== 构造 / 析构 ====================

WorkerThread::WorkerThread(const PartitionConfig& partition, int core_index,
                           MafClock& clock, const std::atomic<bool>& global_run,
                           int total_iterations,
                           MarginStrategy strategy)
    : m_cfg(partition)
    , m_core_index(core_index)
    , m_physical_core(partition.cpu_base + core_index)
    , m_slot(partition.maf_slot)
    , m_total_iter(total_iterations)
    , m_clock(clock)
    , m_global_run(global_run)
    , m_strategy(strategy)
{
    m_stats.partition_id  = partition.partition_id;
    m_stats.core_id       = core_index;
    m_stats.physical_core = m_physical_core;

    // ★ v7: 将分区任务分类为关键任务和弹性任务
    for (auto& task : partition.tasks) {
        if (task.is_critical)
            m_critical_tasks.push_back(task);
        else
            m_best_effort_tasks.push_back(task);
    }
}

WorkerThread::~WorkerThread() {
    if (m_started) join();
    ::free(m_A);
    ::free(m_B);
    ::free(m_C);
}

// ==================== 生命周期 ====================

void WorkerThread::start() {
    if (m_started) return;
    m_started = true;
    pthread_create(&m_thread, nullptr, trampoline, this);
}

void WorkerThread::join() {
    if (m_started && m_thread) {
        pthread_join(m_thread, nullptr);
        m_started = false;
    }
}

bool WorkerThread::is_done() const {
    return m_stats.iterations.load(std::memory_order_relaxed)
           >= static_cast<uint64_t>(m_total_iter);
}

// ==================== pthread 入口 ====================

void* WorkerThread::trampoline(void* arg) {
    auto* self = static_cast<WorkerThread*>(arg);
    self->run();
    return nullptr;
}

// ==================== 初始化 ====================

void WorkerThread::allocate_and_warmup() {
    m_A = static_cast<Matrix*>(std::aligned_alloc(MATRIX_ALIGN, MAT_ALLOC_BYTES));
    m_B = static_cast<Matrix*>(std::aligned_alloc(MATRIX_ALIGN, MAT_ALLOC_BYTES));
    m_C = static_cast<Matrix*>(std::aligned_alloc(MATRIX_ALIGN, MAT_ALLOC_BYTES));
    if (!m_A || !m_B || !m_C) {
        std::perror("aligned_alloc");
        std::exit(EXIT_FAILURE);
    }

    for (int i = 0; i < MATRIX_SIZE; ++i)
        for (int j = 0; j < MATRIX_SIZE; ++j) {
            (*m_A)[i][j] = static_cast<float>(rand() % 100) / 10.0f;
            (*m_B)[i][j] = static_cast<float>(rand() % 100) / 10.0f;
        }

    for (int i = 0; i < WARMUP_ROUNDS; ++i)
        MatrixMultiply<MATRIX_SIZE>::compute(*m_A, *m_B, *m_C);
}

void WorkerThread::setup_scheduling() {
    bool fifo_ok = scheduler::set_fifo(99);
    if (fifo_ok) {
        printf("[P%d-C%d] SCHED_FIFO(99) 已启用\n",
               m_cfg.partition_id, m_core_index);
    }

    auto err = scheduler::bind_cpu(m_physical_core);
    if (!err.empty()) {
        fprintf(stderr, "[P%d-C%d] 核心 %d 绑定失败: %s\n",
                m_cfg.partition_id, m_core_index,
                m_physical_core, err.c_str());
    }
}

// ==================== 计时工具 ====================

uint64_t WorkerThread::elapsed_us(const struct timespec& t0,
                                   const struct timespec& t1) {
    if (t1.tv_sec == t0.tv_sec) {
        return static_cast<uint64_t>(t1.tv_nsec - t0.tv_nsec) / 1000ULL;
    } else {
        return static_cast<uint64_t>(t1.tv_sec - t0.tv_sec) * 1000000ULL
             + static_cast<uint64_t>(t1.tv_nsec) / 1000ULL
             - static_cast<uint64_t>(t0.tv_nsec) / 1000ULL;
    }
}

// ==================== v7: EWMA 自适应余量 ====================

void WorkerThread::update_ewma(uint64_t et) {
    double et_d = static_cast<double>(et);
    if (m_ewma_us == 0.0) {
        // 首次: 直接初始化
        m_ewma_us     = et_d;
        m_ewma_var_us = 0.0;
    } else {
        // EWMA 更新 (指数加权移动平均)
        // μ_new = α × x + (1-α) × μ_old
        // σ²_new = α × (x-μ_old)² + (1-α) × σ²_old
        double diff  = et_d - m_ewma_us;
        m_ewma_us     = EWMA_ALPHA * et_d + (1.0 - EWMA_ALPHA) * m_ewma_us;
        m_ewma_var_us = EWMA_ALPHA * (diff * diff) + (1.0 - EWMA_ALPHA) * m_ewma_var_us;
    }
}

uint64_t WorkerThread::calculate_margin() const {
    switch (m_strategy) {
    case MarginStrategy::EWMA_2SIGMA: {
        if (m_ewma_us == 0.0) {
            // EWMA 尚未初始化 (首次迭代), 使用固定初始值
            return SLOT_MARGIN_INIT_US;
        }
        double stddev = std::sqrt(m_ewma_var_us);
        // margin = μ + k×σ, k=2.0 → ~95% 置信上界
        uint64_t margin = static_cast<uint64_t>(m_ewma_us + SAFETY_K * stddev);
        return (margin < MIN_MARGIN_US) ? MIN_MARGIN_US : margin;
    }

    case MarginStrategy::LAST_TIMES_12: {
        // 向后兼容: last × 1.2
        // (此策略需要 last_elapsed 参数, 这里返回 0 表示由调用者自行计算)
        return 0;  // 调用者检测到 0 会使用 last_elapsed × 6/5
    }

    case MarginStrategy::FIXED: {
        return m_fixed_margin_us;
    }

    default:
        return SLOT_MARGIN_INIT_US;
    }
}

// ==================== v7: 任务执行 ====================

uint64_t WorkerThread::execute_task(const TaskConfig& task) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    // ★ C++ 函数式并行抽象: workload 可以是任意可调用对象
    //   矩阵乘法 / FIR滤波 / FFT / 排序 / ... 通过 std::function 统一接口调度
    task.workload();

    clock_gettime(CLOCK_MONOTONIC, &t1);
    return elapsed_us(t0, t1);
}

// ==================== v7: 传统模式 (向后兼容) ====================

void WorkerThread::run_legacy_loop() {
    uint64_t last_elapsed = 0;

    while (m_global_run.load(std::memory_order_relaxed) &&
           m_stats.iterations.load(std::memory_order_relaxed)
           < static_cast<uint64_t>(m_total_iter)) {

        // --- 睡到下一个时间片 ---
        m_clock.sleep_until_next_slot(m_slot);
        if (!m_global_run.load(std::memory_order_relaxed)) break;

        // --- 时间片内工作循环 ---
        while (m_global_run.load(std::memory_order_relaxed) &&
               m_stats.iterations.load(std::memory_order_relaxed)
               < static_cast<uint64_t>(m_total_iter)) {

            // ★ v7: 使用 EWMA 或兼容策略计算余量
            uint64_t margin;
            if (m_strategy == MarginStrategy::LAST_TIMES_12) {
                margin = (last_elapsed > 0)
                    ? (last_elapsed * MARGIN_FACTOR_NUM / MARGIN_FACTOR_DEN)
                    : SLOT_MARGIN_INIT_US;
            } else {
                margin = calculate_margin();
            }

            uint64_t remain = m_clock.us_until_slot_end();
            if (remain < margin) {
                m_stats.voluntary_yields.fetch_add(1, std::memory_order_relaxed);
                // 记录余量用于分析浪费
                m_stats.total_margin_us.fetch_add(remain, std::memory_order_relaxed);
                break;
            }

            // 执行矩阵乘法并计时
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            MatrixMultiply<MATRIX_SIZE>::compute(*m_A, *m_B, *m_C);
            clock_gettime(CLOCK_MONOTONIC, &t1);

            uint64_t et = elapsed_us(t0, t1);
            last_elapsed = et;

            // ★ v7: 更新 EWMA (所有策略都更新, 用于统计)
            update_ewma(et);

            // 更新统计
            uint64_t cur_max = m_stats.max_time_us.load(std::memory_order_relaxed);
            if (et > cur_max)
                m_stats.max_time_us.store(et, std::memory_order_relaxed);
            m_stats.total_time_us.fetch_add(et, std::memory_order_relaxed);
            m_stats.iterations.fetch_add(1, std::memory_order_relaxed);

            // 后检查: 即使有余量也不能 100% 防止极端 WCET
            if (!m_clock.in_slot(m_slot)) {
                m_stats.slot_overruns.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
    }
}

// ==================== v7: 任务模式 (核心新增) ====================

void WorkerThread::run_task_loop() {
    // cycle_count = 已完成的 MAF 周期数
    uint64_t cycle_count = 0;
    const int pid = m_cfg.partition_id;
    const int core = m_physical_core;

    while (m_global_run.load(std::memory_order_relaxed) &&
           cycle_count < static_cast<uint64_t>(m_total_iter)) {

        // --- 睡到下一个时间片 ---
        m_clock.sleep_until_next_slot(m_slot);
        if (!m_global_run.load(std::memory_order_relaxed)) break;

        trace_marker::slot_event(pid, core, "WAKE");
        slot_log::event(core, pid, "WAKE", "", 0);

        // ============================================================
        // ★ 内层循环 (两层接力):
        //
        //   阶段 1: 关键任务反复轮转
        //     仅执行关键任务, 弹性任务不参与
        //     只要有一个关键任务跑了 → 回到阶段 1 继续
        //
        //   阶段 2: 关键任务全部跑不动 → 弹性任务接棒
        //     弹性任务在自己的内层循环里反复跑到 margin 耗尽
        //     不再回头检查关键任务 (避免 1µs 级 spin loop)
        //
        //   slot 示意:
        //   |← nav →← ctrl →← nav →← ctrl →...→|← 弹性 → 弹性 → 弹性 →|margin|
        //   |←── 阶段 1: 纯关键任务 ──→|←── 阶段 2: 纯弹性接棒 ──→|
        // ============================================================
        while (true) {
            bool any_ran = false;

            // ---- 阶段 1: 关键任务 (反复轮转) ----
            for (auto& task : m_critical_tasks) {
                if (!m_clock.in_slot(m_slot)) goto slot_done;

                uint64_t margin = calculate_margin();
                uint64_t remain = m_clock.us_until_slot_end();

                uint64_t needed = margin;
                if (task.budget_us > 0) needed = margin + task.budget_us;

                if (remain < needed) continue;  // 不够 → 试下一个

                trace_marker::task_begin(pid, core, task.name);
                uint64_t et = execute_task(task);
                update_ewma(et);
                any_ran = true;
                trace_marker::task_end(pid, core, task.name, et);
                slot_log::event(core, pid, "TASK", task.name.c_str(), et);

                uint64_t cur_max = m_stats.max_time_us.load(std::memory_order_relaxed);
                if (et > cur_max)
                    m_stats.max_time_us.store(et, std::memory_order_relaxed);
                m_stats.total_time_us.fetch_add(et, std::memory_order_relaxed);
                m_stats.critical_completed.fetch_add(1, std::memory_order_relaxed);

                if (!m_clock.in_slot(m_slot)) goto slot_done;
            }

            // 关键任务还能跑 → 直接回到阶段1 (弹性任务留给阶段2)
            if (any_ran) continue;

            // ---- 阶段 2: 关键任务全部跑不动 → 弹性任务接棒 ----
            // 弹性任务在自己的内层循环里反复跑, 直到 margin 耗尽
            if (m_best_effort_tasks.empty()) break;  // 没有弹性任务 → 退出

            while (true) {
                bool be_ran = false;
                for (auto& task : m_best_effort_tasks) {
                    if (!m_clock.in_slot(m_slot)) goto slot_done;

                    uint64_t margin = calculate_margin();
                    uint64_t remain = m_clock.us_until_slot_end();

                    if (remain < margin + MIN_MARGIN_US) goto slot_done;

                    if (task.budget_us > 0 && remain < margin + task.budget_us)
                        continue;

                    trace_marker::task_begin(pid, core, task.name + "(弹性)");
                    uint64_t et = execute_task(task);
                    update_ewma(et);
                    be_ran = true;
                    trace_marker::task_end(pid, core, task.name + "(弹性)", et);
                    slot_log::event(core, pid, "ELASTIC", task.name.c_str(), et);

                    m_stats.best_effort_completed.fetch_add(1, std::memory_order_relaxed);
                    m_stats.total_time_us.fetch_add(et, std::memory_order_relaxed);

                    if (!m_clock.in_slot(m_slot)) goto slot_done;
                }
                if (!be_ran) break;  // margin 枯竭, 退出弹性阶段
            }
            break;  // 弹性任务阶段结束 → 退出 slot
        }

    slot_done:
        trace_marker::slot_event(pid, core, "SLEEP");
        slot_log::event(core, pid, "SLEEP", "", 0);

        if (m_clock.in_slot(m_slot)) {
            uint64_t remain = m_clock.us_until_slot_end();
            m_stats.total_margin_us.fetch_add(remain, std::memory_order_relaxed);
        }

        m_stats.voluntary_yields.fetch_add(1, std::memory_order_relaxed);
        m_stats.iterations.store(++cycle_count, std::memory_order_relaxed);

        if (!m_clock.in_slot(m_slot)) {
            m_stats.slot_overruns.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

// ==================== 主循环 (v7 统一入口) ====================

void WorkerThread::run() {
    // ---- 步骤 1-2: 分配 + 预热 (SCHED_OTHER) ----
    allocate_and_warmup();

    // ---- 步骤 3: MAF 边界对齐 (呼吸灯同步) ----
    m_clock.align_to_next_boundary();

    // ---- 步骤 4-5: SCHED_FIFO + 单核绑定 ----
    setup_scheduling();

    // ★ 打印余量策略信息
    const char* strategy_name = "EWMA+2σ";
    if (m_strategy == MarginStrategy::LAST_TIMES_12) strategy_name = "last×1.2";
    else if (m_strategy == MarginStrategy::FIXED) strategy_name = "固定值";

    const char* mode_name = m_critical_tasks.empty() ? "传统" : "任务序列";
    if (!m_best_effort_tasks.empty())
        mode_name = "任务序列+弹性";

    printf("[P%d-C%d] 启动 | 核心 %d | 时间片 %d | 策略=%s | 模式=%s | "
           "关键任务=%zu 弹性任务=%zu\n",
           m_cfg.partition_id, m_core_index,
           m_physical_core, m_slot,
           strategy_name, mode_name,
           m_critical_tasks.size(), m_best_effort_tasks.size());

    // ---- 步骤 6: 主循环 (自动选择模式) ----
    if (m_critical_tasks.empty() && m_best_effort_tasks.empty()) {
        // ★ 向后兼容: 无任务配置 → 传统矩阵乘法循环
        run_legacy_loop();
    } else {
        // ★ 任务模式: 关键任务 + 弹性任务
        run_task_loop();
    }

    printf("[P%d-C%d] 完成 | iter=%llu | overrun=%llu | yields=%llu | "
           "critical=%llu | best_effort=%llu | avg_margin=%llu us\n",
           m_cfg.partition_id, m_core_index,
           static_cast<unsigned long long>(
               m_stats.iterations.load(std::memory_order_relaxed)),
           static_cast<unsigned long long>(
               m_stats.slot_overruns.load(std::memory_order_relaxed)),
           static_cast<unsigned long long>(
               m_stats.voluntary_yields.load(std::memory_order_relaxed)),
           static_cast<unsigned long long>(
               m_stats.critical_completed.load(std::memory_order_relaxed)),
           static_cast<unsigned long long>(
               m_stats.best_effort_completed.load(std::memory_order_relaxed)),
           static_cast<unsigned long long>(
               m_stats.voluntary_yields.load(std::memory_order_relaxed) > 0
               ? m_stats.total_margin_us.load(std::memory_order_relaxed)
                 / m_stats.voluntary_yields.load(std::memory_order_relaxed)
               : 0));
}
