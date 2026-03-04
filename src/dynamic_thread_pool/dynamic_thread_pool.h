#pragma once

#include "../../queue/task_queue.h"
#include <thread>
#include <vector>
#include <functional>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdint>

using Job = std::function<void()>;

// Dynamic thread pool: threads are spawned on demand based on queue depth
// and reaped after an idle timeout. Shares the same TaskQueue and FIFO
// scheduling as StaticThreadPool so that benchmarks compare only the
// thread-lifecycle policy.
class DynamicThreadPool {
private:
    const size_t minThreads_;
    const size_t tasksPerThread_;
    const std::chrono::milliseconds idleTimeout_;

    std::atomic<size_t> activeThreads_{0};
    size_t              idleThreads_{0};          // guarded by threadsMutex_

    TaskQueue                job_queue;            // same name / type as StaticThreadPool
    std::mutex               threadsMutex_;
    std::vector<std::thread> threads_;

    // Must be called with threadsMutex_ held.
    void spawnThread() {
        threads_.emplace_back([this] { workerLoop(); });
        ++activeThreads_;
    }

    void workerLoop() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(threadsMutex_);
                ++idleThreads_;
            }

            auto result = job_queue.pop_for(idleTimeout_);

            {
                std::lock_guard<std::mutex> lock(threadsMutex_);
                --idleThreads_;
            }

            if (!result.has_value()) {
                // Shutdown: clean up and exit.
                if (job_queue.is_shutdown()) {
                    std::lock_guard<std::mutex> lock(threadsMutex_);
                    --activeThreads_;
                    detachCurrentThread();
                    return;
                }

                // Idle timeout: shrink if above minimum thread count.
                {
                    std::lock_guard<std::mutex> lock(threadsMutex_);
                    if (activeThreads_ > minThreads_) {
                        --activeThreads_;
                        detachCurrentThread();
                        return;
                    }
                }
                continue;   // at minimum — go back to waiting
            }

            (*result)();
        }
    }

    // Detach this thread and remove it from the threads_ vector.
    // Must be called with threadsMutex_ held.
    void detachCurrentThread() {
        auto id = std::this_thread::get_id();
        for (auto it = threads_.begin(); it != threads_.end(); ++it) {
            if (it->get_id() == id) {
                it->detach();
                threads_.erase(it);
                return;
            }
        }
    }

public:
    // minThreads      — threads that survive idle timeouts (always alive)
    // tasksPerThread   — queue-depth divisor: spawn 1 thread per this many
    //                    pending tasks (1 = most aggressive)
    // idleTimeout      — how long an idle thread waits before exiting
    DynamicThreadPool(size_t minThreads,
                      size_t tasksPerThread = 1,
                      std::chrono::milliseconds idleTimeout = std::chrono::milliseconds(500))
        : minThreads_(minThreads),
          tasksPerThread_(tasksPerThread),
          idleTimeout_(idleTimeout)
    {
        std::lock_guard<std::mutex> lock(threadsMutex_);
        for (size_t i = 0; i < minThreads_; ++i)
            spawnThread();
    }

    // Submit a task — same signature as StaticThreadPool::add_job().
    // Spawns new threads proportional to queue backpressure.
    void add_job(Job job) {
        {
            std::lock_guard<std::mutex> lock(threadsMutex_);
            if (job_queue.is_shutdown()) return;

            // Queue-depth-based spawning:
            // For every `tasksPerThread_` pending tasks that have no idle
            // thread available, spawn a new worker.
            size_t pending = job_queue.size() + 1;               // +1 for the task about to be pushed
            size_t desired = (pending + tasksPerThread_ - 1) / tasksPerThread_;
            if (desired > idleThreads_) {
                size_t to_spawn = desired - idleThreads_;
                for (size_t i = 0; i < to_spawn; ++i)
                    spawnThread();
            }
        }
        job_queue.push(std::move(job));
    }

    // Expose the queue for MetricsCollector sampling (same as StaticThreadPool).
    const TaskQueue& get_queue() const {
        return job_queue;
    }

    size_t threadCount() const { return activeThreads_.load(); }

    size_t queueSize() const { return job_queue.size(); }

    void shutdown() {
        // Swap threads_ out under the mutex so workers that wake from
        // shutdown can't race with us on the vector.
        std::vector<std::thread> toJoin;
        {
            std::lock_guard<std::mutex> lock(threadsMutex_);
            toJoin.swap(threads_);
        }
        job_queue.shutdown();          // wakes all waiting workers
        for (auto& t : toJoin)
            if (t.joinable()) t.join();
    }

    ~DynamicThreadPool() { shutdown(); }

    DynamicThreadPool(const DynamicThreadPool&)            = delete;
    DynamicThreadPool(DynamicThreadPool&&)                 = delete;
    DynamicThreadPool& operator=(const DynamicThreadPool&) = delete;
    DynamicThreadPool& operator=(DynamicThreadPool&&)      = delete;
};
