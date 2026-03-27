#ifndef REACTOR_HPP
#define REACTOR_HPP

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <liburing.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <unordered_map>

namespace AsyncLab::Epoll
{
    class Reactor
    {
    public:
        using FdCallback = std::function<void(uint32_t events)>;

        Reactor()
        {
            // Create the epoll instance
            epfd_ = epoll_create1(0);
            if (epfd_ == -1)
                throw std::runtime_error("epoll_create1 failed");

            // Create a wakeup eventfd so that external threads can interrupt epoll_wait
            // when pending_work_ drops to 0 (e.g. async coroutine completion).
            wake_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
            if (wake_fd_ == -1)
            {
                ::close(epfd_);
                throw std::runtime_error("eventfd failed");
            }

            // Register the wakeup eventfd with epoll
            epoll_event wakeup_event{};
            wakeup_event.events = EPOLLIN;
            wakeup_event.data.fd = wake_fd_;
            if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, wake_fd_, &wakeup_event) == -1)
            {
                ::close(wake_fd_);
                ::close(epfd_);
                throw std::runtime_error("epoll_ctl ADD wakeup_fd failed");
            }
        }

        ~Reactor()
        {
            if (running_)
            {
                stop();
            }

            if (reactor_thread_.joinable())
            {
                reactor_thread_.join();
            }

            // Clean up: close all registered file descriptors and the epoll instance
            for (const auto& [fd, cb] : callbacks_)
            {
                ::close(fd);
            }

            if (wake_fd_ != -1)
                ::close(wake_fd_);

            // Close the epoll file descriptor
            if (epfd_ != -1)
            {
                ::close(epfd_);
            }
        }

        // Public API (thread-safe)
        void add_fd(int fd, uint32_t events, FdCallback cb)
        {
            Command cmd;
            cmd.type = Command::AddFd;
            cmd.fd = fd;
            cmd.events = events;
            cmd.cb = std::move(cb);
            post_command(std::move(cmd));
        }

        void mod_fd(int fd, uint32_t events)
        {
            Command cmd;
            cmd.type = Command::ModFd;
            cmd.fd = fd;
            cmd.events = events;
            post_command(std::move(cmd));
        }

        void del_fd(int fd)
        {
            Command cmd;
            cmd.type = Command::DelFd;
            cmd.fd = fd;
            post_command(std::move(cmd));
        }

        // Remove fd from epoll and callbacks without closing it (fd is user-owned).
        void unreg_fd(int fd)
        {
            Command cmd;
            cmd.type = Command::UnregFd;
            cmd.fd = fd;
            post_command(std::move(cmd));
        }

        void stop()
        {
            running_ = false;

            Command cmd;
            cmd.type = Command::Stop;
            std::cout << "Posting stop command" << std::endl;
            post_command(std::move(cmd));
        }

        template <typename Rep, typename Period, typename Callback>
        int add_timer(std::chrono::duration<Rep, Period> duration, Callback cb)
        {
            int tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
            if (tfd == -1)
                throw std::runtime_error("timerfd_create failed");

            itimerspec new_value{};
            new_value.it_value.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
            new_value.it_value.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() % 1000000000;
            if (::timerfd_settime(tfd, 0, &new_value, nullptr) == -1)
            {
                ::close(tfd);
                throw std::runtime_error("timerfd_settime failed");
            }

            add_fd(tfd, EPOLLIN, [this, tfd, cb = std::move(cb)](uint32_t events) {
                if (events & EPOLLIN)
                {
                    uint64_t expirations;
                    ssize_t n = ::read(tfd, &expirations, sizeof(expirations));
                    if (n == -1)
                    {
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                        {
                            std::cerr << "Failed to read timerfd: " << std::strerror(errno) << "\n";
                        }
                        return;
                    }
                    cb();
                }
            });

            return tfd;
        }

        void do_run(int timeout_ms = -1)
        {
            running_ = true;
            constexpr int max_events = 64;
            epoll_event events[max_events];

            while (running_)
            {
                // std::cout << "Waiting for events..." << std::endl;
                int n = epoll_wait(epfd_, events, max_events, timeout_ms);
                // std::cout << "epoll_wait returned " << n << " events\n";
                if (n == -1)
                {
                    if (errno == EINTR)
                        continue;
                    throw std::runtime_error("epoll_wait failed");
                }

                for (int i = 0; i < n; ++i)
                {
                    int fd = events[i].data.fd;
                    uint32_t ev = events[i].events;

                    if (fd == wake_fd_)
                    {
                        handle_wakeup();
                        continue;
                    }

                    // Look up the callback for this fd and invoke it
                    if (auto it = callbacks_.find(fd); it != callbacks_.end())
                    {
                        auto& cb = it->second;
                        cb(ev);
                    }
                }
            }
        }

        void run(int timeout_ms = -1)
        {
            std::call_once(run_reactor_flag_, [this, timeout_ms] {
                reactor_thread_ = std::thread([this, timeout_ms] {
                    try
                    {
                        do_run(timeout_ms);
                    }
                    catch (const std::exception& ex)
                    {
                        std::cerr << "Reactor error: " << ex.what() << "\n";
                        std::terminate();
                    }
                });
            });
        }

    private:
        int epfd_{-1};
        int wake_fd_{-1};
        std::atomic<bool> running_{false};
        std::atomic<int> pending_work_{0};
        std::unordered_map<int, FdCallback> callbacks_;
        std::thread reactor_thread_;
        std::once_flag run_reactor_flag_;

        void wakeup()
        {
            uint64_t one = 1;
            ssize_t n = ::write(wake_fd_, &one, sizeof(one));
            (void)n;
        }

        // clang-format off
    struct Command
    {
        enum Type { AddFd, ModFd, DelFd, UnregFd, Stop } type;
        int fd{-1};
        uint32_t events{0};
        FdCallback cb;
    };
        // clang-format on

        std::mutex mtx_cmd_q_;
        std::vector<Command> cmd_q_;

        void post_command(Command cmd)
        {
            {
                std::lock_guard lock(mtx_cmd_q_);
                cmd_q_.push_back(std::move(cmd));
            }

            wakeup();
        }

        void handle_wakeup()
        {
            // Drain eventfd
            uint64_t val;
            while (true)
            {
                ssize_t n = ::read(wake_fd_, &val, sizeof(val));
                if (n == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    break;
                }
            }

            // Take commands
            std::vector<Command> local;
            {
                std::lock_guard lock(mtx_cmd_q_);
                local.swap(cmd_q_);
            }

            // Execute commands in reactor thread
            for (auto& cmd : local)
            {
                switch (cmd.type)
                {
                case Command::AddFd:
                {
                    // std::cout << "Received AddFd command for fd " << cmd.fd << " with events " << cmd.events << "\n";
                    epoll_event ev{};
                    ev.events = cmd.events;
                    ev.data.fd = cmd.fd;
                    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, cmd.fd, &ev) == -1)
                    {
                        std::cerr << "epoll_ctl ADD failed: " << std::strerror(errno) << "\n";
                        break;
                    }
                    callbacks_[cmd.fd] = std::move(cmd.cb);
                    break;
                }
                case Command::ModFd:
                {
                    // std::cout << "Received ModFd command for fd " << cmd.fd << " with events " << cmd.events << "\n";
                    epoll_event ev{};
                    ev.events = cmd.events;
                    ev.data.fd = cmd.fd;
                    if (epoll_ctl(epfd_, EPOLL_CTL_MOD, cmd.fd, &ev) == -1)
                    {
                        std::cerr << "epoll_ctl MOD failed: " << std::strerror(errno) << "\n";
                    }
                    break;
                }
                case Command::DelFd:
                {
                    // std::cout << "Received DelFd command for fd " << cmd.fd << "\n";
                    epoll_ctl(epfd_, EPOLL_CTL_DEL, cmd.fd, nullptr);
                    callbacks_.erase(cmd.fd);
                    ::close(cmd.fd);
                    break;
                }
                case Command::UnregFd:
                {
                    epoll_ctl(epfd_, EPOLL_CTL_DEL, cmd.fd, nullptr);
                    callbacks_.erase(cmd.fd);
                    // fd is user-owned — do NOT close it here
                    break;
                }
                case Command::Stop:
                    // std::cout << "Received stop command\n";
                    running_ = false;
                    break;
                }
            }
        }
    };
} // namespace AsyncLab::Epoll

