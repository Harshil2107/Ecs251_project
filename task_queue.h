#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <optional>
#include <iostream>
// Thread-safe FIFO task queue using mutex + condition variable.
// Designed to serve as the work queue for both static and dynamic thread pools.
class TaskQueue {
public:
    using Task = std::function<void()>;

    TaskQueue() : shutdown_(false) {}

    bool submit(Task task, size_t idleThreads_, std::atomic<size_t>&activeThreads_, const size_t maxThreads_)
    {


        std::unique_lock<std::mutex> lock(mutex_);

        queue_.push(std::move(task));

        if (idleThreads_ == 0 && activeThreads_ < maxThreads_)
        {
         //   cv_.notify_one();
             return true;
        }
        else
        {
          //  cv_.notify_one();
            return false;
        }
    }

    std::optional<Task> workerLoop(size_t &idleThreads_, std::atomic<size_t>&activeThreads_, const size_t &minThreads_)
    {
            while(true)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ++idleThreads_;

                    // Wait up to 2 s for work; if none and above minimum, exit
                bool gotTask = cv_.wait_for(lock, std::chrono::seconds(2),
                    [this] { return shutdown_ || !queue_.empty(); });

                --idleThreads_;

                if (shutdown_ && queue_.empty()) { --activeThreads_; return std::nullopt; }

                if (!gotTask) {          // timed-out with no work
                    if (activeThreads_ > minThreads_) {
                        --activeThreads_;
                        return std::nullopt;          // shrink: let this thread die
                    }
                    continue;            // at minimum, keep waiting
                }

                Task task = std::move(queue_.front());
                queue_.pop();
                return task;
            }
    }

    void shutdown()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;

        cv_.notify_all();
    }

    bool is_shutdown() const{
        std::unique_lock<std::mutex> lock(mutex_);
        return shutdown_;


    }

    void notify()
    {
        cv_.notify_one();
    }

private:
    std::queue<Task> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_;
};

#endif // TASK_QUEUE_H
