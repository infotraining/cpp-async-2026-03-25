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

namespace AsyncLab
{
    namespace ver_1
    {
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
            { }

            TaskResumer(const TaskResumer&) = delete;
            TaskResumer& operator=(const TaskResumer&) = delete;

            TaskResumer(TaskResumer&& source) noexcept
                : handle_{std::exchange(source.handle_, nullptr)}
            { }

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
    } // namespace ver_1

    inline namespace ver_2
    {
        template <typename T>
        struct PromiseBase
        {
            std::optional<T> result_;

            void return_value(T value)
            {
                std::cout << "+ Completing coroutine with co_return with value "
                          << value << "..." << std::endl;
                result_ = std::move(value);
            }

            T&& result() noexcept
            {
                return std::move(*result_);
            }
        };

        template <>
        struct PromiseBase<void>
        {
            void return_void()
            {
                std::cout << "+ Completing coroutine with co_return..." << std::endl;
            }
        };

        template <typename T>
        class TaskResumer
        {
        public:
            struct Promise : PromiseBase<T>
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
            };

            TaskResumer(std::coroutine_handle<Promise> handle)
                : handle_(handle)
            { }

            TaskResumer(const TaskResumer&) = delete;
            TaskResumer& operator=(const TaskResumer&) = delete;

            TaskResumer(TaskResumer&& source) noexcept
                : handle_{std::exchange(source.handle_, nullptr)}
            { }

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

            T result()
            {

                if constexpr (!std::is_void_v<T>)
                    return handle_.promise().result();
                else
                    return;
            }

            using promise_type = Promise;

        private:
            std::coroutine_handle<Promise> handle_;
        };
    } // namespace ver_2
} // namespace AsyncLab

AsyncLab::TaskResumer<void> simple_coroutine(std::string name)
{
    std::cout << "Starting " << name << "\n";

    co_await std::suspend_always(); // suspension point

    std::cout << name << " has been resumed!" << "\n";

    co_await std::suspend_always(); // suspension point

    std::cout << name << " has been resumed again!" << "\n";

    std::cout << name << " has finished!" << "\n";

    co_return;
}

AsyncLab::TaskResumer<int> value_coroutine(std::string name)
{
    std::cout << "Starting " << name << "\n";

    co_await std::suspend_always(); // suspension point

    std::cout << name << " has been resumed!" << "\n";

    co_await std::suspend_always(); // suspension point

    std::cout << name << " has been resumed again!" << "\n";

    std::cout << name << " has finished!" << "\n";

    co_return 42;
}

TEST_CASE("Simple coroutine")
{
    using namespace AsyncLab;

    TaskResumer<void> coro_1 = simple_coroutine("Coroutine#1");
    TaskResumer<int> coro_2 = value_coroutine("Coroutine#2");

    std::cout << "Coroutine#1 created..." << std::endl;
    std::cout << "Coroutine#2 created..." << std::endl;

    do
    {
        std::cout << "Resuming coroutine from main..." << std::endl;
    } while (coro_1.resume() || coro_2.resume());

    std::cout << "Value from coro_2: " << coro_2.result() << std::endl;
}

struct CustomAwaitable
{
    bool await_ready() noexcept
    {
        std::cout << "CustomAwaitable::await_ready()" << std::endl;

        return true;
    }

    void await_suspend(std::coroutine_handle<> awaiting_coro) noexcept
    {
        std::cout << "CustomAwaitable::await_supend() - " << awaiting_coro.address() << std::endl;
    }

    void await_resume() noexcept
    {
        std::cout << "CustomAwaitable::await_resume" << std::endl;
    }
};

CustomAwaitable custom_awaitable()
{
    return CustomAwaitable{};
}

auto immediate_value(int value)
{
    struct ImmediateAwaitable
    {
        bool await_ready() noexcept
        {
            return true;
        }

        void await_suspend(std::coroutine_handle<> awaiting_coro) noexcept
        {
        }

        int await_resume() noexcept
        {
            return value;
        }

        int value;
    };

    return ImmediateAwaitable{value};
}

auto immediate_throw()
{
    struct ThrowingAwaitable : std::suspend_never
    {
        void await_resume()
        {
            throw std::runtime_error("Error#13");
        }
    };

    return ThrowingAwaitable{};
}

AsyncLab::TaskResumer<void> coro_with_awaitable()
{
    std::cout << "Start..." << std::endl;

    co_await CustomAwaitable{};

    std::cout << "Coroutine has been resumed..." << std::endl;

    // co_await immediate_throw();

    int result = co_await immediate_value(42);

    std::cout << "co_await returned " << result << std::endl;
}

TEST_CASE("Awaitables & Awaiters")
{
    using namespace AsyncLab;

    TaskResumer<void> task = coro_with_awaitable();

    do
    {
    } while (task.resume());
}

struct DetachedTask
{
    struct promise_type
    {
        DetachedTask get_return_object()
        {
            return DetachedTask(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_never initial_suspend() // eager coroutine on start
        {
            return {};
        }

        std::suspend_never final_suspend() noexcept
        {
            return {};
        }

        void unhandled_exception()
        {
            std::terminate();
        }

        void return_void()
        {
        }
    };

    DetachedTask(std::coroutine_handle<promise_type> handle)
        : handle_(handle)
    { }

    std::coroutine_handle<promise_type> handle_;
};

auto resume_on_new_thread()
{
    struct Awaitable
    {
        bool await_ready() noexcept
        {
            return false; // always suspend
        }

        void await_suspend(std::coroutine_handle<> awaiting_coro) noexcept
        {
            std::thread([awaiting_coro]() {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                awaiting_coro.resume();
            }).detach();
        }

        std::thread::id await_resume() noexcept
        {
            return std::this_thread::get_id();
        }
    };

    return Awaitable{};
}

DetachedTask calculate()
{
    std::cout << "Start on THD#" << std::this_thread::get_id() << "..." << "\n";

    std::thread::id thd_id = co_await resume_on_new_thread();

    std::cout << "Resumed on THD#" << thd_id << "..." << "\n";
}

TEST_CASE("Resuming on new thread")
{
    DetachedTask task = calculate();

    std::cout << "Main thread is doing some work..." << "THD#" << std::this_thread::get_id()
              << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Main thread has finished..." << std::endl;
}