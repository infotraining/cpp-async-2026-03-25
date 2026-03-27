#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <execution>
#include <numeric>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <vector>
#include <iostream>

using namespace std;

///////////////////////////////////////////////////////////////////////////
// Monte Carlo PI

uintmax_t calculate_hits(uintmax_t no_of_iterations)
{
    uintmax_t hits = 0;

    std::random_device rd;
    std::mt19937 rnd_gen(rd());
    std::uniform_real_distribution<> distr(0.0, 1.0);

    for (uintmax_t i = 0; i < no_of_iterations; ++i)
    {
        // Generate random point (x, y) in the unit square
        double x = distr(rnd_gen);
        double y = distr(rnd_gen);

        // Check if the point is inside the unit circle
        if (x * x + y * y <= 1.0)
        {
            ++hits;
        }
    }

    return hits;
}

double estimate_pi(uintmax_t no_of_iterations, size_t hits)
{
    double pi_estimate = 4.0 * static_cast<double>(hits) / static_cast<double>(no_of_iterations);
    return pi_estimate;
}

TEST_CASE("Monte Carlo PI", "[stdexec][pi][slow]")
{
    constexpr uintmax_t no_of_iterations = 100'000'000;

    const auto threads_count = std::thread::hardware_concurrency();

    std::cout << "No of cores: " << threads_count << std::endl;

    SECTION("single thread")
    {
        auto start = chrono::high_resolution_clock::now();

        uintmax_t hits = calculate_hits(no_of_iterations);
        double pi_estimate = estimate_pi(no_of_iterations, hits);

        auto end = chrono::high_resolution_clock::now();
        
        cout << "Time elapsed (single thread): " << chrono::duration_cast<chrono::milliseconds>(end - start).count() << "ms" << endl;
        cout << "Estimated PI: " << pi_estimate << endl;
    }

    SECTION("senders/receivers")
    {
        using namespace stdexec;

        exec::static_thread_pool thread_pool(threads_count);
        auto cpu_scheduler = thread_pool.get_scheduler();

        auto start = chrono::high_resolution_clock::now();

        sender auto sender_pi = 
            starts_on(cpu_scheduler, just(std::vector<uintmax_t>(threads_count)))
            | bulk(std::execution::par, threads_count, [iterations_per_thread = no_of_iterations / threads_count](size_t i, std::vector<uintmax_t>& partial_hits) {
                partial_hits[i] = calculate_hits(iterations_per_thread);
            })
            | then([no_of_iterations](std::vector<uintmax_t> partial_hits) {
                auto hits = std::accumulate(partial_hits.begin(), partial_hits.end(), uintmax_t{0});
                return estimate_pi(no_of_iterations, hits);
            });

        auto [estimated_pi] = stdexec::sync_wait(sender_pi).value();

        auto end = chrono::high_resolution_clock::now();

        cout << "Time elapsed (senders/receivers): " << chrono::duration_cast<chrono::milliseconds>(end - start).count() << "ms" << endl;
        cout << "Estimated PI: " << estimated_pi << endl;
    }
}