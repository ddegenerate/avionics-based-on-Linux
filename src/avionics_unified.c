#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
// ==================== 功能开关 ====================
// 需要哪个功能就把哪行注释去掉（或编译时加 -D 定义）
// #define USE_TIME_PARTITION    // SCHED_DEADLINE 时间分区
// #define USE_CMA_MEM           // CMA 连续物理内存分区
// ==================== 时间分区相关 ====================
#ifdef USE_TIME_PARTITION
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/sched.h>
#define SCHED_RUNTIME_MS  3
#define SCHED_DEADLINE_MS 10
#define SCHED_PERIOD_MS   10
struct sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};
int sched_setattr(pid_t pid, const struct sched_attr *attr, unsigned int flags) {
    return syscall(SYS_sched_setattr, pid, attr, flags);
}
static void setup_time_partition(void) {
    struct sched_attr attr = {0};
    attr.size = sizeof(attr);
    attr.sched_policy = 6;
    attr.sched_runtime  = SCHED_RUNTIME_MS * 1000000ULL;
    attr.sched_deadline = SCHED_DEADLINE_MS * 1000000ULL;
    attr.sched_period   = SCHED_PERIOD_MS * 1000000ULL;
    if (sched_setattr(0, &attr, 0) == -1) {
        perror("SCHED_DEADLINE 设置失败 (需 root 并在 cgroups 分区内运行)");
        exit(EXIT_FAILURE);
    }
    printf("=> 时间分区: 每 %dms 周期运行 %dms\n", SCHED_PERIOD_MS, SCHED_RUNTIME_MS);
}
#endif
// ==================== 内存分区相关 ====================
#ifdef USE_CMA_MEM
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define ALLOC_SIZE  (2 * 1024 * 1024)
#define TOTAL_SIZE  (500 * 1024 * 1024)
static void *cma_base = NULL;
static void* setup_cma_memory(void) {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall 失败");
    }
    int fd = open("/dev/avionics_cma", O_RDWR);
    if (fd < 0) {
        perror("无法打开 /dev/avionics_cma");
        exit(EXIT_FAILURE);
    }
    void *ptr = mmap(NULL, TOTAL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        perror("CMA mmap 失败");
        exit(EXIT_FAILURE);
    }
    memset(ptr, 0, 3 * ALLOC_SIZE);
    printf("=> 内存分区: 已映射 512MB CMA 连续物理内存\n");
    return ptr;
}
static void teardown_cma_memory(void) {
    if (cma_base) munmap(cma_base, TOTAL_SIZE);
}
#endif
// ==================== 核心计算逻辑 ====================
#define NUM_ITERATIONS 10000
#define MATRIX_SIZE 50
static void matrix_multiply(float A[MATRIX_SIZE][MATRIX_SIZE],
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
#ifdef USE_TIME_PARTITION
    setup_time_partition();
#endif
    // 分配矩阵内存
    float (*A)[MATRIX_SIZE];
    float (*B)[MATRIX_SIZE];
    float (*C)[MATRIX_SIZE];
#ifdef USE_CMA_MEM
    cma_base = setup_cma_memory();
    size_t matrix_bytes = MATRIX_SIZE * MATRIX_SIZE * sizeof(float);
    char *mem = (char *)cma_base;
    A = (float (*)[MATRIX_SIZE])(mem);
    B = (float (*)[MATRIX_SIZE])(mem + matrix_bytes);
    C = (float (*)[MATRIX_SIZE])(mem + 2 * matrix_bytes);
#else
    A = malloc(MATRIX_SIZE * MATRIX_SIZE * sizeof(float));
    B = malloc(MATRIX_SIZE * MATRIX_SIZE * sizeof(float));
    C = malloc(MATRIX_SIZE * MATRIX_SIZE * sizeof(float));
    if (!A || !B || !C) {
        perror("内存分配失败");
        exit(EXIT_FAILURE);
    }
#endif
    // 填充随机数据
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            A[i][j] = (float)(rand() % 100) / 10.0;
            B[i][j] = (float)(rand() % 100) / 10.0;
        }
    }
    printf("=> 矩阵大小: %dx%d, 循环次数: %d\n", MATRIX_SIZE, MATRIX_SIZE, NUM_ITERATIONS);
    // 预热
    for (int i = 0; i < 10; i++)
        matrix_multiply(A, B, C);
    // 正式测试
    struct timespec start, end;
    uint64_t elapsed_us, max_time_us = 0, total_time_us = 0;
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        matrix_multiply(A, B, C);
        clock_gettime(CLOCK_MONOTONIC, &end);
        elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 +
                     (end.tv_nsec - start.tv_nsec) / 1000;
        if (elapsed_us > max_time_us) max_time_us = elapsed_us;
        total_time_us += elapsed_us;
    }
    uint64_t avg_time_us = total_time_us / NUM_ITERATIONS;
    printf("\n测试完成！\n");
    printf("------------------------------------------------\n");
    printf("平均执行时间 (Average): %llu 微秒 (us)\n", (unsigned long long)avg_time_us);
    printf("最坏情况执行时间 (WCET):  %llu 微秒 (us)\n", (unsigned long long)max_time_us);
    printf("系统抖动 (Jitter):      %llu 微秒 (us)\n", (unsigned long long)(max_time_us - avg_time_us));
    printf("------------------------------------------------\n");
    printf("[防优化输出] C[0][0] = %f\n", C[0][0]);
#ifdef USE_CMA_MEM
    teardown_cma_memory();
#else
    free(A); free(B); free(C);
#endif
    return 0;
}
