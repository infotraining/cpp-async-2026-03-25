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
#include <vector>
#include <syncstream>

namespace capy = boost::capy;
namespace corosio = boost::corosio;

static auto sync_cout = [] { return std::osyncstream(std::cout); };

std::string format_message(std::string&& msg) // ensures message ends with newline
{
     if (!msg.ends_with("\n"))
            msg += "\n";

    return msg;
}

capy::task<std::string> read_line(capy::Stream auto& stream)
{
    std::vector<unsigned char> storage;
    capy::vector_dynamic_buffer buffer(&storage);

    while (true)
    {
        // Prepare space and read
        auto space = buffer.prepare(256);
        auto [ec, n] = co_await stream.read_some(space);
        buffer.commit(n);
        if (ec)
            throw std::system_error(ec);

        // Search for newline in readable data
        auto data = buffer.data();
        std::string_view sv(
            static_cast<char const*>(data.data()), data.size());

        auto pos = sv.find('\n');
        if (pos != std::string_view::npos)
        {
            std::string line(sv.substr(0, pos));
            buffer.consume(pos + 1); // Include newline
            co_return line;
        }
    }
}

capy::task<> write_line(capy::Stream auto& stream, capy::const_buffer buffer)
{
    int bytes_written = 0;

    std::string_view buffer_sv(static_cast<char const*>(buffer.data()), buffer.size());

    size_t partial_reads_count = 0;
    while (bytes_written < buffer.size())
    {
        auto [ec, n] = co_await stream.write_some(buffer);
        if (ec)
            throw std::system_error(ec);
        bytes_written += n;
        partial_reads_count++;
    }

    sync_cout() << std::format("Finished writing {} bytes in {} partial reads\n", bytes_written, partial_reads_count);

    co_return;
}

capy::task<> echo_server_session(corosio::tcp_socket socket)
{
    for (;;)
    {
        sync_cout() << "Waiting for message from client...\n";
        std::string message_from_client = co_await read_line(socket);

        sync_cout() << "Received message from client: " << message_from_client << "\n";

        message_from_client = format_message(std::move(message_from_client));

        sync_cout() << "Echoing message back to client...\n";
        co_await write_line(socket, capy::make_buffer(message_from_client));
    }

    socket.close();
}

capy::task<> accept_loop(corosio::tcp_acceptor& acc, corosio::io_context& ioc)
{
    auto ep = acc.local_endpoint();
    std::cout << "Listening on port " << ep.port() << "\n";

    for (;;)
    {
        corosio::tcp_socket peer(ioc);
        auto [ec] = co_await acc.accept(peer);

        if (ec)
        {
            sync_cout() << std::format("Accept error: {}\n", ec.message());
            continue;
        }

        auto remote = peer.remote_endpoint();
        sync_cout() << "Connection from ";
        if (remote.is_v4())
            sync_cout() << remote.v4_address();
        else
            sync_cout() << remote.v6_address();
        sync_cout() << ":" << remote.port() << "\n";

        capy::run_async(ioc.get_executor())(echo_server_session(std::move(peer)));
    }
}

capy::task<> echo_client_session(std::string client_name, corosio::tcp_socket socket)
{
    std::array<char, 1024> buffer;

    while (true)
    {
        std::string message;
        sync_cout() << std::format("({}) Enter message to send: \n", client_name);
        std::getline(std::cin, message);

        if (message == "exit")
        {
            std::string goodbye_msg = std::format("Goodbye!!!", client_name);
            goodbye_msg = format_message(std::move(goodbye_msg));
            co_await write_line(socket, capy::make_buffer(goodbye_msg));
            co_return;
        }

        message = format_message(std::move(message));

        sync_cout() << std::format("({}) Sending message: {}\n", client_name, message);
        co_await write_line(socket, capy::make_buffer(message));

        sync_cout() << std::format("({}) Waiting for echo from server...\n", client_name);
        std::string echo_from_server = co_await read_line(socket);
        sync_cout() << std::format("({}) Echo from server: {}\n", client_name, echo_from_server);
    }
}

capy::task<> run_client(std::string client_name, corosio::io_context& ioc, corosio::ipv4_address address, std::uint16_t port)
{
    corosio::tcp_socket socket(ioc);
    socket.open();

    sync_cout() << std::format("({}) Connecting to {}:{}\n", client_name, address.to_string(), port);
    auto [ec] = co_await socket.connect(corosio::endpoint(address, port));

    if (ec)
    {
        sync_cout() << std::format("({}) Connection failed: {}\n", client_name, ec.message());
        throw std::system_error(ec);
    }

    std::string client_address = [&] {
        std::string address;
        if (socket.local_endpoint().is_v4())
            address += socket.local_endpoint().v4_address().to_string();
        else
            address += socket.local_endpoint().v6_address().to_string();

        address += ":" + std::to_string(socket.local_endpoint().port());
        return address;
    }();

    client_name += "@" + client_address;

    sync_cout() << std::format("({}) Connected to server\n", client_name);
    co_await echo_client_session(std::move(client_name), std::move(socket));
}

TEST_CASE("Echo server")
{
    unsigned short port = 8080;

    corosio::io_context ioc;
    corosio::tcp_acceptor acc(ioc, corosio::endpoint(port));

    capy::Executor auto io_ex = ioc.get_executor();

    sync_cout() << std::format("Running echo server on port {}\n", port);
    capy::run_async(io_ex, 
        []() { }, 
        [](std::exception_ptr ep) {
            try
            {
                if (ep)
                    std::rethrow_exception(ep);
            }
            catch (const std::exception& ex)
            {
                sync_cout() << std::format("Server error: {}\n", ex.what());
            } 
        }
    )(accept_loop(acc, ioc));

    // sync_cout() << std::format("Running echo client on port {}\n", port);
    // capy::run_async(io_ex, 
    //     []() { }, 
    //     [](std::exception_ptr ep) {
    //         try
    //         {
    //             if (ep)
    //                 std::rethrow_exception(ep);
    //         }
    //         catch (const std::exception& ex)
    //         {
    //             sync_cout() << std::format("Client error: {}\n", ex.what());
    //         } 
    //     }
    // )(run_client("One", ioc, corosio::ipv4_address::loopback(), port));

    ioc.run();
}

TEST_CASE("Echo client")
{
    unsigned short port = 8080;
    corosio::io_context ioc;
    capy::Executor auto io_ex = ioc.get_executor();

    sync_cout() << std::format("Running echo client on port {}\n", port);
    capy::run_async(io_ex, 
        []() { }, 
        [](std::exception_ptr ep) {
            try
            {
                if (ep)
                    std::rethrow_exception(ep);
            }
            catch (const std::exception& ex)
            {
                sync_cout() << std::format("Client error: {}\n", ex.what());
            } 
        }
    )(run_client("Client", ioc, corosio::ipv4_address::loopback(), port));

    ioc.run();
}