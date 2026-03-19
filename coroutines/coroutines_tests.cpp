#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <coroutine>
#include <generator>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

std::generator<int> generate_numbers(int count)
{
    for (int i = 0; i < count; ++i)
    {
        co_yield i;
    }
}

TEST_CASE("Generator C++23", "[coroutines]")
{
    std::vector<int> numbers;
    for (int number : generate_numbers(10))
    {
        numbers.push_back(number);
    }

    REQUIRE(numbers == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
}
