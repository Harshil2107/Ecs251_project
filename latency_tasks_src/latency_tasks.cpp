#include <chrono>
#include <functional>
#include <thread>
#include <cstdlib>
#include <iostream>

// Forward declaration so we can use StaticThreadPool without including the whole file
class StaticThreadPool;

using Job = std::function<void()>;

// Light = 100µs, Medium = 1ms, Heavy = 10ms
// 1ms = 1000µs
Job make_latency_task(int sleep_us) {
    return [sleep_us]() {
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
    };
}

Job make_mixed_latency_task() {
    int r = rand() % 100;
    if (r < 50) return make_latency_task(100);
    if (r < 80) return make_latency_task(1000);
    return make_latency_task(10000);
}

void run_benchmarks(StaticThreadPool& pool) {
    // Light
    for (int i = 0; i < 1000; i++)
        pool.add_job(make_latency_task(100));
    pool.wait();
    std::cout << "Light done" << std::endl;

    // Medium
    for (int i = 0; i < 1000; i++)
        pool.add_job(make_latency_task(1000));
    pool.wait();
    std::cout << "Medium done" << std::endl;

    // Heavy
    for (int i = 0; i < 1000; i++)
        pool.add_job(make_latency_task(10000));
    pool.wait();
    std::cout << "Heavy done" << std::endl;

    // Mixed
    for (int i = 0; i < 1000; i++)
        pool.add_job(make_mixed_latency_task());
    pool.wait();
    std::cout << "Mixed done" << std::endl;
}