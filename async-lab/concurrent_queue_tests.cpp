#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <iostream>
#include <algorithm>
#include <numeric>

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
}

