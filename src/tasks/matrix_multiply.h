#pragma once
#include <vector>
#include <functional>

// Returns a task that computes C = A × B for N×N matrices.
// Thread-safe: each call allocates its own matrices.
// N = 3 for demo; use 128 / 256 / 512 for Light / Medium / Heavy benchmarks.
inline std::function<void()> make_matmul_task(int N) {
    return [N]() {
        // Flat 1D storage, row-major: A[i][j] == A[i*N + j]
        std::vector<double> A(N * N), B(N * N), C(N * N, 0.0);

        // Fill with non-zero values — exact values don't matter for benchmarking.
        for (int i = 0; i < N * N; ++i) {
            A[i] = i + 1.0;
            B[i] = N * N - i;
        }

        // C = A × B  (ikj loop order: better cache locality than ijk)
        for (int i = 0; i < N; ++i)
            for (int k = 0; k < N; ++k)
                for (int j = 0; j < N; ++j)
                    C[i*N + j] += A[i*N + k] * B[k*N + j];

    };
}
