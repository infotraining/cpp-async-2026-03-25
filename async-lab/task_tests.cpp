#include "concepts.hpp"
#include "sync_wait.hpp"
#include "task.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace AsyncLab;
using namespace std::literals;

TEST_CASE("Task")
{
    SECTION("returns value by co_return")
    {
        auto task = []() -> Task<int> {
            co_return 42;
        }();

        int result = AsyncLab::sync_wait(std::move(task));
        REQUIRE(result == 42);
    }

    SECTION("can be co_awaited")
    {
        auto task_1 = []() -> Task<int> {
            co_return 42;
        };

        auto task_2 = [task_1](int n) -> Task<std::string> {
            int value = co_await task_1();
            co_return std::to_string(n * value);
        };

        std::string result = sync_wait(task_2(2));

        REQUIRE(result == "84"s);
    }
}

TEST_CASE("sync_wait")
{
    SECTION("propagates exceptions from Task")
    {
        auto task = []() -> AsyncLab::Task<int> {
            throw std::runtime_error("Test exception");
            co_return 0; // Unreachable, but required for return type
        }();

        REQUIRE_THROWS_AS(AsyncLab::sync_wait(std::move(task)), std::runtime_error);
    }

    SECTION("propagates exception from nested Tasks")
    {
        auto task_1 = [](int id) -> Task<int> {
            throw std::runtime_error("Test exception: id="s + std::to_string(id));
            co_return 42;
        };

        auto task_2 = [=]() -> Task<std::string> {
            int value = co_await task_1(13);

            co_return "test";
        }();

        REQUIRE_THROWS_AS(sync_wait(std::move(task_2)), std::runtime_error);
    }

    SECTION("Task<void>")
    {
        int value{};

        auto task = [](int& value) -> Task<void> {
            value = 42;
            co_return;
        };

        REQUIRE(value == 0);

        sync_wait(task(value));

        REQUIRE(value == 42);
    }
}