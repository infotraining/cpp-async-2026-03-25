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

#include <boost/capy.hpp>

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
	std::vector<std::string> philosophers = {"Socrates", "Plato", "Aristotle"};

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
