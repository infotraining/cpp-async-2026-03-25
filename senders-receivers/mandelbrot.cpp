#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <complex>
#include <concepts>
#include <execution>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numeric>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <vector>

#if __cpp_lib_mdspan
#include <mdspan>

using namespace std;

using Complex = complex<double>;

auto scaler(int min_from, int max_from, double min_to, double max_to)
{
    const int width_from{max_from - min_from};
    const double mid_from{(width_from / 2.0) + min_from};

    const double width_to{max_to - min_to};
    const double mid_to{(width_to / 2.0) + min_to};

    return [=](int from) {
        return double(from - mid_from) / width_from * width_to + mid_to;
        ;
    };
}

auto scaled_cmplx(std::invocable<int> auto scaler_x, std::invocable<int> auto scaler_y)
{
    return [=](int x, int y) {
        return Complex{scaler_x(x), scaler_y(y)};
    };
}

size_t mandelbrot_iterations(Complex c, int max_iterations = 10'000)
{
    Complex z{};
    size_t iterations{};

    while (std::abs(z) < 2 && iterations < max_iterations)
    {
        ++iterations;
        z = pow(z, 2) + c;
    }

    return iterations;
}

void print_mandelbrot(const vector<size_t>& v, int width)
{
    auto binfunc = [width, n{0}](auto output_it, auto mdlbrt_iterations) mutable {
        *++output_it = (std::cmp_greater(mdlbrt_iterations, 50) ? '*' : ' ');
        if (++n % width == 0)
            ++output_it = '\n';
        return output_it;
    };

    [[maybe_unused]] auto it = accumulate(begin(v), end(v), ostream_iterator<char>{cout}, binfunc);
}

template <typename Extents>
void print_mandelbrot(const mdspan<size_t, Extents>& canvas)
{
    for (size_t j = 0; j < canvas.extent(1); ++j)
    {
        for (size_t i = 0; i < canvas.extent(0); ++i)
        {
            cout << (canvas[i, j] > 50 ? '*' : ' ');
        }
        cout << '\n';
    }
}

TEST_CASE("Mandelbrot set", "[stdexec][mandelbrot][slow]")
{
    constexpr int width{100};
    constexpr int height{40};

    auto scale = scaled_cmplx(
        scaler(0, width, -2.0, 1.0),
        scaler(0, height, -1.0, 1.0));

    auto i_to_xy = [=](int i) {
        return scale(i % width, i / width);
    };

    auto to_iteration_count = [=](size_t i) { return mandelbrot_iterations(i_to_xy(i)); };

    SECTION("single thread")
    {
        vector<size_t> pixels(width * height);
        mdspan canvas{pixels.data(), width, height};

        auto start = chrono::high_resolution_clock::now();

        for(auto i = 0UZ; i < canvas.extent(0); ++i)
        {
            for(auto j = 0UZ; j < canvas.extent(1); ++j)
            {
                canvas[i, j] = mandelbrot_iterations(scale(i, j));
            }
        }

        auto end = chrono::high_resolution_clock::now();

        cout << "Time elapsed: " << chrono::duration_cast<chrono::milliseconds>(end - start).count() << "ms" << endl;

        print_mandelbrot(canvas);
    }

    SECTION("parallel STL")
    {
        vector<size_t> pixels(width * height);
        iota(begin(pixels), end(pixels), 0);

        auto start = chrono::high_resolution_clock::now();

        transform(std::execution::par_unseq, begin(pixels), end(pixels), begin(pixels), to_iteration_count);

        auto end = chrono::high_resolution_clock::now();

        cout << "Time elapsed: " << chrono::duration_cast<chrono::milliseconds>(end - start).count() << "ms" << endl;

        print_mandelbrot(pixels, width);
    }

    SECTION("senders/receivers")
    {
        const auto threads_count = std::thread::hardware_concurrency();
        exec::static_thread_pool thread_pool{threads_count};

        auto cpu_scheduler = thread_pool.get_scheduler();

        vector<size_t> pixels(width * height);
        mdspan canvas{pixels.data(), width, height};

        auto start = chrono::high_resolution_clock::now();

        auto calculate_row = [&](size_t j) {
            for (size_t i = 0; i < canvas.extent(0); ++i)
            {
                canvas[i, j] = mandelbrot_iterations(scale(i, j));
            }
        };

        stdexec::sender auto work = 
            stdexec::schedule(cpu_scheduler) 
            | stdexec::bulk(height, calculate_row);
        stdexec::sync_wait(std::move(work));

        auto end = chrono::high_resolution_clock::now();

        cout << "Time elapsed: " << chrono::duration_cast<chrono::milliseconds>(end - start).count() << "ms" << endl;

        print_mandelbrot(canvas);
    }
}

#endif // __cpp_lib_mdspan