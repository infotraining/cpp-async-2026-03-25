#ifndef IO_AWAITABLES_HPP
#define IO_AWAITABLES_HPP

#include "reactor.hpp"

#include <coroutine>
#include <system_error>

namespace AsyncLab
{
    using ConstBuffer = std::span<const char>;
    using MutableBuffer = std::span<char>;

    struct IOResult
    {
        std::errc error;
        size_t bytes_transferred;
    };

    inline std::error_code to_error_code(int negative_errno_value)
    {
        // io_uring returns negative errno, e.g. -EAGAIN
        int e = -negative_errno_value;
        return std::error_code(e, std::generic_category());
    }

    inline std::errc to_errc(int negative_errno_value)
    {
        return static_cast<std::errc>(to_error_code(negative_errno_value).value());
    }

    auto async_read(AsyncLab::Epoll::Reactor& reactor, int fd, MutableBuffer buffer)
    {
        struct ReadAwaiter
        {
            AsyncLab::Epoll::Reactor& reactor_;
            int fd_;
            MutableBuffer buffer_;
            ssize_t result_{-1};

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                reactor_.add_fd(fd_, EPOLLIN | EPOLLONESHOT, [this, handle](uint32_t) mutable {
                    result_ = ::read(fd_, buffer_.data(), buffer_.size());

                    handle.resume();
                });
            }

            IOResult await_resume() noexcept
            {
                if (result_ == -1)
                {
                    return {to_errc(errno), 0};
                }

                reactor_.unreg_fd(fd_);
                return {std::errc{}, static_cast<size_t>(result_)};
            }
        };
        return ReadAwaiter{reactor, fd, buffer};
    }

    auto async_write(AsyncLab::Epoll::Reactor& reactor, int fd, ConstBuffer buffer)
    {
        struct WriteAwaiter
        {
            AsyncLab::Epoll::Reactor& reactor_;
            int fd_;
            ConstBuffer buffer_;
            ssize_t result_{-1};

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                reactor_.add_fd(fd_, EPOLLOUT | EPOLLONESHOT, [this, handle](uint32_t) mutable {
                    result_ = ::write(fd_, buffer_.data(), buffer_.size());
                    handle.resume();
                });
            }

            IOResult await_resume() noexcept
            {
                reactor_.unreg_fd(fd_);
                if (result_ == -1)
                {
                    return {to_errc(errno), 0};
                }
                return {std::errc{}, static_cast<size_t>(result_)};
            }
        };
        return WriteAwaiter{reactor, fd, buffer};
    }
} // namespace AsyncLab

////////////////////////////////////////////////////////////
// Awaitables for async read/write via Reactor with io_uring
////////////////////////////////////////////////////////////

namespace AsyncLab::EpollWithIOUring
{
    auto async_io_read(Reactor& reactor, int fd, MutableBuffer buffer)
    {
        struct ReadAwaiter
        {
            Reactor& reactor_;
            int fd_;
            MutableBuffer buffer_;
            std::optional<IOResult> result_;

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                reactor_.read_file(fd_, buffer_.data(), buffer_.size(), 0,
                    [this, handle](int res) {
                        if (res >= 0)
                            result_ = IOResult{std::errc{}, static_cast<size_t>(res)};
                        else
                            result_ = IOResult{to_errc(res), 0};
                        handle.resume();
                    });
            }

            IOResult await_resume() noexcept
            {
                return *result_;
            }
        };

        return ReadAwaiter{reactor, fd, buffer};
    }

    auto async_io_write(Reactor& reactor, int fd, ConstBuffer buffer)
    {
        struct WriteAwaiter
        {
            Reactor& reactor_;
            int fd_;
            ConstBuffer buffer_;
            std::optional<IOResult> result_;

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                reactor_.write_file(fd_, buffer_.data(), buffer_.size(), 0,
                    [this, handle](int res) {
                        if (res >= 0)
                            result_ = IOResult{std::errc{}, static_cast<size_t>(res)};
                        else
                            result_ = IOResult{to_errc(res), 0};
                        handle.resume();
                    });
            }

            IOResult await_resume() noexcept
            {
                return *result_;
            }
        };

        return WriteAwaiter{reactor, fd, buffer};
    }
} // namespace AsyncLab::EpollWithIOUring

#endif