#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include "../../queue/task_queue.h"

// Represents a single time-series data point
struct TimeSample {
    double time_offset_sec;
    uint64_t value;
};

// Configuration for a benchmark run
struct BenchmarkConfig {
    std::string experiment_name;
    std::string pool_type;
    int threads;
    int matrix_size;
    std::string traffic_pattern;
    double traffic_rate;
    double duration_sec;
};

// Statistical summary of a metric
struct MetricStats {
    double mean;
    double std_dev;
    uint64_t min;
    uint64_t max;
    double p50;
    double p95;
    double p99;
};

class MetricsCollector {
public:
    MetricsCollector() : stop_sampling_(false), start_time_(std::chrono::steady_clock::now()) {}
    
    ~MetricsCollector() {
        if (sampling_thread_.joinable()) {
            stop_sampling();
        }
    }

    // Record a single task latency (in microseconds)
    void record_latency(uint64_t latency_us) {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        latencies_us_.push_back(latency_us);
    }

    // Increment task completion counter
    void increment_completed() {
        tasks_completed_.fetch_add(1, std::memory_order_relaxed);
    }

    // Start background sampling thread
    void start_sampling(const TaskQueue* queue) {
        queue_ = queue;
        stop_sampling_.store(false);
        start_time_ = std::chrono::steady_clock::now();
        sampling_thread_ = std::thread(&MetricsCollector::sampling_loop, this);
    }

    // Stop background sampling thread
    void stop_sampling() {
        stop_sampling_.store(true);
        if (sampling_thread_.joinable()) {
            sampling_thread_.join();
        }
    }

    // Export all metrics to JSON string
    std::string export_json(const BenchmarkConfig& config, uint64_t tasks_submitted, double elapsed_sec) const {
        std::ostringstream json;
        json << std::fixed << std::setprecision(2);

        json << "{\n";
        json << "  \"config\": {\n";
        json << "    \"experiment_name\": \"" << config.experiment_name << "\",\n";
        json << "    \"pool_type\": \"" << config.pool_type << "\",\n";
        json << "    \"threads\": " << config.threads << ",\n";
        json << "    \"matrix_size\": " << config.matrix_size << ",\n";
        json << "    \"traffic_pattern\": \"" << config.traffic_pattern << "\",\n";
        json << "    \"traffic_rate\": " << config.traffic_rate << ",\n";
        json << "    \"duration_sec\": " << config.duration_sec << "\n";
        json << "  },\n";

        uint64_t completed = tasks_completed_.load();
        double throughput = completed / elapsed_sec;

        json << "  \"results\": {\n";
        json << "    \"tasks_submitted\": " << tasks_submitted << ",\n";
        json << "    \"tasks_completed\": " << completed << ",\n";
        json << "    \"elapsed_sec\": " << elapsed_sec << ",\n";
        json << "    \"throughput_tasks_per_sec\": " << throughput << ",\n";

        // Latency statistics
        auto latency_stats = calculate_latency_stats();
        json << "    \"latency_ms\": {\n";
        json << "      \"p50\": " << latency_stats.p50 << ",\n";
        json << "      \"p95\": " << latency_stats.p95 << ",\n";
        json << "      \"p99\": " << latency_stats.p99 << ",\n";
        json << "      \"mean\": " << latency_stats.mean << ",\n";
        json << "      \"std_dev\": " << latency_stats.std_dev << ",\n";
        json << "      \"min\": " << (latency_stats.min / 1000.0) << ",\n";
        json << "      \"max\": " << (latency_stats.max / 1000.0) << "\n";
        json << "    },\n";

        // Memory statistics
        auto memory_stats = calculate_stats(memory_samples_);
        json << "    \"memory_kb\": {\n";
        json << "      \"mean\": " << memory_stats.mean << ",\n";
        json << "      \"max\": " << memory_stats.max << ",\n";
        json << "      \"min\": " << memory_stats.min << "\n";
        json << "    },\n";

        // Thread count statistics
        auto thread_stats = calculate_stats(thread_samples_);
        json << "    \"thread_count\": {\n";
        json << "      \"mean\": " << thread_stats.mean << ",\n";
        json << "      \"max\": " << thread_stats.max << ",\n";
        json << "      \"min\": " << thread_stats.min << "\n";
        json << "    },\n";

        // Queue depth statistics
        auto queue_stats = calculate_stats(queue_samples_);
        json << "    \"queue_depth\": {\n";
        json << "      \"mean\": " << queue_stats.mean << ",\n";
        json << "      \"max\": " << queue_stats.max << ",\n";
        json << "      \"p95\": " << queue_stats.p95 << ",\n";
        json << "      \"p99\": " << queue_stats.p99 << "\n";
        json << "    }\n";
        json << "  },\n";

        // Time series data
        json << "  \"time_series\": {\n";
        json << "    \"memory_samples\": " << samples_to_json(memory_samples_) << ",\n";
        json << "    \"thread_count_samples\": " << samples_to_json(thread_samples_) << ",\n";
        json << "    \"queue_depth_samples\": " << samples_to_json(queue_samples_) << "\n";
        json << "  }\n";
        json << "}\n";

        return json.str();
    }

