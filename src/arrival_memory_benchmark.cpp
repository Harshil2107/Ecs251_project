// Arrival process + memory-based task (N×N matmul) on a static thread pool.
// Build: g++ -std=c++17 -pthread -O2 -I../queue -o benchmark_demo benchmark_demo.cpp

#include <iostream>
#include <iomanip>
#include <atomic>
#include <chrono>
#include <cstdint>

#include "static_thread_pool/static_thread_pool.h"
#include "tasks/matrix_multiply.h"
#include "traffic/arrival_process.h"

void run_traffic_pattern(const char* label, int threads, int N, TrafficConfig cfg) {
    std::atomic<int> completed{0};
    StaticThreadPool pool(threads);
    auto memory_task = make_matmul_task(N);

    auto t0 = std::chrono::steady_clock::now();
    auto submitted = run_arrival(cfg, [&] {
        pool.add_job([&] { memory_task(); ++completed; });
    });
    pool.shutdown();
    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();

    std::cout << "\n=== " << label << " ===\n" << std::fixed << std::setprecision(2)
              << "  submitted : " << submitted << "\n"
              << "  completed : " << completed.load() << "\n"
              << "  elapsed   : " << elapsed << " s\n"
              << "  throughput: " << completed.load() / elapsed << " tasks/s\n";
}

int main() {
    const int THREADS = 4, N = 3;  // N = 128/256/512 for real benchmarks

    run_traffic_pattern("Steady", THREADS, N, steady(/*rate=*/50,                           /*total=*/5));
    run_traffic_pattern("Burst",  THREADS, N, burst( /*rate=*/200, /*burst=*/1, /*idle=*/2,  /*total=*/9));
    run_traffic_pattern("Ramp",   THREADS, N, ramp(  /*high=*/100, /*low=*/20,  /*phase=*/2, /*total=*/8));
}
