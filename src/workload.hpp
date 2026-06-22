#pragma once
// ============================================================================
// workload.hpp — 计算负载 (模板化矩阵乘法)
// ============================================================================
//
// 模板参数 SIZE 控制计算量: O(SIZE³), SIZE=50 时约 125K 次浮点运算。
// 可通过调整 SIZE 或替换为其他负载 (FIR滤波/FFT/排序) 来模拟不同 WCET。

template <int SIZE>
class MatrixMultiply {
public:
    // 执行单次矩阵乘法 C = A × B
    static void compute(float A[SIZE][SIZE],
                        float B[SIZE][SIZE],
                        float C[SIZE][SIZE]) {
        for (int i = 0; i < SIZE; ++i) {
            for (int j = 0; j < SIZE; ++j) {
                float sum = 0.0f;
                for (int k = 0; k < SIZE; ++k)
                    sum += A[i][k] * B[k][j];
                C[i][j] = sum;
            }
        }
    }
};
