#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <condition_variable>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <syncstream>
#include <future>

#include "concurrent_queue.hpp"

using namespace std::literals;

std::osyncstream synced_cout()
{
    return std::osyncstream(std::cout);
}

void background_work(const int id, const std::string text, 
                     const std::chrono::milliseconds delay) 
{
	synced_cout() << "Thread#" << id << " started..." << std::endl;
	synced_cout() << "THD#" << std::this_thread::get_id() << " is doing some work..." << std::endl;

    for(const auto& letter : text)
    {
        synced_cout() << "Thread#" << id << " - " << letter << std::endl;
        std::this_thread::sleep_for(delay);
    }
    synced_cout() << "Thread#" << id << " has finished..." << std::endl;
}

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
    decltype(auto) submit(F&& task)
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

int calculate(int x)
{
    synced_cout() << "Starting calulate for " << x << "\n";
    std::this_thread::sleep_for(x * 25ms);
    return x * x;
}

TEST_CASE("Thread pool")
{
    ThreadPool thd_pool(12);

    thd_pool.submit([] { background_work(1, "Hello"s, 10ms); });
    thd_pool.submit([] { background_work(2, "Thread"s, 15ms); });
    thd_pool.submit([] { background_work(3, "Concurrent"s, 50ms); });
    
    std::future<int> f_square = thd_pool.submit([] { return calculate(5); });

    std::vector<std::future<int>> f_squares;

    for(int i = 4; i < 30; ++i)
        f_squares.push_back(thd_pool.submit([i] { return calculate(i); }));

    for(auto& f : f_squares)
    {
        synced_cout() << "Result: " << f.get() << "\n";
    }    
}