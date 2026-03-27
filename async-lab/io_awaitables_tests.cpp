#include "io_awaitables.hpp"
#include "task.hpp"
#include "sync_wait.hpp"
#include "file_descriptor.hpp"
#include "reactor.hpp"

#include <catch2/catch_test_macros.hpp>

using AsyncLab::ConstBuffer;
using AsyncLab::MutableBuffer;

using AsyncLab::async_read;
using AsyncLab::async_write;

/////////////////////////////////////////////////////////////////////
// Tests for async_read and async_write awaitables - for socket I/O on epoll
/////////////////////////////////////////////////////////////////////

TEST_CASE("Reactor - async echo using socketpair", "[reactor][network]")
{
    int fds[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) == 0);

    AsyncLab::Epoll::Reactor reactor;
    reactor.run();

    const std::string message = "hello async echo";

    auto echo_task = [&]() -> AsyncLab::Task<std::string> {
        // 1. Client writes message into fds[1]; data appears at fds[0].
        ConstBuffer wbuf{message.data(), message.size()};
        auto [err, bytes_written] = co_await async_write(reactor, fds[1], wbuf);
        REQUIRE(bytes_written == message.size());
        REQUIRE(err == std::errc{});

        // 2. Echo server: read from fds[0] then write same bytes back.
        char rbuf[256]{};
        MutableBuffer rbuf_span{rbuf};
        auto [read_err, bytes_read] = co_await async_read(reactor, fds[0], rbuf_span);
        REQUIRE(bytes_read > 0);
        REQUIRE(read_err == std::errc{});
        auto [write_err, echo_written] = co_await async_write(reactor, fds[0], ConstBuffer{rbuf, bytes_read});
        REQUIRE(echo_written > 0);
        REQUIRE(write_err == std::errc{});

        // 3. Client reads the echoed reply from fds[1].
        char storage[256]{};
        MutableBuffer echo_buffer{storage};
        auto [echo_err, echo_read] = co_await async_read(reactor, fds[1], echo_buffer);
        REQUIRE(echo_err == std::errc{});
        REQUIRE(echo_read > 0);

        co_return std::string(storage, echo_read);
    };

    auto received = AsyncLab::sync_wait(echo_task());
    REQUIRE(received == message);

    reactor.stop();
    ::close(fds[0]);
    ::close(fds[1]);
}

namespace TestHelpers
{
    // RAII guard: remove the file when the test ends regardless of outcome.
    struct FileGuard
    {
        std::filesystem::path path_;

        FileGuard(std::filesystem::path path) noexcept
            : path_(std::move(path))
        { }

        FileGuard(const FileGuard&) = delete;
        FileGuard& operator=(const FileGuard&) = delete;

        std::filesystem::path get() const { return path_; }

        ~FileGuard()
        {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
    };

    [[nodiscard]] FileGuard write_test_file(const std::filesystem::path& path, const std::string& content)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            throw std::runtime_error("Failed to create test file");
        out.write(content.data(), static_cast<std::streamsize>(content.size()));

        return FileGuard(path);
    }

    [[nodiscard]] std::string read_file_content(const std::filesystem::path& path)
    {
        std::string out_content;
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open())
            throw std::runtime_error("Failed to open test file for reading");
        out_content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        return out_content;
    }
}

/////////////////////////////////////////////////////////////////////
// Awaitables for async read/write via Reactor with io_uring
/////////////////////////////////////////////////////////////////////

using AsyncLab::FileDescriptor;

TEST_CASE("Reactor with io_uring - async file read/write with promise", "[reactor][io_uring]")
{
    namespace fs = std::filesystem;

    // Create a temp file using std::filesystem; an ofstream seeds its content.
    const fs::path tmp_path = fs::temp_directory_path() / "reactor_test.tmp";
    const std::string test_data = "Hello, io_uring!";

    TestHelpers::FileGuard file_guard = TestHelpers::write_test_file(tmp_path, test_data);

    AsyncLab::EpollWithIOUring::Reactor reactor;
    reactor.run();

    // Open the file for reading.
    FileDescriptor file_fd{::open(tmp_path.c_str(), O_RDONLY | O_CLOEXEC)};

    // Async read the data back via io_uring.
    char read_buffer[256]{};
    std::promise<std::string> read_promise;
    reactor.read_file(file_fd.get(), read_buffer, file_fd.size(), 0,
        [&read_promise, &read_buffer](int res) {
            if (res >= 0)
                read_promise.set_value(std::string(read_buffer, static_cast<size_t>(res)));
            else
                read_promise.set_exception(
                    std::make_exception_ptr(std::runtime_error("Async read failed")));
        });

    auto read_result = read_promise.get_future().get();
    REQUIRE(read_result == test_data);

    reactor.stop();
}

TEST_CASE("Reactor with io_uring - awaitable async_io_read", "[reactor][io_uring]")
{
    using AsyncLab::EpollWithIOUring::async_io_read;
    using AsyncLab::EpollWithIOUring::async_io_write;

    namespace fs = std::filesystem;
    using namespace TestHelpers;

    // Create a temp file using std::filesystem; an ofstream seeds its content.
    const fs::path tmp_path = fs::temp_directory_path() / "reactor_test.tmp";
    const std::string test_data = "Hello, io_uring!\nThis file is used to test async read/write via io_uring in the Reactor.";
    FileGuard file_guard = TestHelpers::write_test_file(tmp_path, test_data);

    AsyncLab::EpollWithIOUring::Reactor reactor;
    reactor.run();

    // Open the file for reading and writing.
    FileDescriptor file_fd{::open(tmp_path.c_str(), O_RDWR | O_CLOEXEC)};

    // Async read the data back via io_uring.
    auto async_read_task = [&]() -> AsyncLab::Task<std::string> {
        std::vector<char> storage(file_fd.size());
        MutableBuffer read_buffer{storage};

        auto [err, bytes_read] = co_await async_io_read(reactor, file_fd.get(), read_buffer);

        co_return std::string(read_buffer.data(), bytes_read);
    };

    auto read_result = AsyncLab::sync_wait(async_read_task());

    REQUIRE(read_result == test_data);
}

TEST_CASE("Reactor with io_uring - awaitable async_io_write", "[reactor][io_uring]")
{
    namespace fs = std::filesystem;
    using namespace TestHelpers;

    // Create a temp file using std::filesystem; an ofstream seeds its content.
    const fs::path tmp_path = fs::temp_directory_path() / "reactor_test.tmp";
    const std::string test_data = "This content was written asynchronously via io_uring!";

    AsyncLab::EpollWithIOUring::Reactor reactor;
    reactor.run();

    // Open the file for reading and writing.
    FileDescriptor file_fd{::open(tmp_path.c_str(), O_RDWR | O_CLOEXEC | O_CREAT | O_TRUNC, 0644)};

    // Async write new data to the file via io_uring.
    auto async_write_task = [&]() -> AsyncLab::Task<void> {
        ConstBuffer write_buffer{test_data};

        auto [err, bytes_written] = co_await async_io_write(reactor, file_fd.get(), write_buffer);

        REQUIRE(bytes_written == write_buffer.size());
        REQUIRE(err == std::errc{});
    };

    AsyncLab::sync_wait(async_write_task());

    file_fd.close(); // Ensure data is flushed and file is closed before reading back.

    std::string actual_content = TestHelpers::read_file_content(tmp_path);
    REQUIRE(actual_content == test_data);

    fs::remove(tmp_path);
}