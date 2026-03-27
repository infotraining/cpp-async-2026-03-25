#ifndef ASYNC_LAB_TASK_HPP
#define ASYNC_LAB_TASK_HPP

#include "concepts.hpp"

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <iostream>

namespace AsyncLab
{
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

        private:
            std::coroutine_handle<> continuation_handle_{nullptr};
        };

        using CoroutineHandle_t = std::coroutine_handle<promise_type>;

        explicit Task(CoroutineHandle_t handle)
            : handle_(handle)
        {
            std::cout << "Task created with handle " << handle_.address() << std::endl;
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

        /////////////////////////////////////////////////////////////////////////////////////////////////////
        // Task can be awaited on using co_await, and the result can be retrieved using co_await or sync_wait
        /////////////////////////////////////////////////////////////////////////////////////////////////////

        bool await_ready() const noexcept
        {
            return false;
        }

        // symmetric transfer
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
            {
                return std::move(*handle_.promise().result_);
            }
            else
                return;
        }

        std::coroutine_handle<promise_type> handle() const noexcept
        {
            return handle_;
        }

    private:
        CoroutineHandle_t handle_;
    };
} // namespace AsyncLab

#endif // ASYNC_LAB_TASK_HPP