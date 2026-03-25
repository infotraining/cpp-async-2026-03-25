#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

enum class QueueErrc {
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
    ConcurrentQueue() = default;

	bool empty() const
	{
		std::lock_guard lk{mtx_q_};
		return q_.empty();
	}

    bool push(const T& value)
    {
		if (is_closed_)
			return false;

        {
            std::lock_guard lk{mtx_q_};
            q_.push(value);
        }
        cv_q_not_empty_.notify_one();

		return true;
    }

    bool push(T&& value)
    {
		if (is_closed_)
			return false;

        {
            std::lock_guard lk{mtx_q_};
            q_.push(std::move(value));
        }
        cv_q_not_empty_.notify_one();

		return true;
    }

    [[nodiscard]] std::optional<T> pop()
    {
        std::unique_lock lk{mtx_q_};

		auto open_or_not_empty = [this] { return !q_.empty() || is_closed_; };
        cv_q_not_empty_.wait(lk, open_or_not_empty);

		if (q_.empty() && is_closed_)
			return std::nullopt;

        T value = std::move(q_.front());
        q_.pop();
        return value;
    }

	QueueErrc try_pop(T& value)
	{
		std::unique_lock lk{mtx_q_, std::try_to_lock};

		if (not lk.owns_lock())
			return QueueErrc::Busy;

		if (is_closed_)
			return QueueErrc::Closed;

		if (q_.empty())
			return QueueErrc::Empty;

		value = std::move(q_.front());
		q_.pop();

		return QueueErrc::Success;
	}

	void close()
	{
		{
			std::lock_guard lk{mtx_q_};
			is_closed_ = true;
		}

		cv_q_not_empty_.notify_all();
	}

	bool is_closed() const
	{
		return is_closed_;
	}
};