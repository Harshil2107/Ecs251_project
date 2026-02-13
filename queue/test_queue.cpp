#include "task_queue.h"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <atomic>
#include <sstream>
#include <chrono>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        std::cout << "  " << #name << "... "; \
        try { \
            test_##name(); \
            std::cout << "PASSED\n"; \
            ++tests_passed; \
        } catch (const std::exception& e) { \
            std::cout << "FAILED: " << e.what() << "\n"; \
            ++tests_failed; \
        } \
    } while (0)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::ostringstream oss; \
            oss << "Assertion failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")"; \
            throw std::runtime_error(oss.str()); \
        } \
    } while (0)

void test_basic_push_pop() {
    TaskQueue q;
    int value = 0;
    q.push([&] { value = 42; });
    auto task = q.pop();
    ASSERT(task.has_value());
    (*task)();
    ASSERT(value == 42);
}

void test_fifo_order() {
    TaskQueue q;
    std::vector<int> order;
    for (int i = 0; i < 5; ++i) {
        q.push([&order, i] { order.push_back(i); });
    }
    for (int i = 0; i < 5; ++i) {
        auto task = q.pop();
        ASSERT(task.has_value());
        (*task)();
    }
    ASSERT(order.size() == 5);
    for (int i = 0; i < 5; ++i) {
        ASSERT(order[i] == i);
    }
}

void test_try_pop_empty() {
    TaskQueue q;
    auto task = q.try_pop();
    ASSERT(!task.has_value());
}

void test_size_and_empty() {
    TaskQueue q;
    ASSERT(q.empty());
    ASSERT(q.size() == 0);
    q.push([] {});
    ASSERT(!q.empty());
    ASSERT(q.size() == 1);
    q.push([] {});
    ASSERT(q.size() == 2);
    q.try_pop();
    ASSERT(q.size() == 1);
}

void test_shutdown_rejects_push() {
    TaskQueue q;
    q.shutdown();
    ASSERT(q.is_shutdown());
    bool ok = q.push([] {});
    ASSERT(!ok);
}

void test_shutdown_drains_remaining() {
    TaskQueue q;
    int value = 0;
    q.push([&] { value = 1; });
    q.shutdown();
    auto task = q.pop();
    ASSERT(task.has_value());
    (*task)();
    ASSERT(value == 1);
    auto task2 = q.pop();
    ASSERT(!task2.has_value());
}

void test_pop_unblocks_on_shutdown() {
    TaskQueue q;
    std::atomic<bool> popped{false};
    std::thread t([&] {
        auto task = q.pop();
        popped.store(true);
        ASSERT(!task.has_value());
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT(!popped.load());
    q.shutdown();
    t.join();
    ASSERT(popped.load());
}

void test_concurrent_producers() {
    TaskQueue q;
    const int num_producers = 4;
    const int tasks_per_producer = 1000;
    std::atomic<int> counter{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < tasks_per_producer; ++i) {
                q.push([&counter] { counter.fetch_add(1); });
            }
        });
    }
    for (auto& t : producers) t.join();

    ASSERT(q.size() == num_producers * tasks_per_producer);

    while (auto task = q.try_pop()) {
        (*task)();
    }
    ASSERT(counter.load() == num_producers * tasks_per_producer);
}

void test_concurrent_producer_consumer() {
    TaskQueue q;
    const int total_tasks = 5000;
    std::atomic<int> consumed{0};

    std::thread producer([&] {
        for (int i = 0; i < total_tasks; ++i) {
            q.push([&consumed] { consumed.fetch_add(1); });
        }
        q.shutdown();
    });

    const int num_consumers = 4;
    std::vector<std::thread> consumers;
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&] {
            while (auto task = q.pop()) {
                (*task)();
            }
        });
    }

    producer.join();
    for (auto& t : consumers) t.join();

    ASSERT(consumed.load() == total_tasks);
}

// === Race Condition Tests ===

// Each task increments a unique slot in an array. If two threads ever pop the
// same task (double-consumption), a slot would be incremented twice.
void test_no_double_consumption() {
    TaskQueue q;
    const int total = 10000;
    std::vector<std::atomic<int>> slots(total);
    for (auto& s : slots) s.store(0);

    for (int i = 0; i < total; ++i) {
        q.push([&slots, i] { slots[i].fetch_add(1); });
    }

    const int num_consumers = 8;
    std::vector<std::thread> consumers;
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&] {
            while (auto task = q.try_pop()) {
                (*task)();
            }
        });
    }
    for (auto& t : consumers) t.join();

    for (int i = 0; i < total; ++i) {
        ASSERT(slots[i].load() == 1);
    }
}

