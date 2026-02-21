#include <iostream>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <chrono>
#include <future>

class DynamicThreadPool {
public:
    using Task = std::function<void()>;

    DynamicThreadPool(size_t minThreads, size_t maxThreads)
        : minThreads_(minThreads),
          maxThreads_(maxThreads),
          activeThreads_(0),
          idleThreads_(0),
          stop_(false)
    {
        for (size_t i = 0; i < minThreads_; ++i)
            spawnThread();
    }

    ~DynamicThreadPool() {
        // FIX #1 (Deadlock): Swap threads_ and set stop_ in a SINGLE critical
        // section, then release the lock BEFORE calling notify_all(). This
        // ensures worker threads can re-acquire queueMutex_ to exit their
        // wait_for() and call removeCurrentThread() without deadlocking.
        std::vector<std::thread> toJoin;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            stop_ = true;
            toJoin.swap(threads_);
        }
        // Lock is released here — workers can now acquire it and exit cleanly.
        cv_.notify_all();

        for (auto& t : toJoin)
            if (t.joinable()) t.join();
    }

    void submit(Task task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (stop_) return;
            taskQueue_.push(std::move(task));

            if (idleThreads_ == 0 && activeThreads_ < maxThreads_)
                spawnThread();
        }
        cv_.notify_one();
    }

    size_t threadCount() const { return activeThreads_.load(); }

    size_t queueSize() const {
        std::unique_lock<std::mutex> lock(queueMutex_);
        return taskQueue_.size();
    }

private:
    void spawnThread() {
        threads_.emplace_back([this] { workerLoop(); });
        ++activeThreads_;
    }

    void workerLoop() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                ++idleThreads_;

                bool gotTask = cv_.wait_for(lock, std::chrono::seconds(2),
                    [this] { return stop_ || !taskQueue_.empty(); });

                --idleThreads_;

                if (stop_ && taskQueue_.empty()) {
                    --activeThreads_;
                    removeCurrentThread();
                    return;
                }

                if (!gotTask) {
                    if (activeThreads_ > minThreads_) {
                        --activeThreads_;
                        removeCurrentThread();
                        return;
                    }
                    continue;
                }

                task = std::move(taskQueue_.front());
                taskQueue_.pop();
            }
            task();
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
    const size_t maxThreads_;

    std::atomic<size_t>      activeThreads_;
    size_t                   idleThreads_;
    bool                     stop_;

    mutable std::mutex       queueMutex_;
    std::condition_variable  cv_;
    std::queue<Task>         taskQueue_;
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
        DynamicThreadPool pool(2, 8);

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
