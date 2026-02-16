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
    //std::queue<Job> job_queue; // place holder
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

                job_queue.push(job);

            }

           // cv.notify_one(); // notify one thread that there is a new job
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


void example_job()
{
    std::cout << "Job is running in thread: " << std::this_thread::get_id() << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5)); // Simulate work
}

int main(int argc, const char *argv[])
{

    std::uint64_t threads;

    if(argc != 2)
    {
        std::cout << "Please enter call the program with two commands." << std::endl;
        std::cout<< "Ex: ./File_name 4" << std::endl;
        std::cout << "The last arguement should be the the amount of threads you want to run." << std::endl;
        exit(1);
    }else{

        threads = static_cast<std::uint64_t>(*argv[1]);
    }

    StaticThreadPool pool(threads);
    for(int i = 0; i < 10; ++i)
    {
        pool.add_job(example_job);
    }

    pool.shutdown();


};

