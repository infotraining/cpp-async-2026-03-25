#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <condition_variable>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <vector>
#include <chrono>

#include "concurrent_queue.hpp"

using namespace std::literals;

TEST_CASE("ConcurrentQueue - basic operations")
{
    ConcurrentQueue<int> queue;

    SECTION("push and pop")
    {
        queue.push(1);
        queue.push(2);
        queue.push(3);

        std::optional<int> value;
        value = queue.pop();
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 1);

        value = queue.pop();
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 2);

        value = queue.pop();
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 3);
    }

    SECTION("try_pop")
    {
        queue.push(10);
        int value;

        QueueErrc errc = queue.try_pop(value);
        REQUIRE(errc == QueueErrc::Success);
        REQUIRE(value == 10);

        errc = queue.try_pop(value);
        REQUIRE(errc == QueueErrc::Empty);
    }

	SECTION("empty")
    {
        REQUIRE(queue.empty());

        queue.push(42);
        REQUIRE_FALSE(queue.empty());

        int value;
        std::optional<int> opt_value = queue.pop();
        REQUIRE(queue.empty());
    }

	SECTION("close")
    {
        SECTION("push after close")
        {
            queue.close();
            bool result = queue.push(100);
            REQUIRE_FALSE(result);
        }

        SECTION("try_pop after close")
        {
            queue.close();
            int value;
            QueueErrc errc = queue.try_pop(value);
            REQUIRE(errc == QueueErrc::Closed);
        }

        SECTION("pop after close")
        {
            queue.close();
            std::optional<int> value = queue.pop();
            REQUIRE_FALSE(value.has_value());
        }

        SECTION("close on non-empty queue")
        {
            queue.push(1);
            queue.push(2);
            queue.close();

            std::optional<int> value = queue.pop();
            REQUIRE(value.has_value());
            REQUIRE(value.value() == 1);

            value = queue.pop();
            REQUIRE(value.has_value());
            REQUIRE(value.value() == 2);

            value = queue.pop();
            REQUIRE_FALSE(value.has_value());

            QueueErrc errc = queue.try_pop(value.emplace());
            REQUIRE(errc == QueueErrc::Closed);
        }

		SECTION("all waiting threads are unblocked on close")
		{
			std::thread t1([&queue] {
				std::optional<int> value = queue.pop();
				REQUIRE_FALSE(value.has_value());
			});

			std::thread t2([&queue] {
				std::optional<int> value = queue.pop();
				REQUIRE_FALSE(value.has_value());
			});

			std::this_thread::sleep_for(200ms);
			queue.close();

			t1.join();
			t2.join();
		}
    }
}

TEST_CASE("ConcurrentQueue - close in multithreading")
{
    ConcurrentQueue<int> queue;

    std::atomic<int> items_popped{0};

    std::thread producer([&queue] {
		for (int i = 0; i < 50; ++i) {
			queue.push(i);
			std::this_thread::sleep_for(5ms);
		} 

		queue.close();
	});

    std::thread consumer([&queue, &items_popped] {
        while (true)
        {
            std::this_thread::sleep_for(10ms);
            std::optional<int> value = queue.pop();
            if (!value.has_value())
            {
                break;
            }
            ++items_popped;
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(items_popped == 50);
	REQUIRE(queue.empty());
	REQUIRE(queue.is_closed());
}
