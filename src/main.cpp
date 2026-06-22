// ============================================================================
// main.cpp — ARINC 653 并行分区 v7
// ============================================================================
//
// 编译:
//   g++ -std=c++17 -O2 -pthread \
//       main.cpp maf_clock.cpp worker_thread.cpp \
//       -o avionics_parallel_v7
// 运行:
//   sudo ./avionics_parallel_v7
//
// 依赖: C++17, pthread, Linux RT 调度, cgroups v2
/*# 终端 A: 启动录制 (比之前多 -e ftrace:print)
sudo trace-cmd record \
    -e sched:sched_switch \
    -e sched:sched_wakeup \
    -e sched:sched_wakeup_new \
    -e ftrace:print

# 终端 B: 运行
sudo ./avionics_parallel_v7

# 跑完后终端 A 按 Ctrl+C
kernelshark trace.dat
*/

#include "partition.hpp"
#include "maf_clock.hpp"
#include "worker_thread.hpp"
#include "task_assigner.hpp"
#include "workload.hpp"
#include "slot_logger.hpp"
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <vector>
#include <memory>
#include <unistd.h>

// ==================== 全局运行标志 ====================
static std::atomic<bool> g_running{true};

static void sig_handler(int) {
    g_running.store(false, std::memory_order_relaxed);
}

// ==================== 辅助工具 ====================

// 用随机数填充矩阵 (SAT solver 或 uniform random)
template <int R, int C>
void init_matrix(float (&m)[R][C]) {
    for (int i = 0; i < R; ++i)
        for (int j = 0; j < C; ++j)
            m[i][j] = static_cast<float>(rand() % 100) / 10.0f;
}

// 打印分区和任务一览
static void print_banner(const PartitionTable& table,
                          WorkerThread::MarginStrategy strategy) {
    const char* strat_name = "EWMA+2σ";
    if (strategy == WorkerThread::MarginStrategy::LAST_TIMES_12)
        strat_name = "last×1.2";
    else if (strategy == WorkerThread::MarginStrategy::FIXED)
        strat_name = "固定值";

    printf("============================================================\n");
    printf("  ARINC 653 并行分区 v7 (C++)\n");
    printf("============================================================\n");
    printf("分区数: %zu  总核心数: %d  MAF: %d ms\n",
           table.size(), table.total_cores(), table.total_duration_ms());
    printf("调度: SCHED_FIFO(99) + 精确单核绑定  余量策略: %s\n", strat_name);
    printf("------------------------------------------------------------\n");

    for (auto& p : table.partitions()) {
        const char* mode = (p.mode == PartitionMode::SERIAL) ? "串行" : "并行";
        printf("分区%d | 核%d-%d | 时间片%d | %dms | %s",
               p.partition_id, p.cpu_base,
               p.cpu_base + p.cpu_count - 1,
               p.maf_slot, p.slot_duration_ms, mode);
        if (p.cpu_count > 1 && p.mode == PartitionMode::PARALLEL)
            printf(" ★");
        printf("\n");
        if (p.tasks.empty()) {
            printf("       └ 传统模式: 矩阵乘法(50×50) × %d核\n", p.cpu_count);
        } else {
            printf("       └ 任务(%zu个):\n", p.tasks.size());
            for (auto& t : p.tasks)
                printf("          [%s] budget=%lluus %s\n",
                       t.name.c_str(),
                       static_cast<unsigned long long>(t.budget_us),
                       t.is_critical ? "关键" : "弹性");
        }
    }
    printf("============================================================\n\n");
}

