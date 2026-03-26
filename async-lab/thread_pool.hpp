#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include "concurrent_queue.hpp"
#include <vector>
#include <thread>
#include <future>
#include <functional>

class ThreadPool
{   
public:
    using Task = std::move_only_function<void()>;

    ThreadPool(size_t no_of_threads = std::max(1u, std::thread::hardware_concurrency()))
    {
        thds_.reserve(no_of_threads);
        for(size_t i = 0; i < no_of_threads; ++i)
            thds_.push_back(std::jthread{ [this] { run(); } });    
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool()
    {
        tasks_.close();
    }

    template <typename F>   
    auto submit(F&& task) -> std::future<std::invoke_result_t<F>>
    {
        using Return_t = decltype(task());
        using PackagedTask_t = std::packaged_task<Return_t()>;

        PackagedTask_t ptask(std::forward<F>(task));
        auto future_result = ptask.get_future();

        tasks_.push(std::move(ptask));

        return future_result;
    }

private:
    ConcurrentQueue<Task> tasks_;
    std::vector<std::jthread> thds_;

    void run()
    {
        while(true)
        {
            std::optional<Task> task = tasks_.pop();

            if (not task.has_value())
                return;

            (*task)();
        }
    }
};

#endif