namespace AsyncLab
{
    namespace EpollWithIOUring
    {
        class Reactor
        {
        public:
            using FdCallback = std::function<void(uint32_t events)>;
            using IoCallback = std::function<void(int res)>; // res = cqe->res

            Reactor()
            {
                // Create the epoll instance
                epfd_ = epoll_create1(0);
                if (epfd_ == -1)
                    throw std::runtime_error("epoll_create1 failed");

                // Create a wakeup eventfd so that external threads can interrupt epoll_wait
                // when pending_work_ drops to 0 (e.g. async coroutine completion).
                wake_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
                if (wake_fd_ == -1)
                {
                    ::close(epfd_);
                    throw std::runtime_error("eventfd failed");
                }

                // Register the wakeup eventfd with epoll
                epoll_event wakeup_event{};
                wakeup_event.events = EPOLLIN;
                wakeup_event.data.fd = wake_fd_;
                if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, wake_fd_, &wakeup_event) == -1)
                {
                    ::close(wake_fd_);
                    ::close(epfd_);
                    throw std::runtime_error("epoll_ctl ADD wakeup_fd failed");
                }

                unsigned int sq_entries = 64;
                io_uring_params ring_params{};
                if (io_uring_queue_init_params(sq_entries, &ring_, &ring_params) < 0)
                {
                    ::close(wake_fd_);
                    ::close(epfd_);
                    throw std::runtime_error("io_uring_queue_init failed");
                }

