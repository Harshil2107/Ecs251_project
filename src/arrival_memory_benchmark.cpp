// Arrival process + memory-based task (N×N matmul) on a static or dynamic thread pool.
// Includes comprehensive metrics collection: latency, memory, thread count, queue depth.
// Build: g++ -std=c++17 -pthread -O2 -I../queue -o arrival_memory_benchmark arrival_memory_benchmark.cpp
//
// Usage: ./arrival_memory_benchmark [threads] [matrix_size] [output_dir] [pool=static|dynamic] [min_threads] [tasks_per_thread] [traffic=all|Steady|Burst|Ramp]

#include <iostream>
#include <iomanip>
#include <fstream>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

// C++17-compatible type tag (std::type_identity is C++20 only)
template<typename T> struct TypeTag { using type = T; };

#include "static_thread_pool/static_thread_pool.h"
#include "dynamic_thread_pool/dynamic_thread_pool.h"
#include "tasks/matrix_multiply.h"
#include "traffic/arrival_process.h"
#include "metrics/metrics_collector.h"

using Clock = std::chrono::steady_clock;

// Core benchmark body — templated so it works with either pool type without duplication.
// Both pools share the same add_job / get_queue / shutdown API.
template<typename Pool>
void run_with_pool(Pool& pool, const char* label, int threads, int N, TrafficConfig cfg,
                   const std::string& output_dir, const std::string& pool_type_name) {
    MetricsCollector metrics;
    auto memory_task = make_matmul_task(N);

    // Start background sampling (memory, threads, queue depth)
    metrics.start_sampling(&pool.get_queue());

    // Run benchmark
    auto t0 = Clock::now();
    auto submitted = run_arrival(cfg, [&] {
        auto t_submit = Clock::now();
        pool.add_job([t_submit, &memory_task, &metrics] {
            memory_task();
            auto t_complete = Clock::now();
            auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(
                t_complete - t_submit).count();
            metrics.record_latency(latency_us);
            metrics.increment_completed();
        });
    });

    pool.shutdown();
    metrics.stop_sampling();

    double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();

    std::cout << "\n=== " << label << " ===\n" << std::fixed << std::setprecision(2)
              << "  submitted : " << submitted << "\n"
              << "  completed : " << metrics.get_completed_count() << "\n"
              << "  elapsed   : " << elapsed << " s\n"
              << "  throughput: " << metrics.get_completed_count() / elapsed << " tasks/s\n";

    BenchmarkConfig config;
    config.experiment_name = std::string(label) + "_" + pool_type_name + "_" +
                             std::to_string(threads) + "threads_N" + std::to_string(N);
    config.pool_type = pool_type_name;
    config.threads = threads;
    config.matrix_size = N;
    config.traffic_pattern = label;
    if (cfg.pattern == TrafficPattern::STEADY)       config.traffic_rate = cfg.rate_high;
    else if (cfg.pattern == TrafficPattern::BURST)   config.traffic_rate = cfg.rate_high;
    else                                             config.traffic_rate = (cfg.rate_high + cfg.rate_low) / 2.0;
    config.duration_sec = cfg.total_duration;

    std::string json_output = metrics.export_json(config, submitted, elapsed);
    std::string filename = output_dir + "/results_" + label + ".json";
    std::ofstream outfile(filename);
    if (outfile.is_open()) {
        outfile << json_output;
        outfile.close();
        std::cout << "  results saved to: " << filename << "\n";
    } else {
        std::cerr << "  WARNING: Could not write to " << filename << "\n";
    }
}

// Thin wrapper: constructs the right pool type then delegates to run_with_pool.
template<typename Pool>
void run_traffic_pattern(const char* label, int threads, int N, TrafficConfig cfg,
                         const std::string& output_dir, const std::string& pool_type_name,
                         size_t min_threads, size_t tasks_per_thread) {
    if constexpr (std::is_same_v<Pool, StaticThreadPool>) {
        StaticThreadPool pool(threads);
        run_with_pool(pool, label, threads, N, cfg, output_dir, pool_type_name);
    } else {
        DynamicThreadPool pool(min_threads, tasks_per_thread);
        run_with_pool(pool, label, threads, N, cfg, output_dir, pool_type_name);
    }
}

