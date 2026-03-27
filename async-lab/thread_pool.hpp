#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include "concurrent_queue.hpp"
#include "task.hpp"

#include <thread>
#include <vector>
#include <optional>
#include <cassert>
#include <utility>
#include <functional>
#include <future>

namespace AsyncLab
{
    class ThreadPool
    {
        using Task = std::move_only_function<void()>;

    public:
        ThreadPool(size_t no_of_threads = 0)
        {
            if (no_of_threads == 0)
            {
                no_of_threads = std::max(1u, std::thread::hardware_concurrency());
            }

            assert(no_of_threads > 0);

            for (size_t i = 0; i < no_of_threads; ++i)
            {
                threads_.emplace_back([this] { run(); });
            }
        }

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        ~ThreadPool()
        {
            // std::cout << "ThreadPool shutting down\n";
            tasks_.close();
            
            for (auto& thd : threads_)
            {
                if (thd.joinable())
                {
                    // std::cout << "Joining thread " << thd.get_id() << "\n";
                    thd.join();
                }
            }

            // std::cout << "ThreadPool shutdown complete\n";
            assert(tasks_.empty());
        }

        template <typename F>
        decltype(auto) submit(F&& f)
        {
            using ReturnType = std::invoke_result_t<F>;
            using PackagedTask = std::packaged_task<ReturnType()>;

            PackagedTask ptask(std::forward<F>(f));
            auto future = ptask.get_future();

            [[maybe_unused]] bool result = tasks_.push(std::move(ptask));
            assert(result);

            return future;
        }

        template <typename F>
        auto run_async(F&& f)
        {
            using ReturnType = std::invoke_result_t<F>;

            struct RunAwaitable
            {
                F func_;
                ThreadPool& pool_;
                std::optional<ReturnType> result_;

                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> awaiting_coroutine)
                {
                    auto task_wrapper = [this, awaiting_coroutine] () mutable {
                        result_ = std::invoke(func_);
                        awaiting_coroutine.resume();
                    };

                    [[maybe_unused]] bool result = pool_.tasks_.push(std::move(task_wrapper));
                    assert(result);
                }

                decltype(auto) await_resume() { return *result_; }
            };

            return RunAwaitable{std::forward<F>(f), *this};
        }

    private:
        ConcurrentQueue<Task> tasks_;
        std::vector<std::thread> threads_;

        void run()
        {
            // std::cout << "Thread " << std::this_thread::get_id() << " started\n";

            while (true)
            {
                std::optional<Task> task = tasks_.pop();
                if (task.has_value())
                {
                    // std::cout << "Thread " << std::this_thread::get_id() << " executing task\n";
                    (*task)();
                }
                else
                {
                    // std::cout << "Thread " << std::this_thread::get_id() << " received shutdown signal\n";
                    break;
                }
            }
            // std::cout << "Thread " << std::this_thread::get_id() << " stopping\n";
        }
    };
} // namespace AsyncLab

#endif // THREAD_POOL_HPP