                ring_fd_ = ring_.ring_fd;

                epoll_event io_event{};
                io_event.events = EPOLLIN;
                io_event.data.fd = ring_fd_;
                if (epoll_ctl(epfd_, EPOLL_CTL_ADD, ring_fd_, &io_event) == -1)
                {
                    io_uring_queue_exit(&ring_);
                    ::close(wake_fd_);
                    ::close(epfd_);
                    throw std::runtime_error("epoll_ctl ADD ring_fd failed");
                }
            }

            ~Reactor()
            {
                if (running_)
                {
                    stop();
                }

                if (reactor_thread_.joinable())
                {
                    reactor_thread_.join();
                }

                // Clean up: close all registered file descriptors and the epoll instance
                for (const auto& [fd, cb] : callbacks_)
                {
                    ::close(fd);
                }

                if (wake_fd_ != -1)
                    ::close(wake_fd_);

                // Close the epoll file descriptor
                if (epfd_ != -1)
                {
                    ::close(epfd_);
                }

                // Clean up io_uring
                if (ring_fd_ != -1)
                {
                    io_uring_queue_exit(&ring_);
                }
            }

            // Public API (thread-safe) for fd-based events

            // Register an fd with the reactor to monitor for events (e.g. EPOLLIN, EPOLLOUT).
            void add_fd(int fd, uint32_t events, FdCallback cb)
            {
                Command cmd;
                cmd.type = Command::AddFd;
                cmd.fd = fd;
                cmd.events = events;
                cmd.cb = std::move(cb);
                post_command(std::move(cmd));
            }

            // Modify the events monitored for an fd (e.g. switch from EPOLLIN to EPOLLOUT).
            void mod_fd(int fd, uint32_t events)
            {
                Command cmd;
                cmd.type = Command::ModFd;
                cmd.fd = fd;
                cmd.events = events;
                post_command(std::move(cmd));
            }

            // Remove fd from epoll and callbacks, and close it (fd is reactor-owned).
            void del_fd(int fd)
            {
                Command cmd;
                cmd.type = Command::DelFd;
                cmd.fd = fd;
                post_command(std::move(cmd));
            }

            // Remove fd from epoll and callbacks without closing it (fd is user-owned).
            void unreg_fd(int fd)
            {
                Command cmd;
                cmd.type = Command::UnregFd;
                cmd.fd = fd;
                post_command(std::move(cmd));
            }

