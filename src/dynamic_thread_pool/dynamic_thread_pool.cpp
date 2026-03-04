#include "dynamic_thread_pool.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

// Simple smoke test — mirrors static_thread_pool.cpp structure.
int main(int argc, const char* argv[]) {
    const size_t minThreads    = (argc > 1) ? std::stoull(argv[1]) : 2;
    const size_t tasksPerThread = (argc > 2) ? std::stoull(argv[2]) : 1;
    const int    totalTasks     = 20;
    std::atomic<int> counter{0};

    DynamicThreadPool pool(minThreads, tasksPerThread, std::chrono::milliseconds(500));

    for (int i = 0; i < totalTasks; ++i) {
        pool.add_job([&counter, i, &pool] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            int val = ++counter;
            std::cout << "Task " << i << " done. Threads: "
                      << pool.threadCount() << "\n";
        });
    }

    pool.shutdown();
    std::cout << "All tasks done. Final counter: " << counter.load() << "\n";
}
