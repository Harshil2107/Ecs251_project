#include "static_thread_pool.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>

void example_job() {
    std::cout << "Job running in thread: " << std::this_thread::get_id() << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
}

int main(int argc, const char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <num_threads>\n";
        return 1;
    }

    std::uint64_t threads = std::stoull(argv[1]);
    StaticThreadPool pool(threads);

    for (int i = 0; i < 10; ++i)
        pool.add_job(example_job);

    pool.shutdown();
}