// 打印最终统计
static void print_results(
        const std::vector<std::unique_ptr<WorkerThread>>& workers,
        uint64_t wall_us) {
    printf("\n==================== 结果 ====================\n");
    printf("墙钟耗时: %.3f s\n", wall_us / 1000000.0);

    int last_pid = -1;
    for (auto& w : workers) {
        auto& s = w->stats();
        if (s.partition_id != last_pid) {
            printf("--- 分区 %d ---\n", s.partition_id);
            last_pid = s.partition_id;
        }

        uint64_t iter   = s.iterations.load(std::memory_order_relaxed);
        uint64_t total  = s.total_time_us.load(std::memory_order_relaxed);
        uint64_t wcet   = s.max_time_us.load(std::memory_order_relaxed);
        uint64_t over   = s.slot_overruns.load(std::memory_order_relaxed);
        uint64_t yields = s.voluntary_yields.load(std::memory_order_relaxed);
        uint64_t crit   = s.critical_completed.load(std::memory_order_relaxed);
        uint64_t be     = s.best_effort_completed.load(std::memory_order_relaxed);
        uint64_t m_tot  = s.total_margin_us.load(std::memory_order_relaxed);

        uint64_t avg  = (iter > 0) ? total / iter : 0;
        uint64_t jit  = (wcet > avg) ? wcet - avg : 0;
        uint64_t amgn = (yields > 0) ? m_tot / yields : 0;

        printf("  Core %2d | iter:%6llu avg:%6lluus wcet:%6lluus jitter:%6lluus\n",
               s.physical_core,
               static_cast<unsigned long long>(iter),
               static_cast<unsigned long long>(avg),
               static_cast<unsigned long long>(wcet),
               static_cast<unsigned long long>(jit));
        printf("          | overrun:%llu yields:%llu avg_margin:%6lluus",
               static_cast<unsigned long long>(over),
               static_cast<unsigned long long>(yields),
               static_cast<unsigned long long>(amgn));
        if (crit > 0 || be > 0)
            printf(" critical:%llu best_effort:%llu",
                   static_cast<unsigned long long>(crit),
                   static_cast<unsigned long long>(be));
        printf("\n");
    }
    printf("================================================\n");
}

// ==================== 主函数 ====================

