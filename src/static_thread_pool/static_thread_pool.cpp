#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <memory>
#include <vector>
#include <functional>
#include <condition_variable>
#include <stdexcept>
#include <cinttypes>

/*

constructor : take in pool size

create_threads:

*/

using Job = std::function<void()>;
using thread_vector = std::vector<std::thread>;


class StaticThreadPool
{
    private:
    //collection of threads
    thread_vector t_pool;
    std::uint64_t pool_size;
    std::queue<Job> job_queue; // place holder
    std::condition_variable cv;
    std::mutex mtx;
    bool stop = false;

    thread_vector create_threads(std::uint64_t pool_size)
    {
        thread_vector t_pool;
        t_pool.reserve(pool_size);
        for(std::uint64_t i = 0; i < pool_size; ++i)
        {   
            t_pool.push_back(std::thread([this]()
            {
                while(true)
                {
                    Job job;
                    {
                        std::unique_lock<std::mutex> lock(mtx);

                        // we wait till there is a job or we are stopping the pool
                        cv.wait(lock, [this](){ return stop || !job_queue.empty(); });

                        // if we are stopping and there are no jobs left, we can exit the thread
                        if(stop && job_queue.empty())
                            return;

                        // get the next job from the queue
                        job = std::move(job_queue.front());

                        //remove the job from the queue
                        job_queue.pop();
                    }

                    // run the job outside of the lock to allow other threads to access the queue
                    job();
                }
            }));
        }
        return t_pool;
    };

    public:

        // explicit in a marking in constructor to not change types 
        explicit StaticThreadPool(std::uint64_t pool_size = 4)
        {
            pool_size = pool_size;
            t_pool = create_threads(pool_size);

        };

        void add_job(Job job) {
            {
                std::unique_lock<std::mutex> lock(mtx);
                job_queue.push(job);
            }

            cv.notify_one(); // notify one thread that there is a new job
        }

        void shutdown() {
            {
                std::unique_lock<std::mutex> lock(mtx);
                stop = true; // signal all threads to stop
            }

            cv.notify_all(); // wake up all threads to let them exit

            for(std::thread &worker : t_pool)
            {
                if(worker.joinable())
                    worker.join(); // wait for all threads to finish
            }
        };

        //destructor 
        ~StaticThreadPool()
        {
            shutdown();
        };

        StaticThreadPool(const StaticThreadPool&) = delete;
        StaticThreadPool(StaticThreadPool&&) = delete;
        StaticThreadPool& operator = (const StaticThreadPool&) = delete;
        StaticThreadPool& operator = (StaticThreadPool&&) = delete;
        //End of destructor

};


void example_job()
{
    std::cout << "Job is running in thread: " << std::this_thread::get_id() << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(20)); // Simulate work
}

int main()
{
    StaticThreadPool pool(4);
    for(int i = 0; i < 10; ++i)
    {
        pool.add_job(example_job);
    }


};

