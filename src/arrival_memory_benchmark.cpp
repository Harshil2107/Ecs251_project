// Arrival process + memory-based task (N×N matmul) on a static thread pool.
// Now includes comprehensive metrics collection: latency, memory, thread count, queue depth.
// Build: g++ -std=c++17 -pthread -O2 -I../queue -o arrival_memory_benchmark arrival_memory_benchmark.cpp

#include <iostream>
#include <iomanip>
#include <fstream>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "static_thread_pool/static_thread_pool.h"
#include "tasks/matrix_multiply.h"
#include "traffic/arrival_process.h"
#include "metrics/metrics_collector.h"

using Clock = std::chrono::steady_clock;

void run_traffic_pattern(const char* label, int threads, int N, TrafficConfig cfg,
                         const std::string& output_dir = ".") {
    // Create metrics collector
    MetricsCollector metrics;
    
    // Create thread pool
    StaticThreadPool pool(threads);
    auto memory_task = make_matmul_task(N);

    // Start background sampling (memory, threads, queue depth)
    metrics.start_sampling(&pool.get_queue());

    // Run benchmark
    auto t0 = Clock::now();
    auto submitted = run_arrival(cfg, [&] {
        // Capture submission timestamp
        auto t_submit = Clock::now();
        
        // Submit task with latency tracking
        pool.add_job([t_submit, &memory_task, &metrics] {
            // Execute the actual work
            memory_task();
            
            // Record completion timestamp and calculate latency
            auto t_complete = Clock::now();
            auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(
                t_complete - t_submit).count();
            
            // Record latency and increment counter
            metrics.record_latency(latency_us);
            metrics.increment_completed();
        });
    });
    
    // Wait for all tasks to complete
    pool.shutdown();
    
    // Stop sampling
    metrics.stop_sampling();
    
    // Calculate elapsed time
    double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();

    // Print console summary
    std::cout << "\n=== " << label << " ===\n" << std::fixed << std::setprecision(2)
              << "  submitted : " << submitted << "\n"
              << "  completed : " << metrics.get_completed_count() << "\n"
              << "  elapsed   : " << elapsed << " s\n"
              << "  throughput: " << metrics.get_completed_count() / elapsed << " tasks/s\n";

    // Configure benchmark metadata
    BenchmarkConfig config;
    config.experiment_name = std::string(label) + "_Static_" + std::to_string(threads) + 
                             "threads_N" + std::to_string(N);
    config.pool_type = "static";
    config.threads = threads;
    config.matrix_size = N;
    config.traffic_pattern = label;
    
    // Extract traffic rate from config
    if (cfg.pattern == TrafficPattern::STEADY) {
        config.traffic_rate = cfg.rate_high;
    } else if (cfg.pattern == TrafficPattern::BURST) {
        config.traffic_rate = cfg.rate_high;
    } else {  // RAMP
        config.traffic_rate = (cfg.rate_high + cfg.rate_low) / 2.0;
    }
    config.duration_sec = cfg.total_duration;

    // Export to JSON
    std::string json_output = metrics.export_json(config, submitted, elapsed);
    
    // Write JSON to file
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

int main(int argc, char* argv[]) {
    // CLI args: [threads] [matrix_size] [output_dir]
    const int THREADS    = (argc > 1) ? std::stoi(argv[1]) : 10;
    const int N          = (argc > 2) ? std::stoi(argv[2]) : 3;
    const std::string OUT_DIR = (argc > 3) ? argv[3] : ".";

    std::cout << "Starting Thread Pool Benchmark with Metrics Collection\n";
    std::cout << "Configuration: " << THREADS << " threads, " << N << "x" << N << " matrix\n";
    std::cout << "Output dir   : " << OUT_DIR << "\n";
    std::cout << "========================================================\n";

    // Production benchmark: 120-second runs with high load
    run_traffic_pattern("Steady", THREADS, N, steady(/*rate=*/1000,                                /*total=*/120), OUT_DIR);
    run_traffic_pattern("Burst",  THREADS, N, burst( /*rate=*/2000, /*burst=*/2, /*idle=*/2,       /*total=*/120), OUT_DIR);
    run_traffic_pattern("Ramp",   THREADS, N, ramp(  /*high=*/1500, /*low=*/500, /*phase=*/10,     /*total=*/120), OUT_DIR);

    // Quick test (uncomment to use instead):
    // run_traffic_pattern("Steady", THREADS, N, steady(/*rate=*/50,                           /*total=*/5), OUT_DIR);
    // run_traffic_pattern("Burst",  THREADS, N, burst( /*rate=*/200, /*burst=*/1, /*idle=*/2,  /*total=*/9), OUT_DIR);
    // run_traffic_pattern("Ramp",   THREADS, N, ramp(  /*high=*/100, /*low=*/20,  /*phase=*/2, /*total=*/8), OUT_DIR);

    std::cout << "\n========================================================\n";
    std::cout << "Benchmark complete! Check " << OUT_DIR << "/results_*.json for detailed metrics.\n";
}