    // Get number of completed tasks
    uint64_t get_completed_count() const {
        return tasks_completed_.load();
    }

private:
    // Latency data (per-task measurements)
    mutable std::mutex latency_mutex_;
    std::vector<uint64_t> latencies_us_;

    // Time-series samples
    mutable std::mutex samples_mutex_;
    std::vector<TimeSample> memory_samples_;
    std::vector<TimeSample> thread_samples_;
    std::vector<TimeSample> queue_samples_;

    // Atomic counters
    std::atomic<uint64_t> tasks_completed_{0};
    std::atomic<bool> stop_sampling_{false};

    // Sampling thread
    std::thread sampling_thread_;
    const TaskQueue* queue_{nullptr};
    std::chrono::steady_clock::time_point start_time_;

    // Background sampling loop
    void sampling_loop() {
        using namespace std::chrono;
        const auto sample_interval = milliseconds(200);

        while (!stop_sampling_.load()) {
            auto now = steady_clock::now();
            double time_offset = duration<double>(now - start_time_).count();

            // Sample all metrics
            uint64_t pss_kb = read_pss_kb();
            uint64_t thread_count = read_thread_count();
            uint64_t queue_depth = queue_ ? queue_->size() : 0;

            // Store samples
            {
                std::lock_guard<std::mutex> lock(samples_mutex_);
                memory_samples_.push_back({time_offset, pss_kb});
                thread_samples_.push_back({time_offset, thread_count});
                queue_samples_.push_back({time_offset, queue_depth});
            }

            std::this_thread::sleep_for(sample_interval);
        }
    }

    // Read PSS memory from /proc/self/status (in KB)
    uint64_t read_pss_kb() const {
        std::ifstream status("/proc/self/status");
        std::string line;
        uint64_t rss_kb = 0;

        while (std::getline(status, line)) {
            if (line.substr(0, 6) == "VmRSS:") {
                std::istringstream iss(line);
                std::string label, kb_label;
                iss >> label >> rss_kb >> kb_label;
                break;
            }
        }
        return rss_kb;
    }

