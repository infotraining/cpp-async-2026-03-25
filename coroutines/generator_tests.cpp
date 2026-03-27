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
#include <thread>
#include <utility>
#include <vector>

template <typename T>
class Generator
{
public:
    struct promise_type
    {
        T value_;
        std::exception_ptr exception_;

        Generator get_return_object()
        {
            return Generator(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() { return {}; }

        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(T value)
        {
            value_ = std::move(value);

            return {};
        }

        void unhandled_exception()
        {
            exception_ = std::current_exception();
        }

        void return_void() { }
    };

    Generator(auto handle)
        : handle_{handle}
    { }

    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    Generator(Generator&& other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    Generator& operator=(Generator&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
                handle_.destroy();

            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~Generator()
    {
        if (handle_)
            handle_.destroy();
    }

    struct iterator
    {
        std::coroutine_handle<promise_type> handle_;

        using value_type = T;
        using reference = T&;

        iterator()
            : handle_{nullptr}
        { }

        explicit iterator(std::coroutine_handle<promise_type> handle)
            : handle_{handle}
        { }

        reference operator*() const
        {
            return handle_.promise().value_;
        }

        iterator& operator++() // ++it
        {
            handle_.resume();

            if (handle_.done())
            {
                auto& promise = handle_.promise();

                if (promise.exception_)
                    std::rethrow_exception(promise.exception_);

                handle_ = nullptr;
            }

            return *this;
        }

        iterator operator++(int) // it++
        {
            iterator temp = *this;
            ++(*this);
            return temp;
        }

        bool operator==(const iterator& other) const
        {
            return handle_ == other.handle_;
        }
    };

    iterator begin()
    {
        if (handle_ == nullptr)
        {
            std::cout << "+ nullptr" << std::endl;
            return iterator{};
        }

        handle_.resume();

        if (handle_.done())
        {
            auto& promise = handle_.promise();

            if (promise.exception_)
                std::rethrow_exception(promise.exception_);

            handle_ = nullptr;
            return iterator{};
        }

        return iterator{handle_};
    }

    iterator end()
    {
        return iterator{};
    }

private:
    std::coroutine_handle<promise_type> handle_{};
};

// std::generator<int> sequence(int start, int end, int step = 1)
Generator<int> sequence(int start, int end, int step = 1)
{
    for (int i = start; i < end; i += step)
    {
        co_yield i;
    }
}

TEST_CASE("Generator")
{
    SECTION("for with iterator")
    {
        // std::generator<int> gen = sequence(1, 10);
        Generator<int> gen = sequence(1, 10);

        std::vector<int> vec;
        for (auto it = gen.begin(); it != gen.end(); ++it)
        {
            vec.push_back(*it);
        }

        REQUIRE(vec == std::vector{1, 2, 3, 4, 5, 6, 7, 8, 9});
    }

    SECTION("range-based for")
    {
        auto gen = sequence(1, 10);

        std::vector<int> vec;
        for (auto value : gen)
        {
            vec.push_back(value);
        }

        REQUIRE(vec == std::vector{1, 2, 3, 4, 5, 6, 7, 8, 9});
    }
}

auto zip(std::ranges::range auto& rng1, std::ranges::range auto& rng2)
    -> std::generator<
        std::tuple<
            std::ranges::range_value_t<decltype(rng1)>,
            std::ranges::range_value_t<decltype(rng2)>
            >>
{
    auto it1 = rng1.begin();
    auto it2 = rng2.begin();

    for (; it1 != rng1.end() && it2 != rng2.end(); ++it1, ++it2)
    {
        co_yield {*it1, *it2};
    }
}

TEST_CASE("zip")
{
    std::vector<std::string> words = {"one", "two", "three"};
    std::vector numbers = {1, 2, 3};

    for (auto [v1, v2] : zip(words, numbers))
    {
        std::cout << "v1: " << v1 << " v2: " << v2 << "\n";
    }
}