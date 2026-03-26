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

                if constexpr(!std::is_void_v<T>)
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