            // Async file read via io_uring
            void read_file(int fd, void* buf, unsigned len, off_t offset, IoCallback cb)
            {
                submit_io(fd, buf, len, offset, false, std::move(cb));
            }

            // Async file write via io_uring
            void write_file(int fd, const void* buf, unsigned len, off_t offset, IoCallback cb)
            {
                submit_io(fd, const_cast<void*>(buf), len, offset, true, std::move(cb));
            }

            // Stop the reactor loop
            void stop()
            {
                running_ = false;

                Command cmd;
                cmd.type = Command::Stop;
                // std::cout << "Posting stop command" << std::endl;
                post_command(std::move(cmd));
            }

            template <typename Rep, typename Period, typename Callback>
            int add_timer(std::chrono::duration<Rep, Period> duration, Callback cb)
            {
                int tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
                if (tfd == -1)
                    throw std::runtime_error("timerfd_create failed");

                itimerspec new_value{};
                new_value.it_value.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
                new_value.it_value.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() % 1000000000;
                if (::timerfd_settime(tfd, 0, &new_value, nullptr) == -1)
                {
                    ::close(tfd);
                    throw std::runtime_error("timerfd_settime failed");
                }

                add_fd(tfd, EPOLLIN, [this, tfd, cb = std::move(cb)](uint32_t events) {
                    if (events & EPOLLIN)
                    {
                        uint64_t expirations;
                        ssize_t n = ::read(tfd, &expirations, sizeof(expirations));
                        if (n == -1)
                        {
                            if (errno != EAGAIN && errno != EWOULDBLOCK)
                            {
                                std::cerr << "Failed to read timerfd: " << std::strerror(errno) << "\n";
                            }
                            return;
                        }
                        cb();
                    }
                });

                return tfd;
            }

            void do_run(int timeout_ms = -1)
            {
                running_ = true;
                constexpr int max_events = 64;
                epoll_event events[max_events];

                while (running_)
                {
                    // std::cout << "Waiting for events..." << std::endl;
                    int n = epoll_wait(epfd_, events, max_events, timeout_ms);
                    // std::cout << "epoll_wait returned " << n << " events\n";
                    if (n == -1)
                    {
                        if (errno == EINTR)
                            continue;
                        throw std::runtime_error("epoll_wait failed");
                    }

                    for (int i = 0; i < n; ++i)
                    {
                        int fd = events[i].data.fd;
                        uint32_t ev = events[i].events;

                        if (fd == wake_fd_)
                        {
                            handle_wakeup();
                            continue;
                        }

                        if (fd == ring_fd_)
                        {
                            handle_io_uring_completions();
                            continue;
                        }

                        // Look up the callback for this fd and invoke it
                        if (auto it = callbacks_.find(fd); it != callbacks_.end())
                        {
                            auto& cb = it->second;
                            cb(ev);
                        }
                    }
                }
            }

            void run(int timeout_ms = -1)
            {
                std::call_once(run_reactor_flag_, [this, timeout_ms] {
                    reactor_thread_ = std::thread([this, timeout_ms] {
                        try
                        {
                            do_run(timeout_ms);
                        }
                        catch (const std::exception& ex)
                        {
                            std::cerr << "Reactor error: " << ex.what() << "\n";
                            std::terminate();
                        }
                    });
                });
            }

        private:
            int epfd_{-1};
            int wake_fd_{-1};
            int ring_fd_{-1};
            io_uring ring_;
            std::atomic<bool> running_{false};
            std::atomic<int> pending_work_{0};
            std::unordered_map<int, FdCallback> callbacks_;
            std::unordered_map<uint64_t, IoCallback> io_callbacks_;
            uint64_t next_io_id_{1}; // Start from 1 to avoid confusion with 0 (which could be a valid fd)
            std::thread reactor_thread_;
            std::once_flag run_reactor_flag_;

            void wakeup()
            {
                uint64_t one = 1;
                ssize_t n = ::write(wake_fd_, &one, sizeof(one));
                (void)n;
            }

            // clang-format off
            struct Command
            {
                enum Type { AddFd, ModFd, DelFd, UnregFd, Stop } type;
                int fd{-1};
                uint32_t events{0};
                FdCallback cb;
            };
            // clang-format on

