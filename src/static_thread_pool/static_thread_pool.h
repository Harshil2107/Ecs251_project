#pragma once

#include "../../queue/task_queue.h"
#include <iostream>
#include <thread>
#include <vector>
#include <functional>
#include <cstdint>

using Job = std::function<void()>;
using thread_vector = std::vector<std::thread>;

class StaticThreadPool {
private:
    thread_vector t_pool;
    TaskQueue job_queue;

    thread_vector create_threads(std::uint64_t pool_size) {
        thread_vector pool;
        pool.reserve(pool_size);
        for (std::uint64_t i = 0; i < pool_size; ++i) {
            pool.push_back(std::thread([this]() {
                while (true) {
                    auto job = job_queue.pop();
                    if (!job.has_value()) {
                        // pop() returns nullopt only when shutdown and empty.
                        return;
                    }
                    (*job)();
                }
            }));
        }
        return pool;
    }

public:
    explicit StaticThreadPool(std::uint64_t pool_size) {
        t_pool = create_threads(pool_size);
    }

    void add_job(Job job) {
        job_queue.push(job);
    }

    // Get reference to the task queue (for metrics sampling)
    const TaskQueue& get_queue() const {
        return job_queue;
    }

    void shutdown() {
        job_queue.shutdown();
        for (std::thread& worker : t_pool)
            if (worker.joinable()) worker.join();
    }

    ~StaticThreadPool() { shutdown(); }

    StaticThreadPool(const StaticThreadPool&) = delete;
    StaticThreadPool(StaticThreadPool&&)      = delete;
    StaticThreadPool& operator=(const StaticThreadPool&) = delete;
    StaticThreadPool& operator=(StaticThreadPool&&)      = delete;
};
