#include <iostream>
#include <thread>
#include <functional>
#include <atomic>
#include <vector>
#include <chrono>
#include <future>
#include "../../queue/task_queue.h"

class DynamicThreadPool {
public:
    using Task = std::function<void()>;

    // tasksPerThread: how many queued tasks must accumulate per idle-free
    // submit before a new thread is spawned. 1 = one thread per task (most
    // aggressive). N = one thread per N pending tasks (more conservative).
    // idleTimeoutMs: how long a thread waits with no work before exiting
    // (if above minThreads). Should be shorter than your burst idle period
    // so memory shrinkage is visible in measurements.
    DynamicThreadPool(size_t minThreads,
                      size_t tasksPerThread = 1,
                      std::chrono::milliseconds idleTimeout = std::chrono::milliseconds(500))
        : minThreads_(minThreads),
          tasksPerThread_(tasksPerThread),
          idleTimeout_(idleTimeout),
          activeThreads_(0),
          idleThreads_(0)
    {
        for (size_t i = 0; i < minThreads_; ++i)
            spawnThread();
    }

    ~DynamicThreadPool() {
        // Swap threads_ out under threadsMutex_ so workers that wake up from
        // taskQueue_.shutdown() cannot find their own entry and detach from a
        // vector we are about to join. They will simply return cleanly.
        std::vector<std::thread> toJoin;
        {
            std::unique_lock<std::mutex> lock(threadsMutex_);
            toJoin.swap(threads_);
        }
        // Lock released — workers can now acquire threadsMutex_ freely.
        taskQueue_.shutdown();   // wakes all waiting workers via notify_all

        for (auto& t : toJoin)
            if (t.joinable()) t.join();
    }

    void submit(Task task) {
        {
            std::lock_guard<std::mutex> lock(threadsMutex_);
            if (taskQueue_.is_shutdown()) return;
            // Spawn only when all threads are busy AND there is genuine queue
            // backpressure. "taskQueue_.size() + 1" is the depth the queue is
            // about to reach after this push. Spawning at >= tasksPerThread_
            // means: at tasksPerThread_=1, spawn the moment any task has to
            // wait; at tasksPerThread_=N, wait until N tasks are piling up.
            // This avoids spawning when workers are already keeping up.
            if (idleThreads_ == 0 && taskQueue_.size() + 1 >= tasksPerThread_)
                spawnThread();
        }
        taskQueue_.push(std::move(task));
    }

    size_t threadCount() const { return activeThreads_.load(); }

    size_t queueSize() const { return taskQueue_.size(); }

private:
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

            auto result = taskQueue_.pop_for(idleTimeout_);

            {
                std::lock_guard<std::mutex> lock(threadsMutex_);
                --idleThreads_;
            }

            if (!result.has_value()) {
                if (taskQueue_.is_shutdown()) {
                    --activeThreads_;
                    removeCurrentThread();
                    return;
                }

                // Idle timeout: shrink if above minimum.
                {
                    std::lock_guard<std::mutex> lock(threadsMutex_);
                    if (activeThreads_ > minThreads_) {
                        --activeThreads_;
                        removeCurrentThread();
                        return;
                    }
                }
                continue;
            }

            (*result)();
        }
    }

    void removeCurrentThread() {
        auto id = std::this_thread::get_id();
        for (auto it = threads_.begin(); it != threads_.end(); ++it) {
            if (it->get_id() == id) {
                it->detach();
                threads_.erase(it);
                break;
            }
        }
    }

    const size_t minThreads_;
    const size_t tasksPerThread_;
    const std::chrono::milliseconds idleTimeout_;

    std::atomic<size_t>      activeThreads_;
    size_t                   idleThreads_;

    TaskQueue                taskQueue_;
    std::mutex               threadsMutex_;
    std::vector<std::thread> threads_;
};


int main() {
    const int totalTasks = 20;
    std::atomic<int> counter{0};

    // FIX #2 (Premature pool destruction): Use a barrier that waits for ALL
    // tasks to fully complete their lambda body — not just the last increment.
    // We use a shared_ptr<promise> so the last task to finish sets the value,
    // and we wait on it BEFORE the pool goes out of scope and is destroyed.
    auto allDone = std::make_shared<std::promise<void>>();
    auto doneFuture = allDone->get_future();

    {
        // tasksPerThread=1 → spawn a thread for every task that arrives with
        // no idle thread. Increase to tune spawn aggressiveness for experiments.
        // idleTimeout=500ms → threads exit after 500ms idle; set to match your
        // burst idle period so the pool actually shrinks between bursts.
        DynamicThreadPool pool(2, /*tasksPerThread=*/1, std::chrono::milliseconds(500));

        for (int i = 0; i < totalTasks; ++i) {
            // Capture allDone by value (shared_ptr copy) so the promise stays
            // alive even if main's stack frame is being torn down.
            pool.submit([&counter, allDone, i, totalTasks, &pool] {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                int val = ++counter;
                std::cout << "Task " << i << " done. Threads: "
                          << pool.threadCount() << "\n";
                if (val == totalTasks)
                    allDone->set_value();  // signal only after last task body finishes
            });
        }

        // Wait for all task lambdas to fully finish BEFORE the pool destructor
        // runs. This prevents the pool from being destroyed while tasks are
        // still executing.
        doneFuture.wait();

    } // pool destructor runs here — all tasks are guaranteed to be done

    std::cout << "All tasks done. Final counter: " << counter.load() << "\n";
}
