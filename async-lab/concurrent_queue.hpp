#ifndef CONCURRENT_QUEUE_HPP
#define CONCURRENT_QUEUE_HPP

#include <condition_variable>
#include <optional>
#include <queue>
#include <thread>

namespace AsyncLab
{
    enum class QueueErrc 
    {
        Success,
        Empty,
        Closed,
        Busy
    };

    template <typename T>
    class ConcurrentQueue
    {
        std::queue<T> q_;
        mutable std::mutex mtx_q_;
        std::condition_variable cv_q_not_empty_;
        std::atomic<bool> is_closed_{false};

    public:
        ConcurrentQueue()
        {
        }

        ConcurrentQueue(const ConcurrentQueue&) = delete;
        ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

        using value_type = T;

        /*
         * @brief Checks if the queue is empty.
         * @return true if the queue is empty, false otherwise.
         */
        bool empty() const
        {
            std::lock_guard lock(mtx_q_);
            return q_.empty();
        }

        /*
         * @brief Pushes a value into the queue.
         * @param value The value to be pushed.
         * @return true if the value was pushed, false if the queue is closed.
         */
        bool push(const T& value)
        {
            if (is_closed_.load(std::memory_order_seq_cst))
                return false;

            std::lock_guard lock(mtx_q_);
            q_.push(value);
            cv_q_not_empty_.notify_one();

            return true;
        }

        /*
         * @brief Pushes a value into the queue.
         * @param value The value to be pushed.
         * @return true if the value was pushed, false if the queue is closed.
         */
        bool push(T&& value)
        {
            if (is_closed_.load(std::memory_order_seq_cst))
                return false;

            std::lock_guard lock(mtx_q_);
            q_.push(std::move(value));
            cv_q_not_empty_.notify_one();
            return true;
        }

        /*
         * @brief Pops a value from the queue. Blocks if the queue is empty.
         * @return An optional containing the popped value, or std::nullopt if the queue is closed.
         * @note If the queue is closed and empty, returns std::nullopt. If the queue is closed but not empty, pops all remaining items before returning std::nullopt on the next call.
         */
        [[nodiscard]] std::optional<T> pop()
        {
            std::unique_lock lock(mtx_q_);

            auto open_and_not_empty = [this] { return !q_.empty() || is_closed_.load(std::memory_order_seq_cst); };
            cv_q_not_empty_.wait(lock, open_and_not_empty);

            if (q_.empty() && is_closed_.load(std::memory_order_seq_cst))
                return std::nullopt;

            T value = std::move(q_.front());
            q_.pop();
            return value;
        }

        /*
         * @brief Tries to pop a value from the queue. Does not block.
         * @param value The value to be popped.
         * @return A QueueErrc indicating the result of the operation.
         */
        [[nodiscard]] QueueErrc try_pop(T& value)
        {
            std::unique_lock lock(mtx_q_, std::try_to_lock);

            if (!lock.owns_lock())
                return QueueErrc::Busy;

            if (is_closed_.load(std::memory_order_seq_cst))
                return QueueErrc::Closed;

            if (q_.empty())
                return QueueErrc::Empty;

            value = std::move(q_.front());
            q_.pop();
            return QueueErrc::Success;
        }

        /*
         * @brief Closes the queue. After calling this method, no more items can be pushed into the queue, and
         * all waiting pop operations will be unblocked and return std::nullopt if the queue is empty.
         * If the queue is not empty when closed, pop operations will continue to return items until the queue is empty, after which they will return std::nullopt.
         */
        void close()
        {
            {
                std::lock_guard lock(mtx_q_);
                is_closed_.store(true, std::memory_order_seq_cst);
            }
            cv_q_not_empty_.notify_all();
        }

        /*
         * @brief Checks if the queue is closed.
         * @return true if the queue is closed, false otherwise.
         */
        bool is_closed() const
        {
            return is_closed_.load(std::memory_order_seq_cst);
        }
    };
} // namespace AsyncLab

#endif // CONCURRENT_QUEUE_HPP