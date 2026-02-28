#pragma once
#include <chrono>
#include <functional>
#include <thread>
#include <cstdlib>

// Light = 100µs, Medium = 1ms, Heavy = 10ms
inline std::function<void()> make_latency_task(int sleep_us) {
    return [sleep_us]() {
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
    };
}

inline std::function<void()> make_mixed_latency_task() {
    int r = rand() % 100;
    if (r < 50) return make_latency_task(100);
    if (r < 80) return make_latency_task(1000);
    return make_latency_task(10000);
}