            std::mutex mtx_cmd_q_;
            std::vector<Command> cmd_q_;

            void post_command(Command cmd)
            {
                {
                    std::lock_guard lock(mtx_cmd_q_);
                    cmd_q_.push_back(std::move(cmd));
                }

                wakeup();
            }

            void handle_wakeup()
            {
                // Drain eventfd
                uint64_t val;
                while (true)
                {
                    ssize_t n = ::read(wake_fd_, &val, sizeof(val));
                    if (n == -1)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        break;
                    }
                }

                // Take commands
                std::vector<Command> local;
                {
                    std::lock_guard lock(mtx_cmd_q_);
                    local.swap(cmd_q_);
                }

                // Execute commands in reactor thread
                for (auto& cmd : local)
                {
                    switch (cmd.type)
                    {
                    case Command::AddFd:
                    {
                        // std::cout << "Received AddFd command for fd " << cmd.fd << " with events " << cmd.events << "\n";
                        epoll_event ev{};
                        ev.events = cmd.events;
                        ev.data.fd = cmd.fd;
                        if (epoll_ctl(epfd_, EPOLL_CTL_ADD, cmd.fd, &ev) == -1)
                        {
                            std::cerr << "epoll_ctl ADD failed: " << std::strerror(errno) << "\n";
                            break;
                        }
                        callbacks_[cmd.fd] = std::move(cmd.cb);
                        break;
                    }
                    case Command::ModFd:
                    {
                        // std::cout << "Received ModFd command for fd " << cmd.fd << " with events " << cmd.events << "\n";
                        epoll_event ev{};
                        ev.events = cmd.events;
                        ev.data.fd = cmd.fd;
                        if (epoll_ctl(epfd_, EPOLL_CTL_MOD, cmd.fd, &ev) == -1)
                        {
                            std::cerr << "epoll_ctl MOD failed: " << std::strerror(errno) << "\n";
                        }
                        break;
                    }
                    case Command::DelFd:
                    {
                        // std::cout << "Received DelFd command for fd " << cmd.fd << "\n";
                        epoll_ctl(epfd_, EPOLL_CTL_DEL, cmd.fd, nullptr);
                        callbacks_.erase(cmd.fd);
                        ::close(cmd.fd);
                        break;
                    }
                    case Command::UnregFd:
                    {
                        epoll_ctl(epfd_, EPOLL_CTL_DEL, cmd.fd, nullptr);
                        callbacks_.erase(cmd.fd);
                        // fd is user-owned — do NOT close it here
                        break;
                    }
                    case Command::Stop:
                        // std::cout << "Received stop command\n";
                        running_ = false;
                        break;
                    }
                }
            }

            void submit_io(int fd, void* buf, unsigned len, off_t offset, bool is_write, IoCallback cb)
            {
                io_uring_sqe* sqe = io_uring_get_sqe(&ring_);

                if (!sqe)
                {
                    throw std::runtime_error("io_uring_get_sqe failed");
                }

                uint64_t id = next_io_id_++;
                io_callbacks_.emplace(id, IoCallback{std::move(cb)});

                if (is_write)
                {
                    io_uring_prep_write(sqe, fd, buf, len, offset);
                }
                else
                {
                    io_uring_prep_read(sqe, fd, buf, len, offset);
                }

                io_uring_sqe_set_data64(sqe, id);

                if (int ret = io_uring_submit(&ring_); ret < 0)
                {
                    throw std::runtime_error("io_uring_submit failed");
                }
            }

            void handle_io_uring_completions()
            {
                io_uring_cqe* cqe;
                while (io_uring_peek_cqe(&ring_, &cqe) == 0)
                {
                    uint64_t id = io_uring_cqe_get_data64(cqe);

                    if (auto it = io_callbacks_.find(id); it != io_callbacks_.end())
                    {
                        auto cb = std::move(it->second);
                        io_callbacks_.erase(it);
                        cb(cqe->res);
                    }
                    io_uring_cqe_seen(&ring_, cqe);
                }
            }
        };
    } // namespace EpollWithIOUring

} // namespace AsyncLab

#endif // REACTOR_HPP