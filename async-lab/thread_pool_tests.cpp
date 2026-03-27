#include "concurrent_queue.hpp"
#include "thread_pool.hpp"
#include "task.hpp"
#include "sync_wait.hpp"

#include <catch2/catch_test_macros.hpp>
#include <iostream>

using namespace std::literals;

using namespace AsyncLab;

TEST_CASE("ThreadPool")
{
    SECTION("submit and shutdown")
    {
        std::atomic<int> counter{0};
        {
            ThreadPool pool{4};

            auto submit_tasks = [&pool, &counter](int num_tasks) {
                 for (int i = 0; i < num_tasks; ++i)
                 {
                     pool.submit([&counter] {
                         std::this_thread::sleep_for(std::chrono::milliseconds(100));
                         ++counter;
                     });
                 }
            };

            {
                std::vector<std::jthread> submitters;

                for (int i = 0; i < 10; ++i)
                {
                    submitters.emplace_back(submit_tasks, 25);
                }
            }
        }

        REQUIRE(counter == 250);
    }

    SECTION("submit returns future")
    {
        ThreadPool pool{2};

        auto future_result = pool.submit([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return 42;
        });

        REQUIRE(future_result.get() == 42);
    }
}

TEST_CASE("ThreadPool - submit_task")
{
    using namespace AsyncLab;

    SECTION("submit_task executes function in pool and returns awaitable")
    {
        ThreadPool thd_pool{4};

        auto calculate = []() {
            std::cout << "Calculation started on thread " << std::this_thread::get_id() << std::endl;
            
            return 42;
        };

        auto task = [=, &thd_pool]() -> Task<std::string> {
            std::cout << "Task started on thread " << std::this_thread::get_id() << std::endl;

            int result_1 = co_await thd_pool.run_async(calculate);
            
            std::cout << "Task received result " << result_1 << " on thread " << std::this_thread::get_id() << std::endl;

            int result_2 = co_await thd_pool.run_async([=] {
                std::cout << "Nested calculation started on thread " << std::this_thread::get_id() << std::endl;
                return result_1 * 2;
            });

            std::cout << "Task received next result " << result_2 << " on thread " << std::this_thread::get_id() << std::endl;

            co_return std::to_string(result_1) + " and " + std::to_string(result_2);
        };

        std::string result = AsyncLab::sync_wait(task());
        REQUIRE(result == "42 and 84");
    }
}