#include <catch2/catch_test_macros.hpp>

#include "reactor.hpp"
#include "task.hpp"
#include "sync_wait.hpp"
#include "file_descriptor.hpp"

using namespace std::literals;

using AsyncLab::Epoll::Reactor;

TEST_CASE("Reactor - one timer", "[reactor][epoll]")
{
    Reactor reactor;
    reactor.run();

    bool timer_triggered = false;

    auto timeout_ms = 100ms;
    auto t_start = std::chrono::steady_clock::now();
    decltype(t_start) t_triggered;
    reactor.add_timer(timeout_ms, [&]() {
        t_triggered = std::chrono::steady_clock::now();
        timer_triggered = true;
    });

    std::this_thread::sleep_for(timeout_ms + 50ms);
    REQUIRE(timer_triggered);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t_triggered - t_start);
    REQUIRE(elapsed >= timeout_ms);

    reactor.stop();
}

TEST_CASE("Reactor - many timers", "[reactor][epoll]")
{
    Reactor reactor;
    reactor.run();

    int timer_triggered = 0;

    auto timer_fd_1 = reactor.add_timer(10ms, [&]() {
        std::cout << "Timer expired after 100ms!\n";
        ++timer_triggered;
    });

    auto timer_fd_2 = reactor.add_timer(20ms, [&]() {
        std::cout << "Timer expired after 200ms!\n";
        ++timer_triggered;
    });

    auto timer_fd_3 = reactor.add_timer(40ms, [&]() {
        std::cout << "Timer expired after 400ms!\n";
        ++timer_triggered;
    });

    std::this_thread::sleep_for(100ms);
    REQUIRE(timer_triggered == 3);

    std::cout << "Stopping reactor...\n";
    reactor.stop();
}

TEST_CASE("Reactor - stop cancels timers", "[reactor][epoll]")
{
    Reactor reactor;
    reactor.run();

    auto timer_fd_1 = reactor.add_timer(200ms, [&]() {
        FAIL("Timer 1 should not have triggered");
    });

    std::this_thread::sleep_for(50ms);
    reactor.stop();
    std::cout << "Stopping reactor...\n";
}

template <typename Rep, typename Period>
auto timeout(Reactor& reactor, std::chrono::duration<Rep, Period> duration)
{
    struct TimerAwaiter
    {
        Reactor& reactor_;
        std::chrono::duration<Rep, Period> duration_;
        std::chrono::steady_clock::time_point start_time_;
        std::optional<int> timer_fd_;

        TimerAwaiter(Reactor& reactor, std::chrono::duration<Rep, Period> duration)
            : reactor_(reactor)
            , duration_(duration)
        { }

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            start_time_ = std::chrono::steady_clock::now();

            timer_fd_ = reactor_.add_timer(duration_, [handle]() {
                handle.resume();
            });
        }

        auto await_resume() noexcept
        {
            if (timer_fd_)
            {
                reactor_.del_fd(*timer_fd_);
            }

            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time_);
        }
    };

    return TimerAwaiter{reactor, duration};
}

TEST_CASE("Reactor - awaitable timeout", "[reactor][epoll]")
{
    Reactor reactor;

    auto timeout_task = [&]() -> AsyncLab::Task<int> {
        std::cout << "Starting timeout task..." << std::endl;
        co_await timeout(reactor, 100ms);
        std::cout << "Tik!" << std::endl;
        co_await timeout(reactor, 100ms);
        std::cout << "Tok!" << std::endl;

        co_return 42;
    };

    reactor.run();

    auto result = AsyncLab::sync_wait(timeout_task());
    REQUIRE(result == 42);

    reactor.stop();
}

TEST_CASE("Reactor - tik tok with coroutines", "[reactor][epoll]")
{
    Reactor reactor;

    auto timeout_task = [&]() -> AsyncLab::Task<int> {
        std::cout << "Starting timeout task..." << std::endl;
        for (int i = 0; i < 10; ++i)
        {
            std::chrono::milliseconds elapsed = co_await timeout(reactor, 250ms);
            std::cout << "Tick#" << (i + 1) << " elapsed: " << elapsed.count() << "ms" << std::endl;
        }

        co_return 42;
    };

    reactor.run();

    auto result = AsyncLab::sync_wait(timeout_task());
    REQUIRE(result == 42);

    reactor.stop();
}

