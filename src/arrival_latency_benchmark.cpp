// Arrival process + latency-based task (blocking sleep) on a static thread pool.
// Build: g++ -std=c++17 -pthread -O2 -I../queue -o benchmark_latency arrival_latency_benchmark.cpp

#include <iostream>
#include <iomanip>
#include <atomic>
#include <chrono>
#include <cstdint>

#include "static_thread_pool/static_thread_pool.h"
#include "tasks/latency.h"
#include "traffic/arrival_process.h"

void run_traffic_pattern(const char* label, int threads, int sleep_us, TrafficConfig cfg) {
    std::atomic<int> completed{0};
    StaticThreadPool pool(threads);
    auto latency_task = make_latency_task(sleep_us);

    auto t0 = std::chrono::steady_clock::now();
    auto submitted = run_arrival(cfg, [&] {
        pool.add_job([&] { latency_task(); ++completed; });
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
    const int THREADS = 4;

    // Light = 100µs
    run_traffic_pattern("Steady - Light",  THREADS, 100,  steady(50,         5));
    run_traffic_pattern("Burst  - Light",  THREADS, 100,  burst( 200, 1, 2,  9));
    run_traffic_pattern("Ramp   - Light",  THREADS, 100,  ramp(  100, 20, 2, 8));

    // Medium = 1ms
    run_traffic_pattern("Steady - Medium", THREADS, 1000, steady(50,         5));
    run_traffic_pattern("Burst  - Medium", THREADS, 1000, burst( 200, 1, 2,  9));
    run_traffic_pattern("Ramp   - Medium", THREADS, 1000, ramp(  100, 20, 2, 8));

    // Heavy = 10ms
    run_traffic_pattern("Steady - Heavy",  THREADS, 10000, steady(50,         5));
    run_traffic_pattern("Burst  - Heavy",  THREADS, 10000, burst( 200, 1, 2,  9));
    run_traffic_pattern("Ramp   - Heavy",  THREADS, 10000, ramp(  100, 20, 2, 8));
}