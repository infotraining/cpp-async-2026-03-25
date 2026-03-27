#include "boost/capy.hpp"
#include "boost/corosio/io_context.hpp"
#include "boost/corosio/tcp_acceptor.hpp"
#include "boost/corosio/tcp_socket.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <syncstream>
#include <vector>
#include <filesystem>
#include <fstream>

namespace capy = boost::capy;
namespace corosio = boost::corosio;

capy::task<std::uint64_t> compute_fnv1a(char const* data, std::size_t len)
{
    constexpr std::uint64_t basis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;

    std::uint64_t h = basis;
    for (std::size_t i = 0; i < len; ++i)
    {
        h ^= static_cast<unsigned char>(data[i]);
        h *= prime;
    }

    co_return h;
}

std::string to_hex(std::uint64_t hash)
{
    constexpr char hex_digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        result[i] = hex_digits[hash & 0xf];
        hash >>= 4;
    }
    return result;
}

capy::task<> do_session(corosio::tcp_socket sock, capy::thread_pool& pool)
{
    char buf[4096];

    // 1. Read data from client (on io_context)
    auto [ec, n] = co_await sock.read_some(
        capy::mutable_buffer(buf, sizeof(buf)));

    if (ec)
    {
        sock.close();
        co_return;
    }

    // 2. Switch to thread pool for CPU-bound hash computation,
    //    then automatically resume on io_context when done
    auto hash = co_await capy::run(
        pool.get_executor())(
        compute_fnv1a(buf, n));

    // 3. Send hex result back to client (on io_context)
    auto result = to_hex(hash) + "\n";
    auto [wec, wn] = co_await capy::write(sock, capy::const_buffer(result.data(), result.size()));
    (void)wec;
    (void)wn;

    sock.close();
}

capy::task<> do_accept(corosio::io_context& ioc,
    corosio::tcp_acceptor& acceptor,
    capy::thread_pool& pool)
{
    using namespace std::literals;

    for (;;)
    {
        corosio::tcp_socket peer(ioc);
        auto [ec] = co_await acceptor.accept(peer);

        std::cout << "Accepted connection from " << peer.remote_endpoint().v4_address().to_string() << "\n";

        if (ec)
            break;

        capy::run_async(ioc.get_executor())(
            do_session(std::move(peer), pool));
    }
}

TEST_CASE("Hash server")
{
    size_t port = 8081;

    corosio::io_context ioc;
    capy::thread_pool pool(4);

    corosio::tcp_acceptor acc( ioc, corosio::endpoint(port) );

    std::cout << "Hash server listening on port " << port << "\n";

    capy::run_async(ioc.get_executor())(do_accept( ioc, acc, pool ));

    ioc.run();
    pool.join();
}