// EPOLLIN: write to one end of a socketpair, verify callback fires and reads data on the other end.
TEST_CASE("Reactor - socketpair EPOLLIN", "[reactor][network]")
{
    int fds[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) == 0);

    Reactor reactor;
    reactor.run();

    std::atomic<bool> data_received{false};
    std::string received_data;

    reactor.add_fd(fds[0], EPOLLIN, [&](uint32_t events) {
        if (events & EPOLLIN)
        {
            char buf[256]{};
            ssize_t n = ::read(fds[0], buf, sizeof(buf) - 1);
            if (n > 0)
            {
                received_data.assign(buf, static_cast<size_t>(n));
                data_received.store(true, std::memory_order_release);
            }
        }
    });

    const std::string msg = "hello reactor";
    ::write(fds[1], msg.c_str(), msg.size());

    auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (!data_received.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(5ms);

    REQUIRE(data_received.load(std::memory_order_acquire));
    REQUIRE(received_data == msg);

    ::close(fds[1]);
    reactor.stop();
    // fds[0] closed by reactor destructor
}

// EPOLLOUT: a fresh socket is immediately writable; verify the callback fires.
TEST_CASE("Reactor - socketpair EPOLLOUT", "[reactor][network]")
{
    int fds[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) == 0);

    Reactor reactor;
    reactor.run();

    std::atomic<bool> writable_seen{false};
    std::atomic<bool> del_posted{false};

    // Level-triggered EPOLLOUT fires repeatedly while writable, so guard del_fd with an atomic.
    reactor.add_fd(fds[0], EPOLLOUT, [&, fd0 = fds[0]](uint32_t events) {
        if ((events & EPOLLOUT) && !del_posted.exchange(true))
        {
            writable_seen.store(true, std::memory_order_release);
            reactor.del_fd(fd0); // stop monitoring to avoid a spin loop
        }
    });

    auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (!writable_seen.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(5ms);

    REQUIRE(writable_seen.load(std::memory_order_acquire));

    ::close(fds[1]);
    reactor.stop();
    // fds[0] was already closed inside del_fd
}

// Echo server: fds[0] reads incoming data and writes it straight back;
// fds[1] monitors the echoed reply via EPOLLIN.
TEST_CASE("Reactor - socketpair echo", "[reactor][network]")
{
    int fds[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) == 0);

    Reactor reactor;
    reactor.run();

    std::atomic<bool> echo_received{false};
    std::string echo_data;

    // Echo server on fds[0]: read whatever arrives and write it back.
    reactor.add_fd(fds[0], EPOLLIN, [&](uint32_t events) {
        if (events & EPOLLIN)
        {
            char buf[256]{};
            ssize_t n = ::read(fds[0], buf, sizeof(buf) - 1);
            if (n > 0)
                ::write(fds[0], buf, static_cast<size_t>(n));
        }
    });

    // Client side on fds[1]: collect the echoed reply.
    reactor.add_fd(fds[1], EPOLLIN, [&](uint32_t events) {
        if (events & EPOLLIN)
        {
            char buf[256]{};
            ssize_t n = ::read(fds[1], buf, sizeof(buf) - 1);
            if (n > 0)
            {
                echo_data.assign(buf, static_cast<size_t>(n));
                echo_received.store(true, std::memory_order_release);
            }
        }
    });

    // Sending to fds[1] delivers data to fds[0]'s receive buffer, triggering the echo.
    const std::string msg = "ping";
    ::write(fds[1], msg.c_str(), msg.size());

    auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (!echo_received.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(5ms);

    REQUIRE(echo_received.load(std::memory_order_acquire));
    REQUIRE(echo_data == msg);

    reactor.stop();
    // fds[0] and fds[1] closed by reactor destructor
}