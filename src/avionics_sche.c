#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/sched.h>

#define NUM_ITERATIONS 10000
#define MATRIX_SIZE 50

// 定义 SCHED_DEADLINE 需要的底层数据结构
struct sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime; //运行时
    uint64_t sched_deadline;//周期时
    uint64_t sched_period;  //截止时
};

// 封装底层的系统调用，c语言没有这个函数，去内核调用
// 这个函数就是把哪个进程改成什么调度策略
int sched_setattr(pid_t pid, const struct sched_attr *attr, unsigned int flags) {
    return syscall(SYS_sched_setattr, pid, attr, flags);
}

// 模拟飞行控制逻辑（矩阵乘法保持不变）
void simulate_flight_control_logic(float A[MATRIX_SIZE][MATRIX_SIZE], 
                                   float B[MATRIX_SIZE][MATRIX_SIZE], 
                                   float C[MATRIX_SIZE][MATRIX_SIZE]) {
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            C[i][j] = 0.0;
            for (int k = 0; k < MATRIX_SIZE; k++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
}

int main() {
    // ARINC 653的时间窗口 (SCHED_DEADLINE)
    struct sched_attr attr;
    attr.size = sizeof(attr);
    attr.sched_flags = 0;
    attr.sched_nice = 0;
    attr.sched_priority = 0;
    
    // 设置调度策略为 SCHED_DEADLINE
    // 宏定义 6 对应 SCHED_DEADLINE
    attr.sched_policy = 6; 

    // 参数计算 (基于你之前测试的 WCET 数据)：
    // 之前隔离状态下的 WCET 大约是 2 毫秒 (2000 us = 2,000,000 ns)
    // 我们预留一点余量，申请 3 毫秒的运行时间。
    
    // 1 毫秒 (ms) = 1,000,000 纳秒 (ns)
    attr.sched_runtime  = 3 * 1000000;   // 运行时间：3 毫秒
    attr.sched_deadline = 10 * 1000000;  // 截止期限：10 毫秒
    attr.sched_period   = 10 * 1000000;  // 周期：10 毫秒

    // 应用调度策略 (0 代表当前进程)
    if (sched_setattr(0, &attr, 0) == -1) {
        perror("设置 SCHED_DEADLINE 失败 (必须使用 sudo 运行)");
        exit(EXIT_FAILURE);
    }
    printf("=> 已成功切换至 SCHED_DEADLINE (时间分区) 调度策略\n");
    printf("=> 时间合同: 每 %llu ms 运行 %llu ms\n", 
            (unsigned long long)(attr.sched_period / 1000000), 
            (unsigned long long)(attr.sched_runtime / 1000000));
    // ==========================================================

    struct timespec start, end;
    uint64_t elapsed_us, max_time_us = 0, total_time_us = 0;
    float A[MATRIX_SIZE][MATRIX_SIZE], B[MATRIX_SIZE][MATRIX_SIZE], C[MATRIX_SIZE][MATRIX_SIZE];

    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            A[i][j] = (float)(rand() % 100) / 10.0;
            B[i][j] = (float)(rand() % 100) / 10.0;
        }
    }

    printf("=> 开始运行航电靶标程序\n");
    for (int i = 0; i < 10; i++) simulate_flight_control_logic(A, B, C);

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        simulate_flight_control_logic(A, B, C);
        clock_gettime(CLOCK_MONOTONIC, &end);

        elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
        if (elapsed_us > max_time_us) max_time_us = elapsed_us;
        total_time_us += elapsed_us;
    }

    printf("\n------------------------------------------------\n");
    printf("平均执行时间 (Average): %llu 微秒 (us)\n", (unsigned long long)(total_time_us / NUM_ITERATIONS));
    printf("最坏情况执行时间 (WCET):  %llu 微秒 (us)\n", (unsigned long long)max_time_us);
    printf("系统抖动 (Jitter):      %llu 微秒 (us)\n", (unsigned long long)(max_time_us - (total_time_us / NUM_ITERATIONS)));
    printf("------------------------------------------------\n");
    printf("[防优化输出] C[0][0] = %f\n", C[0][0]);

    return 0;
}