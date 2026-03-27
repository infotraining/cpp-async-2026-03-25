#ifndef FILE_DESCRIPTOR_HPP
#define FILE_DESCRIPTOR_HPP

#include <exception>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace AsyncLab
{
    class FileDescriptor
    {
        int fd_;

    public:
        explicit FileDescriptor(int fd)
            : fd_(fd)
        {
            if (fd_ == -1)
            {
                throw std::runtime_error("Invalid file descriptor");
            }
        }

        ~FileDescriptor()
        {
            if (fd_ != -1)
            {
                ::close(fd_);
            }
        }

        // Disable copy semantics
        FileDescriptor(const FileDescriptor&) = delete;
        FileDescriptor& operator=(const FileDescriptor&) = delete;

        // Enable move semantics
        FileDescriptor(FileDescriptor&& other) noexcept
            : fd_(other.fd_)
        {
            other.fd_ = -1; // Invalidate the moved-from object
        }

        FileDescriptor& operator=(FileDescriptor&& other) noexcept
        {
            if (this != &other)
            {
                if (fd_ != -1)
                {
                    ::close(fd_);
                }
                fd_ = other.fd_;
                other.fd_ = -1; // Invalidate the moved-from object
            }
            return *this;
        }

        int get() const { return fd_; }

        int release()
        {
            int old_fd = fd_;
            fd_ = -1; // Invalidate this object
            return old_fd;
        }

        size_t size() const
        {
            struct stat st;
            if (fstat(fd_, &st) == -1)
            {
                throw std::runtime_error("Failed to get file size");
            }
            return static_cast<size_t>(st.st_size);
        }

        void close()
        {
            if (fd_ != -1)
            {
                ::close(fd_);
                fd_ = -1;
            }
        }
    };

} // namespace AsyncLab

#endif // FILE_DESCRIPTOR_HPP