int main(int argc, char* argv[]) {
    // CLI args: [threads] [matrix_size] [output_dir] [pool=static|dynamic] [min_threads] [tasks_per_thread] [traffic=all|Steady|Burst|Ramp]
    const int THREADS           = (argc > 1) ? std::stoi(argv[1])   : 10;
    const int N                 = (argc > 2) ? std::stoi(argv[2])   : 3;
    const std::string OUT_DIR   = (argc > 3) ? argv[3]              : ".";
    const std::string POOL_TYPE = (argc > 4) ? argv[4]              : "static";
    const size_t MIN_THREADS    = (argc > 5) ? std::stoull(argv[5]) : 1;
    const size_t TASKS_PER_THR  = (argc > 6) ? std::stoull(argv[6]) : 1;
    const std::string TRAFFIC   = (argc > 7) ? argv[7]              : "all";

    std::cout << "Starting Thread Pool Benchmark with Metrics Collection\n";
    std::cout << "Configuration: " << THREADS << " threads, " << N << "x" << N
              << " matrix, pool=" << POOL_TYPE << ", traffic=" << TRAFFIC << "\n";
    if (POOL_TYPE == "dynamic")
        std::cout << "Dynamic opts : min_threads=" << MIN_THREADS
                  << " tasks_per_thread=" << TASKS_PER_THR << "\n";
    std::cout << "Output dir   : " << OUT_DIR << "\n";
    std::cout << "========================================================\n";

    auto run_selected = [&](auto pool_tag) {
        using Pool = typename decltype(pool_tag)::type;
        if (TRAFFIC == "all" || TRAFFIC == "Steady")
            run_traffic_pattern<Pool>("Steady", THREADS, N, steady(/*rate=*/1000,                            /*total=*/120), OUT_DIR, POOL_TYPE, MIN_THREADS, TASKS_PER_THR);
        if (TRAFFIC == "all" || TRAFFIC == "Burst")
            run_traffic_pattern<Pool>("Burst",  THREADS, N, burst( /*rate=*/2000, /*burst=*/2, /*idle=*/2,   /*total=*/120), OUT_DIR, POOL_TYPE, MIN_THREADS, TASKS_PER_THR);
        if (TRAFFIC == "all" || TRAFFIC == "Ramp")
            run_traffic_pattern<Pool>("Ramp",   THREADS, N, ramp(  /*high=*/1500, /*low=*/500, /*phase=*/10, /*total=*/120), OUT_DIR, POOL_TYPE, MIN_THREADS, TASKS_PER_THR);
        if (TRAFFIC != "all" && TRAFFIC != "Steady" && TRAFFIC != "Burst" && TRAFFIC != "Ramp")
            std::cerr << "ERROR: unknown traffic pattern '" << TRAFFIC << "'. Use: all, Steady, Burst, Ramp\n";
    };

    if (POOL_TYPE == "dynamic")
        run_selected(TypeTag<DynamicThreadPool>{});
    else
        run_selected(TypeTag<StaticThreadPool>{});

    // Quick test (uncomment to use instead):
    // run_traffic_pattern("Steady", THREADS, N, steady(/*rate=*/50,                           /*total=*/5), OUT_DIR);
    // run_traffic_pattern("Burst",  THREADS, N, burst( /*rate=*/200, /*burst=*/1, /*idle=*/2,  /*total=*/9), OUT_DIR);
    // run_traffic_pattern("Ramp",   THREADS, N, ramp(  /*high=*/100, /*low=*/20,  /*phase=*/2, /*total=*/8), OUT_DIR);

    std::cout << "\n========================================================\n";
    std::cout << "Benchmark complete! Check " << OUT_DIR << "/results_*.json for detailed metrics.\n";
}