int main() {
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    slot_log::init("trace_log.csv");  // 启动 CSV 事件日志

    constexpr int TOTAL_CYCLES = 20000;      // MAF 周期总数（任务模式）/ 迭代次数（传统模式）
    constexpr WorkerThread::MarginStrategy MARGIN_STRATEGY =
        WorkerThread::MarginStrategy::EWMA_2SIGMA;

    // ########################################################################
    // #                                                                      #
    // #             配  置  区  （直接修改下面内容即可）                         #
    // #                                                                      #
    // ########################################################################

    // ---- 步骤 A: 声明各任务所需矩阵 (每个任务用自己的独立数据) ----
    srand(42);  // 固定种子保证每次运行数据一致

    // 传统模式无需声明矩阵，自动分配。
    // 任务模式：下面按需声明矩阵 + init_matrix 填充。
    //
    // 示例矩阵（如不需要可删掉）:
    // static float A1[40][40], B1[40][40], C1[40][40];
    // init_matrix(A1); init_matrix(B1);

    // 分区1的任务  三个关键类任务，一个日志型弹性任务
    static float P1_A1[40][40], P1_B1[40][40], P1_C1[40][40];  // 关键任务1
    static float P1_A2[50][50], P1_B2[50][50], P1_C2[50][50];  // 关键任务2
    static float P1_A3[55][55], P1_B3[55][55], P1_C3[55][55];  // 关键任务3
    static float P1_A4[10][10], P1_B4[10][10], P1_C4[10][10];  // 弹性任务

    init_matrix(P1_A1); init_matrix(P1_B1);
    init_matrix(P1_A2); init_matrix(P1_B2);
    init_matrix(P1_A3); init_matrix(P1_B3);
    init_matrix(P1_A4); init_matrix(P1_B4);

    // 分区2的任务 四个关键类任务，一个日志型弹性任务
    static float P2_A1[35][35], P2_B1[35][35], P2_C1[35][35];  // 关键任务1
    static float P2_A2[45][45], P2_B2[45][45], P2_C2[45][45];  // 关键任务2
    static float P2_A3[50][50], P2_B3[50][50], P2_C3[50][50];  // 关键任务3
    static float P2_A4[60][60], P2_B4[60][60], P2_C4[60][60];  // 关键任务4
    static float P2_A5[10][10], P2_B5[10][10], P2_C5[10][10];  // 弹性任务

    init_matrix(P2_A1); init_matrix(P2_B1);
    init_matrix(P2_A2); init_matrix(P2_B2);
    init_matrix(P2_A3); init_matrix(P2_B3);
    init_matrix(P2_A4); init_matrix(P2_B4);
    init_matrix(P2_A5); init_matrix(P2_B5);
    // ---- 步骤 B: 构建分区表 ----
    PartitionTable table;

    // ================================================================
    // 分区1: 并行, 核10-11, 时间片0, 10ms, 3关键任务 + 1弹性任务
    // ================================================================
    table.add
    (
        {
        /* partition_id   */ 1,
        /* cpu_base       */ 10,
        /* cpu_count      */ 2,
        /* maf_slot       */ 0,
        /* slot_duration_ms*/ 10,
        /* 模式选择 并行模式*/PartitionMode::PARALLEL,
            {
                {
                    1,                      // task_id
                    "P1-sensor",            // 名称
                    [&]{ MatrixMultiply<40>::compute(P1_A1, P1_B1, P1_C1); },
                    2000                    // budget_us
                },
                {
                    2,
                    "P1-control",
                    [&]{ MatrixMultiply<50>::compute(P1_A2, P1_B2, P1_C2); },
                    3000
                },
                {
                    3,
                    "P1-navigation",
                    [&]{ MatrixMultiply<55>::compute(P1_A3, P1_B3, P1_C3); },
                    3500
                },
                {
                    4,
                    "P1-self_test",
                    [&]{ MatrixMultiply<10>::compute(P1_A4, P1_B4, P1_C4); },
                    500,                    // budget_us
                    false                   // is_critical = false → 弹性
                },
            }
        }
    );

    // ================================================================
    // 分区2: 并行, 核10-11, 时间片1, 15ms, 4关键任务 + 1弹性任务
    // ================================================================
    table.add
    (
        {
            2,                       // 分区id
            10,                      // 核心分区起点
            2,                       // 核心分几个
            1,                       // 时间片编号
            15,                      // 时间片持续时长
            PartitionMode::PARALLEL, // 并行模式
            {
                {
                    5,                                                        //任务id号
                    "P2-fuel_mgmt",                                           //任务名称
                    [&]{ MatrixMultiply<35>::compute(P2_A1, P2_B1, P2_C1); }, //这里是调用矩阵负载
                    1500                                                      //任务分多少微秒运行
                },
                {
                    6,
                    "P2-engine_ctrl",
                    [&]{ MatrixMultiply<45>::compute(P2_A2, P2_B2, P2_C2); },
                    2500
                },
                {
                    7,
                    "P2-flight_ctrl",
                    [&]{ MatrixMultiply<50>::compute(P2_A3, P2_B3, P2_C3); },
                    3000
                },
                {
                    8,
                    "P2-mission_cpu",
                    [&]{ MatrixMultiply<60>::compute(P2_A4, P2_B4, P2_C4); },
                    4500
                },
                {
                    9,
                    "P2-data_log",
                    [&]{ MatrixMultiply<10>::compute(P2_A5, P2_B5, P2_C5); },
                    500,                    // budget_us
                    false                   // is_critical = false → 弹性
                }
            }
        }
    );
    // tasks 为空 → 传统矩阵乘法，直接跑满所有迭代
    //
    // 如需任务模式，改成类似:
    // table.add({
    //     1, 10, 2, 0, 10,
    //     PartitionMode::PARALLEL,
    //     {
    //         {1, "sensor",  [&]{ MatrixMultiply<40>::compute(A1,B1,C1); }, 2000},
    //         {2, "control", [&]{ MatrixMultiply<60>::compute(A2,B2,C2); }, 4000},
    //         {3, "log",     [&]{ MatrixMultiply<20>::compute(A3,B3,C3); },
    //          500, /*is_critical=*/false},   // 弹性任务，利用余量
    //     }
    // });

    // ---- 如需添加更多分区，继续 table.add(...) ----
    //
    // 串行分区示例:
    // table.add({
    //     2, 12, 1, 1, 10,
    //     PartitionMode::SERIAL,
    //     { {4, "nav",  [&]{ ... }, 3000},
    //       {5, "diag", [&]{ ... }, 1000, false} }
    // });
    //
    // 多分区共同用 MAF 周期，需确保每个分区的核心号不重叠，
    // 且 cgroups 隔离区要覆盖所有分区涉及的核心。

    // ########################################################################
    // #                      配  置  区  结  束                                #
    // ########################################################################

    // ======== 初始化 MAF 时钟 ========
    MafClock clock(table);
    clock.init_epoch();

    // ======== 创建工作线程 (根据 tasks 是否为空自动选择模式) ========
    std::vector<std::unique_ptr<WorkerThread>> workers;
    workers.reserve(16);

    for (auto& p : table.partitions()) {
        if (p.tasks.empty()) {
            // ---- 传统模式: 几个核心就几个线程, 各跑各的矩阵乘法 ----
            for (int c = 0; c < p.cpu_count; ++c)
                workers.emplace_back(std::make_unique<WorkerThread>(
                    p, c, clock, g_running, TOTAL_CYCLES, MARGIN_STRATEGY));
        } else if (p.mode == PartitionMode::SERIAL || p.cpu_count == 1) {
            // ---- 串行模式: 全部任务跑在单个核心上 ----
            workers.emplace_back(std::make_unique<WorkerThread>(
                p, 0, clock, g_running, TOTAL_CYCLES, MARGIN_STRATEGY));
        } else {
            // ---- 并行模式: 两阶段分配 (关键任务贪心 + 弹性任务广播) ----
            std::vector<uint64_t> budgets;
            std::vector<bool> is_critical;
            for (auto& t : p.tasks) {
                budgets.push_back(t.budget_us);
                is_critical.push_back(t.is_critical);
            }
            auto assignment = task_assign::wcet_aware_2phase(
                p.cpu_count, budgets, is_critical);

            for (int c = 0; c < p.cpu_count; ++c) {
                PartitionConfig core_cfg = p;
                core_cfg.tasks.clear();
                for (int t_idx : assignment[c])
                    core_cfg.tasks.push_back(p.tasks[t_idx]);

                workers.emplace_back(std::make_unique<WorkerThread>(
                    core_cfg, c, clock, g_running,
                    TOTAL_CYCLES, MARGIN_STRATEGY));
            }
        }
    }

    // ======== 启动 → 打印配置 → 监视 → 等待 → 输出 ========
    for (auto& w : workers) w->start();
    usleep(200000);

    print_banner(table, MARGIN_STRATEGY);
    printf("=> 运行中 (Ctrl+C 停止)\n");

    struct timespec wall0, wall1;
    clock_gettime(CLOCK_MONOTONIC, &wall0);

    while (g_running.load(std::memory_order_relaxed)) {
        struct timespec ts = {5, 0};
        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);

        uint64_t total_iter = 0, total_over = 0, total_be = 0;
        bool all_done = true;
        for (auto& w : workers) {
            auto& s = w->stats();
            total_iter += s.iterations.load(std::memory_order_relaxed);
            total_over += s.slot_overruns.load(std::memory_order_relaxed);
            total_be   += s.best_effort_completed.load(std::memory_order_relaxed);
            if (!w->is_done()) all_done = false;
        }

        printf("  [进度] 迭代 %llu | 溢出 %llu | 弹性 %llu%s\n",
               static_cast<unsigned long long>(total_iter),
               static_cast<unsigned long long>(total_over),
               static_cast<unsigned long long>(total_be),
               all_done ? " (完成)" : "");

        if (all_done) break;
    }

    clock_gettime(CLOCK_MONOTONIC, &wall1);

    for (auto& w : workers) w->join();

    uint64_t wall_us =
        static_cast<uint64_t>(wall1.tv_sec - wall0.tv_sec) * 1000000ULL
        + (static_cast<uint64_t>(wall1.tv_nsec) - wall0.tv_nsec) / 1000ULL;
    print_results(workers, wall_us);

    slot_log::close();
    return 0;
}
