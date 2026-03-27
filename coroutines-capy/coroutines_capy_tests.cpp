#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <random>
#include <syncstream>
#include <latch>
#include "task_logger.hpp"

#include <boost/capy.hpp>

using namespace std::literals;

auto synced_cout = []() { return std::osyncstream(std::cout); };

namespace capy = boost::capy;

capy::io_task<int> find_anwser()
{
	synced_cout() << "Thinking about the meaning of life..."
			  << " on thread " << std::this_thread::get_id() << std::endl;

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(250, 1000);
	auto thinking_time = std::chrono::milliseconds(dis(gen));

	co_await capy::delay(thinking_time);

	co_return capy::io_result<int>{{}, 42};
}

capy::io_task<std::string> meaning_of_life(std::string name)
{
	synced_cout() << name << " starts thinking..."
			  << " on thread " << std::this_thread::get_id() << std::endl;

	auto [ec, answer] = co_await find_anwser();

	co_return capy::io_result<std::string>{{}, name + " says the meaning of life is " + std::to_string(answer)};
}

capy::task<> run_thinking()
{
	synced_cout() << "Starting to think..."
			  << " on thread " << std::this_thread::get_id() << std::endl;

	auto [ec, anwser_1, anwser_2, anwser_3] = co_await capy::when_all(
		meaning_of_life("Socrates"),
		meaning_of_life("Plato"),
		meaning_of_life("Aristotle"));

	synced_cout() << "All philosophers have found their answers:" << std::endl;
	synced_cout() << anwser_1 << std::endl;
	synced_cout() << anwser_2 << std::endl;
	synced_cout() << anwser_3 << std::endl;
}

TEST_CASE("Capy - hello world")
{
	capy::thread_pool thd_pool;
	capy::Executor auto exec = thd_pool.get_executor();

	std::latch is_done(1);

	capy::run_async(exec,
		[&]() { synced_cout() << "Async task completed..." << std::endl; is_done.count_down(); },
		[&](std::exception_ptr ep) {
			try 
			{
				if (ep) 
					std::rethrow_exception(ep);
			} catch (const std::exception& e) 
			{
				synced_cout() << "Async task threw an exception: " << e.what() << std::endl;
				is_done.count_down();
			}
		}
	)(run_thinking());

	is_done.wait();
	thd_pool.join();
}


capy::task<int> calculate(int n)
{
	std::stop_token tkn = co_await capy::this_coro::stop_token;

	for(int i = 0; i < 10; ++i)
	{
		if (tkn.stop_requested())
			co_return -1;

		co_await capy::delay(250ms);
		std::cout << "Step#" << i << std::endl;
	}

	co_return n * n;
}

capy::io_task<int> io_calculate(int n)
{
	std::stop_token tkn = co_await capy::this_coro::stop_token;

	for(int i = 0; i < 10; ++i)
	{
		if (tkn.stop_requested())
			co_return {capy::make_error_code(capy::error::canceled), -1};

		co_await capy::delay(250ms);
		std::cout << "Step#" << i << std::endl;
	}

	co_return capy::io_result{{}, n * n};
}

capy::task<std::optional<int>> service(int n)
{
	auto [ec, result] = co_await capy::timeout(io_calculate(n), 1s);

	if (ec)
	{
		std::cout << "Timeout fired...\n";
		co_return std::nullopt;
	}

	co_return result;
}

TEST_CASE("Stop token - coroutines", "[capy][stop_token]")
{
	capy::thread_pool thd_pool;
	capy::Executor auto ex = thd_pool.get_executor();

	std::stop_source stop_src;

	capy::run_async(ex,
		stop_src.get_token(),
		[](int result){  std::cout << "calc(13) = " << result << "\n"; },
		[](auto eptr) { std::cout << "Exception!!!" << "\n"; }
	)(calculate(13));

	capy::run_async(ex,
		stop_src.get_token(),
		[](int result){  std::cout << "calc(42) = " << result << "\n"; },
		[](auto eptr) { std::cout << "Exception!!!" << "\n"; }
	)(calculate(42));

	std::this_thread::sleep_for(2s);

	stop_src.request_stop();

	thd_pool.join();
}

//////////////////////////////////////////////////////////////////
// When any

using capy::io_result;

auto int_task(int id, int value) -> capy::task<io_result<int>>
{
    LOGGER << std::format("Task#{} started in THD#{}\n", id, std::this_thread::get_id());
    co_return {{}, value};
};

auto string_task(int id, std::string str) -> capy::io_task<std::string>
{
    LOGGER << std::format("Task#{} started in THD#{}\n", id, std::this_thread::get_id());
    co_return {{}, str};
};

template <typename T>
auto value_task(int id, T value) -> capy::io_task<T>
{
    LOGGER << std::format("Task#{} started in THD#{}\n", id, std::this_thread::get_id());
    co_return {{}, value};
};

