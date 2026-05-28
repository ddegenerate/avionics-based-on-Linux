#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#define NUM_ITERATIONS 10000
#define MATRIX_SIZE 50
//矩阵乘法
void simulate_flight_control_logic(float A[MATRIX_SIZE][MATRIX_SIZE], 
                                   float B[MATRIX_SIZE][MATRIX_SIZE], 
                                   float C[MATRIX_SIZE][MATRIX_SIZE]) 
{
    for (int i = 0; i < MATRIX_SIZE; i++) 
    {
        for (int j = 0; j < MATRIX_SIZE; j++) 
        {
            C[i][j] = 0.0;
            for (int k = 0; k < MATRIX_SIZE; k++) 
            {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
}

int main() {
    struct timespec start, end;  //结构体里面有s和ns相当于就是计现在是多少秒零多少纳秒
    uint64_t elapsed_us;
    uint64_t max_time_us = 0;
    uint64_t total_time_us = 0;

    // 初始化矩阵
    float A[MATRIX_SIZE][MATRIX_SIZE];
    float B[MATRIX_SIZE][MATRIX_SIZE];
    float C[MATRIX_SIZE][MATRIX_SIZE];

    // 填充随机数据
    for (int i = 0; i < MATRIX_SIZE; i++) 
    {
        for (int j = 0; j < MATRIX_SIZE; j++) 
        {
            A[i][j] = (float)(rand() % 100) / 10.0;
            B[i][j] = (float)(rand() % 100) / 10.0;
        }
    }
    printf("=> 矩阵大小: %dx%d, 循环次数: %d\n", MATRIX_SIZE, MATRIX_SIZE, NUM_ITERATIONS);
    // 先空跑10次达到稳态
    for (int i = 0; i < 10; i++) 
    {
        simulate_flight_control_logic(A, B, C);
    }

    // 正式测试循环
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        // 1. 获取循环开始时间
        clock_gettime(CLOCK_MONOTONIC, &start);  //CLOCK_MONOTONIC是单调递增时钟，修改系统时间不影响
        // 2. 执行飞行控制逻辑
        simulate_flight_control_logic(A, B, C);
        // 3. 获取循环结束时间
        clock_gettime(CLOCK_MONOTONIC, &end);
        // 4. 计算耗费的微秒 (us)
  
        elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 + 
                     (end.tv_nsec - start.tv_nsec) / 1000;

        // 5. 记录所有次数里面的最长运行时间，算是伪WCET
        if (elapsed_us > max_time_us) 
        {
            max_time_us = elapsed_us;
        }
        // 累加时间用于计算平均值
        total_time_us += elapsed_us;
    }

    // 计算平均执行时间
    uint64_t avg_time_us = total_time_us / NUM_ITERATIONS;
    // 打印测试结果
    printf("\n测试完成！\n");
    printf("------------------------------------------------\n");
    printf("平均执行时间 (Average): %llu 微秒 (us)\n", (unsigned long long)avg_time_us);
    printf("最坏情况执行时间 (WCET):  %llu 微秒 (us)\n", (unsigned long long)max_time_us);
    printf("系统抖动 (Jitter):      %llu 微秒 (us)\n", (unsigned long long)(max_time_us - avg_time_us));
    printf("------------------------------------------------\n");
    // 防优化机制：打印矩阵中的一个值，防止聪明的现代编译器把没有输出的矩阵运算直接删掉 (Dead Code Elimination)
    printf("[防优化输出] C[0][0] = %f\n", C[0][0]);
    return 0;
}