#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <coroutine>
#include <generator>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>
#include <coroutine>
#include <utility>

class TaskResumer
{
public:
    struct Promise
    {   
        Promise()
        {
            std::cout << "Promise()" << std::endl;
        }

        ~Promise()
        {
            std::cout << "~Promise()" << std::endl;
        }

        TaskResumer get_return_object()
        {
            return TaskResumer(std::coroutine_handle<Promise>::from_promise(*this));
        }

        std::suspend_always initial_suspend() // lazy coroutine on start
        {
            std::cout << "+ initial_suspend() suspends coroutine..." << std::endl;
            return {}; 
        }

        std::suspend_always final_suspend() noexcept
        {
            std::cout << "+ final_suspend() suspends..." << std::endl;
            return {};
        }

        void unhandled_exception() 
        {
            std::cout << "+ unhandled_exception()" << std::endl;
            std::terminate();
        }

        void return_void()
        {
            std::cout << "+ Completing coroutine with co_return..." << std::endl;
        }
    };   

    TaskResumer(std::coroutine_handle<Promise> handle)
        : handle_(handle)
    {}

    TaskResumer(const TaskResumer&) = delete;
    TaskResumer& operator=(const TaskResumer&) = delete;

    TaskResumer(TaskResumer&& source) noexcept 
        : handle_{std::exchange(source.handle_, nullptr)}
    {}    

    ~TaskResumer()
    {
        if (handle_)
            handle_.destroy();
    }

    bool resume() 
    {
        if (!handle_ || handle_.done())
            return false;
        
        handle_.resume(); // resuming suspended coroutine
        return true;
    }

    using promise_type = Promise;
private:
    std::coroutine_handle<Promise> handle_;
};

TaskResumer simple_coroutine(std::string name)
{
    std::cout << "Starting " << name << "\n";

    co_await std::suspend_always(); // suspension point

    std::cout << name << " has been resumed!" << "\n";

    co_await std::suspend_always(); // suspension point

    std::cout << name << " has been resumed again!" << "\n";

    std::cout << name << " has finished!" << "\n";

    co_return;
}

TEST_CASE("Simple coroutine")
{
    TaskResumer coro_1 = simple_coroutine("Coroutine#1");

    std::cout << "Coroutine#1 created..." << std::endl;

    do
    {
        std::cout << "Resuming coroutine from main..." << std::endl;
    } while (coro_1.resume());
}   