template <typename T>
auto long_value_task(int id, T value, std::chrono::milliseconds duration) -> capy::io_task<T>
{
    LOGGER << std::format("Task#{} started in THD#{}\n", id, std::this_thread::get_id());
    co_await capy::delay(duration);
    co_return {{}, value};
};

auto void_task(int id) -> capy::io_task<>
{
    LOGGER << std::format("Task#{} started in THD#{}\n", id, std::this_thread::get_id());
    co_return std::error_code{};
};

auto exception_task(int id, std::chrono::milliseconds duration = 0ms) -> capy::io_task<std::string>
{
    LOGGER << std::format("Task#{} started in THD#{}\n", id, std::this_thread::get_id());
    co_await capy::delay(duration);
    throw std::runtime_error(std::format("Task#{} encountered an error", id));
    co_return {{}, std::format("Task#{} completed", id)};
}

template <typename T>
auto value_exception_task(int id, T value, std::chrono::milliseconds duration = 0ms) -> capy::io_task<T>
{
    LOGGER << std::format("Task#{} started in THD#{}\n", id, std::this_thread::get_id());
    co_await capy::delay(duration);
    throw std::runtime_error(std::format("Task#{} encountered an error", id));
    co_return {{}, value};
}

auto cancellable_task(int id, std::chrono::milliseconds duration = 100ms) -> capy::io_task<std::optional<std::string>>
{
    LOGGER << std::format("Task#{} started in THD#{}\n", id, std::this_thread::get_id());

    std::stop_token stop_tkn = co_await capy::this_coro::stop_token;

    for (int i = 0; i < 5; ++i)
    {
        LOGGER << std::format("Task {} working in THD#{}\n", id, std::this_thread::get_id());
        co_await capy::delay(duration);

        if (stop_tkn.stop_requested())
        {
            LOGGER << std::format("Task {} received stop request in THD#{}\n", id, std::this_thread::get_id());
            co_return {{}, std::nullopt}; // Indicate cancellation
        }
    }

    co_return {{}, std::format("Task {} completed", id)};
};


template <typename... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};

template <typename TVariant>
struct Visitor
{
    TVariant& value_;

    explicit Visitor(TVariant& value)
        : value_(value)
    { }

    decltype(auto) with(auto&&... handlers)
    {
        return std::visit(overloaded{std::forward<decltype(handlers)>(handlers)...}, value_);
    }
};

template <typename TVariant>
auto visit(TVariant& variant)
{
    return Visitor<TVariant>(variant);
}

TEST_CASE("When any", "[capy][task]")
{
    SECTION("First completed task wins")
    {
        auto composed_task = []() -> capy::task<void> {
            std::variant<std::error_code, int, std::string, std::string> result = co_await capy::when_any(
                long_value_task<int>(1, 42, 200ms),
                long_value_task<std::string>(2, "Task 2 has completed first!", 100ms),
                long_value_task<std::string>(3, "Task 3 completed", 300ms));

            REQUIRE(result.index() == 2); // Task 2 should complete first

            std::visit(
                overloaded{
                    [](std::error_code ec) {
                        LOGGER << std::format("Task failed with error: {}\n", ec.message());
                    },
                    [](auto&& value) {
                        LOGGER << std::format("Result: {}\n", value);
                    }},
                result);
        };

        capy::thread_pool thd_pool;
        capy::run_async(thd_pool.get_executor())(composed_task());

        thd_pool.join();
    }

    SECTION("Other tasks are cancelled")
    {
        auto task = [](int id) -> capy::task<std::string> {
            co_await capy::delay(150ms);
            co_return std::format("Task {} completed", id);
        };

        auto composed_task = []() -> capy::task<void> {
            std::variant<std::error_code, std::optional<std::string>, int, std::optional<std::string>> result = 
				co_await capy::when_any(cancellable_task(1, 50ms), long_value_task(2, 20, 25ms), cancellable_task(3, 75ms));
            REQUIRE(result.index() == 2);

            // std::visit(overloaded{
            //         [](std::error_code ec) { LOGGER << std::format("Task failed with error: {}\n", ec.message()); },
            //         [](int value) { LOGGER << std::format("Result: {}\n", value); },
            //         [](const std::optional<std::string>& value) { LOGGER << std::format("Result: {}\n", value.value_or("(none)")); }},
            //     result
            // );

            visit(result).with(
                [](std::error_code ec) { LOGGER << std::format("Task failed with error: {}\n", ec.message()); },
                [](int value) { LOGGER << std::format("Result: {}\n", value); },
                [](const std::optional<std::string>& value) { LOGGER << std::format("Result: {}\n", value.value_or("(none)")); } 
            );
        };

        capy::thread_pool thd_pool{1};
        capy::Executor auto ex = thd_pool.get_executor();
        capy::run_async(ex)(composed_task());

        thd_pool.join();
    }
}