#ifndef ASYNC_LAB_SYNC_WAIT_HPP
#define ASYNC_LAB_SYNC_WAIT_HPP

#include "concepts.hpp"

#include <coroutine>
#include <exception>
#include <optional>
#include <semaphore>
#include <type_traits>
#include <utility>


namespace AsyncLab
{
    template <typename TResult>
    class SyncWaitTask;

    template <typename TResult>
    class SyncWaitTaskPromise
    {
        using CoroutineHandle_t = std::coroutine_handle<SyncWaitTaskPromise<TResult>>;

    public:
        static_assert(!std::is_reference_v<TResult>, "SyncWaitTask does not support reference types. Please use std::ref or std::cref to wrap reference types.");

        SyncWaitTaskPromise() = default;

        SyncWaitTask<TResult> get_return_object()
        {
            return {CoroutineHandle_t::from_promise(*this)};
        }

        void unhandled_exception()
        {
            exception_ = std::current_exception();
        }

        std::suspend_always initial_suspend() const noexcept
        {
            return {};
        }

        auto final_suspend() const noexcept
        {
            class CompletionNotifier
            {
            public:
                bool await_ready() const noexcept { return false; }

                void await_suspend(CoroutineHandle_t handle) const noexcept
                {
                    handle.promise().sync_->release();
                }

                void await_resume() const noexcept { }
            };

            return CompletionNotifier{};
        }

        template <typename U>
        auto yield_value(U&& value)
        {
            result_ = std::addressof(value);
            return final_suspend();
        }

        decltype(auto) result()
        {
            if (exception_)
            {
                std::rethrow_exception(exception_);
            }

            return static_cast<TResult&&>(*result_);
        }

        void start(std::binary_semaphore& sync)
        {
            sync_ = std::addressof(sync);
            CoroutineHandle_t::from_promise(*this).resume();
        }

        void return_void()
        {
            // No result to store, just return.
        }

    private:
        std::binary_semaphore* sync_{nullptr};
        std::remove_reference_t<TResult>* result_;
        std::exception_ptr exception_{nullptr};
    };

    template <typename TResult>
    class SyncWaitTask
    {
    public:
        using promise_type = SyncWaitTaskPromise<TResult>;
        using CoroutineHandle_t = std::coroutine_handle<promise_type>;

        SyncWaitTask(CoroutineHandle_t handle) noexcept
            : handle_(handle)
        {
        }

        SyncWaitTask(const SyncWaitTask&) = delete;
        SyncWaitTask& operator=(const SyncWaitTask&) = delete;

        SyncWaitTask(SyncWaitTask&& other) noexcept
            : handle_(std::exchange(other.handle_, nullptr))
        {
        }

        ~SyncWaitTask()
        {
            if (handle_)
            {
                handle_.destroy();
            }
        }

        void start(std::binary_semaphore& sync)
        {
            handle_.promise().start(sync);
        }

        decltype(auto) result()
        {
            if constexpr(!std::is_void_v<TResult>)
                return handle_.promise().result();
            else 
                return;
        }

    private:
        CoroutineHandle_t handle_;
    };

    template <Awaitable TAwaitable, typename TResult = AwaitResult_t<TAwaitable>>
    SyncWaitTask<TResult> make_sync_wait_task(TAwaitable&& awaitable)
    {
        if constexpr (std::is_void_v<TResult>)
        {
            co_await std::forward<TAwaitable>(awaitable);
        }
        else
        {
            co_yield co_await std::forward<TAwaitable>(awaitable);
        }
    }

    template <Awaitable TAwaitable>
    auto sync_wait(TAwaitable&& awaitable)
    {
        static_assert(!std::is_reference_v<TAwaitable>, "sync_wait does not support reference types. Please pass an rvalue or use std::move.");

        SyncWaitTask sync_task = make_sync_wait_task(std::forward<TAwaitable>(awaitable));

        std::binary_semaphore sync{0};
        sync_task.start(sync);
        sync.acquire(); // wait for the task to complete
        return sync_task.result();
    }
} // namespace AsyncLab

#endif // ASYNC_LAB_SYNC_WAIT_HPP