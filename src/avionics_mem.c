#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define NUM_ITERATIONS 10000
#define MATRIX_SIZE 50
#define ALLOC_SIZE  (2 * 1024 * 1024)   // 保留 2MB 常量用于缺页预热
#define TOTAL_SIZE  (512 * 1024 * 1024) // 必须与驱动申请的 CMA 大小一致

// 模拟飞行控制逻辑（矩阵乘法）
void simulate_flight_control_logic(float A[MATRIX_SIZE][MATRIX_SIZE],
                                   float B[MATRIX_SIZE][MATRIX_SIZE],
                                   float C[MATRIX_SIZE][MATRIX_SIZE])
{
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            C[i][j] = 0.0;
            for (int k = 0; k < MATRIX_SIZE; k++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
}

// 从内核 CMA 驱动映射物理连续内存
void* map_cma_memory(void)
{
    int fd = open("/dev/avionics_cma", O_RDWR);
    if (fd < 0) {
        perror("无法打开 /dev/avionics_cma");
        exit(EXIT_FAILURE);
    }
    
    void *ptr = mmap(NULL, TOTAL_SIZE, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    close(fd);
    
    if (ptr == MAP_FAILED) {
        perror("CMA mmap 失败");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

int main()
{
    // 锁定全部虚拟地址空间，防止被换出到 Swap
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("内存锁定失败");
        exit(EXIT_FAILURE);
    }

    void *base = map_cma_memory(); // 获取分配的连续物理内存的开始地址
    
    // 预热：强制触碰前 6MB，让 MMU 提前建立页表项，消除首次访问的缺页突刺
    memset(base, 0, 3 * ALLOC_SIZE);

    // 计算单个矩阵的实际字节数 (50 * 50 * 4 = 10000 字节)
    size_t matrix_bytes = MATRIX_SIZE * MATRIX_SIZE * sizeof(float);

    // 将 base 转换为 char* 以便进行精准的字节级指针偏移
    char *mem_ptr = (char *)base;

    // 紧凑切分：让 A、B、C 紧挨着排布，总共约 30KB，完美塞进 L1 数据缓存
    float (*A)[MATRIX_SIZE] = (float (*)[MATRIX_SIZE])(mem_ptr);
    float (*B)[MATRIX_SIZE] = (float (*)[MATRIX_SIZE])(mem_ptr + matrix_bytes);
    float (*C)[MATRIX_SIZE] = (float (*)[MATRIX_SIZE])(mem_ptr + 2 * matrix_bytes);
    printf("=> 航电系统：已成功接管 512MB 专属物理连续内存 (基于 CMA)\n");
    
    // 填充随机数据
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            A[i][j] = (float)(rand() % 100) / 10.0;
            B[i][j] = (float)(rand() % 100) / 10.0;
        }
    }

    // ---- 时间测量与主循环 ----
    struct timespec start, end;
    uint64_t elapsed_us, max_time_us = 0, total_time_us = 0;
    
    printf("=> 矩阵大小: %dx%d, 循环次数: %d\n", MATRIX_SIZE, MATRIX_SIZE, NUM_ITERATIONS);
    
    // 空跑 10 次达到稳态
    for (int i = 0; i < 10; i++)
        simulate_flight_control_logic(A, B, C);

    // 正式测试循环
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        simulate_flight_control_logic(A, B, C);
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 +
                     (end.tv_nsec - start.tv_nsec) / 1000;
                     
        if (elapsed_us > max_time_us)
            max_time_us = elapsed_us;
            
        total_time_us += elapsed_us;
    }
    
    uint64_t avg_time_us = total_time_us / NUM_ITERATIONS;
    
    printf("\n测试完成！\n");
    printf("------------------------------------------------\n");
    printf("平均执行时间 (Average): %llu 微秒 (us)\n", (unsigned long long)avg_time_us);
    printf("最坏情况执行时间 (WCET):  %llu 微秒 (us)\n", (unsigned long long)max_time_us);
    printf("系统抖动 (Jitter):      %llu 微秒 (us)\n",
           (unsigned long long)(max_time_us - avg_time_us));
    printf("------------------------------------------------\n");
    printf("[防优化输出] C[0][0] = %f\n", C[0][0]);
    
    return 0;
}