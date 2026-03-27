#include "sync_wait.hpp"

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
#include <thread>
#include <utility>
#include <vector>

template <typename T>
struct TaskResultBase
{
    std::optional<T> result_;

    void return_value(T value)
    {
        result_ = std::move(value);
    }

    T&& result() noexcept
    {
        return std::move(*result_);
    }
};

template <>
struct TaskResultBase<void>
{
    void return_void() noexcept
    {
    }
};

template <typename T>
class Task
{
public:
    struct promise_type : TaskResultBase<T>
    {
        std::exception_ptr exception_;
        std::coroutine_handle<> continuation_handle_{nullptr};

        Task get_return_object()
        {
            return Task{CoroutineHandle_t::from_promise(*this)};
        }

        std::suspend_always initial_suspend() const noexcept
        {
            return {};
        }

        struct FinalAwaitable
        {
            bool await_ready() const noexcept { return false; }

            template <typename TPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<TPromise> handle) const noexcept
            {
                if (auto continuation_handle = handle.promise().continuation_handle_; continuation_handle)
                {
                    return continuation_handle;
                }
                else
                {
                    return std::noop_coroutine();
                }
            }

            void await_resume() const noexcept { }
        };

        FinalAwaitable final_suspend() const noexcept
        {
            return {};
        }

        void unhandled_exception()
        {
            exception_ = std::current_exception();
        }

        void set_continuation(std::coroutine_handle<> continuation_handle) noexcept
        {
            continuation_handle_ = continuation_handle;
        }
    };

    using CoroutineHandle_t = std::coroutine_handle<promise_type>;

    explicit Task(CoroutineHandle_t handle)
        : handle_(handle)
    {
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    Task& operator=(Task&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
            {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~Task()
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    // Task can be awaited - Awaitable interface

    bool await_ready() const noexcept
    {
        return false; // always suspend
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
    {
        handle_.promise().set_continuation(awaiting_coroutine);
        return handle_;
    }

    auto await_resume()
    {
        if (handle_.promise().exception_)
        {
            std::rethrow_exception(handle_.promise().exception_);
        }

        if constexpr (!std::is_void_v<T>)
            return handle_.promise().result();
        else
            return;
    }

private:
    CoroutineHandle_t handle_;
};

Task<int> coroutine(int value)
{
    co_return value * 2;
}

Task<std::string> nested_coroutine(int value)
{
    int result = co_await coroutine(value);

    co_return std::to_string(result);
}

TEST_CASE("Task")
{
    using namespace AsyncLab;

    SECTION("simple case")
    {
        Task<int> tsk_1 = coroutine(42);

        int result = sync_wait(std::move(tsk_1));
        REQUIRE(result == 84);
    }

    SECTION("nested tasks")
    {
        std::string result = sync_wait(nested_coroutine(21));
        REQUIRE(result == "42");
    }
}