    // Read thread count from /proc/self/stat
    uint64_t read_thread_count() const {
        std::ifstream stat("/proc/self/stat");
        std::string line;
        std::getline(stat, line);

        // Parse the line - thread count is field 20 (after the closing paren)
        // Format: pid (comm) state ppid pgrp ... num_threads ...
        // Find the last closing paren to handle commands with spaces/parens
        size_t pos = line.rfind(')');
        if (pos == std::string::npos) return 0;
        
        // Skip past ") " and parse fields after
        std::istringstream iss(line.substr(pos + 2));
        std::string token;
        
        // Fields after ): state(1) ppid(2) pgrp(3) session(4) tty_nr(5) tpgid(6) 
        //                flags(7) minflt(8) cminflt(9) majflt(10) cmajflt(11)
        //                utime(12) stime(13) cutime(14) cstime(15) priority(16) 
        //                nice(17) num_threads(18) ...
        // So we need field 18 after the closing paren
        for (int i = 0; i < 18 && iss >> token; ++i);
        
        uint64_t num_threads = 0;
        if (!token.empty()) {
            num_threads = std::stoull(token);
        }
        return num_threads;
    }

    // Calculate latency statistics (converts from microseconds to milliseconds)
    MetricStats calculate_latency_stats() const {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        
        if (latencies_us_.empty()) {
            return {0, 0, 0, 0, 0, 0, 0};
        }

        // Make a copy for sorting
        std::vector<uint64_t> sorted_latencies = latencies_us_;
        std::sort(sorted_latencies.begin(), sorted_latencies.end());

        size_t n = sorted_latencies.size();
        uint64_t min_val = sorted_latencies.front();
        uint64_t max_val = sorted_latencies.back();
        uint64_t p50_val = sorted_latencies[n * 50 / 100];
        uint64_t p95_val = sorted_latencies[n * 95 / 100];
        uint64_t p99_val = sorted_latencies[n * 99 / 100];

        // Calculate mean
        double sum = std::accumulate(sorted_latencies.begin(), sorted_latencies.end(), 0.0);
        double mean_us = sum / n;

        // Calculate standard deviation
        double sq_sum = 0.0;
        for (uint64_t lat : sorted_latencies) {
            double diff = lat - mean_us;
            sq_sum += diff * diff;
        }
        double std_dev_us = std::sqrt(sq_sum / n);

        // Convert microseconds to milliseconds for output
        return {
            mean_us / 1000.0,      // mean
            std_dev_us / 1000.0,   // std_dev
            min_val,               // min (keep in us for internal use)
            max_val,               // max (keep in us for internal use)
            p50_val / 1000.0,      // p50
            p95_val / 1000.0,      // p95
            p99_val / 1000.0       // p99
        };
    }

    // Calculate statistics for time-series samples
    MetricStats calculate_stats(const std::vector<TimeSample>& samples) const {
        std::lock_guard<std::mutex> lock(samples_mutex_);
        
        if (samples.empty()) {
            return {0, 0, 0, 0, 0, 0, 0};
        }

        std::vector<uint64_t> values;
        values.reserve(samples.size());
        for (const auto& sample : samples) {
            values.push_back(sample.value);
        }

        std::sort(values.begin(), values.end());

        size_t n = values.size();
        uint64_t min_val = values.front();
        uint64_t max_val = values.back();
        uint64_t p50_val = values[n * 50 / 100];
        uint64_t p95_val = values[n * 95 / 100];
        uint64_t p99_val = values[n * 99 / 100];

        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double mean_val = sum / n;

        double sq_sum = 0.0;
        for (uint64_t val : values) {
            double diff = val - mean_val;
            sq_sum += diff * diff;
        }
        double std_dev_val = std::sqrt(sq_sum / n);

        return {mean_val, std_dev_val, min_val, max_val, (double)p50_val, (double)p95_val, (double)p99_val};
    }

    // Convert time-series samples to JSON array
    std::string samples_to_json(const std::vector<TimeSample>& samples) const {
        std::lock_guard<std::mutex> lock(samples_mutex_);
        std::ostringstream json;
        json << std::fixed << std::setprecision(2);
        
        json << "[";
        for (size_t i = 0; i < samples.size(); ++i) {
            if (i > 0) json << ", ";
            json << "{\"time_offset_sec\": " << samples[i].time_offset_sec 
                 << ", \"value\": " << samples[i].value << "}";
        }
        json << "]";
        
        return json.str();
    }
};
