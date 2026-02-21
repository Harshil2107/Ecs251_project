#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <optional>

// Thread-safe FIFO task queue using mutex + condition variable.
// Designed to serve as the work queue for both static and dynamic thread pools.
class TaskQueue {
public:
    using Task = std::function<void()>;

    TaskQueue() : shutdown_(false) {}

    // Enqueue a task. Returns false if the queue has been shut down.
    bool push(Task task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutdown_) return false;
            queue_.push(std::move(task));
        }
        cv_.notify_one();
        return true;
    }

    // Blocking pop. Returns std::nullopt if the queue is shut down and empty.
    std::optional<Task> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
        if (queue_.empty()) return std::nullopt;
        Task task = std::move(queue_.front());
        queue_.pop();
        return task;
    }

    // Non-blocking try_pop. Returns std::nullopt immediately if no task available.
    std::optional<Task> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        Task task = std::move(queue_.front());
        queue_.pop();
        return task;
    }

    // Signal shutdown. Wakes all waiting threads.
    // After shutdown, no new tasks can be pushed.
    // Remaining tasks can still be drained via pop/try_pop.
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        cv_.notify_all();
    }

    bool is_shutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<Task> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_;
};

#endif // TASK_QUEUE_H