// Push and pop happen simultaneously on different threads.
// Every pushed task must be popped exactly once — no lost tasks.
void test_simultaneous_push_pop() {
    TaskQueue q;
    const int total = 10000;
    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};

    // 4 producers, 4 consumers running at the same time
    const int num_threads = 4;
    const int per_producer = total / num_threads;

    std::vector<std::thread> producers;
    for (int p = 0; p < num_threads; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < per_producer; ++i) {
                q.push([&pop_count] { pop_count.fetch_add(1); });
                push_count.fetch_add(1);
            }
        });
    }

    std::vector<std::thread> consumers;
    std::atomic<bool> done{false};
    for (int c = 0; c < num_threads; ++c) {
        consumers.emplace_back([&] {
            while (!done.load() || !q.empty()) {
                auto task = q.try_pop();
                if (task) (*task)();
            }
        });
    }

    for (auto& t : producers) t.join();
    done.store(true);
    for (auto& t : consumers) t.join();

    // Drain any remaining tasks
    while (auto task = q.try_pop()) {
        (*task)();
    }

    ASSERT(push_count.load() == total);
    ASSERT(pop_count.load() == total);
}

// Multiple threads call push while another thread calls shutdown.
// Every push must either succeed (task is in the queue) or fail (return false).
// The total of succeeded pushes must equal the number of tasks in the queue.
void test_push_during_shutdown() {
    for (int trial = 0; trial < 50; ++trial) {
        TaskQueue q;
        const int num_pushers = 4;
        const int per_pusher = 500;
        std::atomic<int> accepted{0};

        std::vector<std::thread> pushers;
        for (int p = 0; p < num_pushers; ++p) {
            pushers.emplace_back([&] {
                for (int i = 0; i < per_pusher; ++i) {
                    if (q.push([] {}))
                        accepted.fetch_add(1);
                }
            });
        }

        // Shutdown from another thread while pushes are in flight
        std::thread killer([&] { q.shutdown(); });

        for (auto& t : pushers) t.join();
        killer.join();

        // Drain and count tasks actually in the queue
        int in_queue = 0;
        while (q.try_pop()) ++in_queue;

        ASSERT(in_queue == accepted.load());
    }
}

// Multiple threads blocked on pop() are all woken by shutdown.
// None should deadlock or miss the signal.
void test_multiple_blocked_poppers_shutdown() {
    TaskQueue q;
    const int num_waiters = 8;
    std::atomic<int> woke_up{0};

    std::vector<std::thread> waiters;
    for (int i = 0; i < num_waiters; ++i) {
        waiters.emplace_back([&] {
            auto task = q.pop(); // all block here
            ASSERT(!task.has_value());
            woke_up.fetch_add(1);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT(woke_up.load() == 0);

    q.shutdown();
    for (auto& t : waiters) t.join();

    ASSERT(woke_up.load() == num_waiters);
}

// Producers and consumers race with shutdown happening mid-stream.
// No task may be lost or double-consumed.
void test_push_pop_shutdown_race() {
    for (int trial = 0; trial < 20; ++trial) {
        TaskQueue q;
        std::atomic<int> produced{0};
        std::atomic<int> consumed{0};

        std::thread producer([&] {
            for (int i = 0; i < 5000; ++i) {
                if (q.push([&consumed] { consumed.fetch_add(1); }))
                    produced.fetch_add(1);
            }
        });

        std::vector<std::thread> consumers;
        for (int c = 0; c < 4; ++c) {
            consumers.emplace_back([&] {
                while (auto task = q.pop()) {
                    (*task)();
                }
            });
        }

        // Shutdown mid-stream
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        q.shutdown();

        producer.join();
        for (auto& t : consumers) t.join();

        // Drain anything left after consumers exit
        while (auto task = q.try_pop()) {
            (*task)();
        }

        ASSERT(consumed.load() == produced.load());
    }
}

int main() {
    std::cout << "=== TaskQueue Tests ===\n";
    // Push a task and pop it, verify it executes correctly.
    TEST(basic_push_pop);
    // Push multiple tasks and verify they come out in FIFO order.
    TEST(fifo_order);
    // try_pop on an empty queue returns nullopt immediately.
    TEST(try_pop_empty);
    // size() and empty() reflect pushes and pops accurately.
    TEST(size_and_empty);
    // push() is rejected after shutdown.
    TEST(shutdown_rejects_push);
    // Tasks enqueued before shutdown can still be popped and executed.
    TEST(shutdown_drains_remaining);
    // A thread blocked on pop() is woken when shutdown is called.
    TEST(pop_unblocks_on_shutdown);
    // 4 threads push 1000 tasks each; all 4000 tasks are present and execute.
    TEST(concurrent_producers);
    // 1 producer, 4 consumers, 5000 tasks; all tasks are consumed exactly once.
    TEST(concurrent_producer_consumer);

    std::cout << "\n=== Race Condition Tests ===\n";
    // 8 threads pop from the same queue; each task must execute exactly once.
    TEST(no_double_consumption);
    // 4 producers and 4 consumers run simultaneously; no tasks are lost.
    TEST(simultaneous_push_pop);
    // Multiple threads push while another thread calls shutdown; accepted count matches queue contents.
    TEST(push_during_shutdown);
    // 8 threads blocked on pop() must all wake up when shutdown is called.
    TEST(multiple_blocked_poppers_shutdown);
    // Producers, consumers, and shutdown all race; consumed count equals produced count.
    TEST(push_pop_shutdown_race);

    std::cout << "\n=== Results: " << tests_passed << " passed, "
              << tests_failed << " failed ===\n";
    return tests_failed > 0 ? 1 : 0;
}
