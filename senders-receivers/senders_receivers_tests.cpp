#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <deque>
#include <exec/single_thread_context.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/task.hpp>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <stdexec/execution.hpp>
#include <string>
#include <vector>
#include <syncstream>

namespace execution = stdexec;
using namespace exec;

TEST_CASE("Senders/Receivers")
{
    SECTION("basics")
    {
        static_thread_pool thread_pool{8};

        execution::scheduler auto thd_pool = thread_pool.get_scheduler();

        execution::sender auto start = execution::schedule(thd_pool);

        execution::sender auto hi = execution::then(start, [] {
            std::cout << "Hello, World! Have an int." << std::endl;
            return 13;
        });

        execution::sender auto add_42 = execution::then(hi, [](int arg) {
            std::cout << "Adding 42 to " << arg << std::endl;
            return arg + 42;
        });

        // lazy start with sync_wait
        auto [result] = execution::sync_wait(add_42).value();

        CHECK(result == 55);
    }

    SECTION("pipes")
    {
        static_thread_pool thread_pool{8};

        execution::scheduler auto thd_pool = thread_pool.get_scheduler();

        execution::sender auto algorithm = execution::schedule(thd_pool)
            | execution::then([] {
                  std::cout << "Hello, World! Have an int." << std::endl;
                  return 13;
              })
            | execution::then([](int arg) {
                  std::cout << "Adding 42 to " << arg << std::endl;
                  return arg + 42;
              });

        // lazy start with sync_wait
        auto [result] = execution::sync_wait(algorithm).value();

        CHECK(result == 55);
    }
}

TEST_CASE("Execution contexts")
{
    exec::single_thread_context thread_context_1;
    exec::single_thread_context thread_context_2;

    execution::scheduler auto scheduler_1 = thread_context_1.get_scheduler();
    execution::scheduler auto scheduler_2 = thread_context_2.get_scheduler();

    auto step_1 = [](int i) {
        std::cout << "Work#1 on thread " << std::this_thread::get_id() << std::endl;
        return i * 2;
    };

    auto step_2 = [](int i) {
        std::cout << "Work#2 on thread " << std::this_thread::get_id() << std::endl;
        return i + 1;
    };

    execution::sender auto work = execution::starts_on(scheduler_1, execution::just(42))
        | execution::then(step_1)
        | execution::continues_on(scheduler_2)
        | execution::then(step_2);

    auto [result] = execution::sync_wait(std::move(work)).value();
}

TEST_CASE("When all")
{
    using namespace stdexec;

    static_thread_pool thread_pool{3};

    auto scheduler = thread_pool.get_scheduler();

    auto func = [](int i) -> int {
        return i * i;
    };

    auto work = execution::when_all(
        starts_on(scheduler, just(1) | then(func)),
        starts_on(scheduler, just(2) | then(func)),
        starts_on(scheduler, just(3) | then(func)));

    auto [i, j, k] = sync_wait(std::move(work)).value();
}

TEST_CASE("Senders + Coroutines")
{
    using namespace stdexec;
    using namespace exec;

    static_thread_pool thread_pool{3};

    auto sch = thread_pool.get_scheduler();

    sender auto work = on(sch, just(42) | then([](int x) { return x * x; }));     

    auto coro_task = [](sender auto work) -> exec::task<std::string> {
        int result = co_await work;

        co_return "Value: " + std::to_string(result);
    };

    sender auto sender_task = coro_task(std::move(work)) | then([](std::string s) { return s + "!!!"; });

    auto [result] = sync_wait(std::move(sender_task)).value();

    std::cout << "Result: " << result << "\n";
}