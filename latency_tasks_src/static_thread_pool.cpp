#include "task_queue.h"
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
    TaskQueue job_queue;

    //for latency tasks
    std::atomic<int> pending_tasks{0};   
    std::condition_variable cv_wait;     
    std::mutex wait_mtx;  

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


                    try {

                        job = job_queue.pop().value();
                        job();
                        pending_tasks--;  //for task1, wait, task2      
                        cv_wait.notify_all(); //for task1, wait, task2 

                    } catch (const std::bad_optional_access& e) {
                        if(job_queue.is_shutdown() == true)
                            return;

                        std::cout << "Job is a NULL pointer." << std::endl;

                    }

                }
            }));
        }
        return t_pool;
    };

    public:

        // explicit in a marking in constructor to not change types
        explicit StaticThreadPool(std::uint64_t pool_size /* = 4*/)
        {
            //this->pool_size = pool_size;
            t_pool = create_threads(pool_size);

        };

        void add_job(Job job) {
            {
                pending_tasks++; //for task1, wait, task2 
                job_queue.push(job);

            }

           // cv.notify_one(); // notify one thread that there is a new job
        }

        // For latency tasks
        void wait() {
            std::unique_lock<std::mutex> lock(wait_mtx);
            cv_wait.wait(lock, [this]() { return pending_tasks == 0; });
        }

        void shutdown() {


            job_queue.shutdown();

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

#include "latency_tasks.cpp"

void example_job()
{
    std::cout << "Job is running in thread: " << std::this_thread::get_id() << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5)); // Simulate work
}

int main(int argc, const char *argv[])
{

    if(argc != 2)
    {
        std::cout << "Please enter call the program with two commands." << std::endl;
        std::cout<< "Ex: ./File_name 4" << std::endl;
        std::cout << "The last arguement should be the the amount of threads you want to run." << std::endl;
        exit(1);
    }

    std::uint64_t threads = std::stoull(argv[1]);

    StaticThreadPool pool(threads);

    //ADD JOB AREA
    run_benchmarks(pool);


    pool.shutdown();


};

